# Meshy: multiple views and local game review

The cabin workflow now calls Meshy's API and can load the generated triangles and their UV texture into Tristram. Generation success does not approve fidelity. Keep the original camera composition as the reference and inspect 360° views before adopting an asset.

## Current experiment

On 7 October 2026 the east cabin received:

1. A Meshy Image to Image request using the complete native cabin composition and `generate_multi_view: true`. It returned three complementary images and consumed **9 credits**.
2. A Multi-Image to 3D request using the **original image first**, then the generated back and gable views. Meshy 7.1, Ultra 2K geometry, 4K PBR textures, 6,000 target triangles, and a preserved pre-remesh master were requested. It consumed **35 credits** and returned **5,783 triangles**.

The 44-credit experiment is stored locally in `models/meshy/cabin-east-v2-views` and `models/meshy/cabin-east-v2-multiview`. The backs in the input are generated interpretations: Diablo supplies no original back view.

The resulting stone surfaces and complete shape improve the starting material, but the candidate still duplicates a dark round window on both gables, changes the ridge and glass, and includes floor plates. The inspected mesh also has boundary and nonmanifold edges. It is **an experimental review asset, not an accepted replacement**.

The exterior and lighting were approved for continuing local work. The [selected review baseline](ASSET-APPROVAL.md) now keeps the same private model and calibrated lighting across normal and presentation launchers when available. This selection does not establish full artistic acceptance. Window cuts and the lit interior are added by executable code; the original runtime mesh hash remains unchanged. Preserve both model and executable identity when comparing revisions.

## Run the review

After generating and converting the local candidate, use `Comparar-Cabana-Meshy.cmd`, or:

```powershell
.\Iniciar-Tristram.ps1 -MeshyReview
```

This opens a separate `perfil-meshy-review` profile with an initial copy of the regular player's save. Only its east cabin loads the optional `d3d-models/cabin-east.d3d`. The regular launcher keeps the reconstructed cabin. F4 enables the 3D view; rotate slightly to leave the native Home route and see the imported model. Home draws the original backend and cannot prove mesh fidelity.

## Reproduce the API workflow

Read the [Multi-Image to 3D contract](https://docs.meshy.ai/en/api/multi-image-to-3d) and [Image to Image contract](https://docs.meshy.ai/en/api/image-to-image). Multi-image generation accepts one to four views. The first is primary. `texture_image_urls` supplies multiple texture references; `remove_lighting` requests base color suitable for scene lighting.

Credentials come from `MESHY_API_KEY` or the current Windows user's protected local secret. Metadata records input hashes, options, task IDs and actual credit costs, omitting credentials and embedded image bytes. Each output directory owns one paid submission; an uncertain POST is never automatically retried.

```powershell
python tools/meshy_multiview.py views --image cabin-original.png --prompt-file cabin-views.txt --out-dir models/meshy/cabin-views
python tools/meshy_multiview.py status --out-dir models/meshy/cabin-views
python tools/meshy_multiview.py download --out-dir models/meshy/cabin-views
# Inspect the generated views before paying for the 3D step.
python tools/meshy_multiview.py model --images cabin-original.png back.png gable.png --out-dir models/meshy/cabin-model
python tools/meshy_multiview.py download --out-dir models/meshy/cabin-model
```

`tools/inspect_glb.py` extracts the real mesh, materials and effective UVs. `tools/render_glb_views.py` renders them with an explicit 3D fit and the native isometric projection; it does not align the resulting pixels to conceal differences. `tools/export_town_model.py` converts a single-material inspection and a measured fit JSON into the bounded D3DMESH1 runtime format. The fit records yaw, source bounds after rotation and target bounds relative to the scene model's reference tile.

```powershell
python tools/inspect_glb.py models/meshy/cabin-model/downloads/model.glb --out-dir models/meshy/cabin-model/inspection
python tools/export_town_model.py models/meshy/cabin-model/inspection/inspection.json --fit measured-fit.json --output models/meshy/cabin-model/runtime/cabin-east.d3d
```

The loader checks exact sizes, finite geometry and UVs, and falls back atomically for missing or invalid files. The current candidate uses a 1024-pixel base-color texture. Its unlit source color is decoded from sRGB and lit in linear RGB using the [shared Tristram profile](TRISTRAM-LIGHTING.md) before the final game-palette mapping. D3DMESH1 materials render both sides; visible back-face lighting is oriented accordingly without modifying the authored mesh. The downloaded 4K originals and PBR maps remain available locally; the software renderer does not yet apply normal, roughness or metallic maps. Imported geometry participates in the architecture shadow map. Native simulation and collision remain authoritative; a successful load still requires position, silhouette, doorway, occlusion and topology review. Original extracted art, local generated candidates, game archives, saves and credentials are excluded from publication.

The optional east-cabin review adds a timber floor and an inner room with explicit openings, wall-depth window tunnels and physical candle sources as a measured runtime adjunct. The source model and base-color file are preserved, and the native door stays closed. A successful new model import releases any prior room adjunct; rejected imports preserve the old model and room atomically. See the interior section in the lighting reference for geometry, source positions, bounded caches and actual fire-on/off evidence.

## Specify complete buildings before generation

Follow the [building openings and fire-light standard](BUILDING-OPENINGS.md). Keep a face inventory with each opening's identity, position, dimensions, shape, wall depth and open/closed state. Mark unseen features as inferred and require an explicit artistic decision. Tristram is the first fixture for a pipeline intended to cover the whole game, including procedural dungeon assets.

Describe that inventory consistently in every multi-view input: preserve the original visible facade, place each door/window on its declared face, retain roof gaps supported by the reference, and complete the walls, floor, frames and roof underside. Request real holes through the wall where required, rather than a black or luminous window texture. A generation prompt alone does not prove that an opening is cut correctly; inspect the returned geometry and repair it when necessary.

Interior illumination comes from candles, candelabra, hearths and fires appropriate to Diablo's setting. Request unlit base-color materials without electric bulbs, artificial lighting baked into walls or painted ground shadows. Place the actual fire meshes and point emitters in the runtime interior, with warm colors and independent subtle flicker. Plan more than one emitter where the original visible glow requires it, and validate their combined contribution instead of multiplying one calibrated light's intensity indiscriminately.

Check all views before adoption. Each hole must continue through the outer mesh and inner shell, show the same interior, and match its light-visibility boundary. Visual openness never overrides the native map's collision. Sloping roof apertures need an appropriate occlusion representation; the current axis-aligned cabin fixture is not proof that every roof gap already works.

## Characters: reuse a rig or request automatic rigging

Inspect a downloaded character's existing skeleton, skin weights and clips first. Keep its complete master and attribution record. The [reference-source audit](REFERENCE-SOURCES.md) records the supplied Sketchfab Diablo candidate; its published rig and one animation do not yet prove compatibility with native combat or fidelity to Diablo I.

Meshy's [Rigging API](https://docs.meshy.ai/en/api/rigging) is an alternative for textured humanoid bipeds with clearly separated limbs. It accepts a textured GLB or an eligible Meshy task; external GLBs must face +Z. The API does not promise suitable rigs for quadrupeds or unusual anatomy. Review deformation and any tail, wing or attachment bones separately.

The [Animation API](https://docs.meshy.ai/en/api/animation) applies preset actions to a completed Meshy rig. As checked on 7 October 2026, the [price table](https://docs.meshy.ai/en/api/pricing) lists 5 credits for rigging and 3 per preset action. Record actual costs with each task; do not spend credits merely to replace a usable existing rig.

The current D3DMESH1 town format is static and cannot preserve bones, weights or animation tracks. Character integration needs a separate animated visual path following the [native animation contract](MODEL-ANIMATION-CONTRACT.md). Game state and native event timing control movement, hits, projectiles and death; clips only draw the corresponding pose. Automatic rigging does not provide that synchronization. No rigging request or character integration has been executed by this audit.
