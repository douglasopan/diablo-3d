---
title: "Cathedral: native materials and lighter transparency"
date: 2026-10-09
description: "The first-floor pilot receives floors, masonry and wood from the original, with explicit visual approximations. Regional copying reduces the measured transparency cost; shapes, doors and gameplay still need review."
slug: catedral-materiais-transparencia
image: /assets/captures/cathedral-materials-after.png
image_alt: "Offscreen technical test of the Cathedral pilot with floors and masonry using approximated native materials, a closed door and actors as sprites; this is not gameplay or final artwork."
category: Geometria
order: 22
status: published
---

The [first Cathedral pilot](/devlog/catedral-primeiro-andar-piloto/) used a checkerboard floor and walls in simple colors. The area now receives **materials from the original**, while transparency copies only the region needed for each triangle. The change brings the appearance closer and reduces renderer costs in the measured cases.

The delivery in [commit a783aff4](https://github.com/douglasopan/diablo-3d/commit/a783aff47ff66fc9abb45d0345c604b444957087) was installed on **October 9, 2026, at 17:34, Brasília time**, through the same **Iniciar-Tristram.cmd** launcher. The scope remains **nine regions near the player, only on the Cathedral's standard first floor**. Other floors and quest levels retain the native presentation; actors in this pilot remain sprites.

## Original materials, shapes still provisional

The floor uses **CEL/MIN** data already prepared by the engine: a **64×32** bitmap and a coverage mask independent of color. Visual alignment was corrected without replacing the physical mesh or native selection bindings. Masonry uses strips oriented by the face and SOL source; wood comes from a small section of the door's **CLX**.

Repetition, donor materials, caps and faces without a reference are **approximations**. Shapes, heights, arches, stairs and details are unfinished. Using native materials does not constitute full artistic approval or mean that the entire floor has been reconstructed.

The pair below shows the same nearby technical framing, without antialiasing: before, with simple colors; after, with materials and regional copying. These are **offscreen runs, with a stationary scene and partial initialization**, without a HUD or a gameplay session using physical input. The visual floor changes deliberately; the before-and-after comparison does not claim equal pixels.

<figure><a href="/assets/captures/cathedral-materials-before.png" data-lightbox="true"><img src="/assets/captures/cathedral-materials-before.png" alt="Before: offscreen technical Cathedral test with nearby framing and no AA, showing a checkerboard floor, walls in simple colors, a door and native actors as sprites." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>Before — nearby technical baseline, without AA, using temporary materials. Stationary scene and partial initialization; this is not a gameplay capture.</figcaption></figure>

<figure><a href="/assets/captures/cathedral-materials-after.png" data-lightbox="true"><img src="/assets/captures/cathedral-materials-after.png" alt="After: the same nearby technical framing without AA, with floors and masonry using approximated native materials, a closed door and actors as sprites." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>After — approximated native materials and regional copying in the same technical pose. The visual floor was corrected; shapes and details remain provisional.</figcaption></figure>

The following comparison forces geometry on at the original perspective, alongside the native reference. It reveals differences in coverage, walls and arches that still need review. **Home** continues to use the original drawing path; that return alone does not validate the reconstruction's fidelity.

<figure><a href="/assets/captures/cathedral-materials-original-perspective.png" data-lightbox="true"><img src="/assets/captures/cathedral-materials-original-perspective.png" alt="Original perspective with the pilot's geometry forced on: approximated native flooring and flat walls, with shapes and details still differing from the reference." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>Forced geometry — offscreen test at the original perspective. Materials bring the area closer to the reference; shapes and details still differ.</figcaption></figure>

<figure><a href="/assets/captures/cathedral-pilot-native-reference.png" data-lightbox="true"><img src="/assets/captures/cathedral-pilot-native-reference.png" alt="Native reference of the Cathedral technical state, with original floors, walls and arches; offscreen capture without a HUD or a gameplay session using physical input." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>Native reference — this PNG is identical to the one already published and has been reused. It serves the technical comparison; it does not represent every intermediate failure or artistic approval of the pilot.</figcaption></figure>

The three new PNGs retain their bytes and original **640×480** resolution, without conversion, cropping or retouching. The existing reference also remains intact.

## Transparency with less work per frame

Previously, each transparent triangle saved a copy of the whole screen. The backend now copies a conservative area around the triangle, preserving order, colors, depth, selection and admission limits. The isolated A/B comparison kept **nine complete buffers** identical; the backend A/B comparison in the native scene preserved **12 byte-exact PNGs**. This equality covers the isolated optimization: adding the materials afterward deliberately changes the image.

The measurement used a **real Radeon RX 570**, six pose and quality combinations, five warmups and twenty samples per case, with **156 renderer calls per phase**. Logical resolution was **640×480**; AA2x draws at **1280×960**. Two excerpts from the warm distributions:

| Case | Before: median / p95 | Materials + regional copying: median / p95 |
| --- | --- | --- |
| Nearby, without AA | 13.42 / 13.76 ms | 10.27 / 11.29 ms |
| Distant, AA2x | 77.73 / 79.05 ms | 65.86 / 68.64 ms |

These are wall-clock times for the **renderer, including GPU waits and readback**. They exclude gameplay, HUD, presentation and external native preparation; they are neither sustained FPS nor a promise of 60 FPS. Cold frames that create resources are kept separate from warm samples. The other cases and intermediate phase are in the [versioned GPU record](https://github.com/douglasopan/diablo-3d/blob/a783aff47ff66fc9abb45d0345c604b444957087/docs/GPU-RENDERER.md).

In the distant AA2x case, snapshot payload fell from **253,132,800 to 820,803 bytes per frame**. This is the volume of the copied regions, not a measurement of total bus traffic. The GPU remained active in every sample, without CPU 3D rasterization or warm texel/LUT uploads. Commands and readback remain significant costs.

## What was installed and what remains to be tested

The material gate passed **168,022 checks over 81,152 texels**, compared with the native rasterizers. There were eight frames with geometry forced on and seven technical PNGs. The area's cache has **72 entries and 162,304 bytes** of pixels and coverage; four warm redraws performed no new decoding. Its limits and lack of automatic eviction still need work before expanding the floor.

Recovery was checked separately on the same build: **157 harness checks, 188 contextual checks and 93 allocation checks**, with four failures and immediate GPU recoveries. Ogden and Tristram passed **1,348 checks**, preserving twenty byte-exact PNGs. The **290,243-byte** native witness remained identical, with the map, SOL, RNG and transparency preserved. The fixture did not contain an open door: **its operation was not tested**.

The three installed executables have the same **6,559,744 bytes**, SHA-256 **929a78f6c15d7e642225c11bc035462c5d93009a87786165eddf9d48746be652**. The **48 original profile files** retained their contents, sizes and timestamps; only the launcher's derived receipt was renewed. There was no reduction or replacement of Tristram masters, selections or textures, payload installation, process termination or new paid operation.

The next step is to test entry, movement, combat, transitions and doors in a play session, review shapes and details, and measure commands, readback and the cache before expanding the levels. Technical captures and renderer times do not replace those tests. The [versioned integration status](https://github.com/douglasopan/diablo-3d/blob/a783aff47ff66fc9abb45d0345c604b444957087/docs/PROJECT-EXECUTION.md) and [roadmap](/roadmap/) keep the pilot separate from the goal of rebuilding **all of Diablo 1 in 3D**.

### Continuity update — October 9, 18:48 Brasília time (UTC−3)

Douglas reported areas where the view returns from 3D to 2D during play, without being able to identify a specific location. Revision [f710f47b](https://github.com/douglasopan/diablo-3d/commit/f710f47b6d5f9e5e1ff05976410c8577ef73e868), installed at **18:48** through the same launcher, adds context logging to investigate this return. **The specific cause has not yet been reproduced or fixed.**

The [continuity diagnostic](https://github.com/douglasopan/diablo-3d/blob/f710f47b6d5f9e5e1ff05976410c8577ef73e868/docs/CATHEDRAL-CONTINUITY.md) records the stage, seed, position, cache and GPU state before refusal. Materials, fallback policies and limits remain the same. Accepted preparation does not announce recovery: recovery is recorded only after the complete 3D drawing has been published.

A partial technical setup passed **121 CPU checks and 196 GPU checks on the RX 570**. Logical and HD images remained byte-exact; picking and depth were checked at one test point. Installation preserved the **49 current profile files**, renewing only the derived receipt. These tests verify the diagnostic; they do not establish a fix for the spontaneous return, full gameplay or sustained FPS.

Wall composition remains a separate investigation: the material still repeats a MIN strip and may use a donor source. Checking its bindings does not establish the complete facade or the pixels of every face. The next step is to collect the new diagnostic and reproduce the observed refusal. **There was no new benchmark; the timings and captures above still refer to revision a783aff4.**
