---
title: "Tavern: removing remnants of the old scenery"
date: 2026-10-09
description: "The installed fix removes an old facade and a stone that overlapped the imported tavern, preserving the model and navigation. See the technical CPU comparison before and after."
slug: taverna-sem-residuos
image: /assets/captures/tavern-cleanup-after.webp
image_alt: "Offscreen technical CPU preview of the tavern after the native remnants were removed, at orbit zero; this is not a game-window capture or evidence of artistic approval."
category: Geometria
order: 20
status: published
---

An isolated wall and a stone in front of the tavern drew attention during the Tristram review. The investigation located elements of the old native scenery that were still being drawn alongside the imported building. The fix removes those remnants when the corresponding model is loaded, **preserving the tavern's artwork**.

The adjustment in [commit feb9ca77e](https://github.com/douglasopan/diablo-3d/commit/feb9ca77e73e66c2cc209af51ca9bb9b3504e6c1) has been installed in the same **Iniciar-Tristram.cmd** launcher since **October 9, 2026, at 00:44, Brasília time**. This is a limited scenery-composition fix; the town remains under review.

## Why the fragments appeared

The imported tavern already provided the building's masonry, but the visual replacement did not cover all of the old facade artwork. A native filler stone also remained in front of it, outside the conservative boundary used to hide elements replaced by the architecture.

The adjustment stops drawing **20 cells of the old facade and one specific stone group made up of four cells**. Identifying that stone requires an exact combination of position, native pieces and association with the imported tavern. This avoids extending the removal to neighboring stones. Without that model loaded, the native fallback path is preserved.

The stone group has **1,760 triangles**, which are now hidden in this case without changing its geometry. This count describes the visual cleanup; it is not a measurement of a performance gain. The model, its master, scale and placement adjustments, lighting and textures remain unchanged. Native collision and actors were also preserved.

## Before and after with the same camera

The two images below come from the integrator's own reproduction, with **the same camera and the same starting scene**. They are **offscreen CPU** previews, produced without capturing a gameplay window or running the comparison on the GPU. The opening image shows the result after the fix.

<figure><a href="/assets/captures/tavern-cleanup-before.webp" data-lightbox="true"><img src="/assets/captures/tavern-cleanup-before.webp" alt="Before: offscreen CPU preview of the tavern at orbit zero, with remnants of the native facade and the filler stone still drawn alongside the imported model." width="960" height="720" loading="lazy" decoding="async"></a><figcaption>Before — offscreen CPU reproduction, orbit zero. The old facade and filler stone still overlap the tavern composition.</figcaption></figure>

<figure><a href="/assets/captures/tavern-cleanup-after.webp" data-lightbox="true"><img src="/assets/captures/tavern-cleanup-after.webp" alt="After: the same offscreen CPU camera view of the tavern, with native remnants suppressed and the model, lighting, textures and actors preserved." width="960" height="720" loading="lazy" decoding="async"></a><figcaption>After — the same offscreen CPU camera, with the identified remnants suppressed. This is not a game-window capture or new artistic approval.</figcaption></figure>

The images were published in full, as lossless WebP, retaining **960×720 and every pixel** of the two authorized sources. Neither was cropped, retouched or generated again.

## What the comparison confirmed

The before-and-after reproduction passed **113 checks per version**, with **54 comparisons and eight orbit views**. The native map and architecture files compared remained byte-for-byte identical. This supports removal of the remnants in the exercised set, preserving the imported architecture and intended fallback behavior.

The Release x64 build passed before installation. The three aliases received the same executable, with a backup, and **no process was terminated**. The **44 profile files** retained their bytes, sizes and timestamps during installation; subsequent launcher preparation changed only its expected runtime receipt.

Validation is finite: a historical failure in the broader suite related to the Cathedral's footprint remains separate and **was not resolved by this tavern adjustment**. The result does not mean the entire scenery suite passed. There was no new GPU run, FPS measurement, physical input test or full artistic acceptance.

## Tristram's review continues

The next check is to observe the tavern in the usual play session and continue reviewing objects' orientation, proportions, materials and fit. The fix does not regenerate the model or complete the town assembly, HUD or editor tools.

The goal remains **all of Diablo 1 in 3D**. The Map Builder and first procedural level workstreams have private prototypes under review; the corresponding add-on remains disabled pending identity checks, and no new procedural 3D level is installed or playable. See the [roadmap](/roadmap/) to distinguish the current delivery from the next milestones.
