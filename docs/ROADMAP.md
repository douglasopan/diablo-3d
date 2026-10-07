# D3D roadmap

This roadmap describes priorities for a Tristram-first 3D renderer. It is not a release schedule or a claim that planned features already work. Join [Discord](https://discord.gg/4YxQ7s69S) or open an issue to coordinate an object, rendering improvement or research task.

**Resumo em português:** completar Tristram, seus objetos 3D e o pipeline de recursos continua sendo a prioridade. A iluminação de materiais importados e as sombras da arquitetura já têm uma primeira implementação; ciclo dia/noite, horizonte e neblina ainda são propostas, sem ativação no jogo. Catedral, mais jogadores e voz continuam como etapas futuras, sem promessa de multiplayer no build local atual.

## Current baseline

The v4 prototype switches views with F4 in the same local game. It renders Tristram only. The Home pose uses the actual original backend, so its pixel identity must be reported separately from forced rendering of the reconstructed meshes. Rotated views contain closed architectural meshes, complete native tree and rock groups, and character volumes. Some scenery still uses relief per fragment, and unseen geometry remains inferred from limited original views.

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

The first pass is implemented for static architecture with a 512×512 directional depth map, receiver-plane bias and filtered comparisons. Imported base-color lighting uses bounded cached RGB tables, with palette conversion after the linear shader. Eight audited cabin ground pieces use frozen masks to replace selected painted shadow pixels with nearby native grass, preserving original opacity and every unselected pixel. Other original ground shadows and baked surface light remain. The optional east-cabin review now includes a timber floor, an opaque room shell and a warm point source visible through its measured polygonal window. The following work extends that bounded starting point.

- Define scene lights, material normals and a clean separation between base color and baked illumination. Prefer unlit base-color sources when available and licensed for redistribution.
- Cast shadows from buildings, branches, foliage and actors onto actual receiving geometry. Moving actors and changed light positions must change their shadows.
- Develop and measure a bounded CPU solution compatible with the current software renderer, with explicit passes and correct depth/occlusion. Evaluate a GPU renderer with shadow maps or another suitable technique as a separate architectural choice.
- Preserve the original Home backend for comparison. Replace compatibility shadow decals in the 3D path only after geometric shadows cover the relevant objects and contact cases.
- Extend the calibrated cabin's local point lighting and interiors to other measured objects, including attenuation, opaque walls and explicit openings, before enabling it as a general scene feature.

The remaining lighting work accompanies coherent meshes and the asset pipeline, and precedes broad procedural-quality work. Painted dark pixels, flat decals and normal-based color shading alone do not establish geometry-cast shadows. Test roof overhangs, branch gaps, feet, walls, moving characters and shadow self-occlusion, and report frame cost alongside visual results.

### Planned after the Tristram completion gate: day and night

There is no runtime day/night cycle enabled in the current prototype. A future cycle should vary world time, sun direction, light intensity and color together, with shadows moving because the geometry is sampled from the changed light direction. Camera rotation must not move the sun. Night requires an explicit lighting policy; the current profile accepts an elevated directional light and is not itself a night-time implementation.

Keep render time separate from the simulation and its RNG. Define when changed light invalidates the shadow cache, measure rebuild cost, and test gradual color changes, shadow movement, contact and visibility at dawn, day, dusk and night. The original Home backend remains the original comparison path rather than inheriting this new visual cycle.

### Planned: Tristram horizon, fog and depth-aware visibility

A visual horizon and distance fog are not currently implemented. The choice between expanding the explorable town and adding a decorative horizon remains open. Until expansion is specified and tested separately, distant scenery is visual only: it must not create collision, pathing, trigger or interaction changes outside the original map.

Prototype a bounded horizon at the town edge and fog derived from world/view depth, with a defined transition around silhouettes. Preserve foreground roofs, branches, actors and doors; use depth-aware blending so a far background cannot wash through nearer objects. Keep the same lighting model for the horizon and visible town, and document how painted backgrounds differ from actual distant geometry.

Validate the result across 360° rotation, camera distance and pan, including the native pose, map boundary, overlapping silhouettes and transparent foliage. Measure overdraw, blend cost, memory and frame time at each supported resolution. Picking and occlusion must continue to identify the nearest actionable native object; fogged decorative scenery must not become selectable gameplay terrain.

## 4. Improve deterministic procedural reconstruction

- Read native piece layouts and live scene geometry to produce complete objects rather than independent tile proxies.
- Use the existing game seed for reproducible visual variation without reseeding or consuming the simulation RNG.
- Keep scene construction and caches independent of camera changes, and rebuild them safely after resource reloads or genuine map changes.
- Measure construction time, runtime frame time, memory and diagnostic coverage. Add meaningful fixtures for occlusion, seams and irregular contours.

The target is a consistent town across viewpoints. A high whole-frame pixel score must not conceal errors in a small house, character or tree.

## 5. Extend beyond the town

Begin the Cathedral after the Tristram object, material and lighting pipeline is sufficiently stable. Dungeon work must consume the actual generated map and seed, preserve doors, walls, stairs, triggers and collision, and handle changes during play. F4 should remain a reversible view change in the same simulation. Other levels currently retain the original renderer.

## 6. Research larger multiplayer and voice

More players, shared hubs or multiple world instances and optional proximity voice are exploratory goals. Changing a player-count constant or adding an audio SDK does not establish a compatible multiplayer architecture. First document protocol, state, save/resync, world-instance, moderation, privacy and operating-cost constraints.

Track evidence and open decisions in [networking research](NETWORKING-RESEARCH.md). Its recommendations are research, not a shipped API. The current local `NONET` build remains the baseline until a separately tested networking implementation exists.

## How to propose work

Read the [contribution guide](CONTRIBUTING.md), select a bounded object or subsystem, and state the current problem, intended result and validation fixture. Use the 3D asset issue form for complete objects or imports. Discuss large rendering and networking changes before implementation so interfaces and ownership can be agreed without duplicate work.
