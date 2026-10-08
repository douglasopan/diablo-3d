---
title: "First person: look with the mouse, walk with the arrow keys"
date: 2026-10-08
updated: 2026-10-08
description: "First-person controls have reached the usual launcher: mouse look, camera-relative arrow keys and center selection, while preserving native movement."
slug: controles-primeira-pessoa
image: /assets/captures/tristram-camera-modes.webp
image_alt: "Historical technical comparison of Tristram's four camera modes, used as context; it does not show the new controls, eye-height calibration or visual validation of the crosshair."
category: Protótipo
order: 18
status: published
---

Tristram already had a first-person camera. Now it also has its own controls: **look with the mouse and walk with the arrow keys relative to the camera's direction**, while retaining Diablo's movement rules. The update was published in [commit a227d4afc](https://github.com/douglasopan/diablo-3d/commit/a227d4afcab5829cfa82ab9bad9e0eac34a2c9c9) and installed through the same **Iniciar-Tristram.cmd** launcher on **October 8, 2026, at 17:30, Brasília time**.

The camera comparison above was published earlier and serves only as context. **This post contains no new gameplay or crosshair captures.** The implementation and test limits are described in the [documentation for this revision](https://github.com/douglasopan/diablo-3d/blob/a227d4afcab5829cfa82ab9bad9e0eac34a2c9c9/docs/TRISTRAM-FIRST-PERSON-INPUT.md).

## Entering the mode and moving

In Tristram, **F4** switches between the original view and the 3D prototype. **K** cycles through isometric, free orbit, third person and first person; this shortcut can be remapped. **Home** restores the original camera.

In first person, horizontal mouse movement turns the camera and vertical movement changes its pitch. There is no need to hold the middle mouse button; sensitivity uses the preference already saved. Control becomes active only when the platform confirms relative mouse capture.

| Input | First-person action |
| --- | --- |
| ↑ / ↓ | Move forward / backward relative to the camera. |
| ← / → | Strafe left / right. |
| Esc | Release the mouse and continue with the native action. |
| Click in the world after suspension | Resume capture; this first click is consumed. |

Arrow-key combinations are converted into the **eight native directions**, at the same walking cadence. Camera pitch does not tilt movement. Position, speed, animation and collision remain under the game's existing routines.

## Opening interfaces and resuming mouse control

Menus, inventory, other panels and loss of focus suspend first-person control and clear the inputs it owns. Leaving this mode also releases the mouse.

**Closing an interface or returning to the window does not recapture the mouse automatically.** A new left or right click inside the world resumes control; that click and its release do not trigger an interaction. Old movement deltas are discarded, and arrow keys that were already held must be released and pressed again to move.

The other camera modes retain their controls. When a controller takes over, the game keeps its native gamepad behavior and suspends this keyboard-and-mouse adapter.

## Selecting after turning the camera

During capture, selection uses the center of the world's logical viewport, where the code draws the crosshair. Turning the camera invalidates the previous selection.

A click received before the next draw now **waits for an updated selection**, preserving the order of mouse movements, clicks and releases. This avoids using the target from before the turn or sending an unintended walking command to the hero's tile. Looking at the sky without a valid target does not produce a ground command.

The fix was exercised with ground selection through the CPU path. NPC and item interactions and combat still need review in a real play session, as does the new crosshair's appearance at different scales.

## Eye height

**October 8 update, at 18:18:44 Brasília time:** a report that the camera sat too low in front of NPCs led to an independent calibration. First-person eye height increased from **1.1 to 1.7 units**; the third-person target remains at **1.1**. The fix in [commit 4a276a10b](https://github.com/douglasopan/diablo-3d/commit/4a276a10b090c6bb46f8db5c59c0383bff3bcc84) is installed through the same launcher.

**202 camera checks** passed, along with a repeat of the **1,652 control checks**. A native CPU comparison at one position facing Griswold and the smithy, at 640×480, kept the third-person indexed image and palette byte-for-byte identical. The **44 profile files** retained their bytes, sizes and timestamps during this installation.

**1.7 is an initial calibration**, still awaiting gameplay review around other townspeople and with other classes and poses. This fix changed no models, materials or lighting and included no physical mouse test, GPU run or FPS measurement. The [calibration documentation](https://github.com/douglasopan/diablo-3d/blob/4a276a10b090c6bb46f8db5c59c0383bff3bcc84/docs/TRISTRAM-HORIZON-CAMERAS.md#calibração-da-altura-ocular--8-de-outubro) records the sample's limitations.

## What passed the tests

The **Release/NONET x64** build passed. Validation passed **673 input-policy checks** and **1,652 production-path checks**, along with regression suites of **946 HUD checks** and **59,452 settings checks**.

The tests exercised movement at all eight orientations, arrow-key combinations, native commands and movement cadence, collision, suspension and resumption, discarded deltas and ordered clicks after turning the camera. The random-number generator's state was preserved.

The production diagnostic uses the game's real handler and native commands, but substitutes the system's focus and capture services and runs with a hidden window, the GPU disabled and a temporary profile. **This does not establish how a physical mouse, Alt+Tab or a real window will behave.** The HUD and settings regressions also do not constitute visual approval of the crosshair.

## Installation and the next review

During the initial controls installation, the **41 files in the usual profile** retained their bytes, sizes and timestamps. Subsequent launcher preparation changed only the expected runtime receipt. Models, textures, lighting and the GPU backend were not changed by this update.

After the previous batch of GPU improvements, the author reported that “the game's performance improved a lot!” That feedback is a **subjective assessment of the earlier update**; first-person controls add neither an FPS measurement nor a new benchmark. The [zoom fix and GPU recovery](/devlog/gpu-zoom-recuperacao/) remain documented separately.

The next step is to test through the usual launcher: eye height around townspeople, mouse capture and release, Alt+Tab, interfaces, the crosshair and clicks on NPCs and items. Legacy demo recording/playback, capture in SDL1 and camera collision with architecture are outside this increment.

The goal remains **all of Diablo 1 in 3D**. These controls apply to the Tristram prototype; the other levels still use the original rendering. The [queue for this revision](https://github.com/douglasopan/diablo-3d/blob/a227d4afcab5829cfa82ab9bad9e0eac34a2c9c9/docs/PROJECT-EXECUTION.md) retains model review and the next dependencies.
