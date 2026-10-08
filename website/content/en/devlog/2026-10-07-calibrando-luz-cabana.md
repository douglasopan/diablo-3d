---
title: "Calibrating the hut's light from the original perspective"
date: 2026-10-07
description: "Actual original, before and after comparisons guided the Meshy hut's exterior lighting before conversion to the game's palette."
slug: calibrando-luz-cabana
image: /assets/captures/lighting-comparison.webp
image_alt: "Comparison of actual hut captures: original backend, Meshy model before calibration and model with calibrated exterior light."
category: Luz
order: 8
---

The imported hut needed predictable lighting. Some walls were too bright, and large triangular patches appeared on the roof. Before continuing the interior, the comparison fixed the camera, actor position, geometry, UVs and texture. The isolated change was the lighting treatment.

Exterior calibration was published on October 7, 2026, at 18:57, São Paulo time, in [commit f673fc709](https://github.com/douglasopan/diablo-3d/commit/f673fc7090f21afc2f09a4fff3bd44a028943315). The images in this article come from actual diagnostic runs. Artificial site-interface references and views generated for Meshy have a different role and are not presented as game results.

## Applying light before the palette

The imported base-color texture is decoded from sRGB, receives lighting in linear RGB and returns to sRGB before selecting the closest color in Diablo's palette. This preserves use of the original palette while separating material color from the intensity of the light it receives.

The shared profile defines ambient light, directional light and the sun's direction. Direct light and the shadow map use the same world-space direction. Ambient light continues to contribute in regions blocked by the building. Rotating the camera does not change the source's position.

Imported materials are drawn on both sides. The normal of a face viewed from behind now receives the appropriate orientation for this lighting. The correction removed the large triangular patches without changing the model's vertices, UVs or texture.

Native artwork already contains painted lighting and retains the compatibility treatment. This calibration therefore governs imported base-color assets. It does not remove embedded lighting from every scene material, as explained in the [lighting reference](https://github.com/douglasopan/diablo-3d/blob/f673fc7090f21afc2f09a4fff3bd44a028943315/docs/TRISTRAM-LIGHTING.md).

## Comparing the roof and walls separately

The comparison uses the same material crops in the original backend, the previous candidate and the calibrated version. Encoded RGB averages help interpret the change, but are not a score for geometric fidelity.

| Region | Original | Before | Calibrated |
| --- | --- | --- | --- |
| Roof | 37.30 / 29.86 / 28.84 | 35.23 / 28.64 / 27.98 | 37.76 / 29.98 / 30.47 |
| Lower gable wall | 7.04 / 6.26 / 8.29 | 38.51 / 34.48 / 40.65 | 7.22 / 5.90 / 9.31 |
| Lower door wall | 15.88 / 14.20 / 18.18 | 25.57 / 22.81 / 26.71 | 13.52 / 11.31 / 15.24 |

The gable moved closer to the original dark tone, the side lost its excess brightness and the roof retained its brightness. The Meshy texture still has less contrast, and the wall beneath the door eave remains darker. These differences remain material and geometry issues to review.

![Captures of rotations near the original angle during the lighting review.](/assets/captures/lighting-orbits.webp)

*Rotations of minus five and plus five degrees help reveal problems close to the comparison perspective.*

![Captures of the hut from other angles during the lighting review.](/assets/captures/lighting-angles.webp)

*The back and sides were inspected through rotation. Without native views of these surfaces, the review cannot claim a color match against an original that does not exist.*

## Evidence and the next work

Lighting and scene tests passed with full GOG data, shareware and the separate Meshy profile. The final run uses the distributed profile, without local configuration overrides. This validation does not complete the artistic review or resolve the candidate's topological defects.

Home continues to use the original backend; calibration uses forced meshes at the same framing to measure the actual change. The yellow window, interior light and wooden floor are the next stage and **are not active in this commit**. Later advances need their own evidence before appearing as complete.

The [roadmap](https://github.com/douglasopan/diablo-3d/blob/f673fc7090f21afc2f09a4fff3bd44a028943315/docs/ROADMAP.md) keeps day/night, horizon and fog as plans after Tristram is complete. The choice between a walkable expansion and a decorative horizon remains open. This stage delivers a common exterior reference for continuing model and material review.
