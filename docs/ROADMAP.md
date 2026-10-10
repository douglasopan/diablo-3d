# D3D roadmap

For the current execution stage, architecture, dependencies and completion gates, use [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md). This roadmap describes the desired capabilities; the execution guide controls their implementation order and evidence.

The goal of D3D is to render **the whole Diablo 1 game in 3D**: Tristram, the procedurally generated dungeon levels, characters, monsters, objects and effects. F4 should switch views within the same running game while preserving its simulation. Tristram is the first implementation stage, where we establish the asset and rendering pipeline before tackling generated maps. This roadmap describes that sequence; it is not a release schedule or a claim that planned features already work. Join [Discord](https://discord.gg/4YxQ7s69S) or open an issue to coordinate work.

**Resumo em português:** a proposta é concluir e estabilizar o Diablo 1 inteiro em 3D, incluindo todos os níveis procedurais, personagens, monstros, itens e efeitos. Tristram e o piloto do primeiro andar normal da Catedral já podem ser testados; terceira/primeira pessoa, GPU opcional e horizonte provisório estão integrados, com qualidade e gameplay ainda em revisão. Depois do jogo completo, vem o estudo de multiplayer ampliado e mundo persistente, com respawn/reset de loot, objetos e estoque dos NPCs; só então, a expansão original da superfície. 50–100 jogadores são uma meta futura de pesquisa, não capacidade disponível. Diablo 2 foi cancelado e fica fora deste roadmap.

## Agreed phase order

| Phase | Scope and prerequisite | Current state |
| --- | --- | --- |
| **1. Complete and stabilize Diablo 1 in 3D** | Finish Tristram and the procedural pipeline, then all original levels, heroes, monsters, equipment, items, animation, effects and interfaces. Validate progression, combat, quests, transitions, save/load and rendering budgets from start to finish. | Partial implementation. The first normal Cathedral level is a bounded pilot; it does not complete the full game. |
| **2. Expanded multiplayer and persistent world** | Follows the stabilized full game. Research a 50–100-player target and define authority, synchronization, persistence and respawn/reset before scaling. | Future research and implementation; no supported capacity or delivery date. |
| **3. Original expansion** | Follows the multiplayer/persistence phase and an approved gameplay proposal. Prioritize new surface areas, with additional content and levels defined separately. | Future, without a closed content scope. |

The existing execution milestones remain authoritative; the new networking dependency is recorded in [the execution guide](PROJECT-EXECUTION.md#marcos-e-dependências). Research may run in parallel, but it does not satisfy a completion gate. Diablo 2 is cancelled and outside this roadmap. Decorative horizon work does not introduce explorable expansion terrain.

## Current baseline

The current offline prototype renders Tristram and a nine-region pilot of the first normal Cathedral level from the live map and native materials. F4 switches 3D/native rendering within the same game. Gameplay offers Third Person and First Person, connected by the mouse wheel; Home restores Third Person. Windows has an optional Direct3D 11 renderer and a CPU fallback. Imported buildings, volumetric scenery and a rigged Ogden pilot coexist with temporary geometry and native sprites; not every object or actor has a finished 3D replacement. See [GPU rendering](GPU-RENDERER.md), [cameras and horizon](TRISTRAM-HORIZON-CAMERAS.md) and the execution guide for installed revisions and test limits. Native-backend pixels are comparison evidence, not proof of reconstructed-mesh fidelity.

Static architecture, including an optional imported model, casts directional shadows through a cached software depth map. Imported RGB base color is decoded from sRGB, receives ambient and directional illumination in linear light, and is then mapped to the game palette. The light and shadow map share one world-space sun direction, configured by a bounded optional `d3d-lighting.ini` profile. Native painted textures retain compatibility shading; their baked lighting has not been universally removed.

The existing actor shadow decal preserves original artwork; it is not a shadow cast by a scene light. Actors, trees and props do not yet cast into the structural shadow map. [Meshy multi-view generation and optional local cabin import](MESHY-WORKFLOW.md) are implemented for review. The east cabin exterior has been approved for continuing local work, but the optional import does not establish acceptance of every generated component, interior or topology defect. Generated art stays outside the public source repository. The current Windows configuration disables networking with `NONET=ON` and does not establish extra-player or voice support.

## 1. Complete coherent objects in Tristram

- Replace remaining fragmented scenery with whole-object geometry, including ruins, fences, stairs, small decorations and recognizable building details.
- Refine roof and wall shapes, ground contact, doors, windows, trunks and branches through source measurements instead of arbitrary box heights.
- Improve single-view NPC bodies and unseen materials while preserving their native identity and gameplay positions. Validate other hero classes separately from the existing warrior fixture.
- Keep every object closed with useful depth and opaque unseen faces. Preserve original-map collision, triggers, pathing and interaction.

A complete object should pass a native-angle comparison using forced meshes and a 360° review, with no accidental holes or resurfacing of filler hidden inside architecture. Selection and actor occlusion must work at the same poses. A topology pass is required but does not establish visual fidelity by itself.

## 2. Make asset import and provenance reproducible

- Define one local import workflow with documented units, origin, orientation, ground plane, material slots, normals, UVs and runtime limits.
- Record author, license, source, generator version and settings. Keep original master assets separate from runtime conversions and quality decisions.
- Use the user's local archives for matching references without distributing their contents. Create or accept redistributable art only with clear rights and attribution.
- Reject generated candidates that change defining features until they are corrected and pass the same object review. Keep credentials and paid-generation settings outside public artifacts.

This work should support both authored and procedural assets. It is a prerequisite for replacing temporary relief with maintainable models, rather than importing isolated files with unexplained scale and materials.

## 3. Extend lighting and real geometry-cast shadows

The first pass is implemented for static architecture with a 512×512 directional depth map, receiver-plane bias and filtered comparisons. Imported base-color lighting uses bounded cached RGB tables, with palette conversion after the linear shader. Eight audited cabin ground pieces use frozen masks to replace selected painted shadow pixels with nearby native grass, preserving original opacity and every unselected pixel. Other original ground shadows and baked surface light remain. The optional east-cabin review includes a timber floor, opaque inner walls with two explicit windows, and two physical candle sources with subtle independent flicker. The following work extends that bounded starting point, using the [building openings and fire-light standard](BUILDING-OPENINGS.md).

- Define scene lights, material normals and a clean separation between base color and baked illumination. Prefer unlit base-color sources when available and licensed for redistribution.
- Cast shadows from buildings, branches, foliage and actors onto actual receiving geometry. Moving actors and changed light positions must change their shadows.
- Extend and measure the existing CPU/Direct3D 11 paths, with explicit passes, bounded memory and correct depth/occlusion. The optional GPU renderer is already integrated; broader geometric shadows, persistent scene geometry and distance-based mesh LOD still require implementation and validation. Preserve the highest-quality masters for close-up views.
- Preserve the original renderer through F4 for comparison. Replace compatibility shadow decals in the 3D path only after geometric shadows cover the relevant objects and contact cases.
- Extend the calibrated cabin's local point lighting and interiors to other measured objects, including attenuation, opaque walls and explicit openings, before enabling it as a general scene feature.

The remaining lighting work accompanies coherent meshes and the asset pipeline, and precedes broad procedural-quality work. Painted dark pixels, flat decals and normal-based color shading alone do not establish geometry-cast shadows. Test roof overhangs, branch gaps, feet, walls, moving characters and shadow self-occlusion, and report frame cost alongside visual results.

### Planned after the Tristram completion gate: day and night

There is no runtime day/night cycle enabled in the current prototype. A future cycle should vary world time, sun direction, light intensity and color together, with shadows moving because the geometry is sampled from the changed light direction. Camera rotation must not move the sun. Night requires an explicit lighting policy; the current profile accepts an elevated directional light and is not itself a night-time implementation.

Keep render time separate from the simulation and its RNG. Define when changed light invalidates the shadow cache, measure rebuild cost, and test gradual color changes, shadow movement, contact and visibility at dawn, day, dusk and night. The native renderer selected through F4 remains the original comparison path rather than inheriting this new visual cycle.

### Partial implementation: Tristram horizon and fog

A provisional decorative horizon and fog are integrated with the perspective views in Tristram. This is a visual prototype, not expanded playable terrain or a finished day/night system. Its artistic continuity, silhouettes and rendering cost still require review; see [the camera/horizon contract](TRISTRAM-HORIZON-CAMERAS.md). Distant scenery must not create collision, pathing, trigger or interaction changes outside the original map.

Refine the bounded horizon and depth-aware fog, with a defined transition around silhouettes. Preserve foreground roofs, branches, actors and doors; use depth-aware blending so a far background cannot wash through nearer objects. Keep the same lighting model for the horizon and visible town, and document how painted backgrounds differ from actual distant geometry.

Validate the result across 360° rotation, camera distance and pan, including the native pose, map boundary, overlapping silhouettes and transparent foliage. Measure overdraw, blend cost, memory and frame time at each supported resolution. Picking and occlusion must continue to identify the nearest actionable native object; fogged decorative scenery must not become selectable gameplay terrain.

## 4. Improve deterministic procedural reconstruction

- Read native piece layouts and live scene geometry to produce complete objects rather than independent tile proxies.
- Use the existing game seed for reproducible visual variation without reseeding or consuming the simulation RNG.
- Keep scene construction and caches independent of camera changes, and rebuild them safely after resource reloads or genuine map changes.
- Measure construction time, runtime frame time, memory and diagnostic coverage. Add meaningful fixtures for occlusion, seams and irregular contours.

The target is a consistent town across viewpoints. A high whole-frame pixel score must not conceal errors in a small house, character or tree.

## 5. Reconstruct the procedural dungeons and the rest of Diablo 1

The user authorized a first-Cathedral pilot in parallel with Tristram. It is installed with native materials in nine nearby regions, and a reproduced transparency-budget fallback has been corrected; wall composition and physical gameplay still require review. This does not conclude the Tristram or Cathedral completion gates. Dungeon work must consume the actual generated map and seed, preserve doors, walls, stairs, triggers and collision, and handle changes during play. F4 remains a reversible view change in the same simulation. Other dungeon levels and quest-level variants currently retain the original renderer.

1. Validate the first generated Cathedral level across several existing seeds, including rooms, corridors, doors, stairs, actors and occlusion. Derive the reusable wall, floor and prop inventory from those real layouts.
2. Extend that pipeline through the Cathedral and then the Catacombs, Caves and Hell, accounting for each generator's topology, materials, elevation cues, lighting and interactive pieces.
3. Complete the remaining hero classes, monster families, bosses, equipment, animation and spell/effect representations needed by the full game. Retain native combat, quests, inventory and identity while replacing their visual representations.
4. Expand the collaborative asset catalog with stable IDs and reservation issues as each environment is analyzed, so contributors can claim whole models without duplicating another person's work.

The present [asset catalog](ASSET-CATALOG.md) covers the first town stage. It is not the final scope of the project or an exhaustive inventory of the full game. New environments require their own measured fixtures and acceptance evidence; completing Tristram alone does not complete D3D.

## 6. Future expanded multiplayer and persistent world

This phase follows completion and stabilization of the whole Diablo 1 reconstruction. **50–100 simultaneous players is an exploratory target, not supported capacity.** Restoring and testing the inherited four-player networking is a bounded preliminary experiment; changing a player-count constant does not establish a scalable or compatible server.

A persistent world needs defined authority, synchronization, ownership, save/restart and reconnect behavior. **Respawn/reset of loot, lootable world objects/containers and NPC shop stock is a prerequisite**, so new players can replay the route. Define instance boundaries, quest-state preservation, item ownership and duplication prevention before choosing reset policies. A configurable **30-minute interval is only an example to define and test**, neither an implemented feature nor a default. Actual intervals, reset triggers and exceptions remain design decisions.

Player names and levels, chat bubbles and friends belong to this future phase. Shared hubs, world instances and optional proximity voice remain architectural questions; no voice provider has been selected. Document protocol compatibility, resynchronization, moderation, privacy and operating costs, then validate multiple clients, load, transfers, reconnects, resets and persistent saves before claiming a capacity.

Track technical evidence and earlier alternatives in [networking research](NETWORKING-RESEARCH.md). Its recommendations and smaller experimental counts are research, not a shipped API or a chosen final architecture. The current local `NONET` build does not deliver network multiplayer, persistence, reset/respawn or proximity voice.

## 7. Evolve presentation, entry screens and project identity

The D3D logo and custom main-menu background are integrated; settings, music selection and project credits also have installed increments. The complete responsive HUD and its submenus remain work in progress, with candidate studies separate from the running game. Build project-specific screens on the inherited character, settings and input flows, with readable interface scale across display sizes and preserved upstream credits. Record the actual [engine bases and reference influences](ENGINE-BASES-AND-CREDITS.md): DevilutionX is modified source; Belzebub is studied through [binary reverse engineering](BELZEBUB-BINARY-ANALYSIS.md), with no code incorporated.

[Presentation review profiles](DISPLAY-PRESENTATION.md) now compare output sampling and wider view areas separately. Optional world-only 2× sampling smooths reconstructed edges while retaining logical camera/UI coordinates, under an explicit memory budget; it defaults off and does not increase source-art detail. Higher sampling increases rendering and memory cost. GPU rendering and edge smoothing are independent settings; acceleration is already available within its documented scope. Further work should improve scene preparation, GPU residency and independent readable UI layout with actual performance and image evidence. Keep gameplay timing, collision, saves and the original comparison renderer while extending this to the whole game.

## 8. Plan an original expansion after the faithful reconstruction

D3D should have its own coherent identity, integrating what we learn from the native game, DevilutionX and the Belzebub presentation study. First reconstruct the whole original game in 3D, preserving its recognizable elements, positions, rules, progression and generated dungeon layouts. Complete and stabilize that foundation first, then validate the expanded multiplayer/persistence phase before beginning the main implementation of original expansion content. Parallel research does not change this order.

The owner's preferred future expansion direction is above ground: additional outdoor areas beyond the existing surface town. New outdoor areas and additional content or levels require a separate approved proposal; scope, maps, traversal, quests and multiplayer implications are not defined or implemented. Diablo 2 is cancelled and outside this roadmap. Treat this separately from the decorative horizon/fog work and from reproducing the original underground levels. New traversable terrain requires its own collision, world and gameplay design; a background alone does not create an expansion.

## How to propose work

Read the [contribution guide](CONTRIBUTING.md), select a bounded object or subsystem, and state the current problem, intended result and validation fixture. Use the 3D asset issue form for complete objects or imports. Discuss large rendering and networking changes before implementation so interfaces and ownership can be agreed without duplicate work.
