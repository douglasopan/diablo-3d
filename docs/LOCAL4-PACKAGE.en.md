## The edited map in the package

Local4 includes Tristram frozen on **14 October at 19:05**: positions, geometry, removals, 14 architectures, 18 light sources, 25 adjusted cells and four routes. Edits saved after this snapshot are not included. Lighting is native; **18 Godot size, specular and shadow settings remain pending**. Native movement, collision, actors and saves remain authoritative; loader success does not qualify every relocated building footprint.

The download includes the executable, libraries, own resources, Warrior, HUD R4 and internal preparation component. **Only DIABDAT.MPQ from a compatible original Diablo retail copy is required.** No Python, compiler or private files are needed. Original materials are reconstructed locally; the package contains no MPQ, saves or extracted original bitmap artwork.

## Warrior and fixes

Warrior Light with Short Sword and Buckler passed actual CPU drawing in Tristram: world body in third person and arms/equipment in first person. The well passed four material states; the combined suite completed 130 checks and restored the original quest state. This does not qualify actual quest completion or final GPU appearance.

The Cathedral cache change reclaims materials outside the current frame's required set while keeping the 8 MiB limit. The candidate passed **16 cold controls and 48 route/revisit frames, with 2,023 checks and no refusals**. The comparison route did not reproduce the original failure, so reproduction of the specific reported defect remains inconclusive.

Input changes preserve native movement, combat and interaction commands and handle map/UI ownership. The Town steps passed in the fixture; **the complete Cathedral input sequence failed with RefreshLiveFrameRefused** before those controls could be assessed. That refusal remains under investigation. We do not claim complete Cathedral validation.

## Playing this preview

Download the full ZIP from the [local4 release](https://github.com/douglasopan/diablo-3d/releases/tag/v0.1.0-preview.local4-tristram.full1), extract everything into a new folder and run **Iniciar-Diablo3D.cmd**. Select your **DIABDAT.MPQ**. First installation reconstructs and checks all 19 scene files; clean installation, profile reuse/preservation and refusal of a modified scene are verified.

**K** switches first/third person, **Home** restores third person and **F4** switches 3D/native. The new profile uses `%LOCALAPPDATA%/Diablo3D-local4-Tristram/profile`, with Speed32. Local3 and its saves are not replaced.

This is a **preview**, with GPU required by the launcher. New results above are hidden CPU tests and file verification. New-assembly GPU, sustained FPS and physical gameplay are not qualified. Inherited HUD does not mean a redesigned interface or new stash.

Executable SHA256: `ffd8d95e0a610c7e64c04b0b7f4076b8064666af1cf969c33203ecbb3d1e533d`. Private production source: `c433ca4137d248fdbbfcd462934786fd16aec6ef`; the inherited embedded version still shows `24080e8`. Corresponding code is available on GitHub and with the release; public CRLF/LF differences do not change the corresponding C++ text.

Original credits and licenses are retained. Custom music: **Douglas Pan**, also recorded in file tags.
