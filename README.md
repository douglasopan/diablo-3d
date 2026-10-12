<p align="center"><img src="assets/branding/diablo3d-banner.png" alt="Diablo 3D" width="100%"></p>

# Diablo 3D · D3D

A Vibecoded community project led by **Douglas Pan** to bring **almost the whole Diablo 1 game into 3D**, including its procedurally generated dungeon levels, built on [DevilutionX](https://github.com/diasurgical/devilutionX). The native simulation remains responsible for maps, seeds, combat, collision, items and saves. The visual layer adds 3D rendering and gameplay cameras while retaining original-view comparison. Tristram and a pilot of nine nearby regions in the first normal Cathedral level exist; reconstruction, loading/reentry and performance still need work.

**A proposta é o Diablo 1 inteiro em 3D.** Tristram e um piloto do primeiro andar normal da Catedral já podem ser testados em 3D. Terceira e primeira pessoa estão integradas, com horizonte provisório em Tristram. Modelos e composição das paredes continuam em revisão; os demais níveis, personagens, monstros e efeitos seguem no plano do jogo completo. O catálogo colaborativo crescerá com essas etapas para coordenar os modelos que ainda precisam ser criados.

**Checkpoint — 11 October 2026 (America/Sao_Paulo), documentation revision 2026.10.11-r1:** general game fronts and the previous release remain **on hold**. The owner-authorized exception now prioritizes documenting the real code and completing Cathedral visuals/lifecycle in a **separate clean base**, retaining our camera, input, UI, Town and native simulation. An experimental floor/wall preview was delivered separately; it is not final visual acceptance. HD and Shift/stamina preparations are deferred. This publication contains documentation only and promotes no gameplay binary.

**Estado em português:** as demais frentes continuam em espera. A exceção autorizada é documentação/auditoria do código e conclusão visual da Catedral em base isolada. O preview, o habitual e a base limpa são versões diferentes; o fechamento de uma partida não autoriza promover WIP desconhecido.

**[Architecture and execution guide](docs/PROJECT-EXECUTION.md)** — current stage, dependencies, completion criteria and ongoing work. Contributors and agents should consult this guide and [AGENTS.md](AGENTS.md) before implementing changes.

**[Real code map and lifecycle contracts](docs/CODE-ARCHITECTURE.md)** · **[Versioned functionality/build checkpoint](docs/CHECKPOINT-2026-10-11.md)** · **[Code gaps and duplication audit](docs/CODE-AUDIT.md)** — existing paths, state owners, NEW/load/transitions, native/web build templates and validation gates for human and AI-assisted development. Planned architecture is kept separate from deployed behavior.

**[Website & Devlog](https://douglasopan.github.io/diablo-3d/)** · **[Official Discord](https://discord.gg/4YxQ7s69S)** · **[Contribute](docs/CONTRIBUTING.md)** · **[Roadmap](docs/ROADMAP.md)** · **[Build and play](docs/BUILDING-D3D.md)** · **[Estado em português](docs/TRISTRAM-STATUS.pt-BR.md)**

**[Community model inspector / Inspetor de modelos](docs/MODEL-INSPECTOR.md)** — review public metadata identifying **130 distinct GLB files**, including source revisions, derivatives, candidates, rigs and animation clips. Compare locally available original GLB and converted D3D files, distinguish map bindings and export review notes. This is a review catalog, not 130 finished or approved assets. Model and reference media are kept separate and are not bundled with the public catalog.

**[Items, concepts and matching inventory icons / Itens e conceitos](docs/ITEM-ASSET-CATALOG.md)** — a source-linked inventory of all current Diablo/Hellfire item tables, with **nine initial concept turnarounds** and a searchable [review gallery](docs/items-study/gallery.html). The intended pipeline uses the same accepted 3D master for equipped items, ground objects and rendered inventory icons. The remaining concepts, models and runtime bindings are still pending.

> This is modified DevilutionX software, distributed under its inherited **[Sustainable Use License](LICENSE.md)**. Source is public for collaboration; distribution must be free of charge and non-commercial. This is not an MIT/GPL release or an OSI-approved open-source license. Preserve upstream notices. Original Diablo game data is required and is not included.

## Builds and evidence

These identities describe the configured developer workspace, not downloadable public game releases. A clean clone is not the same snapshot as the local installed build. Component documents contain historical checkpoints; the [execution guide](docs/PROJECT-EXECUTION.md) is the operational source of truth.

| Version | Verified state | Limits |
| --- | --- | --- |
| **Windows `b77562161864…`** | Installed through `Iniciar-Tristram.cmd`; three aliases match. Meshy OFF rejects 14 known bindings before heavy loading. HUD R4 and the first normal Cathedral pilot are retained. | Simpler Tristram substitutes and native Ogden sprite. Loading existing saves or reentering a dungeon can refuse the 3D frame. Full-game completion and sustained FPS are unproven. |
| **Checkpoint `win-b77562161864-assets-c5b32ee3c8e2`** | Local copy of the executable and 227 support assets; pinned payload/profile overrides verified. | Verification returned before starting a game. This is a reproducibility checkpoint, not the load fix or a second edition to run concurrently. |
| **Local browser R12** | Emscripten/WebGL2 prototype; local data import/storage, Tristram/HUD and view switching were exercised. Meshy OFF verified in the data package. | Served at `http://127.0.0.1:8765/` on the developer's PC only. Freezes/performance remain under investigation; no public browser release. |
| **Experimental preview `337eba485565…` / R3** | Delivered locally with its own pinned payload/profile; rectified Cathedral floors/primary walls and loader prerequisites. | Frozen during human testing; no R4 E/stationary-attack change. Visual/camera complaints remain open. Speed 20 differs from habitual 32. |
| **Private candidate `ed0e0ad8566d…` / R4** | Normal EXE compiled. Separate native loader/input fixtures and four hardware TP/FP frames passed bounded gates. | Not installed; E/targets/held-combat/focus/UI and complete visual/FPS qualification still pending. Fixture success is not a complete session in the normal EXE. |
| **Clean C1 base / upstream `dac104babfb6…`** | Separate checkout with 186 declared Source deltas preserving our camera/input/UI/Town. | Source layers applied, not freshly compiled/executed; minimal dependency closure and complete Cathedral adaptation unproven. HD and run/stamina deferred. |
| **Other private preparations** | Broader base-dungeon adapters, camera/UI changes and a fullscreen button are preserved. | Preparation is not promotion, installation, family coverage or FPS evidence. |

Installed Windows SHA-256: `b775621618640cc3f2e3a6f3c440c92d90cb67933a7a3a938d6b89b8c71d12f3`. Full preview/candidate hashes and gate limits are in the [dated checkpoint](docs/CHECKPOINT-2026-10-11.md). Technical checks and offscreen images do not establish visual approval or gameplay frame rate.

## Installed local prototype

- F4 compares original/3D in Tristram and the first normal Cathedral pilot when the 3D frame is valid. The pilot covers nine nearby regions; load/reentry refusal remains a blocker. Other dungeon levels remain native in b775.
- Choose **Third Person or First Person** in the gameplay camera settings or switch with K. The mouse wheel zooms between them; movement and combat retain the native simulation. Legacy isometric/orbit poses remain available to diagnostics.
- Look through 360°, adjust pitch and zoom. Tristram architecture obstructing or containing the camera can be hidden locally and restored when clear; trees, props and Cathedral geometry are outside this first visibility implementation. A **provisional external horizon and fog** are integrated; terrain continuity and class-specific eye calibration still need work.
- **Meshy OFF** disables the 13 known architectural bindings and Ogden. Tristram uses simpler substitutes; models, masters, revisions and the historical import pipeline are preserved for review and reconstruction.
- **Ogden currently uses his native sprite.** His imported rigged model/idle was an earlier prototype, not active in b775. Other sprite-derived actor proxies remain provisional. The player's rigged hero is not installed.
- Walking, collisions, inventory and NPC interaction use the existing game simulation.
- Windows build, headless scene diagnostics and native-view comparison tools are included as source.
- A [Godot editor](docs/GODOT-EDITOR.md) supports local architecture/model inspection and explicit static exports to a separate review profile. Native collision remains authoritative. Godot is an authoring tool; the game still runs in DevilutionX.
- [Soundtrack settings](docs/MUSIC.md) select Vanilla, Rock or a mix for eight environments, in the main settings and during play. Rock2 is the local menu default; the earlier composition stays selectable, and missing replacements use the original music. [Listen and download the five custom tracks for free](https://douglasopan.github.io/diablo-3d/en/musica/), with **Douglas Pan** embedded in their authorship tags. The installed custom tracks carry the same credits.
- [Subscribe for project updates](https://douglasopan.github.io/diablo-3d/en/novidades/): optional email signup, language preference and consent, with private responses and a cancellation form. Reading and downloads remain open without signup.
- The [responsive HD HUD](docs/HUD-IMPLEMENTATION.md) presents the original controls with new RGBA stone, metal, sculptures and resource globes, without adding gameplay actions. Its layout and the main menu layout can be edited in Godot; the game retains native handlers, item/spell icons and bitmap fonts. The approved menu background is included. The HD revision passed technical checks and is installed in the regular launcher; artistic review during play remains pending.
- In-game settings omit options that cannot apply during play and use responsive pages: eight content rows at 960×540 and eighteen at Full HD, with shared drawing/input geometry.
- New games prefer the 3D town view. **Start in 3D** is a saved video preference; **F4** switches views during play, and **Home** restores Third Person for the session without changing the saved camera preference. Only Tristram and the first normal Cathedral pilot support 3D so far.
- The owner-provided [animated logo](docs/ANIMATED-LOGO.md) contains 240 frames over eight seconds and covers the main, title and Escape menus.

The **installed build is an offline prototype with optional Direct3D 11 on Windows and a CPU fallback**. GPU rendering and edge smoothing are independent settings under **Esc → Settings → Graphics**. Simulation and scene preparation still use the CPU. The original GLB, converted package and actual frame are separate evidence; an inspector preview does not reproduce game lighting/palette. [Generation and model review tools](docs/MESHY-WORKFLOW.md) preserve provenance; generated candidates still require fidelity approval.

**Conservative CPU/GPU camera culling has passed native-data comparisons preserving color, depth, picking and offscreen shadows.** A texture-cache fix prevents the reproduced CPU fallback in Full HD first person. Regional transparency admission now also fixes a reproduced Cathedral 3D-to-native fallback: eight stress views stayed on the RX 570, including 408 overlays, while retaining the 2048-overlay/512 MiB budgets. These checks do not establish complete gameplay coverage. Distance-based mesh LOD remains under development and is not connected to the game yet. We have not established a sustained frame-rate target for the full town or close-up camera modes.

**F4 provides the native-rendering comparison; Home restores Third Person.** Identical original-backend pixels do not prove a perfect 3D reconstruction. Forced-mesh comparisons still show differences. Contributors must compare the actual geometry with the native view and inspect rotated views before calling an asset faithful.

## Technical stages and technologies

The core is **C++/DevilutionX with SDL**: native state feeds a visual scene, CPU/D3D11 rasterization and existing SDL composition. The browser prototype uses **Emscripten/WebGL2** and local storage. **Godot, GLB/D3D revision manifests and Python tools** support authoring and reproducibility; Godot is not a port of the whole game. Meshy/Tripo are generation workflows, not automatic asset acceptance. Belzebub was studied as a reference; this README does not claim its executable code or a new HD-texture set was integrated.

| Stage | Implemented / tested | Pending |
| --- | --- | --- |
| **Map / Tristram** | Native map, seed, collision and simulation preserved; visual scene, provisional horizon/fog, static architecture shadows and Godot review. | Faithful complete objects, terrain, lighting and placement. Meshy OFF is reversible retirement, not completed reconstruction. |
| **Procedural 3D** | First normal Cathedral pilot, nine nearby regions and native materials. | Load/reentry, wall/grate/object composition and transitions. Private preparations for 16 base floors/four special levels are not installed; Hellfire is not automatically covered. |
| **First / Third Person** | Gameplay modes, remappable switching, pitch/zoom and native movement integration; third person is default. FP capture has technical checks. | Complete target/interactions testing and performance. Free TP mouse look and newer UI/play-state rules remain pending. |
| **Controls** | Native simulation and configurable movement/camera bindings. | Requested stationary-attack-only LMB, `S` conflict guard and fresh-frame policy are private candidates; input R4 is not installed in b775. |
| **HUD / menus** | Responsive HD main HUD, native handlers, menu background, soundtrack/language/project credits and Godot previews. | New dragging/resizing and other panel candidates. Next visual request concerns ESC/Settings only; approved inventory transparency is preserved. |
| **Actors / items / effects** | Native sprites, review catalog and concepts; earlier Ogden rig/idle prototype preserved. | Complete heroes, rigs, animation sync, monsters, equipped/ground items and final inventory icons. Concepts are not installed models. |

General occlusion behind buildings, bounded scene residency/streaming and distance-based LOD remain work ahead. Preserve close-up master quality when deriving LODs. Day/night, voice and larger servers remain future work.

## Build and play

See [BUILDING-D3D.md](docs/BUILDING-D3D.md) for Windows compiler prerequisites and diagnostics. With Visual Studio 2022 or later, CMake and Ninja available:

```powershell
.\build.ps1
.\Iniciar-Tristram.cmd
```

This compiles the revision in your checkout; it does not reproduce unpublished local candidates or provide the developer's versioned checkpoint. In the configured workspace, **Iniciar-Tristram.cmd** remains the regular shortcut, selecting `build/devilutionx-tristram-v4.exe` and `perfil-tristram/`. The pinned b775 checkpoint exists separately for reproducibility. Save and close an existing game before starting another build. Video/soundtrack preferences belong in the game settings; no new candidate was promoted in this documentation update.

Supply your own `DIABDAT.MPQ`, or separately obtain supported shareware data. For a data directory outside the launcher's detected locations, use `Iniciar-Tristram.ps1 -DataDirectory 'C:\path\to\your\Diablo'` during setup. Saves and settings remain in the ignored `perfil-tristram/` folder. Nothing needs to be installed over your original game.

| Control | Action |
| --- | --- |
| F4 | Original / 3D in Tristram and the first normal Cathedral pilot, subject to a valid frame |
| K (default, remappable) | Switch Third Person / First Person while 3D is active |
| Graphics → 3D Camera Mode | Choose Third Person / First Person |
| Middle mouse + drag | Orbit or look around and adjust pitch |
| Mouse wheel | Approach Third Person → First Person; move away to return |
| Home | Restore Third Person for this session |
| Configured movement keys / arrows | Native movement in eligible follow-camera contexts |
| Click ground / NPC | Existing native-context rules; pending LMB/R4 policy is not installed |

Remap the camera shortcut in **Settings → Keymapping**. Existing saved bindings, including an explicitly unbound camera shortcut, remain intact; another action already using K keeps priority over the new default.

Older component documents describe earlier camera defaults. Use this build-qualified table for the installed checkpoint; loading a save successfully is not proof of a current 3D frame.

The helper build defaults to **`NONET=ON`**. Voice, larger sessions and multiplayer compatibility have not been implemented or validated by this prototype.

To help assemble and inspect the scene, use **`Abrir-Editor-Godot.cmd`** after building the game and `town_view_smoke`. Save in Godot, export the selected replacements, then use **`Aplicar-Mapa-Godot.cmd`** and **`Testar-Mapa-Godot.cmd`**. Local art/data are excluded from Git. The initial static format protects the east cabin's existing fire/interior adjunct; see the [editor guide](docs/GODOT-EDITOR.md) for setup, limits and the distinction between technical checks and visual approval.

## Help create complete 3D objects

**Human developers are welcome**, including people contributing with AI assistance. Help with renderer performance, procedural adapters, controls/UI, Godot tools, art, rigs/animations and reproducible tests. **AI-assisted work requires human responsibility, review and testing**: explain the change, provenance, build tested and remaining limits. Generated code or art does not bypass review.

**Desenvolvedores humanos são bem-vindos**, inclusive com auxílio de IA. Consulte o guia, combine um escopo rastreável e envie propostas com testes/evidências. A pesquisa de novos canais de colaboração permanece pendente; este convite público não representa contatos individuais ou novas frentes em execução.

Contributions support the whole-game goal. Current art review focuses on Tristram houses, the cathedral exterior, trees, rocks, props, townspeople and characters; dungeon reconstruction will expand the catalog in later stages. Renderer, import, camera, tooling and procedural-map contributions are also welcome. Match composition, scale, silhouette, doors, windows, ground contact and placement at the original camera angle. Then complete the unseen faces and review the entire object through 360°.

Consult the [Tristram asset catalog](docs/ASSET-CATALOG.md), reserve its model ID in one issue, and use the [3D asset issue form](https://github.com/douglasopan/diablo-3d/issues/new?template=3d_asset.yml). Read the [contribution guide](docs/CONTRIBUTING.md) or coordinate in [Discord](https://discord.gg/4YxQ7s69S). Procedural geometry and asset-import contributions are welcome alongside independently created art with documented provenance.

Do not submit game archives, extracted game artwork, saved characters, private credentials or paid-service keys. Local tools can read a user's own archives; their generated extracts are not part of this repository.

## Future milestones — game development on hold

1. Refine complete Tristram objects and establish a reproducible import and visual-validation pipeline.
2. Reduce scene-preparation costs through persistent GPU geometry and volume batching; develop distance-based mesh LOD while preserving close-up model quality. Camera culling and resident GPU volumes for perspective props/vegetation are integrated. Persistent geometry for the remaining scene and distance-based LOD still need work.
3. Extend the implemented **static architecture shadow map** to moving lights, actors, trees and props. Original actor decals and some terrain lighting remain compatibility approximations.
4. Validate the first procedural Cathedral level from the live map and existing seed, then extend the pipeline through the Cathedral, Catacombs, Caves and Hell, with the characters, monsters, objects and effects required by the full game.
5. After completing and stabilizing the whole Diablo 1 reconstruction, research **expanded multiplayer and a persistent world**, targeting 50–100 simultaneous players as an exploratory goal. Respawn/reset of loot, lootable objects/containers and NPC stock is a prerequisite; protect quests, ownership and persistent saves. A configurable 30-minute interval is only an example to define and test. Names/levels, chat bubbles and friends belong to this future phase; proximity voice remains optional research.
6. After that multiplayer/persistence phase, plan an original expansion, prioritizing new surface areas and defining additional content and levels through a separate gameplay proposal. Diablo 2 is cancelled and outside this roadmap.

The agreed order is **complete and stabilize Diablo 3D → expanded multiplayer/persistence → original expansion**. The [roadmap](docs/ROADMAP.md) and [execution guide](docs/PROJECT-EXECUTION.md#marcos-e-dependências) define prerequisites. The [networking research](docs/NETWORKING-RESEARCH.md) records existing limits and earlier alternatives, not a selected final architecture. Increasing a player constant alone will not produce a scalable server. The current NONET build does not deliver these future multiplayer capabilities.

The historical import pipeline uses a [Tristram light profile](docs/TRISTRAM-LIGHTING.md), calibrated against the original cabin. The optional east-cabin review contains a timber floor, physical windows and candle sources; this describes preserved review work, not an imported cabin active under Meshy OFF. Future buildings follow the [openings and fire-light standard](docs/BUILDING-OPENINGS.md).

The next recorded visual request removes the gray/opaque background and centers navigation in **ESC/Settings**, while preserving aligned Graphics/Audio values and the **approved inventory transparency**. It remains backlog during the pause. Significant delivered milestones belong in the devlog; a small visual adjustment is not a new loot or gameplay system.

## Dungeon reference study and adaptation

Thank you to **plopsDOTio / plops.io**, author of [Diablo First Person](https://forge.plops.io/t/diablo-first-person-devilutionx-mod-for-windows), for publishing its source and dungeon reconstruction work. The author's [announcement](https://www.reddit.com/r/Diablo1/comments/1x1wtjz/diablo_1_in_first_person_original_automap_butcher/) identifies an independent DevilutionX mod inspired by Drelvak's video; we do not have Drelvak's unpublished code.

**Adaptation is in progress in an isolated candidate, not an installed release.** We are adapting dungeon materials and geometry to our existing scene/GPU pipeline, preserving our Tristram, camera, movement and UI. The first floor/material lifecycle tests passed; this does not qualify the whole campaign, performance or browser build. [Scope, attribution, source pins and validation limits](docs/DUNGEON-REFERENCE-ADAPTATION.md).

## Project identity and upstream

Use the full [Diablo 3D banner](assets/branding/diablo3d-banner.png) for project pages and the square [D3D mark](assets/branding/d3d-avatar.png) for icons and project avatars. [Artwork provenance](assets/branding/README.md) records their origin. The new [animated menu logo](docs/ANIMATED-LOGO.md) uses the owner-provided PNG sequence. The older optional tool rebuilds an alternative locally from the user's own game data; those generated game-art replacements are not distributed here.

This independent fan project is not affiliated with Blizzard Entertainment. Diablo and associated marks belong to their respective owners. Public source access does not grant rights to proprietary game assets.

The original engine [README](docs/UPSTREAM-README.md), [contribution instructions](docs/UPSTREAM-CONTRIBUTING.md), [license](LICENSE.md) and other upstream notices are preserved. The renderer prototype started from DevilutionX commit `dac104babfb6187415432f428ac2516747ffc154`.
