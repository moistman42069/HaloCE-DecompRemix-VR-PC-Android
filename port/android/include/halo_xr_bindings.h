#ifndef HALO_XR_BINDINGS_H
#define HALO_XR_BINDINGS_H
/* Test72: the OpenXR action layer every host shares (the Android host:
Quest, PICO, Steam Frame; the Windows PCVR host): the actions, each
controller profile's bindings to them, and the profiles' paths, each
checked against the Khronos registry and Valve's Frame documentation by
tools/test_openxr_bindings.py (one path a profile lacks discards the whole
profile). Moved verbatim from port/android/host/host_xr.c. The game sees
only the logical buttons (halo_android_abi.h), never a controller. */

enum
{
	_action_move,
	_action_look,
	_action_trigger_left,
	_action_trigger_right,
	_action_squeeze_left,
	_action_squeeze_right,
	_action_a,
	_action_b,
	_action_x,
	_action_y,
	_action_white,
	_action_black,
	_action_start,
	_action_back,
	_action_left_thumb,
	_action_right_thumb,
	_action_dpad_up,
	_action_dpad_down,
	_action_dpad_left,
	_action_dpad_right,
	_action_grip_pose,
	_action_aim_pose,
	_action_haptic,
	/* each hand's buttons as they are (hand_buttons) */
	_action_hand_south,
	_action_hand_east,
	_action_hand_west,
	_action_hand_north,
	_action_hand_bumper,
	_action_hand_stick,
	_action_hand_menu,
	_action_hand_view,
	/* where the fingers rest (Touch's capacitive sensors), for the hands'
	finger poses: the thumb on a face button, the stick or the thumbrest;
	the index finger on the trigger */
	_action_hand_thumb_touch,
	_action_hand_index_touch,
	NUMBER_OF_ACTIONS
};

struct binding
{
	int action;
	const char *path;
};

#define POSES(hand) \
	{ _action_grip_pose, "/user/hand/" hand "/input/grip/pose" }, \
	{ _action_aim_pose, "/user/hand/" hand "/input/aim/pose" }, \
	{ _action_haptic, "/user/hand/" hand "/output/haptic" }

/* Steam Frame controllers: a split gamepad. Right: A/B/X/Y, menu,
bumper, trigger, grip; left: d-pad, view, bumper, trigger, grip
(Steamworks "Steam Frame Input"). The Xbox pad maps across as it is. */
static const struct binding halo_xr_bindings_frame[] =
{
	{ _action_move, "/user/hand/left/input/thumbstick" },
	{ _action_look, "/user/hand/right/input/thumbstick" },
	{ _action_trigger_left, "/user/hand/left/input/trigger/value" },
	{ _action_trigger_right, "/user/hand/right/input/trigger/value" },
	{ _action_squeeze_left, "/user/hand/left/input/squeeze/value" },
	{ _action_squeeze_right, "/user/hand/right/input/squeeze/value" },
	{ _action_a, "/user/hand/right/input/a/click" },
	{ _action_b, "/user/hand/right/input/b/click" },
	{ _action_x, "/user/hand/right/input/x/click" },
	{ _action_y, "/user/hand/right/input/y/click" },
	{ _action_white, "/user/hand/left/input/bumper/click" },
	{ _action_black, "/user/hand/right/input/bumper/click" },
	{ _action_start, "/user/hand/right/input/menu/click" },
	{ _action_back, "/user/hand/left/input/view/click" },
	{ _action_left_thumb, "/user/hand/left/input/thumbstick/click" },
	{ _action_right_thumb, "/user/hand/right/input/thumbstick/click" },
	{ _action_dpad_up, "/user/hand/left/input/dpad_up/click" },
	{ _action_dpad_down, "/user/hand/left/input/dpad_down/click" },
	{ _action_dpad_left, "/user/hand/left/input/dpad_left/click" },
	{ _action_dpad_right, "/user/hand/left/input/dpad_right/click" },
	POSES("left"), POSES("right"),
	{ _action_hand_south, "/user/hand/right/input/a/click" },
	{ _action_hand_east, "/user/hand/right/input/b/click" },
	{ _action_hand_west, "/user/hand/right/input/x/click" },
	{ _action_hand_north, "/user/hand/right/input/y/click" },
	{ _action_hand_bumper, "/user/hand/left/input/bumper/click" },
	{ _action_hand_bumper, "/user/hand/right/input/bumper/click" },
	{ _action_hand_stick, "/user/hand/left/input/thumbstick/click" },
	{ _action_hand_stick, "/user/hand/right/input/thumbstick/click" },
	{ _action_hand_menu, "/user/hand/right/input/menu/click" },
	{ _action_hand_view, "/user/hand/left/input/view/click" },
};

/* Test73: where the fingers rest on the Steam Frame controllers (each
button's touch in "Steam Frame Input"), for the hands' finger poses: the
thumb on the stick or a face button, the index finger on the trigger.
Suggested together with halo_xr_bindings_frame, which is suggested alone if
the runtime refuses them (suggest_pair in host_xr.c) */
static const struct binding halo_xr_bindings_frame_fingers[] =
{
	{ _action_hand_thumb_touch, "/user/hand/left/input/thumbstick/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/thumbstick/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/a/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/b/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/x/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/y/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/menu/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/dpad_up/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/dpad_down/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/dpad_left/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/dpad_right/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/view/touch" },
	{ _action_hand_index_touch, "/user/hand/left/input/trigger/touch" },
	{ _action_hand_index_touch, "/user/hand/right/input/trigger/touch" },
};

/* Touch: X/Y on the left, A/B on the right */
static const struct binding halo_xr_bindings_touch[] =
{
	{ _action_move, "/user/hand/left/input/thumbstick" },
	{ _action_look, "/user/hand/right/input/thumbstick" },
	{ _action_trigger_left, "/user/hand/left/input/trigger/value" },
	{ _action_trigger_right, "/user/hand/right/input/trigger/value" },
	{ _action_squeeze_left, "/user/hand/left/input/squeeze/value" },
	{ _action_squeeze_right, "/user/hand/right/input/squeeze/value" },
	{ _action_a, "/user/hand/right/input/a/click" },
	{ _action_b, "/user/hand/right/input/b/click" },
	{ _action_x, "/user/hand/left/input/x/click" },
	{ _action_y, "/user/hand/left/input/y/click" },
	{ _action_start, "/user/hand/left/input/menu/click" },
	{ _action_left_thumb, "/user/hand/left/input/thumbstick/click" },
	{ _action_right_thumb, "/user/hand/right/input/thumbstick/click" },
	POSES("left"), POSES("right"),
	{ _action_hand_south, "/user/hand/right/input/a/click" },
	{ _action_hand_south, "/user/hand/left/input/x/click" },
	{ _action_hand_east, "/user/hand/right/input/b/click" },
	{ _action_hand_east, "/user/hand/left/input/y/click" },
	{ _action_hand_stick, "/user/hand/left/input/thumbstick/click" },
	{ _action_hand_stick, "/user/hand/right/input/thumbstick/click" },
	{ _action_hand_menu, "/user/hand/left/input/menu/click" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/thumbrest/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/thumbrest/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/thumbstick/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/thumbstick/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/x/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/y/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/a/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/b/touch" },
	{ _action_hand_index_touch, "/user/hand/left/input/trigger/touch" },
	{ _action_hand_index_touch, "/user/hand/right/input/trigger/touch" },
};

static const struct binding halo_xr_bindings_index[] =
{
	{ _action_move, "/user/hand/left/input/thumbstick" },
	{ _action_look, "/user/hand/right/input/thumbstick" },
	{ _action_trigger_left, "/user/hand/left/input/trigger/value" },
	{ _action_trigger_right, "/user/hand/right/input/trigger/value" },
	{ _action_squeeze_left, "/user/hand/left/input/squeeze/value" },
	{ _action_squeeze_right, "/user/hand/right/input/squeeze/value" },
	{ _action_a, "/user/hand/right/input/a/click" },
	{ _action_b, "/user/hand/right/input/b/click" },
	{ _action_x, "/user/hand/left/input/a/click" },
	{ _action_y, "/user/hand/left/input/b/click" },
	{ _action_start, "/user/hand/left/input/system/click" },
	{ _action_left_thumb, "/user/hand/left/input/thumbstick/click" },
	{ _action_right_thumb, "/user/hand/right/input/thumbstick/click" },
	POSES("left"), POSES("right"),
	{ _action_hand_south, "/user/hand/right/input/a/click" },
	{ _action_hand_south, "/user/hand/left/input/a/click" },
	{ _action_hand_east, "/user/hand/right/input/b/click" },
	{ _action_hand_east, "/user/hand/left/input/b/click" },
	{ _action_hand_stick, "/user/hand/left/input/thumbstick/click" },
	{ _action_hand_stick, "/user/hand/right/input/thumbstick/click" },
	{ _action_hand_menu, "/user/hand/left/input/system/click" },
};

/* Test73: the Index controllers' touches, for the finger poses, suggested
with halo_xr_bindings_index as the Frame's are (suggest_pair) */
static const struct binding halo_xr_bindings_index_fingers[] =
{
	{ _action_hand_thumb_touch, "/user/hand/left/input/thumbstick/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/thumbstick/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/a/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/b/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/a/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/b/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/trackpad/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/trackpad/touch" },
	{ _action_hand_index_touch, "/user/hand/left/input/trigger/touch" },
	{ _action_hand_index_touch, "/user/hand/right/input/trigger/touch" },
};

/* Test72: PICO 4, Neo 3 and 4 Ultra controllers: Touch's layout (A/B
right, X/Y and menu left) without a thumbrest; the system button is the
headset's own. One table for the three profiles, each path checked against
them (tools/test_openxr_bindings.py) */
static const struct binding halo_xr_bindings_pico[] =
{
	{ _action_move, "/user/hand/left/input/thumbstick" },
	{ _action_look, "/user/hand/right/input/thumbstick" },
	{ _action_trigger_left, "/user/hand/left/input/trigger/value" },
	{ _action_trigger_right, "/user/hand/right/input/trigger/value" },
	{ _action_squeeze_left, "/user/hand/left/input/squeeze/value" },
	{ _action_squeeze_right, "/user/hand/right/input/squeeze/value" },
	{ _action_a, "/user/hand/right/input/a/click" },
	{ _action_b, "/user/hand/right/input/b/click" },
	{ _action_x, "/user/hand/left/input/x/click" },
	{ _action_y, "/user/hand/left/input/y/click" },
	{ _action_start, "/user/hand/left/input/menu/click" },
	{ _action_left_thumb, "/user/hand/left/input/thumbstick/click" },
	{ _action_right_thumb, "/user/hand/right/input/thumbstick/click" },
	POSES("left"), POSES("right"),
	{ _action_hand_south, "/user/hand/right/input/a/click" },
	{ _action_hand_south, "/user/hand/left/input/x/click" },
	{ _action_hand_east, "/user/hand/right/input/b/click" },
	{ _action_hand_east, "/user/hand/left/input/y/click" },
	{ _action_hand_stick, "/user/hand/left/input/thumbstick/click" },
	{ _action_hand_stick, "/user/hand/right/input/thumbstick/click" },
	{ _action_hand_menu, "/user/hand/left/input/menu/click" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/thumbstick/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/thumbstick/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/x/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/y/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/a/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/b/touch" },
	{ _action_hand_index_touch, "/user/hand/left/input/trigger/touch" },
	{ _action_hand_index_touch, "/user/hand/right/input/trigger/touch" },
};

/* Test72: the HP Reverb G2 controllers, under Windows Mixed Reality's own
runtime (SteamVR remaps them itself): Touch's layout (A/B right, X/Y and the
menu left) with no capacitive sensors; the right menu button is left free, as
Touch's system button is */
static const struct binding halo_xr_bindings_hp[] =
{
	{ _action_move, "/user/hand/left/input/thumbstick" },
	{ _action_look, "/user/hand/right/input/thumbstick" },
	{ _action_trigger_left, "/user/hand/left/input/trigger/value" },
	{ _action_trigger_right, "/user/hand/right/input/trigger/value" },
	{ _action_squeeze_left, "/user/hand/left/input/squeeze/value" },
	{ _action_squeeze_right, "/user/hand/right/input/squeeze/value" },
	{ _action_a, "/user/hand/right/input/a/click" },
	{ _action_b, "/user/hand/right/input/b/click" },
	{ _action_x, "/user/hand/left/input/x/click" },
	{ _action_y, "/user/hand/left/input/y/click" },
	{ _action_start, "/user/hand/left/input/menu/click" },
	{ _action_left_thumb, "/user/hand/left/input/thumbstick/click" },
	{ _action_right_thumb, "/user/hand/right/input/thumbstick/click" },
	POSES("left"), POSES("right"),
	{ _action_hand_south, "/user/hand/right/input/a/click" },
	{ _action_hand_south, "/user/hand/left/input/x/click" },
	{ _action_hand_east, "/user/hand/right/input/b/click" },
	{ _action_hand_east, "/user/hand/left/input/y/click" },
	{ _action_hand_stick, "/user/hand/left/input/thumbstick/click" },
	{ _action_hand_stick, "/user/hand/right/input/thumbstick/click" },
	{ _action_hand_menu, "/user/hand/left/input/menu/click" },
};

/* HTC Vive Focus 3 and Vive XR Elite controllers
(XR_HTC_vive_focus3_controller_interaction): Touch's layout (A/B right, X/Y and
menu left) with capacitive sensors on the thumbrest, stick and trigger, but not
on the A/B/X/Y face buttons (VIVE WAVE's XrInputSrc maps face-button touch paths
onto their click paths and overwrites the click bindings if suggested). */
static const struct binding halo_xr_bindings_focus3[] =
{
	{ _action_move, "/user/hand/left/input/thumbstick" },
	{ _action_look, "/user/hand/right/input/thumbstick" },
	{ _action_trigger_left, "/user/hand/left/input/trigger/value" },
	{ _action_trigger_right, "/user/hand/right/input/trigger/value" },
	{ _action_squeeze_left, "/user/hand/left/input/squeeze/value" },
	{ _action_squeeze_right, "/user/hand/right/input/squeeze/value" },
	{ _action_a, "/user/hand/right/input/a/click" },
	{ _action_b, "/user/hand/right/input/b/click" },
	{ _action_x, "/user/hand/left/input/x/click" },
	{ _action_y, "/user/hand/left/input/y/click" },
	{ _action_start, "/user/hand/left/input/menu/click" },
	{ _action_left_thumb, "/user/hand/left/input/thumbstick/click" },
	{ _action_right_thumb, "/user/hand/right/input/thumbstick/click" },
	POSES("left"), POSES("right"),
	{ _action_hand_south, "/user/hand/right/input/a/click" },
	{ _action_hand_south, "/user/hand/left/input/x/click" },
	{ _action_hand_east, "/user/hand/right/input/b/click" },
	{ _action_hand_east, "/user/hand/left/input/y/click" },
	{ _action_hand_stick, "/user/hand/left/input/thumbstick/click" },
	{ _action_hand_stick, "/user/hand/right/input/thumbstick/click" },
	{ _action_hand_menu, "/user/hand/left/input/menu/click" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/thumbrest/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/thumbrest/touch" },
	{ _action_hand_thumb_touch, "/user/hand/left/input/thumbstick/touch" },
	{ _action_hand_thumb_touch, "/user/hand/right/input/thumbstick/touch" },
	{ _action_hand_index_touch, "/user/hand/left/input/trigger/touch" },
	{ _action_hand_index_touch, "/user/hand/right/input/trigger/touch" },
};

#endif
