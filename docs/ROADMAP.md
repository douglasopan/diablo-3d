# D3D roadmap

This roadmap describes priorities for a Tristram-first 3D renderer. It is not a release schedule or a claim that planned features already work. Join [Discord](https://discord.gg/4YxQ7s69S) or open an issue to coordinate an object, rendering improvement or research task.

**Resumo em português:** primeiro completar objetos 3D de Tristram e um pipeline de recursos com origem e licença claras; depois implementar iluminação e sombras geradas pela geometria. Melhorar a geração procedural vem em seguida. Catedral, mais jogadores e voz continuam como etapas futuras, sem promessa de multiplayer no build local atual.

## Current baseline

The v4 prototype switches views with F4 in the same local game. It renders Tristram only. The Home pose uses the actual original backend, so its pixel identity must be reported separately from forced rendering of the reconstructed meshes. Rotated views contain closed architectural meshes, complete native tree and rock groups, and character volumes. Some scenery still uses relief per fragment, and unseen geometry remains inferred from limited original views.

The existing actor shadow decal preserves original artwork; it is not a shadow cast by a scene light. Meshy output remains a stored candidate outside the runtime. The current Windows configuration disables networking with `NONET=ON` and does not establish extra-player or voice support.

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

## 3. Add lighting and real geometry-cast shadows

- Define scene lights, material normals and a clean separation between base color and baked illumination. Prefer unlit base-color sources when available and licensed for redistribution.
- Cast shadows from buildings, branches, foliage and actors onto actual receiving geometry. Moving actors and changed light positions must change their shadows.
- Develop and measure a bounded CPU solution compatible with the current software renderer, with explicit passes and correct depth/occlusion. Evaluate a GPU renderer with shadow maps or another suitable technique as a separate architectural choice.
- Preserve the original Home backend for comparison. Replace compatibility shadow decals in the 3D path only after geometric shadows cover the relevant objects and contact cases.

This lighting milestone follows coherent meshes and the asset pipeline, and precedes broad procedural-quality work. Painted dark pixels, flat decals and normal-based color shading alone do not satisfy it. Test roof overhangs, branch gaps, feet, walls, moving characters and shadow self-occlusion, and report frame cost alongside visual results.

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
