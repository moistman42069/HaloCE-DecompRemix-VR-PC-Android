# Halo CE Quest VR + Android 1.0.19

**OpenCE Build 157 base · network 24 · Android version code 63**

> **VR feature status — read before enabling options:** Several physical-interaction settings are placeholders or unfinished and currently do not work reliably. Parkour (wall-run, wall-jump and climbing), ragdolls/body-retention behavior, grabbing walls, and grabbing enemies/NPCs/allies are not supported features in this release. Enemy grabbing may activate when enabled, but it is broken and not recommended. Leave these options off. Do not assume another advanced interaction works merely because its toggle appears in the menu. The working refinements in this release are listed below.

## 1. Installation

Download the APK for your device:

- **Android / flat:** [HaloCE-Android-1.0.19.apk](https://github.com/moistman42069/HaloCE-Quest-VR/releases/download/v1.0.19/HaloCE-Android-1.0.19.apk)
- **Meta Quest / VR:** [HaloCE-Quest-1.0.19.apk](https://github.com/moistman42069/HaloCE-Quest-VR/releases/download/v1.0.19/HaloCE-Quest-1.0.19.apk)

Both apps are ARM64 and require Android 9 / API 28 or newer. Allow installation from your browser or file manager on Android. On Quest, enable Developer Mode and sideload the Quest APK with SideQuest or ADB. Install over the existing app to keep saves, settings and imported game data. **Do not uninstall the old app or clear its data.** Back up important data before updating.

The APKs do not include Halo game data. On first launch, open **Game files & versions** and import your own supported Xbox Halo CE ISO/XISO or extracted game files.

## 2. New Features and Major Changes

### Confirmed in this release

- **Quest multiplayer join crash fixed.** The game was being admitted to a server and loading the match, then crashing inside the voice-configuration logging path. The diagnostic used incompatible mixed string/floating-point formatting in the Android guest logger. It now uses integer-only formats and records host voice settings before logging. The owner confirmed the game no longer crashes while joining.
- **Voice chat works.** OpenCE Build167 host-relayed Opus voice is retained, and the owner confirmed voice chat is audible in testing. No voice protocol or routing change was made for the crash fix.
- **Mounted vehicle and turret reticles align with shots.** Driver/gunner firing follows the selected aim ray without camera aim-assist snapping. The shared path covers the tested Ghost, Scorpion/tank and turret cases. Random projectile spread still applies.
- **Sprint can be toggled.** In VR Settings → Locomotion, enable **Button Sprint** and set **Sprint Toggle** to **Toggle** to click the left stick once to sprint and again to stop. **Hold** remains available.

### Preserved working refinements

- Signed weapon handles now use the exact empty sentinel. Grip release can drop a weapon, holster release can stow it, and a stored weapon can be drawn again. This fixes the release/holster regression.
- Surface-contact haptics follow actual hand contact, and weapon-fire haptics preserve the v1.0.18 behavior. **HAPTICS** controls overall controller vibration; **HOLSTER HAPTICS** can mute only holster buzzes.
- The arm solver preserves authored upper-arm and forearm lengths and clamps unreachable wrist targets instead of stretching the arms to reach them.
- Third-person vehicles and right-hand steering remain the defaults. Vehicle/turret aim can be assigned to the right hand, left hand, head or stick. First-person vehicle view remains experimental and is **not recommended**.
- Safe Geometry remains the default for both Quest VR and flat Android. The Android touch HUD, controller support, touch-layout import/export and per-control hiding remain available.
- OpenCE campaign co-op, the in-game server browser, launcher network selection, game-data management, saved invites and detailed launch logs are retained.

## 3. Controls and Inputs

### Quest Touch — right-handed defaults

| Input | Action |
| --- | --- |
| Left stick | Move / strafe |
| Right stick | Turn; hold straight down to crouch |
| Weapon-hand trigger | Fire |
| Off-hand trigger | Zoom with compatible weapons |
| Right A / Right B | Jump / confirm; reload / use |
| Left X / Left Y | Throw grenade / switch weapon; hold X to switch grenade type |
| Weapon-hand Grip | Hold or release a physical weapon |
| Off-hand Grip near the support point | Use the configured two-hand weapon mode |
| Left stick click | Toggle reticle visibility |
| Right stick click | Native melee |
| Click both sticks | Recenter |
| Left menu button | Pause / menu |
| Off hand near the left side of the head | Flashlight gesture, when enabled |

The pointer trigger selects menu items; thumbstick/D-pad navigation is also available. Left-handed mode mirrors supported actions. To restore crouch to the left-stick click, open **VR menu → Controls → Page 2 → L Stick Click → Crouch**; assign the reticle to another button if desired.

### Flat Android controls

Common Xbox-style USB/Bluetooth gamepads use Android/SDL3 input. Defaults: left stick move, right stick aim, RT fire, LT grenade, A jump, B melee/cancel, X reload/use, Y switch weapon, LB flashlight, RB grenade type, stick clicks crouch/zoom, and Start pause. D-pad/left stick navigate menus; A confirms and B returns. Controller labels vary by profile.

The touch HUD supports multitouch, swipe-to-look, fire-drag, a basic touch-look toggle, layout editing, hiding individual controls, and layout preset import/export. Controller-aware touch visibility can be Auto, Always show or Always hide. Auto restores the touch HUD after controller disconnect.

## 4. VR Features and Settings

- OpenXR Quest rendering, room-scale movement, recentering, smooth/snap turning, tracked hands, finger poses, weapon alignment and adjustable scopes.
- **Legs + Arms** is the body default. Other body/hand display modes remain available. Tracked controllers animate the fingers; this is not optical finger tracking.
- Physical weapon holding, two-hand options, holsters, weapon release/drop and holster haptics are available. Reload/grenade/native hand-animation handoff remains supported.
- The HUD has wrist placement and head-gesture controls. Vehicle view, horizon, seating offsets, turret aim and steering options are in the VR settings.
- **Third Person + Right Hand steering** are the vehicle defaults. First-person vehicle view still needs refinement.
- Safe Geometry is enabled by default. Restart after changing geometry mode. Resolution and refresh-rate choices are requests and can affect performance.

Settings for comfort, body, hands, weapons, scopes, HUD, vehicles, graphics and display are in the in-game VR pages. Some physical-interaction toggles are unfinished; see the warning at the top and Known Issues.

## 5. Android / Mobile Features and Settings

- Flat Android play with editable touch controls, relative touch-look, multitouch, optional gyro aim and control visibility policy.
- Common XInput-style and other Android gamepads are supported through Android input/SDL3, including analog triggers, hot-plug/reconnect, menu navigation and vibration where supported.
- Open **HUD** in game to customize positions, sizes, opacity and visibility. Layout presets can be imported/exported; a selected control can be hidden and restored from the editor.
- Safe Geometry is the default. The launcher manages imported game data, network selection, updates, saved invites, help and per-launch logs.
- No Android launcher voice options were added. Use the game's existing Audio controls.

## 6. Multiplayer, Co-op and Server Compatibility

Open **Play → Multiplayer → Join Game → Server Browser** for public games or **LAN** for nearby hosts. Use **Direct Link** for an invite. To host PvP, choose **Create Game → Internet or LAN → Multiplayer**, configure **Server Setup**, and start the lobby. To host campaign co-op, choose **Create Game → Internet or LAN → Singleplayer**, select a mission/difficulty, configure **Server Setup**, and start the lobby.

Campaign co-op and PvP are supported through OpenCE's in-game flow. The user has tested co-op on this project. The server setup exposes player-limit choices up to 128, but that setting is not a guarantee that a campaign, mission, device or network can stably handle 128 players; start with a small group.

Hosting remains network 24. The launcher client target can be set to a specific supported version from 11–24 or **All compatible networks**. Targets 11–22 are limited to original Xbox-map PvP; campaign co-op and Custom Edition require network 23 or 24 and matching content. Close the game before changing networks so the new target is used at next launch. Population counts come from catalogs and may be stale or incomplete.

Voice uses the Build167 host relay. In VR, set **VR Settings → Voice Chat** to Off, Push to Talk or Open Mic, set receive volume above zero, and optionally bind Push to Talk in **Buttons**. Flat Android uses the existing in-game Audio settings and its keyboard PTT binding or Open Mic. The host must allow lobby voice and select a route that includes the players in **Server Setup**. The host and peers must support the voice relay; no audio is sent while local transmit is Off.

If a join fails, check game-data/map matches, selected client network, host version, lobby state and NAT/firewall/VPN restrictions. A compatible directory entry does not guarantee that the host is reachable. For crashes, connection failures or desyncs, include logs from both peers.

## 7. Game Revision and Version Compatibility

Use **Game files & versions** to import, scan, select, rename and delete inactive managed game-data sets. The active set and existing/base data are protected. Deleting a managed copy also deletes its local saves/config; an ISO/XISO selected through Android's file picker stays at its original location and is not deleted by the launcher.

For the broadest game-data compatibility, use the **original Xbox Halo CE XISO**. Rev 1 and Rev 2 can be imported where recognized, but are not recommended. The launcher cannot reliably identify every disc revision from a filename, authenticate every image, or automatically switch to the exact revision a server expects. Different maps or modified content can prevent a match.

## 8. Known Issues and Important Notes

- Parkour, wall-run/jump/climb, ragdoll behavior/body-retention changes, wall grabbing, and enemy/NPC/ally grabbing are unfinished and should be treated as non-working. Enemy grabbing may activate but is broken and not recommended. Other advanced physical-interaction toggles may also be placeholders.
- Manual physical reload has appeared in the settings, but successful end-to-end use is not confirmed for this release. Do not rely on it as a required gameplay control.
- First-person vehicle view is experimental; use the default third-person view.
- Co-op is supported, but every mission, revision, player count and network condition has not been validated. The 128-player option is not a stability guarantee.
- The engine is based on OpenCE Build157/network24 with selected compatible changes from Builds167, 169, 170, 173 and 176 plus reticle assets from Build177. This is **not** a full Build177 rebase; network24 is unchanged.
- APK builds and automated checks do not certify every headset/Android model, disc image, server or co-op configuration.

## 9. Technical Details, Credits and Support

- App IDs: **com.halo.decomp** (Android) and **com.halo.decomp.vr** (Quest); version **1.0.19**, Android version code **63**, ARM64, minimum API 28.
- Voice chat uses the Build167 host-relayed Opus messages. The join-crash correction changes diagnostic formatting and ordering, not the voice/network protocol.
- The glasses-FOV and resolution contribution by [Willem Horak, PR #1](https://github.com/moistman42069/HaloCE-Quest-VR/pull/1) is included. See [CREDITS.md](../CREDITS.md) and [third-party notices](../THIRD-PARTY-NOTICES.txt) for contributors and licenses. This unofficial port includes no Halo game data.
- For help, DM **@MeWhenINameMyself** or post in the [Halo CE Decomp Discord](https://discord.gg/S9uSCKxKx). Include headset/phone model and OS, app and network versions, game-data set, map/mission, connection type, host/client role, reproduction steps and logs. Remove private invite/device details before public sharing.
- Build, APK hashes and automated validation are recorded in [RELEASE-PROVENANCE-1.0.19.md](RELEASE-PROVENANCE-1.0.19.md).

