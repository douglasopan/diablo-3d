# Contributing to D3D

D3D is a community prototype for rendering Diablo 1's Tristram in 3D while keeping the existing game simulation. We welcome contributors working on complete 3D objects, procedural geometry, rendering, asset import, visual comparison and documentation. Coordinate work in an issue or in [Discord](https://discord.gg/4YxQ7s69S), then open a pull request with a reviewable change.

**Resumo em português:** contribua com um objeto completo ou uma melhoria pequena e verificável. Compare a vista original e o giro de 360°, preserve colisão e partida, informe a origem dos recursos e não envie arquivos extraídos do Diablo nem chaves. O foco atual é Tristram; iluminação com sombras geométricas, geração procedural, voz e mais jogadores são trabalho futuro.

## Understand the current prototype

- F4 switches between the original view and the Tristram 3D view without reloading the map. Other levels use the original renderer.
- Home restores the native camera pose and uses the **actual original rendering backend**, including its selection rules. Identical pixels there do not prove that the reconstructed meshes match the original. Geometry comparisons must explicitly force the mesh renderer.
- Rotated views show reconstructed closed buildings, grouped trees and rocks, and characters with depth. Hero and cow meshes can use eight native animation views; current hero diagnostics use the warrior. Other townspeople have a single painted view, so their depth and unseen anatomy are inferred.
- Unknown scenery can still use closed relief per MIN fragment. It may be closed and selectable while remaining visually incoherent as a whole object. Meshy candidates can be imported for local review through the [documented workflow](MESHY-WORKFLOW.md), but generation and loading do not establish artistic acceptance.
- Actor ground shadows currently include compatibility decals from the original artwork. These are **not dynamic shadows cast by 3D lights**.
- The supplied Windows build uses `NONET=ON` and a separate local player profile. This prototype does not implement a multiplayer hub, extra player capacity or voice chat.

See the [roadmap](ROADMAP.md) for the order of work. Report the build or commit you tested; a completed build alone is not a claim of visual approval.

## Source, data and build setup

The engine source is at this repository root, with game code in `Source/`. Preserve its [license and notices](../LICENSE.md). The currently bundled engine uses the Sustainable Use License; making source public does not replace those terms or grant rights to the original game artwork.

Provide your own local game data. Do not commit `DIABDAT.MPQ`, other Diablo/Hellfire archives, extracted CEL/CL2/MIN/TIL/SOL/PCX artwork, textures copied from them, or derived assets that redistribute that artwork. A local reference extraction tool may read a user's archives without putting its output in the repository. New redistributable art must have an explicit compatible license and attribution. Do not commit saves, player profiles, generated build folders, API keys, credential files or protected secret blobs.

For a clean Windows clone, follow [Building D3D](BUILDING-D3D.md). The root `build.ps1` detects the flat public source layout or the existing nested workspace, locates installed Visual Studio C++ tools, CMake and Ninja, and builds the current local-only configuration. The public repository does not include a `.tools` bundle or game archives. Keep machine-specific paths out of portable build changes and explain any new prerequisite in the PR. The upstream [engine build documentation](../docs/building.md) remains available for general setup and other platforms.

In a configured Windows workspace, the current diagnostic build is:

```powershell
.\build.ps1 -WithSmoke -Targets devilutionx,town_view_smoke
.\build\town_view_smoke.exe 'C:\path\to\your\Diablo' '.\build\assets' '.\diagnostics\local-review'
```

Use a local game-data path, and report whether it was retail or shareware. Keep generated captures and asset extracts local unless they are cleared for public redistribution.

## Work on the whole object

1. Identify the actual native object: map coordinates, piece stencil, orientation, footpoint, bounds and nearby actors or triggers. Match source pieces by layout; do not merge unrelated scenery through a broad solid-cell flood fill.
2. Capture an original-backend reference and a forced-geometry reference with the same camera and game state. Record which renderer produced each image. Compare silhouette, doors, roof, window, base and contact with the ground.
3. Build one coherent object with real depth and closed surfaces. Paper cards, disconnected cell columns and an original photograph stretched over every face do not solve the unseen sides. Use the native art only on corresponding visible faces; give other faces appropriate opaque materials.
4. Preserve the live map and gameplay. Visual replacement must not alter collision, occupancy, triggers, NPC identity, item identity, movement or the game seed. A visual footpoint is not a reason to move a gameplay actor.
5. Review the object through 360°, including small changes on both sides of the original angle. Check occlusion by roofs and walls, ground contact, picking, movement and resource reloads. A whole-object silhouette can still be wrong even when every triangle is finite.
6. Cache expensive construction by actual source identity and relevant frame, and invalidate it when resources or scene data change. Do not retain pointers into released level or sprite resources.

For procedural work, derive placement from the live map and the existing game seed. Renderer code must not reseed or consume the simulation RNG. Keep any visual variation deterministic and separate from gameplay state.

## Asset and lighting pipeline

An asset proposal should record its author, license, original source, tool or generator version, reproducible settings and any transformation. Keep a master asset and a runtime asset separate. Record object origin, world scale, orientation, ground contact, material slots, UVs, triangle count and the intended native reference. An imported mesh must pass the same whole-object review as a procedural mesh.

Prefer base-color textures without painted lighting or ground shadows where a clean, legitimately redistributable source is available. Keep normals and material properties explicit. The original game's baked light remains a compatibility constraint; copying it into an albedo texture does not implement 3D lighting.

The current CPU depth map casts static architecture shadows from a directional light onto drawn receiving geometry. It caches construction and filters depth comparisons with receiver-plane bias. Trees, props, actors and moving lights still need caster support; baked surface illumination remains a compatibility constraint. Include shadow tests for roof overhangs, tree branches, character feet and moving actors as that support expands, without replacing collision rules with shadow geometry.

If you use a paid service, keep credentials local and make spending explicit before generation. Do not put keys in an issue, a PR, a prompt dump or a committed environment file. The local Meshy tooling and stored candidate are experiments, not evidence that a generated asset is approved or ready for the game.

## Validation and pull requests

For a rendering change, provide the original-backend view, forced-geometry view and relevant rotated views from a fixed fixture. Where uploads would redistribute proprietary art, describe the local reproduction and provide only evidence you have permission to publish. Report the data edition, viewport, camera pose, source coordinates and affected object.

Run checks appropriate to the change. Geometry work should demonstrate finite triangles, nonzero depth, closed edges with consistent orientation, outward component volume and deterministic reconstruction. Also verify picking and occlusion, ground contact and preserved game state. Test retail and shareware when available, and state any untested edition. Do not weaken a check to conceal a failure.

Pixel equality at Home tests original-backend reuse. Forced-geometry image comparisons measure the reconstruction. Round-trip picking tests measure selection. These are different results: report each for what it actually verifies.

Keep PRs focused, explain the problem and resulting behavior, link the issue, and include validation and remaining differences. Follow engine formatting and contribution conventions for engine changes. Use the [asset issue form](../.github/ISSUE_TEMPLATE/3d_asset.yml) before starting a large object or import pipeline change so contributors can avoid overlapping work.
