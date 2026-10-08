<p align="center"><img src="assets/branding/diablo3d-banner.png" alt="Diablo 3D" width="100%"></p>

# Diablo 3D · D3D

A community project to bring **the whole Diablo 1 game into 3D**, including its procedurally generated dungeon levels, built on [DevilutionX](https://github.com/diasurgical/devilutionX). The intended experience lets players switch between original and 3D views in the same running game. **Tristram is the current development stage**: original/3D switching, four camera modes and a provisional horizon are integrated. Models, proportions, orientation, materials and interiors remain under review before the first procedural Cathedral level.

**A proposta é o Diablo 1 inteiro em 3D.** Tristram é nosso ponto de partida; depois vêm o primeiro andar procedural da Catedral, os demais ambientes, personagens, monstros e efeitos. Já há quatro modos de câmera e horizonte provisório em Tristram; a qualidade dos modelos continua em revisão. O catálogo colaborativo crescerá com essas etapas para coordenar os modelos que ainda precisam ser criados.

**[Architecture and execution guide](docs/PROJECT-EXECUTION.md)** — current stage, dependencies, completion criteria and ongoing work. Contributors and agents should consult this guide and [AGENTS.md](AGENTS.md) before implementing changes.

**[Website & Devlog](https://douglasopan.github.io/diablo-3d/)** · **[Official Discord](https://discord.gg/4YxQ7s69S)** · **[Contribute](docs/CONTRIBUTING.md)** · **[Roadmap](docs/ROADMAP.md)** · **[Build and play](docs/BUILDING-D3D.md)** · **[Estado em português](docs/TRISTRAM-STATUS.pt-BR.md)**

**[Community model inspector / Inspetor de modelos](docs/MODEL-INSPECTOR.md)** — review public metadata identifying **130 distinct GLB files**, including source revisions, derivatives, candidates, rigs and animation clips. Compare locally available original GLB and converted D3D files, distinguish map bindings and export review notes. This is a review catalog, not 130 finished or approved assets. Model and reference media are kept separate and are not bundled with the public catalog.

**[Items, concepts and matching inventory icons / Itens e conceitos](docs/ITEM-ASSET-CATALOG.md)** — a source-linked inventory of all current Diablo/Hellfire item tables, with **nine initial concept turnarounds** and a searchable [review gallery](docs/items-study/gallery.html). The intended pipeline uses the same accepted 3D master for equipped items, ground objects and rendered inventory icons. The remaining concepts, models and runtime bindings are still pending.

> This is modified DevilutionX software, distributed under its inherited **[Sustainable Use License](LICENSE.md)**. Source is public for collaboration; distribution must be free of charge and non-commercial. This is not an MIT/GPL release or an OSI-approved open-source license. Preserve upstream notices. Original Diablo game data is required and is not included.

## What works today

- F4 switches between the original rendering and the Tristram prototype without reloading the map.
- Choose **Isometric, Free Orbit, Third Person or First Person** in the camera settings. Each mode retains its own pose; camera changes keep native movement and combat controls.
- Orbit through 360°, adjust pitch, zoom and framing. A **provisional external horizon and fog** are integrated; camera collision and terrain continuity still need work.
- Imported building candidates and procedural volumes render together. Their proportions, orientation, silhouettes, doors, windows, ground contact and materials are under active review.
- Characters have depth. Warrior and cow reconstruction can use eight original views; unseen anatomy for single-view townspeople is inferred.
- Walking, collisions, inventory and NPC interaction use the existing game simulation.
- Windows build, headless scene diagnostics and native-view comparison tools are included as source.
- A [Godot editor](docs/GODOT-EDITOR.md) inspects the actual architecture, model identities and locked native collision, and exports explicitly selected static replacements to a separate game review profile. It is an authoring tool; the game still runs in DevilutionX.
- [Soundtrack settings](docs/MUSIC.md) select Vanilla, Rock or a mix for eight environments, in the main settings and during play. Rock2 is the local menu default; the earlier composition stays selectable, and missing replacements use the original music. [Listen and download the five custom tracks for free](https://douglasopan.github.io/diablo-3d/en/musica/), with **Douglas Pan** embedded in their authorship tags. The installed custom tracks carry the same credits.
- [Subscribe for project updates](https://douglasopan.github.io/diablo-3d/en/novidades/): optional email signup, language preference and consent, with private responses and a cancellation form. Reading and downloads remain open without signup.
- The [responsive HD HUD](docs/HUD-IMPLEMENTATION.md) presents the original controls with new RGBA stone, metal, sculptures and resource globes, without adding gameplay actions. Its layout and the main menu layout can be edited in Godot; the game retains native handlers, item/spell icons and bitmap fonts. The approved menu background is included. The HD revision passed technical checks and is installed in the regular launcher; artistic review during play remains pending.
- In-game settings omit options that cannot apply during play and use responsive pages: eight content rows at 960×540 and eighteen at Full HD, with shared drawing/input geometry.
- New games prefer the 3D town view. **Start in 3D** is a saved video preference; **F4** switches views during play, and **Home** keeps the native comparison. Other levels still use the original renderer.
- The owner-provided [animated logo](docs/ANIMATED-LOGO.md) contains 240 frames over eight seconds and covers the main, title and Escape menus.

The **current build is an offline prototype in Tristram, with an optional Direct3D 11 GPU renderer on Windows and a CPU fallback**. Toggle GPU rendering and edge smoothing independently through **Esc → Settings → Graphics**; see [the GPU renderer](docs/GPU-RENDERER.md) for its scope and validation. Dungeon levels still use the original renderer and are part of the planned whole-game reconstruction. Visual quality is under active review, particularly model fit, facade direction, openings, interiors, unseen faces, characters and compound scenery. The original GLB, its converted game package and the actual game frame are separate review evidence; the inspector preview does not reproduce game lighting or its palette. [Generation and local model review tools](docs/MESHY-WORKFLOW.md) are available; generated candidates still require fidelity approval.

**Conservative CPU/GPU camera culling has passed native-data comparisons preserving color, depth, picking and offscreen shadows.** A texture-cache fix also prevents the reproduced CPU fallback in Full HD first person. Distance-based mesh LOD remains under development and is not connected to the game yet. We have not established a sustained frame-rate target for the full town or close-up camera modes.

**Home currently returns to the original rendering backend.** Identical pixels at that pose verify the original-backend dispatch, not a perfect reconstruction. Forced-mesh comparisons still show differences. Contributors must compare the actual geometry with the native view and inspect rotated views before calling an asset faithful.

## Build and play

See [BUILDING-D3D.md](docs/BUILDING-D3D.md) for Windows compiler prerequisites and diagnostics. With Visual Studio 2022 or later, CMake and Ninja available:

```powershell
.\build.ps1
.\Iniciar-Tristram.cmd
```

Use **Iniciar-Tristram.cmd** as the single regular play shortcut. It selects `build/devilutionx-tristram-v4.exe` and the habitual `perfil-tristram/` profile; video and soundtrack preferences belong in the game settings. Comparison profiles and candidate executables are development tools, not alternative editions to choose for normal play.

Supply your own `DIABDAT.MPQ`, or separately obtain supported shareware data. For a data directory outside the launcher's detected locations, use `Iniciar-Tristram.ps1 -DataDirectory 'C:\path\to\your\Diablo'` during setup. Saves and settings remain in the ignored `perfil-tristram/` folder. Nothing needs to be installed over your original game.

| Control | Action |
| --- | --- |
| F4 | Switch original / 3D in Tristram |
| K (default, remappable) | Cycle camera modes while the 3D view is active |
| Graphics → 3D Camera Mode | Choose Isometric / Free Orbit / Third Person / First Person |
| Middle mouse + drag | Orbit or look around and adjust pitch |
| Mouse wheel | Zoom in Isometric / Free Orbit / Third Person |
| Shift + middle mouse + drag | Move framing in Isometric / Free Orbit |
| Home | Restore the native pose and original backend |
| Click ground / NPC | Native movement / interaction |

Remap the camera shortcut in **Settings → Keymapping**. Existing saved bindings, including an explicitly unbound camera shortcut, remain intact; another action already using K keeps priority over the new default.

The helper build defaults to **`NONET=ON`**. Voice, larger sessions and multiplayer compatibility have not been implemented or validated by this prototype.

To help assemble and inspect the scene, use **`Abrir-Editor-Godot.cmd`** after building the game and `town_view_smoke`. Save in Godot, export the selected replacements, then use **`Aplicar-Mapa-Godot.cmd`** and **`Testar-Mapa-Godot.cmd`**. Local art/data are excluded from Git. The initial static format protects the east cabin's existing fire/interior adjunct; see the [editor guide](docs/GODOT-EDITOR.md) for setup, limits and the distinction between technical checks and visual approval.

## Help create complete 3D objects

Contributions support the whole-game goal. Current art review focuses on Tristram houses, the cathedral exterior, trees, rocks, props, townspeople and characters; dungeon reconstruction will expand the catalog in later stages. Renderer, import, camera, tooling and procedural-map contributions are also welcome. Match composition, scale, silhouette, doors, windows, ground contact and placement at the original camera angle. Then complete the unseen faces and review the entire object through 360°.

Consult the [Tristram asset catalog](docs/ASSET-CATALOG.md), reserve its model ID in one issue, and use the [3D asset issue form](https://github.com/douglasopan/diablo-3d/issues/new?template=3d_asset.yml). Read the [contribution guide](docs/CONTRIBUTING.md) or coordinate in [Discord](https://discord.gg/4YxQ7s69S). Procedural geometry and asset-import contributions are welcome alongside independently created art with documented provenance.

Do not submit game archives, extracted game artwork, saved characters, private credentials or paid-service keys. Local tools can read a user's own archives; their generated extracts are not part of this repository.

## Next milestones

1. Refine complete Tristram objects and establish a reproducible import and visual-validation pipeline.
2. Reduce scene-preparation costs through persistent GPU geometry and volume batching; develop distance-based mesh LOD while preserving close-up model quality. Camera culling is validated; persistent meshes and LOD are not connected to the game yet.
3. Extend the implemented **static architecture shadow map** to moving lights, actors, trees and props. Original actor decals and some terrain lighting remain compatibility approximations.
4. Validate the first procedural Cathedral level from the live map and existing seed, then extend the pipeline through the Cathedral, Catacombs, Caves and Hell, with the characters, monsters, objects and effects required by the full game.
5. Restore and validate the existing four-player networking in a separate experiment, then investigate a shared town hub and larger sessions.
6. Evaluate optional proximity voice through Mumble, Discord Social SDK, TeamSpeak or a project-managed transport.

The [networking research](docs/NETWORKING-RESEARCH.md) explains the current limits and integration choices. Increasing a player constant alone will not produce a scalable server. These milestones are plans, not shipped capabilities.

Imported 3D models share a [Tristram light profile](docs/TRISTRAM-LIGHTING.md), calibrated against the original cabin's roof and walls. Base color is lit before mapping into the game palette. The optional east-cabin review now has a timber floor, physical front/rear windows and two candle sources with subtle independent flicker. Further buildings follow the [openings and fire-light standard](docs/BUILDING-OPENINGS.md); their interiors are still work ahead.

## Project identity and upstream

Use the full [Diablo 3D banner](assets/branding/diablo3d-banner.png) for project pages and the square [D3D mark](assets/branding/d3d-avatar.png) for icons and project avatars. [Artwork provenance](assets/branding/README.md) records their origin. The new [animated menu logo](docs/ANIMATED-LOGO.md) uses the owner-provided PNG sequence. The older optional tool rebuilds an alternative locally from the user's own game data; those generated game-art replacements are not distributed here.

This independent fan project is not affiliated with Blizzard Entertainment. Diablo and associated marks belong to their respective owners. Public source access does not grant rights to proprietary game assets.

The original engine [README](docs/UPSTREAM-README.md), [contribution instructions](docs/UPSTREAM-CONTRIBUTING.md), [license](LICENSE.md) and other upstream notices are preserved. The renderer prototype started from DevilutionX commit `dac104babfb6187415432f428ac2516747ffc154`.
