# Tristram lighting reference

The east cabin is the first calibration fixture for imported 3D materials. Compare actual game captures at the native camera pose, using the original backend as the reference. Geometry, UVs, camera, actor position and source texture must remain fixed during a lighting comparison.

## Shared light profile

`assets/d3d-lighting.ini` defines the shared Tristram environment. A local `d3d-lighting.ini` in a player's profile overrides it. Close and reopen the game after changing it; no game archive is modified. Missing or malformed settings fall back to the calibrated defaults in `TristramLightingConfig()`.

`AmbientRGB` and `DirectionalRGB` are **linear light factors**, not painted texture colors or sRGB bytes. `SunDirection` points from a surface toward the light in world X/height/Z coordinates. `DirectionalIntensity` scales the directional source. The depth map uses the same direction as the material shader. Ambient illumination remains when a building blocks the direct light.

The calibrated environment is:

```ini
[Tristram]
AmbientRGB=0.08,0.085,0.09
DirectionalRGB=1.55,1.47,1.62
SunDirection=0.32,1,-0.3
DirectionalIntensity=1
```

Imported base-color textures are decoded from sRGB, lit in linear RGB, and encoded back to sRGB before the final nearest-color mapping into the game's palette. The cached renderer uses six source bits per channel and 64 levels of directional irradiance; expensive conversion and palette searches happen when a texture cache is prepared. Geometry normals remain in world space. D3DMESH1 imports render both material sides, so the normal of a visible back face is oriented toward its visible side before lighting. This follows the [glTF two-sided material rule](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/schema/material.schema.json) and changes no mesh positions or UVs.

Native pixel artwork already includes painted lighting. Its compatibility rendering is retained instead of treating those pixels as unlit albedo. This first calibration therefore governs imported base-color assets; it does not remove baked light from all existing native materials. The Home reference continues to use the original renderer.

## Prepare subsequent models

- Supply clean base-color/albedo, without a painted ground shadow or a new light baked into the image. Keep the source albedo separate from the environment settings.
- Preserve complete object composition, physical scale, doorway/window positions and UV orientation. Reuse this light profile for the first comparison instead of giving every model its own light to hide a material error.
- Compare roof and wall patches separately, including their dark and bright ranges. A global average can conceal an overbright wall beside a correctly lit roof.
- Inspect nearby camera angles and a complete orbit. Lighting can expose bad normals, wrong material sides or geometry which casts an unexpected shadow.
- Keep the full-resolution original albedo and PBR maps locally. Normal, roughness and metallic maps are not yet applied by this software shader.

`tools/qa_cabin_lighting.py` checks identical camera and actor fixtures, matching game data and model hashes, then produces actual original/before/after captures and material statistics. Its optional isolated mathematical projection is labeled separately; it cannot approve the actual game's shadow rendering.

## Local calibration evidence

The 07 October 2026 calibration uses the east cabin at the fixed native pose with the actual mesh renderer forced on. Mean encoded RGB in corresponding material patches is below; these are distributions within the same patches, not pixel-perfect geometry scores.

| Material patch | Original backend | Meshy before | Calibrated Meshy |
|---|---|---|---|
| Roof | 37.30, 29.86, 28.84 | 35.23, 28.64, 27.98 | 37.76, 29.98, 30.47 |
| Lower window gable | 7.04, 6.26, 8.29 | 38.51, 34.48, 40.65 | 7.22, 5.90, 9.31 |
| Lower door side | 15.88, 14.20, 18.18 | 25.57, 22.81, 26.71 | 13.52, 11.31, 15.24 |

The window gable darkens toward the original without flattening the correctly lit roof. Correct orientation of visible material backs removes the large dark triangle patches. Actual captures at −5°/+5° and 90°/180°/270° were also inspected; there is no native reference for those unseen sides, so this does not establish an original-game color match there.

The Meshy roof still has less texture contrast than the native artwork, and the wall under the doorway eave remains darker. Those material and geometry differences need later asset refinement; the shared environment should not be retuned for each object to conceal them. The window is dark in these exterior-calibration captures; the subsequent interior stage is documented below.

Final local outputs are `diagnostics/v4-lighting-release-gog`, `diagnostics/v4-lighting-release-shareware` and `diagnostics/v4-lighting-release-meshy-gog`. The last uses the shipped profile with no player-profile override. `diagnostics/tristram-lighting-release-audit.json` records executable hashes, tests and comparison provenance. Captures and the proprietary-data-derived model stay local.

## Interior stage

The optional east-cabin Meshy review now contains a simple physical room: thick timber boards over a closed subfloor, inner stone walls, a gabled ceiling, a stone window tunnel, wooden pane dividers and a small volumetric luminous bulb. It is visible through the model's existing round window. The opening measured on the actual imported mesh is centered at world `(71.460, 2.215, 71.608)`; an inscribed 20-sided opening of radius `0.35` fits entirely inside it. No source triangle or source UV needs removal for this candidate. The inner rear wall seals the generated duplicate rear opening.

The bulb emits independently of diffuse illumination and supplies a warm point light in linear RGB `(1.0, 0.70, 0.08)`, radius `4.0` and intensity `6.0`. Its world position is `(70.85, 1.605, 70.888)`. Interior materials use clean procedural timber/stone base colors, low ambient light and distance/normal-dependent point illumination. A 64-level point-irradiance cache maps their final lit colors to the game palette. The room shell blocks light through opaque walls, and visibility through the window respects its actual polygon, including the stone corners outside the circular panes. There is no opaque yellow billboard covering the room.

`cabin-interior/lamp-on.png`, `lamp-off.png` and `interior-light.json` from the scene diagnostic record an actual same-camera runtime light/emission toggle; they are not an isolated mathematical mockup. The fixture verifies warm changed pixels, unrelated geometry preservation, rear-wall occlusion and unchanged native game state. The Meshy source file remains unchanged. Other models still need their own measured interiors; native collision continues to keep this cabin closed to the player.

## Ground-shadow cleanup and future moving light

The cabin cleanup now covers eight audited ground pieces: `867→859`, `868→860`, `871→875`, `872→876`, `873→875`, `874→876`, `882→881`, `884→883`. Frozen masks replace only the old painted-shadow regions with corresponding clean native terrain. Both opaque and transparent unselected pixels retain their original values, and original coverage is preserved. This operates in the 3D path; the original archive, map, collision and Home renderer stay unchanged. The live geometry shadow supplies the cabin's ground shadow.

The eight target pieces and 80 control pieces are checked independently from actual runtime floor readback. Dark terrain beside the well was inspected and kept because it is a legitimate grass/dirt transition, rather than a proven painted shadow. Other town objects require the same material-specific audit; do not erase arbitrary dark pixels to prepare a moving sun. Day/night and the Tristram horizon/fog are planned in the [roadmap](ROADMAP.md), not active features in this revision.
