---
title: "Meshy and shadows: generation is where review begins"
date: 2026-10-07
description: "The eastern hut received multi-view generation and optional importing; buildings began casting shadows, with defects and limitations documented."
slug: meshy-sombras-revisao
image: /assets/captures/meshy-review.webp
image_alt: "Meshy hut in Tristram during the intermediate v4-lighting-meshy-gog run, before final exterior lighting calibration."
category: Ferramentas
order: 7
---

The eastern hut is the Meshy pipeline's first fixture. It offers an identifiable composition, with a roof, side door and round window, that can be compared from the native perspective. The experiment aims to obtain a candidate that can be inspected and check what generation preserves or changes.

This stage was published on October 7, 2026, at 18:08, São Paulo time, in [commit 2218b439b](https://github.com/douglasopan/diablo-3d/commit/2218b439bea8ab8b38c186283928ac6409c96163). The candidate was not adopted as the hut's default replacement.

The opening image comes from `v4-lighting-meshy-gog`, an intermediate run in the later lighting work. It records the model in the game before the final exterior calibration in [f673fc709](https://github.com/douglasopan/diablo-3d/commit/f673fc7090f21afc2f09a4fff3bd44a028943315), rather than the exact capture from the first import commit.

## From the first image to multiple views

The first test used the hut's original composition, without trees or characters. It consumed 30 credits and produced 2,760 triangles, along with a preserved master mesh. It recovered the general shape but changed the window, finishes and roof. This candidate stayed outside the game, available for local inspection.

The second experiment began by generating complementary views. That stage consumed nine credits. The next 3D request placed the original image first, followed by generated views of the back and gable, and consumed 35 credits. The result has 5,783 triangles, 4K textures, PBR maps and a master mesh.

The rear images are generated interpretations: Diablo does not provide an original view of that part of the building. They must appear as generation references, never as game screenshots. The [documented workflow](https://github.com/douglasopan/diablo-3d/blob/2218b439bea8ab8b38c186283928ac6409c96163/docs/MESHY-WORKFLOW.md) records this role and the observed costs, totaling 44 credits for the second experiment.

## Loading the candidate does not replace review

The model gained actual optional importing in a separate comparison profile. The normal launcher retains the locally reconstructed hut. Rotating the camera slightly leaves Home's native rendering path and makes it possible to observe the imported model.

Inspection and conversion preserve geometry, materials and UVs, with orientation and scale recorded. The loader checks sizes, finite values and texture coordinates; a missing or invalid file retains the procedural alternative. A valid load still requires checking placement, silhouette, doors, occlusion and topology.

The candidate duplicates a dark window on both gables, changes the roof ridge and glass, and includes ground slabs. Inspection also found boundary and non-manifold edges. These problems prevent treating the generated result as an approved final object. Normal, roughness and metallic maps are available locally but are not yet applied by the software shader.

## Shadows produced by architecture

The same commit introduced a directional depth map of 512 by 512. Roofs, walls and imported buildings block the light. The ground and rendered surfaces receive the shadow, with edge filtering; the map stays cached as the camera rotates.

Four tiles containing painted hut shadows received bounded masks. Replacement uses grass from donor tiles only for selected pixels covered by both tiles. Pixels outside the mask and the original opacity remain preserved. The map and collision remain native.

This system covers buildings. Trees, rocks and characters do not yet cast shadows through it, and actors retain their compatibility shadows. Removing the remaining painted shadows, adding new emitters and following moving objects are further tasks, described in the [status at this stage](https://github.com/douglasopan/diablo-3d/blob/2218b439bea8ab8b38c186283928ac6409c96163/docs/TRISTRAM-STATUS.pt-BR.md).

The next review calibrates exterior light with the model, camera and texture fixed. This separation makes it possible to identify lighting errors and differences that remain in the generated asset.
