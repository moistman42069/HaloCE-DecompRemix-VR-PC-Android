/*
HOST_XR.C

OpenXR for the guest (HALO_VR builds; port/linux/src/vr_*.c drives it).
The host owns the loader, instance, session, swapchains and controller
actions, all called on the game thread with its OpenGL ES context current
(the binding is that context's EGL display, config and context). The
guest sees plain structures (halo_android_abi.h) and GL texture names,
which it can draw into directly: its GL calls reach the same driver.

Poses go to the guest relative to a recentred LOCAL frame: the head's yaw
and position when the session gains focus or the guest asks
(host_xr_recenter) become the origin, facing -z.
*/

#ifdef HALO_VR

#ifdef _WIN32
/* Test72: the Windows PCVR build (halo-vr.exe): this host on WGL and desktop
OpenGL, the game's own context (port/windows/src/win32_xr.c gives it
host_logf and the guest's host_gl_has_extension) */
#include "win32_xr_host.h"
#include <windows.h>
/* (openxr_platform.h's Win32 declarations name COM's IUnknown) */
#include <unknwn.h>
#include <psapi.h>
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#else
#include "host.h"

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <jni.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* (sched_setaffinity: xr_fast_cores, the Steam Frame's) */
#include <sched.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define FRAME_CONTROLLER_EXTENSION "XR_VALVE_frame_controller_interaction"
/* Test72: the PICO controllers (pull request #4, adapted): the PICO 4 and
Neo 3's profiles come with XR_BD_controller_interaction, the PICO 4 Ultra's
with XR_BD_ultra_controller_interaction (the Khronos registry) */
#define BD_CONTROLLER_EXTENSION "XR_BD_controller_interaction"
#define BD_ULTRA_CONTROLLER_EXTENSION "XR_BD_ultra_controller_interaction"
/* Test72: the HP Reverb G2 controllers' profile (Windows Mixed Reality) */
#define HP_CONTROLLER_EXTENSION "XR_EXT_hp_mixed_reality_controller"
/* HTC Vive Focus 3 and Vive XR Elite controllers */
#define FOCUS3_CONTROLLER_EXTENSION "XR_HTC_vive_focus3_controller_interaction"
#define REFRESH_RATE_EXTENSION "XR_FB_display_refresh_rate"

/* Test72: the actions and every controller's bindings, shared with the
Windows PCVR host */
#include "halo_xr_bindings.h"

struct swapchain
{
	XrSwapchain handle;
	uint32_t width, height, count;
	uint32_t images[HALO_XR_MAXIMUM_IMAGES];
	int acquired;
	/* Test73: drawn, its release left for the frame's end (xr.defer_releases) */
	int held;
};


static struct
{
	int initialized, failed;
	XrInstance instance;
	XrSystemId system;
	XrSession session;
	XrSessionState state;
	int running, focused, exiting, frame_controller, refresh_rate_extension, bd_controller, bd_ultra_controller, hp_controller, focus3_controller;
	/* Test73: SteamVR's runtime, and (the Steam Frame) the frames' thread kept on the fast cores: xr_fast_cores */
	int steamvr, cores_pinned;
	/* Test73: the images released together as the frame ends, not each as
	it is drawn (the Steam Frame: host_xr_release) */
	int defer_releases;
	XrSpace local, view;
	XrSpace grip[2], aim[2];
	XrActionSet action_set;
	XrAction actions[NUMBER_OF_ACTIONS];
	XrPath hands[2];
	int actions_ready;
	struct swapchain swapchains[HALO_XR_SWAPCHAIN_COUNT];
	/* the largest eye image the runtime takes (host_xr_resize_eyes) */
	uint32_t eye_max_width, eye_max_height;
	int64_t color_format;
	XrFrameState frame_state;
	int frame_begun;
	XrView views[2];
	int views_valid;
	/* recentring: LOCAL yaw (about +y) and position of the origin */
	int recentre_pending, recentred_this_frame;
	float recentre_yaw, recentre_position[3];
#ifndef _WIN32
	JavaVM *vm;
	jobject activity;
#else
	/* XR_KHR_opengl_enable offered (Virtual Desktop's runtime has none) */
	int opengl_extension;
#endif
	/* statistics */
	long long frames, frames_rendered;
	/* the refresh rate asked for, and windows in a row it was not held */
	float refresh_rate;
	int refresh_misses;
	/* Test73 (astromaddie's HaloCE-VR, 82b6d12e, CC0): the fall back to 72 Hz
	was asked for and the runtime has no such rate (the Steam Frame's SteamVR
	offers only its display setting's) */
	int refresh_fallback_failed;
	struct timespec stats_start;
	/* XR_EXT_performance_settings: the CPU and GPU levels are asked for */
	int performance_settings;
	/* diagnostics (the log): the time xrWaitFrame waits, the game's time
	between beginning and ending a frame, frames not rendered, the layers
	of the last frame and the first frame shown */
	double wait_ms_total, wait_ms_max, game_ms_total, game_ms_max;
	struct timespec frame_begun_at;
	long long frames_not_rendered, frames_without_layers;
	uint32_t last_layer_flags;
	int first_frame_logged;
	/* Test72: the controllers bound (HALO_XR_PROFILE_*), for the game */
	uint32_t controller_profile;
} xr;
static int haptic_output_logged;

static double elapsed_ms(const struct timespec *from, const struct timespec *to)
{
	return (double)(to->tv_sec - from->tv_sec) * 1000.0 + (double)(to->tv_nsec - from->tv_nsec) / 1000000.0;
}

#ifdef _WIN32
/* the process's working set and its peak, in MB */
static void memory_use(double *resident, double *peak)
{
	PROCESS_MEMORY_COUNTERS counters;

	*resident = *peak = 0.0;
	counters.cb = sizeof(counters);
	if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
	{
		*resident = counters.WorkingSetSize / 1048576.0;
		*peak = counters.PeakWorkingSetSize / 1048576.0;
	}
}
#else
/* the process's resident memory and its peak, in MB, from /proc/self/status */
static void memory_use(double *resident, double *peak)
{
	FILE *status = fopen("/proc/self/status", "r");
	char line[128];

	*resident = *peak = 0.0;
	if (!status)
		return;
	while (fgets(line, sizeof(line), status))
	{
		long kilobytes;

		if (sscanf(line, "VmRSS: %ld kB", &kilobytes) == 1)
			*resident = kilobytes / 1024.0;
		else if (sscanf(line, "VmHWM: %ld kB", &kilobytes) == 1)
			*peak = kilobytes / 1024.0;
	}
	fclose(status);
}
#endif

static const char *result_name(XrResult result)
{
	static char text[XR_MAX_RESULT_STRING_SIZE];

	text[0] = 0;
	if (xr.instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(xr.instance, result, text)))
		snprintf(text, sizeof(text), "%d", (int)result);
	return text;
}

/* A failed call is logged, but one failing every frame would bury the log:
each call (by the text naming it) is logged its first 20 times, then every
500th, with its count. */
static int check(XrResult result, const char *what)
{
	static struct { const char *what; unsigned long count; } failures[48];
	unsigned long count = 1;
	int slot;

	if (XR_SUCCEEDED(result))
		return 1;
	for (slot = 0; slot < (int)(sizeof(failures) / sizeof(failures[0])); slot++)
	{
		if (failures[slot].what == what || !failures[slot].what)
		{
			failures[slot].what = what;
			count = ++failures[slot].count;
			break;
		}
	}
	if (count <= 20 || count % 500 == 0)
		host_logf(HOST_LOG_ERROR, "[openxr] %s failed: %s (failure %lu of this call)", what, result_name(result), count);
	return 0;
}

static const char *state_name(XrSessionState state)
{
	switch (state)
	{
	case XR_SESSION_STATE_IDLE: return "IDLE";
	case XR_SESSION_STATE_READY: return "READY";
	case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
	case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
	case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
	case XR_SESSION_STATE_STOPPING: return "STOPPING";
	case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
	case XR_SESSION_STATE_EXITING: return "EXITING";
	default: return "UNKNOWN";
	}
}

/* ---------- math (LOCAL space: +y up, -z forward) */

static void quaternion_multiply(const float a[4], const float b[4], float out[4])
{
	float r[4];

	r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
	r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
	r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
	r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
	memcpy(out, r, sizeof(r));
}

static void rotate_y(float yaw, const float in[3], float out[3])
{
	float c = cosf(yaw), s = sinf(yaw);
	float x = in[0], z = in[2];

	out[0] = c * x + s * z;
	out[1] = in[1];
	out[2] = -s * x + c * z;
}

/* a LOCAL pose, relative to the recentred origin */
static void recentred_pose(const XrPosef *pose, struct halo_xr_pose *out)
{
	float inverse[4] = { 0.0f, sinf(-xr.recentre_yaw * 0.5f), 0.0f, cosf(-xr.recentre_yaw * 0.5f) };
	float orientation[4] = { pose->orientation.x, pose->orientation.y, pose->orientation.z, pose->orientation.w };
	float position[3] = {
		pose->position.x - xr.recentre_position[0],
		pose->position.y - xr.recentre_position[1],
		pose->position.z - xr.recentre_position[2],
	};

	rotate_y(-xr.recentre_yaw, position, out->position);
	quaternion_multiply(inverse, orientation, out->orientation);
}

/* the inverse: a recentred pose back in LOCAL space */
static void local_pose(const struct halo_xr_pose *pose, XrPosef *out)
{
	float yaw[4] = { 0.0f, sinf(xr.recentre_yaw * 0.5f), 0.0f, cosf(xr.recentre_yaw * 0.5f) };
	float orientation[4];
	float position[3];

	rotate_y(xr.recentre_yaw, pose->position, position);
	quaternion_multiply(yaw, pose->orientation, orientation);
	out->position.x = position[0] + xr.recentre_position[0];
	out->position.y = position[1] + xr.recentre_position[1];
	out->position.z = position[2] + xr.recentre_position[2];
	out->orientation.x = orientation[0];
	out->orientation.y = orientation[1];
	out->orientation.z = orientation[2];
	out->orientation.w = orientation[3];
}

static void update_recentre(XrTime time)
{
	XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
	const XrQuaternionf *q;
	float forward_x, forward_z;

	xr.recentred_this_frame = 0;
	if (!xr.recentre_pending)
		return;
	if (XR_FAILED(xrLocateSpace(xr.view, xr.local, time, &location)) ||
		!(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ||
		!(location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT))
	{
		return; /* not tracked yet (the headset in standby): next frame */
	}
	q = &location.pose.orientation;
	/* the view's -z axis */
	forward_x = -2.0f * (q->x * q->z + q->w * q->y);
	forward_z = -(1.0f - 2.0f * (q->x * q->x + q->y * q->y));
	if (forward_x * forward_x + forward_z * forward_z < 1e-4f)
		return; /* looking straight up or down: no heading */
	/* a turn by yaw about +y takes (0, 0, -1) to (-sin yaw, 0, -cos yaw) */
	xr.recentre_yaw = atan2f(-forward_x, -forward_z);
	xr.recentre_position[0] = location.pose.position.x;
	xr.recentre_position[1] = location.pose.position.y;
	xr.recentre_position[2] = location.pose.position.z;
	xr.recentre_pending = 0;
	xr.recentred_this_frame = 1;
	host_logf(HOST_LOG_INFO, "[openxr] recentred: yaw %.1f, head (%.2f, %.2f, %.2f)",
		xr.recentre_yaw * 57.29578f, xr.recentre_position[0], xr.recentre_position[1], xr.recentre_position[2]);
}

/* ---------- controller actions */

static XrPath path(const char *text)
{
	XrPath result = XR_NULL_PATH;

	xrStringToPath(xr.instance, text, &result);
	return result;
}

static int create_action(int index, XrActionType type, const char *name, const char *label, int per_hand)
{
	XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };

	info.actionType = type;
	strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(info.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	if (per_hand)
	{
		info.countSubactionPaths = 2;
		info.subactionPaths = xr.hands;
	}
	return check(xrCreateAction(xr.action_set, &info, &xr.actions[index]), name);
}


#define SUGGESTED_MAX (NUMBER_OF_ACTIONS * 2)

static XrResult suggest(const char *profile, const struct binding *bindings, int count)
{
	XrActionSuggestedBinding suggested[SUGGESTED_MAX];
	XrInteractionProfileSuggestedBinding info = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	XrResult result;
	int index;

	if (count > SUGGESTED_MAX)
		return XR_ERROR_VALIDATION_FAILURE;
	for (index = 0; index < count; index++)
	{
		suggested[index].action = xr.actions[bindings[index].action];
		suggested[index].binding = path(bindings[index].path);
	}
	info.interactionProfile = path(profile);
	info.countSuggestedBindings = (uint32_t)count;
	info.suggestedBindings = suggested;
	result = xrSuggestInteractionProfileBindings(xr.instance, &info);
	host_logf(HOST_LOG_INFO, "[openxr] bindings %s: %s", profile,
		XR_SUCCEEDED(result) ? "suggested" : result_name(result));
	return result;
}

/* Test73: a profile's bindings with the fingers' touches (the Steam Frame's,
the Index's), or, if the runtime refuses them, its bindings alone: one path
it lacks would discard the whole suggestion and leave that controller with
no input at all */
static void suggest_pair(const char *profile, const struct binding *bindings, int count,
	const struct binding *fingers, int finger_count)
{
	struct binding both[SUGGESTED_MAX];

	if (count + finger_count <= SUGGESTED_MAX)
	{
		memcpy(both, bindings, (size_t)count * sizeof(*both));
		memcpy(both + count, fingers, (size_t)finger_count * sizeof(*both));
		if (XR_SUCCEEDED(suggest(profile, both, count + finger_count)))
			return;
		host_logf(HOST_LOG_INFO, "[openxr] bindings %s: again without the fingers' touches", profile);
	}
	suggest(profile, bindings, count);
}


static int create_actions(void)
{
	XrActionSetCreateInfo set_info = { XR_TYPE_ACTION_SET_CREATE_INFO };
	XrSessionActionSetsAttachInfo attach = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	int hand, ok = 1;

	xr.hands[0] = path("/user/hand/left");
	xr.hands[1] = path("/user/hand/right");
	strncpy(set_info.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
	strncpy(set_info.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
	if (!check(xrCreateActionSet(xr.instance, &set_info, &xr.action_set), "xrCreateActionSet"))
		return 0;
	ok &= create_action(_action_move, XR_ACTION_TYPE_VECTOR2F_INPUT, "move", "Move", 0);
	ok &= create_action(_action_look, XR_ACTION_TYPE_VECTOR2F_INPUT, "turn", "Turn", 0);
	ok &= create_action(_action_trigger_left, XR_ACTION_TYPE_FLOAT_INPUT, "grenade", "Throw grenade (left trigger)", 0);
	ok &= create_action(_action_trigger_right, XR_ACTION_TYPE_FLOAT_INPUT, "fire", "Fire (right trigger)", 0);
	ok &= create_action(_action_squeeze_left, XR_ACTION_TYPE_FLOAT_INPUT, "grip_left", "Left grip", 0);
	ok &= create_action(_action_squeeze_right, XR_ACTION_TYPE_FLOAT_INPUT, "grip_right", "Right grip", 0);
	ok &= create_action(_action_a, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_a", "A (jump)", 0);
	ok &= create_action(_action_b, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_b", "B (melee)", 0);
	ok &= create_action(_action_x, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_x", "X (action, reload)", 0);
	ok &= create_action(_action_y, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_y", "Y (switch weapon)", 0);
	ok &= create_action(_action_white, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_white", "White (flashlight)", 0);
	ok &= create_action(_action_black, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_black", "Black (switch grenade)", 0);
	ok &= create_action(_action_start, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_start", "Start (pause)", 0);
	ok &= create_action(_action_back, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_back", "Back (scores)", 0);
	ok &= create_action(_action_left_thumb, XR_ACTION_TYPE_BOOLEAN_INPUT, "left_thumb", "Crouch (left stick)", 0);
	ok &= create_action(_action_right_thumb, XR_ACTION_TYPE_BOOLEAN_INPUT, "right_thumb", "Zoom (right stick)", 0);
	ok &= create_action(_action_dpad_up, XR_ACTION_TYPE_BOOLEAN_INPUT, "dpad_up", "D-pad up", 0);
	ok &= create_action(_action_dpad_down, XR_ACTION_TYPE_BOOLEAN_INPUT, "dpad_down", "D-pad down", 0);
	ok &= create_action(_action_dpad_left, XR_ACTION_TYPE_BOOLEAN_INPUT, "dpad_left", "D-pad left", 0);
	ok &= create_action(_action_dpad_right, XR_ACTION_TYPE_BOOLEAN_INPUT, "dpad_right", "D-pad right", 0);
	ok &= create_action(_action_grip_pose, XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Hand", 1);
	ok &= create_action(_action_aim_pose, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim", 1);
	ok &= create_action(_action_haptic, XR_ACTION_TYPE_VIBRATION_OUTPUT, "haptic", "Vibration", 1);
	ok &= create_action(_action_hand_south, XR_ACTION_TYPE_BOOLEAN_INPUT, "hand_south", "Lower face button", 1);
	ok &= create_action(_action_hand_east, XR_ACTION_TYPE_BOOLEAN_INPUT, "hand_east", "Upper face button", 1);
	ok &= create_action(_action_hand_west, XR_ACTION_TYPE_BOOLEAN_INPUT, "hand_west", "Left face button", 1);
	ok &= create_action(_action_hand_north, XR_ACTION_TYPE_BOOLEAN_INPUT, "hand_north", "Top face button", 1);
	ok &= create_action(_action_hand_bumper, XR_ACTION_TYPE_BOOLEAN_INPUT, "hand_bumper", "Bumper", 1);
	ok &= create_action(_action_hand_stick, XR_ACTION_TYPE_BOOLEAN_INPUT, "hand_stick", "Stick click", 1);
	ok &= create_action(_action_hand_menu, XR_ACTION_TYPE_BOOLEAN_INPUT, "hand_menu", "Menu", 1);
	ok &= create_action(_action_hand_view, XR_ACTION_TYPE_BOOLEAN_INPUT, "hand_view", "View", 1);
	ok &= create_action(_action_hand_thumb_touch, XR_ACTION_TYPE_BOOLEAN_INPUT, "hand_thumb_touch", "Thumb resting", 1);
	ok &= create_action(_action_hand_index_touch, XR_ACTION_TYPE_BOOLEAN_INPUT, "hand_index_touch", "Finger on the trigger", 1);
	if (!ok)
		return 0;
	if (xr.frame_controller)
		suggest_pair("/interaction_profiles/valve/frame_controller_valve", halo_xr_bindings_frame, sizeof(halo_xr_bindings_frame) / sizeof(halo_xr_bindings_frame[0]),
			halo_xr_bindings_frame_fingers, sizeof(halo_xr_bindings_frame_fingers) / sizeof(halo_xr_bindings_frame_fingers[0]));
	if (xr.focus3_controller)
	{
		suggest("/interaction_profiles/htc/vive_focus3_controller", halo_xr_bindings_focus3, sizeof(halo_xr_bindings_focus3) / sizeof(halo_xr_bindings_focus3[0]));
		suggest("/interaction_profiles/oculus/touch_controller", halo_xr_bindings_focus3, sizeof(halo_xr_bindings_focus3) / sizeof(halo_xr_bindings_focus3[0]));
	}
	else
	{
		suggest("/interaction_profiles/oculus/touch_controller", halo_xr_bindings_touch, sizeof(halo_xr_bindings_touch) / sizeof(halo_xr_bindings_touch[0]));
	}
	suggest_pair("/interaction_profiles/valve/index_controller", halo_xr_bindings_index, sizeof(halo_xr_bindings_index) / sizeof(halo_xr_bindings_index[0]),
		halo_xr_bindings_index_fingers, sizeof(halo_xr_bindings_index_fingers) / sizeof(halo_xr_bindings_index_fingers[0]));
	if (xr.bd_controller)
	{
		suggest("/interaction_profiles/bytedance/pico4_controller", halo_xr_bindings_pico, sizeof(halo_xr_bindings_pico) / sizeof(halo_xr_bindings_pico[0]));
		suggest("/interaction_profiles/bytedance/pico_neo3_controller", halo_xr_bindings_pico, sizeof(halo_xr_bindings_pico) / sizeof(halo_xr_bindings_pico[0]));
	}
	if (xr.bd_ultra_controller)
		suggest("/interaction_profiles/bytedance/pico_ultra_controller_bd", halo_xr_bindings_pico, sizeof(halo_xr_bindings_pico) / sizeof(halo_xr_bindings_pico[0]));
	if (xr.hp_controller)
		suggest("/interaction_profiles/hp/mixed_reality_controller", halo_xr_bindings_hp, sizeof(halo_xr_bindings_hp) / sizeof(halo_xr_bindings_hp[0]));
	attach.countActionSets = 1;
	attach.actionSets = &xr.action_set;
	if (!check(xrAttachSessionActionSets(xr.session, &attach), "xrAttachSessionActionSets"))
		return 0;
	for (hand = 0; hand < 2; hand++)
	{
		XrActionSpaceCreateInfo space = { XR_TYPE_ACTION_SPACE_CREATE_INFO };

		space.subactionPath = xr.hands[hand];
		space.poseInActionSpace.orientation.w = 1.0f;
		space.action = xr.actions[_action_grip_pose];
		check(xrCreateActionSpace(xr.session, &space, &xr.grip[hand]), "xrCreateActionSpace(grip)");
		space.action = xr.actions[_action_aim_pose];
		check(xrCreateActionSpace(xr.session, &space, &xr.aim[hand]), "xrCreateActionSpace(aim)");
	}
	return 1;
}

static int action_boolean(int action)
{
	XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };

	info.action = xr.actions[action];
	return XR_SUCCEEDED(xrGetActionStateBoolean(xr.session, &info, &state)) && state.isActive && state.currentState;
}

/* a per-hand action's state for one hand (0 left, 1 right) */
static int action_boolean_hand(int action, int hand)
{
	XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };

	info.action = xr.actions[action];
	info.subactionPath = xr.hands[hand];
	return XR_SUCCEEDED(xrGetActionStateBoolean(xr.session, &info, &state)) && state.isActive && state.currentState;
}

static float action_float(int action)
{
	XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };

	info.action = xr.actions[action];
	if (XR_SUCCEEDED(xrGetActionStateFloat(xr.session, &info, &state)) && state.isActive)
		return state.currentState;
	return 0.0f;
}

static void action_vector(int action, float *x, float *y)
{
	XrActionStateGetInfo info = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateVector2f state = { XR_TYPE_ACTION_STATE_VECTOR2F };

	info.action = xr.actions[action];
	*x = *y = 0.0f;
	if (XR_SUCCEEDED(xrGetActionStateVector2f(xr.session, &info, &state)) && state.isActive)
	{
		*x = state.currentState.x;
		*y = state.currentState.y;
	}
}

static void sync_input(struct halo_xr_frame *frame)
{
	static const struct { int action; uint32_t bit; } buttons[] =
	{
		{ _action_a, HALO_XR_BUTTON_A },
		{ _action_b, HALO_XR_BUTTON_B },
		{ _action_x, HALO_XR_BUTTON_X },
		{ _action_y, HALO_XR_BUTTON_Y },
		{ _action_white, HALO_XR_BUTTON_WHITE },
		{ _action_black, HALO_XR_BUTTON_BLACK },
		{ _action_start, HALO_XR_BUTTON_START },
		{ _action_back, HALO_XR_BUTTON_BACK },
		{ _action_left_thumb, HALO_XR_BUTTON_LEFT_THUMB },
		{ _action_right_thumb, HALO_XR_BUTTON_RIGHT_THUMB },
		{ _action_dpad_up, HALO_XR_BUTTON_DPAD_UP },
		{ _action_dpad_down, HALO_XR_BUTTON_DPAD_DOWN },
		{ _action_dpad_left, HALO_XR_BUTTON_DPAD_LEFT },
		{ _action_dpad_right, HALO_XR_BUTTON_DPAD_RIGHT },
	};
	XrActiveActionSet active = { xr.action_set, XR_NULL_PATH };
	XrActionsSyncInfo sync = { XR_TYPE_ACTIONS_SYNC_INFO };
	unsigned int index;
	int hand;

	if (!xr.actions_ready || !xr.focused)
		return;
	sync.countActiveActionSets = 1;
	sync.activeActionSets = &active;
	if (XR_FAILED(xrSyncActions(xr.session, &sync)))
		return;
	for (index = 0; index < sizeof(buttons) / sizeof(buttons[0]); index++)
	{
		if (action_boolean(buttons[index].action))
			frame->buttons |= buttons[index].bit;
	}
	frame->trigger[0] = action_float(_action_trigger_left);
	frame->trigger[1] = action_float(_action_trigger_right);
	frame->squeeze[0] = action_float(_action_squeeze_left);
	frame->squeeze[1] = action_float(_action_squeeze_right);
	action_vector(_action_move, &frame->thumb[0], &frame->thumb[1]);
	action_vector(_action_look, &frame->thumb[2], &frame->thumb[3]);
	{
		static const struct { int action; uint32_t bit; } hand_buttons[] =
		{
			{ _action_hand_south, HALO_XR_HAND_SOUTH },
			{ _action_hand_east, HALO_XR_HAND_EAST },
			{ _action_hand_west, HALO_XR_HAND_WEST },
			{ _action_hand_north, HALO_XR_HAND_NORTH },
			{ _action_hand_bumper, HALO_XR_HAND_BUMPER },
			{ _action_hand_stick, HALO_XR_HAND_STICK },
			{ _action_hand_menu, HALO_XR_HAND_MENU },
			{ _action_hand_view, HALO_XR_HAND_VIEW },
			{ _action_hand_thumb_touch, HALO_XR_HAND_THUMB_TOUCH },
			{ _action_hand_index_touch, HALO_XR_HAND_INDEX_TOUCH },
		};
		int side;

		for (side = 0; side < 2; side++)
		{
			for (index = 0; index < sizeof(hand_buttons) / sizeof(hand_buttons[0]); index++)
			{
				if (action_boolean_hand(hand_buttons[index].action, side))
					frame->hand_buttons[side] |= hand_buttons[index].bit;
			}
		}
		/* the Frame's d-pad is on its left controller */
		if (action_boolean(_action_dpad_up)) frame->hand_buttons[0] |= HALO_XR_HAND_DPAD_UP;
		if (action_boolean(_action_dpad_down)) frame->hand_buttons[0] |= HALO_XR_HAND_DPAD_DOWN;
		if (action_boolean(_action_dpad_left)) frame->hand_buttons[0] |= HALO_XR_HAND_DPAD_LEFT;
		if (action_boolean(_action_dpad_right)) frame->hand_buttons[0] |= HALO_XR_HAND_DPAD_RIGHT;
		/* Runtimes whose input table keeps one action per physical path (such as
		VIVE WAVE's XrInputSrc) fire the per-hand action bound last rather than
		both the pad and per-hand actions: mirror per-hand states into buttons. */
		if (frame->hand_buttons[1] & HALO_XR_HAND_SOUTH) frame->buttons |= HALO_XR_BUTTON_A;
		if (frame->hand_buttons[1] & HALO_XR_HAND_EAST) frame->buttons |= HALO_XR_BUTTON_B;
		if (frame->hand_buttons[0] & HALO_XR_HAND_SOUTH) frame->buttons |= HALO_XR_BUTTON_X;
		if (frame->hand_buttons[0] & HALO_XR_HAND_EAST) frame->buttons |= HALO_XR_BUTTON_Y;
		if (frame->hand_buttons[0] & HALO_XR_HAND_BUMPER) frame->buttons |= HALO_XR_BUTTON_WHITE;
		if (frame->hand_buttons[1] & HALO_XR_HAND_BUMPER) frame->buttons |= HALO_XR_BUTTON_BLACK;
		if ((frame->hand_buttons[0] | frame->hand_buttons[1]) & HALO_XR_HAND_MENU) frame->buttons |= HALO_XR_BUTTON_START;
		if ((frame->hand_buttons[0] | frame->hand_buttons[1]) & HALO_XR_HAND_VIEW) frame->buttons |= HALO_XR_BUTTON_BACK;
		if (frame->hand_buttons[0] & HALO_XR_HAND_STICK) frame->buttons |= HALO_XR_BUTTON_LEFT_THUMB;
		if (frame->hand_buttons[1] & HALO_XR_HAND_STICK) frame->buttons |= HALO_XR_BUTTON_RIGHT_THUMB;
	}
	for (hand = 0; hand < 2; hand++)
	{
		XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
		const XrSpaceLocationFlags valid = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;

		if (XR_SUCCEEDED(xrLocateSpace(xr.grip[hand], xr.local, xr.frame_state.predictedDisplayTime, &location)) &&
			(location.locationFlags & valid) == valid)
		{
			recentred_pose(&location.pose, &frame->grip[hand]);
			frame->hand_valid[hand] |= 1;
		}
		location.locationFlags = 0;
		if (XR_SUCCEEDED(xrLocateSpace(xr.aim[hand], xr.local, xr.frame_state.predictedDisplayTime, &location)) &&
			(location.locationFlags & valid) == valid)
		{
			recentred_pose(&location.pose, &frame->aim[hand]);
			frame->hand_valid[hand] |= 2;
		}
	}
}

/* ---------- session */

static int create_swapchain(int which, uint32_t width, uint32_t height)
{
	struct swapchain *swapchain = &xr.swapchains[which];
	XrSwapchainCreateInfo info = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
#ifdef _WIN32
	XrSwapchainImageOpenGLKHR images[HALO_XR_MAXIMUM_IMAGES];
#else
	XrSwapchainImageOpenGLESKHR images[HALO_XR_MAXIMUM_IMAGES];
#endif
	uint32_t count = 0, index;

	info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
		XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
	info.format = xr.color_format;
	info.sampleCount = 1;
	info.width = width;
	info.height = height;
	info.faceCount = 1;
	info.arraySize = 1;
	info.mipCount = 1;
	if (!check(xrCreateSwapchain(xr.session, &info, &swapchain->handle), "xrCreateSwapchain"))
		return 0;
	xrEnumerateSwapchainImages(swapchain->handle, 0, &count, NULL);
	if (count == 0 || count > HALO_XR_MAXIMUM_IMAGES)
	{
		host_logf(HOST_LOG_ERROR, "[openxr] swapchain has %u images (at most %d handled)", count, HALO_XR_MAXIMUM_IMAGES);
		return 0;
	}
	for (index = 0; index < count; index++)
	{
#ifdef _WIN32
		images[index].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
#else
		images[index].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
#endif
		images[index].next = NULL;
	}
	if (!check(xrEnumerateSwapchainImages(swapchain->handle, count, &count,
		(XrSwapchainImageBaseHeader *)images), "xrEnumerateSwapchainImages"))
	{
		return 0;
	}
	for (index = 0; index < count; index++)
		swapchain->images[index] = images[index].image;
	swapchain->count = count;
	swapchain->width = width;
	swapchain->height = height;
	/* (a new swapchain has no image of its own held: Test73) */
	swapchain->held = 0;
	host_logf(HOST_LOG_INFO, "[openxr] swapchain %d: %ux%u, %u images", which, width, height, count);
	return 1;
}

#ifdef _WIN32
/* the Windows loader finds the runtime itself (the registry's active runtime,
or XR_RUNTIME_JSON); nothing to give it */
static int initialize_loader(void)
{
	return 1;
}
#else
static int initialize_loader(void)
{
	PFN_xrInitializeLoaderKHR initialize = NULL;
	XrLoaderInitInfoAndroidKHR info = { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
	JNIEnv *environment = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = environment ? (jobject)SDL_GetAndroidActivity() : NULL;

	if (!environment || !activity || (*environment)->GetJavaVM(environment, &xr.vm) != 0)
	{
		host_logf(HOST_LOG_ERROR, "[openxr] no Java VM or activity: %s", SDL_GetError());
		return 0;
	}
	xr.activity = (*environment)->NewGlobalRef(environment, activity);
	(*environment)->DeleteLocalRef(environment, activity);
	if (!check(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&initialize),
		"xrGetInstanceProcAddr(xrInitializeLoaderKHR)"))
	{
		return 0;
	}
	info.applicationVM = xr.vm;
	info.applicationContext = xr.activity;
	return check(initialize((const XrLoaderInitInfoBaseHeaderKHR *)&info), "xrInitializeLoaderKHR");
}
#endif

static int create_instance(struct halo_xr_info *out)
{
#ifndef _WIN32
	XrInstanceCreateInfoAndroidKHR android = { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
#endif
	XrInstanceCreateInfo info = { XR_TYPE_INSTANCE_CREATE_INFO };
	XrInstanceProperties properties = { XR_TYPE_INSTANCE_PROPERTIES };
	XrSystemGetInfo system_info = { XR_TYPE_SYSTEM_GET_INFO };
	XrSystemProperties system = { XR_TYPE_SYSTEM_PROPERTIES };
	XrExtensionProperties *available = NULL;
	const char *extensions[12];
	uint32_t count = 0, index;
	/* Test73: listed once; the Steam Frame's wait for its runtime asks again
	each second (host_xr_init) */
	static int attempts;
	int quiet = attempts++ > 0;

	/* as many as the runtime has (the Quest 3's are 80 and rising): asked
	for their count first */
	if (check(xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL), "xrEnumerateInstanceExtensionProperties(count)") &&
		count > 0 && (available = calloc(count, sizeof(*available))) != NULL)
	{
		for (index = 0; index < count; index++)
			available[index].type = XR_TYPE_EXTENSION_PROPERTIES;
		if (!check(xrEnumerateInstanceExtensionProperties(NULL, count, &count, available), "xrEnumerateInstanceExtensionProperties"))
			count = 0;
	}
	else
	{
		count = 0;
	}
	if (!quiet)
		host_logf(HOST_LOG_INFO, "[openxr] %u extensions", count);
	for (index = 0; index < count; index++)
	{
		if (!quiet)
			host_logf(HOST_LOG_INFO, "[openxr] extension %s", available[index].extensionName);
		if (!strcmp(available[index].extensionName, FRAME_CONTROLLER_EXTENSION))
			xr.frame_controller = 1;
		if (!strcmp(available[index].extensionName, REFRESH_RATE_EXTENSION))
			xr.refresh_rate_extension = 1;
		if (!strcmp(available[index].extensionName, XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME))
			xr.performance_settings = 1;
		if (!strcmp(available[index].extensionName, BD_CONTROLLER_EXTENSION))
			xr.bd_controller = 1;
		if (!strcmp(available[index].extensionName, BD_ULTRA_CONTROLLER_EXTENSION))
			xr.bd_ultra_controller = 1;
		if (!strcmp(available[index].extensionName, HP_CONTROLLER_EXTENSION))
			xr.hp_controller = 1;
		if (!strcmp(available[index].extensionName, FOCUS3_CONTROLLER_EXTENSION))
			xr.focus3_controller = 1;
#ifdef _WIN32
		if (!strcmp(available[index].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME))
			xr.opengl_extension = 1;
#endif
	}
	free(available);
	count = 0;
#ifdef _WIN32
	if (!xr.opengl_extension)
	{
		/* (Virtual Desktop's own runtime offers Direct3D and Vulkan only:
		SteamVR's or Meta's runtime runs this build) */
		host_logf(HOST_LOG_ERROR, "[openxr] the runtime has no %s; choose SteamVR or the Meta runtime as the "
			"OpenXR runtime", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
		return 0;
	}
	extensions[count++] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
#else
	extensions[count++] = XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
	extensions[count++] = XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME;
#endif
	if (xr.frame_controller)
		extensions[count++] = FRAME_CONTROLLER_EXTENSION;
	if (xr.refresh_rate_extension)
		extensions[count++] = REFRESH_RATE_EXTENSION;
	if (xr.performance_settings)
		extensions[count++] = XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME;
	if (xr.bd_controller)
		extensions[count++] = BD_CONTROLLER_EXTENSION;
	if (xr.bd_ultra_controller)
		extensions[count++] = BD_ULTRA_CONTROLLER_EXTENSION;
	if (xr.hp_controller)
		extensions[count++] = HP_CONTROLLER_EXTENSION;
	if (xr.focus3_controller)
		extensions[count++] = FOCUS3_CONTROLLER_EXTENSION;
	for (index = 0; index < count && !quiet; index++)
		host_logf(HOST_LOG_INFO, "[openxr] enabling %s", extensions[index]);
#ifndef _WIN32
	android.applicationVM = xr.vm;
	android.applicationActivity = xr.activity;
	info.next = &android;
#endif
	strncpy(info.applicationInfo.applicationName, "Halo CE VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
	info.applicationInfo.applicationVersion = 1;
	strncpy(info.applicationInfo.engineName, "halo-ce-universal", XR_MAX_ENGINE_NAME_SIZE - 1);
	info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
	info.enabledExtensionCount = count;
	info.enabledExtensionNames = extensions;
	if (!check(xrCreateInstance(&info, &xr.instance), "xrCreateInstance"))
		return 0;
	xrGetInstanceProperties(xr.instance, &properties);
	xr.steamvr = strstr(properties.runtimeName, "SteamVR") != NULL;
	{
		/* Test73: the Steam Frame defers the releases (host_xr_release);
		HALO_XR_DEFERRED 1 or 0 says otherwise, on any runtime */
		const char *choice = getenv("HALO_XR_DEFERRED");
#ifndef _WIN32
		int lepton = host_lepton_running();
#else
		int lepton = 0;
#endif

		xr.defer_releases = choice && *choice ? atoi(choice) != 0 : xr.steamvr && lepton;
		if (xr.defer_releases || (choice && *choice))
			host_logf(HOST_LOG_INFO, "[openxr] images released %s", xr.defer_releases ?
				"together as each frame ends" : "each as it is drawn");
	}
	system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	if (!check(xrGetSystem(xr.instance, &system_info, &xr.system), "xrGetSystem"))
		return 0;
	xrGetSystemProperties(xr.instance, xr.system, &system);
	snprintf(out->runtime, sizeof(out->runtime), "%s %u.%u.%u", properties.runtimeName,
		XR_VERSION_MAJOR(properties.runtimeVersion), XR_VERSION_MINOR(properties.runtimeVersion),
		XR_VERSION_PATCH(properties.runtimeVersion));
	if (strstr(system.systemName, "WAVE") || strstr(properties.runtimeName, "VIVE"))
		snprintf(out->system, sizeof(out->system), "%s (Quest 2)", system.systemName);
	else
		snprintf(out->system, sizeof(out->system), "%s", system.systemName);
	return 1;
}

#ifdef _WIN32
static int create_session(void)
{
	PFN_xrGetOpenGLGraphicsRequirementsKHR requirements_function = NULL;
	XrGraphicsRequirementsOpenGLKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
	XrGraphicsBindingOpenGLWin32KHR binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
	XrSessionCreateInfo info = { XR_TYPE_SESSION_CREATE_INFO };
	XrReferenceSpaceCreateInfo space = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };

	if (!check(xrGetInstanceProcAddr(xr.instance, "xrGetOpenGLGraphicsRequirementsKHR",
		(PFN_xrVoidFunction *)&requirements_function), "xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)") ||
		!check(requirements_function(xr.instance, xr.system, &requirements), "xrGetOpenGLGraphicsRequirementsKHR"))
	{
		return 0;
	}
	host_logf(HOST_LOG_INFO, "[openxr] OpenGL required %u.%u .. %u.%u, context %s",
		XR_VERSION_MAJOR(requirements.minApiVersionSupported), XR_VERSION_MINOR(requirements.minApiVersionSupported),
		XR_VERSION_MAJOR(requirements.maxApiVersionSupported), XR_VERSION_MINOR(requirements.maxApiVersionSupported),
		(const char *)glGetString(GL_VERSION));
	binding.hDC = wglGetCurrentDC();
	binding.hGLRC = wglGetCurrentContext();
	if (!binding.hDC || !binding.hGLRC)
	{
		host_logf(HOST_LOG_ERROR, "[openxr] no current WGL context");
		return 0;
	}
	info.next = &binding;
	info.systemId = xr.system;
	if (!check(xrCreateSession(xr.instance, &info, &xr.session), "xrCreateSession"))
		return 0;
	space.poseInReferenceSpace.orientation.w = 1.0f;
	space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	if (!check(xrCreateReferenceSpace(xr.session, &space, &xr.local), "xrCreateReferenceSpace(LOCAL)"))
		return 0;
	space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	return check(xrCreateReferenceSpace(xr.session, &space, &xr.view), "xrCreateReferenceSpace(VIEW)");
}
#else
static int create_session(void)
{
	PFN_xrGetOpenGLESGraphicsRequirementsKHR requirements_function = NULL;
	XrGraphicsRequirementsOpenGLESKHR requirements = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
	XrGraphicsBindingOpenGLESAndroidKHR binding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
	XrSessionCreateInfo info = { XR_TYPE_SESSION_CREATE_INFO };
	XrReferenceSpaceCreateInfo space = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	EGLDisplay display = eglGetCurrentDisplay();
	EGLContext context = eglGetCurrentContext();
	EGLConfig config = NULL;
	EGLint config_id = 0, configs = 0;

	if (!check(xrGetInstanceProcAddr(xr.instance, "xrGetOpenGLESGraphicsRequirementsKHR",
		(PFN_xrVoidFunction *)&requirements_function), "xrGetInstanceProcAddr(xrGetOpenGLESGraphicsRequirementsKHR)") ||
		!check(requirements_function(xr.instance, xr.system, &requirements), "xrGetOpenGLESGraphicsRequirementsKHR"))
	{
		return 0;
	}
	host_logf(HOST_LOG_INFO, "[openxr] GLES required %u.%u .. %u.%u, context %s",
		XR_VERSION_MAJOR(requirements.minApiVersionSupported), XR_VERSION_MINOR(requirements.minApiVersionSupported),
		XR_VERSION_MAJOR(requirements.maxApiVersionSupported), XR_VERSION_MINOR(requirements.maxApiVersionSupported),
		(const char *)glGetString(GL_VERSION));
	if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT)
	{
		host_logf(HOST_LOG_ERROR, "[openxr] no current EGL context");
		return 0;
	}
	eglQueryContext(display, context, EGL_CONFIG_ID, &config_id);
	{
		const EGLint attributes[] = { EGL_CONFIG_ID, config_id, EGL_NONE };

		eglChooseConfig(display, attributes, &config, 1, &configs);
	}
	binding.display = display;
	binding.config = configs > 0 ? config : NULL;
	binding.context = context;
	info.next = &binding;
	info.systemId = xr.system;
	if (!check(xrCreateSession(xr.instance, &info, &xr.session), "xrCreateSession"))
		return 0;
	space.poseInReferenceSpace.orientation.w = 1.0f;
	space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	if (!check(xrCreateReferenceSpace(xr.session, &space, &xr.local), "xrCreateReferenceSpace(LOCAL)"))
		return 0;
	space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	return check(xrCreateReferenceSpace(xr.session, &space, &xr.view), "xrCreateReferenceSpace(VIEW)");
}
#endif

static int choose_format(void)
{
	/* the game's image is gamma-encoded already: an sRGB swapchain shows
	it as it is when written without conversion (vr_frame.c) */
	static const int64_t preferred[] = { GL_SRGB8_ALPHA8, GL_RGBA8 };
	int64_t *formats;
	uint32_t count = 0, index, want;

	/* as many as the runtime offers (the Quest 3, more than 64): asked for
	their count first */
	if (!check(xrEnumerateSwapchainFormats(xr.session, 0, &count, NULL), "xrEnumerateSwapchainFormats(count)") ||
		count == 0 || !(formats = calloc(count, sizeof(*formats))))
	{
		host_logf(HOST_LOG_ERROR, "[openxr] no swapchain formats (%u)", count);
		return 0;
	}
	if (!check(xrEnumerateSwapchainFormats(xr.session, count, &count, formats), "xrEnumerateSwapchainFormats"))
	{
		free(formats);
		return 0;
	}
	host_logf(HOST_LOG_INFO, "[openxr] %u swapchain formats", count);
	for (index = 0; index < count; index++)
		host_logf(HOST_LOG_INFO, "[openxr] swapchain format 0x%llx", (long long)formats[index]);
	for (want = 0; want < sizeof(preferred) / sizeof(preferred[0]) && !xr.color_format; want++)
	{
		for (index = 0; index < count; index++)
		{
			if (formats[index] == preferred[want])
				xr.color_format = formats[index];
		}
	}
	free(formats);
	if (!xr.color_format)
		host_logf(HOST_LOG_ERROR, "[openxr] neither SRGB8_ALPHA8 nor RGBA8 is offered");
	else
		host_logf(HOST_LOG_INFO, "[openxr] swapchain format chosen: 0x%llx (%s)", (long long)xr.color_format,
			xr.color_format == GL_SRGB8_ALPHA8 ? "SRGB8_ALPHA8" : "RGBA8");
	return xr.color_format != 0;
}

/* Test73: OpenXR could not be started. In the Steam Frame's Lepton the game
ends, its log saying why: the flat screen it would fall back on is not shown
there, and frames posted to it take the container down (host_xr_init) */
static int xr_unavailable(void)
{
#ifndef _WIN32
	if (host_lepton_running())
	{
		host_logf(HOST_LOG_ERROR, "[openxr] OpenXR could not be started: quitting, as Lepton has no flat screen to "
			"play on");
		_exit(1);
	}
#endif
	return -1;
}

int host_xr_init(struct halo_xr_info *out, uint32_t quad_width, uint32_t quad_height)
{
	XrViewConfigurationView views[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW }, { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
	uint32_t view_count = 0;
	int which, index;

	memset(out, 0, sizeof(*out));
	if (xr.failed)
		return -1;
	if (xr.initialized)
		goto describe;
	xr.failed = 1;
	if (!initialize_loader())
		return xr_unavailable();
	if (!create_instance(out))
	{
#ifndef _WIN32
		/* Test73 (astromaddie's HaloCE-VR, e92d54fa, CC0): in the Steam
		Frame's Lepton, SteamVR may not be running yet, or be asleep with the
		headset (it refuses the instance, XR_ERROR_RUNTIME_FAILURE): it is
		waited for, a minute. There is no flat screen to fall back on there
		(Lepton shows the app no window: the frames posted to it are refused
		and its display composer aborts, taking the container down), so the
		game ends instead, its log saying why; no message box, which nobody
		could close. Elsewhere (Quest, PICO) the flat screen stays */
		int attempt;
		struct timespec second = { 1, 0 };

		if (!host_lepton_running())
			return -1;
		for (attempt = 1;; attempt++)
		{
			/* (an instance made before its system failed, given back first) */
			if (xr.instance != XR_NULL_HANDLE)
			{
				xrDestroyInstance(xr.instance);
				xr.instance = XR_NULL_HANDLE;
			}
			if (attempt > 60)
			{
				host_logf(HOST_LOG_ERROR, "[openxr] no OpenXR runtime after a minute (is SteamVR running, the "
					"headset awake?): quitting, as Lepton has no flat screen to play on");
				_exit(1);
			}
			if (attempt % 10 == 1)
				host_logf(HOST_LOG_WARN, "[openxr] no runtime yet (the headset asleep?): trying again for a minute");
			nanosleep(&second, NULL);
			if (create_instance(out))
				break;
		}
#else
		return -1;
#endif
	}
	if (!create_session() || !choose_format())
		return xr_unavailable();
	if (!check(xrEnumerateViewConfigurationViews(xr.instance, xr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		2, &view_count, views), "xrEnumerateViewConfigurationViews") || view_count != 2)
	{
		return xr_unavailable();
	}
	for (index = 0; index < 2; index++)
	{
		if (!create_swapchain(index, views[index].recommendedImageRectWidth, views[index].recommendedImageRectHeight))
			return xr_unavailable();
	}
	xr.eye_max_width = views[0].maxImageRectWidth;
	xr.eye_max_height = views[0].maxImageRectHeight;
	if (!create_swapchain(HALO_XR_SWAPCHAIN_QUAD, quad_width, quad_height) ||
		!create_swapchain(HALO_XR_SWAPCHAIN_RETICLE, 256, 256) ||
		!create_swapchain(HALO_XR_SWAPCHAIN_FADE, 16, 16) ||
		!create_swapchain(HALO_XR_SWAPCHAIN_SCOPE, 768, 768) ||
		!create_swapchain(HALO_XR_SWAPCHAIN_WRIST, 512, 384))
	{
		return xr_unavailable();
	}
	xr.actions_ready = create_actions();
	if (!xr.actions_ready)
		host_logf(HOST_LOG_WARN, "[openxr] controller actions unavailable");
	host_logf(xr.actions_ready && xr.actions[_action_haptic] != XR_NULL_HANDLE ? HOST_LOG_INFO : HOST_LOG_WARN,
		"[openxr] haptic output action %s", xr.actions_ready && xr.actions[_action_haptic] != XR_NULL_HANDLE ? "ready" : "unavailable");
	xr.views[0].type = xr.views[1].type = XR_TYPE_VIEW;
	xr.recentre_pending = 1;
	clock_gettime(CLOCK_MONOTONIC, &xr.stats_start);
	xr.failed = 0;
	xr.initialized = 1;
	/* the runtime's GL binding brings its own contexts' trace markers */
	host_gl_quiet_trace_markers();
	host_logf(HOST_LOG_INFO, "[openxr] %s, %s: eyes %ux%u (max %ux%u), quad %ux%u, format 0x%llx, frame controller %s",
		out->runtime, out->system, views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight,
		views[0].maxImageRectWidth, views[0].maxImageRectHeight, quad_width, quad_height,
		(long long)xr.color_format, xr.frame_controller ? "yes" : "no");
describe:
	out->color_format = xr.color_format;
	for (which = 0; which < HALO_XR_SWAPCHAIN_COUNT; which++)
	{
		out->width[which] = xr.swapchains[which].width;
		out->height[which] = xr.swapchains[which].height;
		out->image_count[which] = xr.swapchains[which].count;
		memcpy(out->images[which], xr.swapchains[which].images, sizeof(out->images[which]));
	}
	return 0;
}

/* the eye images remade at another size (vr.resolution_scale,
vr.fov_mode): sharper than the runtime's recommendation, or smaller for a
narrower field of view. The new ones are made before the old ones go, so a
failure leaves the eyes as they were. Only info's eye entries change. */
int host_xr_resize_eyes(struct halo_xr_info *out, uint32_t width, uint32_t height)
{
	int eye, result = 0;

	if (!xr.initialized || xr.failed || xr.swapchains[0].acquired || xr.swapchains[1].acquired)
		return -1;
	if (xr.eye_max_width && width > xr.eye_max_width)
		width = xr.eye_max_width;
	if (xr.eye_max_height && height > xr.eye_max_height)
		height = xr.eye_max_height;
	if (width < 64)
		width = 64;
	if (height < 64)
		height = 64;
	for (eye = 0; eye < 2; eye++)
	{
		struct swapchain old = xr.swapchains[eye];

		if (old.width == width && old.height == height)
			continue;
		if (!create_swapchain(eye, width, height))
		{
			xr.swapchains[eye] = old;
			host_logf(HOST_LOG_ERROR, "[openxr] eye %d stays %ux%u: no swapchain of %ux%u", eye, old.width,
				old.height, width, height);
			/* (the other eye may be remade already: info told of both as
			they are, never of a destroyed image) */
			result = -1;
			break;
		}
		check(xrDestroySwapchain(old.handle), "xrDestroySwapchain");
	}
	for (eye = 0; eye < 2; eye++)
	{
		out->width[eye] = xr.swapchains[eye].width;
		out->height[eye] = xr.swapchains[eye].height;
		out->image_count[eye] = xr.swapchains[eye].count;
		memcpy(out->images[eye], xr.swapchains[eye].images, sizeof(out->images[eye]));
	}
	return result;
}

/* XR_EXT_performance_settings: the headset's CPU and GPU kept at their
sustained high levels. The game's main thread does the simulation, the
renderer's work and the sound's feeding; at a lower CPU level the headset
saves power by slowing it, and the sound starves first (crackling). */
static void set_performance_levels(void)
{
	PFN_xrPerfSettingsSetPerformanceLevelEXT set_level = NULL;

	if (!xr.performance_settings ||
		!check(xrGetInstanceProcAddr(xr.instance, "xrPerfSettingsSetPerformanceLevelEXT",
			(PFN_xrVoidFunction *)&set_level), "xrGetInstanceProcAddr(xrPerfSettingsSetPerformanceLevelEXT)"))
	{
		host_logf(HOST_LOG_INFO, "[openxr] performance levels: the runtime's own");
		return;
	}
	if (check(set_level(xr.session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT),
			"xrPerfSettingsSetPerformanceLevelEXT(CPU)") &&
		check(set_level(xr.session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT),
			"xrPerfSettingsSetPerformanceLevelEXT(GPU)"))
	{
		host_logf(HOST_LOG_INFO, "[openxr] performance levels: CPU and GPU sustained high");
	}
}

static const char *path_name(XrPath path)
{
	static char text[XR_MAX_PATH_LENGTH];
	uint32_t length = 0;

	text[0] = 0;
	if (path == XR_NULL_PATH || XR_FAILED(xrPathToString(xr.instance, path, sizeof(text), &length, text)))
		snprintf(text, sizeof(text), "%s", path == XR_NULL_PATH ? "(none)" : "(unknown)");
	return text;
}

/* the interaction profile each hand's controller is bound as (the Touch
controllers', usually), when it changes */
/* Test72: a profile's kind, for the game's names of its buttons */
static uint32_t profile_kind(const char *name)
{
	if (!name || name[0] != '/')
		return HALO_XR_PROFILE_UNKNOWN;
	if (strstr(name, "/oculus/touch_controller") || strstr(name, "/meta/touch") ||
		strstr(name, "/htc/vive_focus3_controller"))
		return HALO_XR_PROFILE_TOUCH;
	if (strstr(name, "/valve/index_controller"))
		return HALO_XR_PROFILE_INDEX;
	if (strstr(name, "/valve/frame_controller"))
		return HALO_XR_PROFILE_FRAME;
	if (strstr(name, "/htc/vive_controller"))
		return HALO_XR_PROFILE_VIVE;
	if (strstr(name, "/bytedance/"))
		return HALO_XR_PROFILE_PICO;
	if (strstr(name, "/hp/mixed_reality_controller"))
		return HALO_XR_PROFILE_HP;
	return HALO_XR_PROFILE_OTHER;
}

static void log_interaction_profiles(void)
{
	int hand;
	uint32_t kind = HALO_XR_PROFILE_UNKNOWN;

	for (hand = 0; hand < 2; hand++)
	{
		XrInteractionProfileState state = { XR_TYPE_INTERACTION_PROFILE_STATE };

		if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(xr.session, xr.hands[hand], &state)))
		{
			const char *name = path_name(state.interactionProfile);
			host_logf(HOST_LOG_INFO, "[openxr] %s hand: %s", hand ? "right" : "left", name);
			/* the right controller's, else the left's */
			if (kind == HALO_XR_PROFILE_UNKNOWN || hand == 1)
			{
				uint32_t this_kind = profile_kind(name);
				if (this_kind != HALO_XR_PROFILE_UNKNOWN)
					kind = this_kind;
			}
		}
	}
	xr.controller_profile = kind;
}

static void session_state_changed(XrSessionState state)
{
	host_logf(HOST_LOG_INFO, "[openxr] session %s", state_name(state));
	if (state == XR_SESSION_STATE_FOCUSED && !xr.focused)
		xr.recentre_pending = 1; /* where the player faces on gaining focus is forward */
	xr.state = state;
	xr.focused = state == XR_SESSION_STATE_FOCUSED;
	switch (state)
	{
	case XR_SESSION_STATE_READY:
	{
		XrSessionBeginInfo begin = { XR_TYPE_SESSION_BEGIN_INFO };

		begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		if (check(xrBeginSession(xr.session, &begin), "xrBeginSession"))
		{
			xr.running = 1;
			host_logf(HOST_LOG_INFO, "[openxr] session begun: frames go to the headset");
			set_performance_levels();
		}
		break;
	}
	case XR_SESSION_STATE_STOPPING:
		check(xrEndSession(xr.session), "xrEndSession");
		xr.running = 0;
		break;
	case XR_SESSION_STATE_EXITING:
	case XR_SESSION_STATE_LOSS_PENDING:
		xr.running = 0;
		xr.exiting = 1;
		break;
	default:
		break;
	}
}

static void poll_events(void)
{
	XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };

	while (xrPollEvent(xr.instance, &event) == XR_SUCCESS)
	{
		if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
			session_state_changed(((const XrEventDataSessionStateChanged *)&event)->state);
		else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
		{
			host_logf(HOST_LOG_WARN, "[openxr] the instance is to be lost");
			xr.exiting = 1;
		}
		else if (event.type == XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED)
			log_interaction_profiles();
		else if (event.type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING)
			host_logf(HOST_LOG_INFO, "[openxr] the reference space changes (recentred by the system)");
		else if (event.type == XR_TYPE_EVENT_DATA_EVENTS_LOST)
			host_logf(HOST_LOG_WARN, "[openxr] %u events lost",
				((const XrEventDataEventsLost *)&event)->lostEventCount);
		else if (event.type == XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT)
		{
			const XrEventDataPerfSettingsEXT *perf = (const XrEventDataPerfSettingsEXT *)&event;

			host_logf(HOST_LOG_WARN, "[openxr] performance notice: %s %s from level %d to %d",
				perf->domain == XR_PERF_SETTINGS_DOMAIN_CPU_EXT ? "CPU" : "GPU",
				perf->subDomain == XR_PERF_SETTINGS_SUB_DOMAIN_COMPOSITING_EXT ? "compositing" :
				perf->subDomain == XR_PERF_SETTINGS_SUB_DOMAIN_RENDERING_EXT ? "rendering" : "thermal",
				(int)perf->fromLevel, (int)perf->toLevel);
		}
		else
			host_logf(HOST_LOG_INFO, "[openxr] event %d", (int)event.type);
		event.type = XR_TYPE_EVENT_DATA_BUFFER;
		event.next = NULL;
	}
}

float host_xr_set_refresh_rate(float hertz);

static void log_statistics(void)
{
	struct timespec now;
	double seconds;

	clock_gettime(CLOCK_MONOTONIC, &now);
	seconds = (double)(now.tv_sec - xr.stats_start.tv_sec) + (now.tv_nsec - xr.stats_start.tv_nsec) * 1e-9;
	if (seconds < 10.0)
		return;
	{
		double resident, peak;

		memory_use(&resident, &peak);
		host_logf(HOST_LOG_INFO, "[vr-perf] %.1f frames/s (%lld rendered, %lld the runtime said not to render, %lld "
			"with no layer) over %.1f s, period %.3f ms, state %s; waiting on the headset %.2f ms average (%.2f "
			"longest), the game's frame %.2f ms average (%.2f longest); memory %.0f MB (peak %.0f)",
			xr.frames / seconds, xr.frames_rendered, xr.frames_not_rendered, xr.frames_without_layers, seconds,
			xr.frame_state.predictedDisplayPeriod * 1e-6, state_name(xr.state),
			xr.frames ? xr.wait_ms_total / xr.frames : 0.0, xr.wait_ms_max,
			xr.frames ? xr.game_ms_total / xr.frames : 0.0, xr.game_ms_max, resident, peak);
		xr.wait_ms_total = xr.wait_ms_max = xr.game_ms_total = xr.game_ms_max = 0.0;
		xr.frames_not_rendered = xr.frames_without_layers = 0;
	}
	/* A rate above 72 that the game does not hold while worn (focused) is
	worse than 72 held: after two windows of ten seconds short of 90% of
	it, fall back */
	if (xr.refresh_rate > 72.5f && xr.focused && xr.frames_rendered > 0)
	{
		if (xr.frames / seconds < xr.refresh_rate * 0.9f)
		{
			if (++xr.refresh_misses >= 2 && !xr.refresh_fallback_failed)
			{
				host_logf(HOST_LOG_WARN, "[openxr] %.0f Hz not held (%.1f frames/s): back to 72 Hz",
					xr.refresh_rate, xr.frames / seconds);
				/* (the Steam Frame's SteamVR offers only the rate its display
				settings have: there is no going back from here, and the
				runtime runs the game at half its rate. Said once, rather
				than asked again every twenty seconds) */
				if (host_xr_set_refresh_rate(72.0f) <= 0.0f || xr.refresh_rate > 72.5f)
				{
					host_logf(HOST_LOG_WARN, "[openxr] the runtime offers no rate at or below 72 Hz: set the "
						"headset's display to 72 Hz for this game to run at a full rate");
					xr.refresh_fallback_failed = 1;
				}
			}
		}
		else
		{
			xr.refresh_misses = 0;
		}
	}
	xr.frames = xr.frames_rendered = 0;
	xr.stats_start = now;
}

/* Waits for and begins the next frame when the session runs; otherwise
polls and sleeps briefly so the caller's loop keeps turning. */
int host_xr_begin_frame(struct halo_xr_frame *frame)
{
	XrFrameWaitInfo wait = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameBeginInfo begin = { XR_TYPE_FRAME_BEGIN_INFO };
	XrViewLocateInfo locate = { XR_TYPE_VIEW_LOCATE_INFO };
	XrViewState view_state = { XR_TYPE_VIEW_STATE };
	XrSpaceLocation head = { XR_TYPE_SPACE_LOCATION };
	uint32_t count = 0;
	int eye;

	memset(frame, 0, sizeof(*frame));
	if (!xr.initialized)
		return 0;
	poll_events();
	frame->session_state = (uint32_t)xr.state;
	frame->controller_profile = xr.controller_profile; /* Test72 */
	if (xr.exiting)
		frame->flags |= HALO_XR_FRAME_EXIT;
	if (!xr.running || xr.frame_begun)
	{
		if (!xr.running)
		{
			struct timespec pause = { 0, 10 * 1000 * 1000 };

			nanosleep(&pause, NULL);
		}
		return 0;
	}
	xr.frame_state.type = XR_TYPE_FRAME_STATE;
	xr.frame_state.next = NULL;
	{
		struct timespec before, after;
		double waited;

		clock_gettime(CLOCK_MONOTONIC, &before);
		if (!check(xrWaitFrame(xr.session, &wait, &xr.frame_state), "xrWaitFrame") ||
			!check(xrBeginFrame(xr.session, &begin), "xrBeginFrame"))
		{
			return 0;
		}
		clock_gettime(CLOCK_MONOTONIC, &after);
		waited = elapsed_ms(&before, &after);
		xr.wait_ms_total += waited;
		if (waited > xr.wait_ms_max)
			xr.wait_ms_max = waited;
		xr.frame_begun_at = after;
	}
	if (!xr.frame_state.shouldRender)
		xr.frames_not_rendered++;
	xr.frame_begun = 1;
	xr.frames++;
	frame->flags |= HALO_XR_FRAME_BEGUN;
	frame->predicted_display_time = xr.frame_state.predictedDisplayTime;
	frame->predicted_display_period = xr.frame_state.predictedDisplayPeriod;
	if (xr.frame_state.shouldRender)
		frame->flags |= HALO_XR_FRAME_SHOULD_RENDER;
	if (xr.focused)
		frame->flags |= HALO_XR_FRAME_FOCUSED;
	update_recentre(xr.frame_state.predictedDisplayTime);
	if (xr.recentred_this_frame)
		frame->flags |= HALO_XR_FRAME_RECENTRED;

	locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locate.displayTime = xr.frame_state.predictedDisplayTime;
	locate.space = xr.local;
	xr.views[0].type = xr.views[1].type = XR_TYPE_VIEW;
	xr.views[0].next = xr.views[1].next = NULL;
	xr.views_valid = XR_SUCCEEDED(xrLocateViews(xr.session, &locate, &view_state, 2, &count, xr.views)) &&
		count == 2 &&
		(view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) &&
		(view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);
	if (xr.views_valid)
	{
		frame->flags |= HALO_XR_FRAME_VIEWS_VALID;
		for (eye = 0; eye < 2; eye++)
		{
			recentred_pose(&xr.views[eye].pose, &frame->eye[eye]);
			frame->fov[eye][0] = xr.views[eye].fov.angleLeft;
			frame->fov[eye][1] = xr.views[eye].fov.angleRight;
			frame->fov[eye][2] = xr.views[eye].fov.angleUp;
			frame->fov[eye][3] = xr.views[eye].fov.angleDown;
		}
	}
	if (XR_SUCCEEDED(xrLocateSpace(xr.view, xr.local, xr.frame_state.predictedDisplayTime, &head)) &&
		(head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
	{
		recentred_pose(&head.pose, &frame->head);
	}
	else
	{
		frame->head.orientation[3] = 1.0f;
	}
	sync_input(frame);
	log_statistics();
	return 1;
}

/* the index of the acquired image in info.images[which], or -1 */
/* Test73: a swapchain's drawn image let go (host_xr_release) */
static void release_image(struct swapchain *swapchain)
{
	XrSwapchainImageReleaseInfo release = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };

	swapchain->held = 0;
	check(xrReleaseSwapchainImage(swapchain->handle, &release), "xrReleaseSwapchainImage");
}

int host_xr_acquire(uint32_t which)
{
	struct swapchain *swapchain;
	XrSwapchainImageAcquireInfo acquire = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	uint32_t index = 0;

	if (which >= HALO_XR_SWAPCHAIN_COUNT || !xr.frame_begun)
		return -1;
	swapchain = &xr.swapchains[which];
	if (swapchain->acquired)
		return -1;
	/* (Test73: drawn again in the frame, its last image let go first) */
	if (swapchain->held)
		release_image(swapchain);
	if (!check(xrAcquireSwapchainImage(swapchain->handle, &acquire, &index), "xrAcquireSwapchainImage"))
		return -1;
	wait.timeout = XR_INFINITE_DURATION;
	if (!check(xrWaitSwapchainImage(swapchain->handle, &wait), "xrWaitSwapchainImage"))
		return -1;
	swapchain->acquired = 1;
	return (int)index;
}

void host_xr_release(uint32_t which)
{
	struct swapchain *swapchain;

	if (which >= HALO_XR_SWAPCHAIN_COUNT || !xr.swapchains[which].acquired)
		return;
	swapchain = &xr.swapchains[which];
	swapchain->acquired = 0;
	/* Test73, the Steam Frame (after ringfall-mjolnir-vr, CC0, field-tested
	there): a release waits there for Mesa's GL thread (zink) or the GPU,
	and the frame's second half came late. The images are released
	together as the frame ends; the eyes' work, most of the frame's, goes
	to the GPU as soon as both are drawn */
	if (xr.defer_releases)
	{
		swapchain->held = 1;
		if (which == HALO_XR_SWAPCHAIN_RIGHT)
			glFlush();
		return;
	}
	release_image(swapchain);
}

/* Ends the frame begun by host_xr_begin_frame with the layers the guest
drew (their swapchains released); a projection layer uses this frame's
views, so it needs them valid. */
#ifndef _WIN32
/* Test73, the Steam Frame (after ringfall-mjolnir-vr's xr_fast_cores, CC0): Android in Lepton, without
XR_EXT_performance_settings to ask for the clocks, and the scheduler free to put the game's one busy thread, which
simulates and draws each frame, on a slow core now and then: frames late to the compositor, shown twice. The thread
that ends the frames is kept on the cores no slower than nine tenths of the fastest. */
static void xr_fast_cores(void)
{
	cpu_set_t set;
	long fastest = 0, speeds[64];
	int cpu, count = 0;
	char line[160] = "";

	for (cpu = 0; cpu < 64 && cpu < CPU_SETSIZE; cpu++)
	{
		char path[96];
		FILE *file;

		speeds[cpu] = 0;
		snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
		file = fopen(path, "r");
		if (!file)
			continue;
		if (fscanf(file, "%ld", &speeds[cpu]) != 1)
			speeds[cpu] = 0;
		fclose(file);
		if (speeds[cpu] > fastest)
			fastest = speeds[cpu];
	}
	if (!fastest)
	{
		host_logf(HOST_LOG_INFO, "[openxr] the cores' speeds are unknown: the frames' thread stays where it runs");
		return;
	}
	CPU_ZERO(&set);
	for (cpu = 0; cpu < 64 && cpu < CPU_SETSIZE; cpu++)
	{
		if (speeds[cpu] && speeds[cpu] * 10 >= fastest * 9)
		{
			char item[8];

			CPU_SET(cpu, &set);
			count++;
			snprintf(item, sizeof(item), " %d", cpu);
			if (strlen(line) + strlen(item) < sizeof(line))
				strcat(line, item);
		}
	}
	if (count && sched_setaffinity(0, sizeof(set), &set) == 0)
		host_logf(HOST_LOG_INFO, "[openxr] the frames' thread on the fast cores:%s (fastest %ld MHz)", line, fastest / 1000);
	else
		host_logf(HOST_LOG_INFO, "[openxr] the frames' thread could not be kept on the fast cores");
}
#endif

void host_xr_end_frame(const struct halo_xr_layers *layers)
{
	XrCompositionLayerProjectionView views[2];
	XrCompositionLayerProjection projection = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	XrCompositionLayerQuad quad = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	XrCompositionLayerQuad reticle = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	XrCompositionLayerQuad screen[2] = { { XR_TYPE_COMPOSITION_LAYER_QUAD }, { XR_TYPE_COMPOSITION_LAYER_QUAD } };
	XrCompositionLayerQuad fade = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	XrCompositionLayerQuad scope = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	XrCompositionLayerQuad wrist = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	const XrCompositionLayerBaseHeader *list[9];
	XrFrameEndInfo end = { XR_TYPE_FRAME_END_INFO };
	uint32_t count = 0;
	int which;

#ifndef _WIN32
	/* (Test73: the Steam Frame's frames' thread on its fast cores, once; SteamVR in Lepton only) */
	if (xr.steamvr && !xr.cores_pinned)
	{
		xr.cores_pinned = 1;
		if (host_lepton_running() && !getenv("HALO_XR_ANY_CORE"))
			xr_fast_cores();
	}
#endif

	if (!xr.frame_begun)
		return;
	for (which = 0; which < HALO_XR_SWAPCHAIN_COUNT; which++)
		host_xr_release((uint32_t)which);
	/* (Test73: the images drawn, released together: xr.defer_releases) */
	for (which = 0; which < HALO_XR_SWAPCHAIN_COUNT; which++)
	{
		if (xr.swapchains[which].held)
			release_image(&xr.swapchains[which]);
	}
	if (layers && (layers->flags & HALO_XR_LAYER_PROJECTION) && xr.views_valid && xr.frame_state.shouldRender)
	{
		int eye;

		for (eye = 0; eye < 2; eye++)
		{
			memset(&views[eye], 0, sizeof(views[eye]));
			views[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			views[eye].pose = xr.views[eye].pose;
			views[eye].fov = xr.views[eye].fov;
			/* the eyes drawn narrower than the runtime's view (vr.fov_mode):
			the compositor shows them over that part only */
			if (layers->flags & HALO_XR_LAYER_EYE_FOV)
			{
				views[eye].fov.angleLeft = layers->eye_fov[eye][0];
				views[eye].fov.angleRight = layers->eye_fov[eye][1];
				views[eye].fov.angleUp = layers->eye_fov[eye][2];
				views[eye].fov.angleDown = layers->eye_fov[eye][3];
			}
			views[eye].subImage.swapchain = xr.swapchains[eye].handle;
			views[eye].subImage.imageRect.extent.width = (int32_t)xr.swapchains[eye].width;
			views[eye].subImage.imageRect.extent.height = (int32_t)xr.swapchains[eye].height;
		}
		projection.space = xr.local;
		projection.viewCount = 2;
		projection.views = views;
		list[count++] = (const XrCompositionLayerBaseHeader *)&projection;
	}
	/* a 3D screen: each eye sees its own image on the same quad */
	if (layers && (layers->flags & HALO_XR_LAYER_STEREO_SCREEN) && xr.frame_state.shouldRender)
	{
		int eye;

		for (eye = 0; eye < 2; eye++)
		{
			const struct swapchain *swapchain = &xr.swapchains[eye];

			screen[eye].eyeVisibility = eye ? XR_EYE_VISIBILITY_RIGHT : XR_EYE_VISIBILITY_LEFT;
			screen[eye].subImage.swapchain = swapchain->handle;
			screen[eye].subImage.imageRect.extent.width = (int32_t)swapchain->width;
			screen[eye].subImage.imageRect.extent.height = (int32_t)swapchain->height;
			screen[eye].size.width = layers->quad_size[0];
			screen[eye].size.height = layers->quad_size[1];
			screen[eye].space = xr.local;
			local_pose(&layers->quad_pose, &screen[eye].pose);
			list[count++] = (const XrCompositionLayerBaseHeader *)&screen[eye];
		}
	}
	/* the reticle sits in the world, under the HUD; the menus' pointer over
	their screen (the layers are drawn in order: an opaque screen after the
	dot would hide it) */
	if (layers && (layers->flags & HALO_XR_LAYER_RETICLE) && xr.frame_state.shouldRender)
	{
		const struct swapchain *swapchain = &xr.swapchains[HALO_XR_SWAPCHAIN_RETICLE];

		reticle.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		reticle.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		reticle.subImage.swapchain = swapchain->handle;
		reticle.subImage.imageRect.extent.width = (int32_t)swapchain->width;
		reticle.subImage.imageRect.extent.height = (int32_t)swapchain->height;
		reticle.size.width = layers->reticle_size[0];
		reticle.size.height = layers->reticle_size[1];
		reticle.space = xr.local;
		local_pose(&layers->reticle_pose, &reticle.pose);
		if (!(layers->flags & HALO_XR_LAYER_RETICLE_ON_TOP))
			list[count++] = (const XrCompositionLayerBaseHeader *)&reticle;
	}
	if (layers && (layers->flags & HALO_XR_LAYER_QUAD) && xr.frame_state.shouldRender)
	{
		const struct swapchain *swapchain = &xr.swapchains[HALO_XR_SWAPCHAIN_QUAD];

		if (layers->flags & HALO_XR_LAYER_QUAD_ALPHA)
			quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = swapchain->handle;
		quad.subImage.imageRect.extent.width = (int32_t)swapchain->width;
		quad.subImage.imageRect.extent.height = (int32_t)swapchain->height;
		quad.size.width = layers->quad_size[0];
		quad.size.height = layers->quad_size[1];
		if (layers->flags & HALO_XR_LAYER_QUAD_HEAD_LOCKED)
		{
			quad.space = xr.view;
			memcpy(&quad.pose.position, layers->quad_pose.position, sizeof(quad.pose.position));
			memcpy(&quad.pose.orientation, layers->quad_pose.orientation, sizeof(quad.pose.orientation));
		}
		else
		{
			quad.space = xr.local;
			local_pose(&layers->quad_pose, &quad.pose);
		}
		list[count++] = (const XrCompositionLayerBaseHeader *)&quad;
	}
	if (layers && (layers->flags & (HALO_XR_LAYER_RETICLE | HALO_XR_LAYER_RETICLE_ON_TOP)) ==
		(HALO_XR_LAYER_RETICLE | HALO_XR_LAYER_RETICLE_ON_TOP) && xr.frame_state.shouldRender)
	{
		list[count++] = (const XrCompositionLayerBaseHeader *)&reticle;
	}
	/* a weapon's scope, held in the hand: nearer than the HUD */
	if (layers && (layers->flags & HALO_XR_LAYER_SCOPE) && xr.frame_state.shouldRender)
	{
		const struct swapchain *swapchain = &xr.swapchains[HALO_XR_SWAPCHAIN_SCOPE];

		scope.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		scope.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		scope.subImage.swapchain = swapchain->handle;
		scope.subImage.imageRect.extent.width = (int32_t)swapchain->width;
		scope.subImage.imageRect.extent.height = (int32_t)swapchain->height;
		scope.size.width = layers->scope_size[0];
		scope.size.height = layers->scope_size[1];
		scope.space = xr.local;
		local_pose(&layers->scope_pose, &scope.pose);
		list[count++] = (const XrCompositionLayerBaseHeader *)&scope;
	}
	/* test26: the wrist HUD, on the off hand's wrist: nearer than the HUD */
	if (layers && (layers->flags & HALO_XR_LAYER_WRIST) && xr.frame_state.shouldRender)
	{
		const struct swapchain *swapchain = &xr.swapchains[HALO_XR_SWAPCHAIN_WRIST];

		wrist.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		wrist.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		wrist.subImage.swapchain = swapchain->handle;
		wrist.subImage.imageRect.extent.width = (int32_t)swapchain->width;
		wrist.subImage.imageRect.extent.height = (int32_t)swapchain->height;
		wrist.size.width = layers->wrist_size[0];
		wrist.size.height = layers->wrist_size[1];
		wrist.space = xr.local;
		local_pose(&layers->wrist_pose, &wrist.pose);
		list[count++] = (const XrCompositionLayerBaseHeader *)&wrist;
	}
	/* a fade to black over everything: a quad just ahead of the eyes,
	wider than they see */
	if (layers && (layers->flags & HALO_XR_LAYER_FADE) && xr.frame_state.shouldRender)
	{
		const struct swapchain *swapchain = &xr.swapchains[HALO_XR_SWAPCHAIN_FADE];

		fade.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		fade.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
		fade.subImage.swapchain = swapchain->handle;
		fade.subImage.imageRect.extent.width = (int32_t)swapchain->width;
		fade.subImage.imageRect.extent.height = (int32_t)swapchain->height;
		fade.size.width = fade.size.height = 2.0f;
		fade.space = xr.view;
		fade.pose.orientation.w = 1.0f;
		fade.pose.position.z = -0.25f;
		list[count++] = (const XrCompositionLayerBaseHeader *)&fade;
	}
	if (count)
		xr.frames_rendered++;
	else
		xr.frames_without_layers++;
	{
		struct timespec now;
		double game;
		uint32_t flags = layers ? layers->flags : 0;

		clock_gettime(CLOCK_MONOTONIC, &now);
		game = elapsed_ms(&xr.frame_begun_at, &now);
		xr.game_ms_total += game;
		if (game > xr.game_ms_max)
			xr.game_ms_max = game;
		/* what is shown, when it changes: the projection (stereo), the flat
		screen or HUD quad, the reticle, a 3D screen, the fade, the scope */
		if (flags != xr.last_layer_flags || (count && !xr.first_frame_logged))
		{
			host_logf(HOST_LOG_INFO, "[openxr] layers now:%s%s%s%s%s%s%s%s (%u submitted%s)",
				(flags & HALO_XR_LAYER_PROJECTION) ? " stereo" : "",
				(flags & HALO_XR_LAYER_QUAD) ? ((flags & HALO_XR_LAYER_QUAD_HEAD_LOCKED) ? " hud" : " screen") : "",
				(flags & HALO_XR_LAYER_RETICLE) ? ((flags & HALO_XR_LAYER_RETICLE_ON_TOP) ? " pointer" : " reticle") : "",
				(flags & HALO_XR_LAYER_STEREO_SCREEN) ? " 3d-screen" : "",
				(flags & HALO_XR_LAYER_FADE) ? " fade" : "",
				(flags & HALO_XR_LAYER_SCOPE) ? " scope" : "",
				(flags & HALO_XR_LAYER_WRIST) ? " wrist" : "",
				flags ? "" : " none",
				count, xr.views_valid ? "" : ", head not tracked");
			xr.last_layer_flags = flags;
		}
	}
	end.displayTime = xr.frame_state.predictedDisplayTime;
	end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end.layerCount = count;
	end.layers = list;
	if (check(xrEndFrame(xr.session, &end), "xrEndFrame") && count)
		host_debug_frame_shown();
	if (count && !xr.first_frame_logged)
	{
		xr.first_frame_logged = 1;
		host_logf(HOST_LOG_INFO, "[openxr] the first frame is in the headset");
	}
	xr.frame_begun = 0;
}

/* asks the runtime for a display refresh rate (XR_FB_display_refresh_rate):
the nearest it offers at or below `hertz`; returns the rate asked for, or 0
when the runtime cannot change it */
float host_xr_set_refresh_rate(float hertz)
{
	PFN_xrEnumerateDisplayRefreshRatesFB enumerate = NULL;
	PFN_xrRequestDisplayRefreshRateFB request = NULL;
	float rates[32], chosen = 0.0f;
	uint32_t count = 0, index;

	if (!xr.initialized || !xr.refresh_rate_extension ||
		!isfinite(hertz) || hertz < 0.0f ||
		XR_FAILED(xrGetInstanceProcAddr(xr.instance, "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction *)&request)))
	{
		host_logf(HOST_LOG_WARN, "[openxr] the display's refresh rate cannot be chosen");
		return 0.0f;
	}
	/* OpenXR defines 0.0 as no application preference. This restores the
	 * runtime's choice without requiring rate enumeration. */
	if (hertz == 0.0f)
	{
		if (!check(request(xr.session, 0.0f), "xrRequestDisplayRefreshRateFB(default)"))
			return 0.0f;
		host_logf(HOST_LOG_INFO, "[openxr] display refresh rate runtime default requested");
		xr.refresh_rate = 0.0f;
		xr.refresh_misses = 0;
		return 0.0f;
	}
	if (XR_FAILED(xrGetInstanceProcAddr(xr.instance, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction *)&enumerate)) ||
		!check(enumerate(xr.session, 0, &count, NULL), "xrEnumerateDisplayRefreshRatesFB(count)") ||
		count > 32 ||
		!check(enumerate(xr.session, count, &count, rates), "xrEnumerateDisplayRefreshRatesFB"))
	{
		host_logf(HOST_LOG_WARN, "[openxr] the display's refresh rate cannot be chosen (%u rates)", count);
		return 0.0f;
	}
	for (index = 0; index < count; index++)
	{
		host_logf(HOST_LOG_INFO, "[openxr] display refresh rate %.1f Hz offered", rates[index]);
		if (rates[index] <= hertz + 0.5f && rates[index] > chosen)
			chosen = rates[index];
	}
	/* If the runtime only offers rates above the requested target (e.g. VIVE
	 * WAVE offering 75 Hz and 90 Hz when 72 Hz is requested), pick the lowest
	 * offered rate rather than failing. */
	if (chosen <= 0.0f)
	{
		for (index = 0; index < count; index++)
		{
			if (rates[index] > 0.0f && (chosen <= 0.0f || rates[index] < chosen))
				chosen = rates[index];
		}
	}
	if (chosen <= 0.0f || !check(request(xr.session, chosen), "xrRequestDisplayRefreshRateFB"))
		return 0.0f;
	host_logf(HOST_LOG_INFO, "[openxr] display refresh rate %.1f Hz asked for", chosen);
	xr.refresh_rate = chosen;
	xr.refresh_misses = 0;
	return chosen;
}

void host_xr_recenter(void)
{
	xr.recentre_pending = 1;
}

void host_xr_haptic(uint32_t hand, float amplitude, float seconds)
{
	XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
	XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
	XrResult result;

	if (!xr.actions_ready || !xr.focused || hand > 1)
		return;
	info.action = xr.actions[_action_haptic];
	info.subactionPath = xr.hands[hand];
	if (info.action == XR_NULL_HANDLE || info.subactionPath == XR_NULL_PATH)
	{
		host_logf(HOST_LOG_WARN, "[openxr] haptic output skipped: action or hand path is unavailable");
		return;
	}
	vibration.amplitude = amplitude;
	vibration.duration = (XrDuration)(seconds * 1e9f);
	vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
	result = xrApplyHapticFeedback(xr.session, &info, (const XrHapticBaseHeader *)&vibration);
	if (check(result, "xrApplyHapticFeedback") && !haptic_output_logged)
	{
		haptic_output_logged = 1;
		host_logf(HOST_LOG_INFO, "[openxr] controller haptic output accepted by runtime");
	}
}

#endif /* HALO_VR */
