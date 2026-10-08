---
title: "Two candles, two windows and the same room"
date: 2026-10-07
description: "The optional hut received two physical candles with subtle flicker and an actual rear opening, preserving the exterior model and its calibration."
slug: velas-janelas-cabana
image: /assets/captures/cabin-fire-tone.webp
image_alt: "Front window: original reference, interior with the previous source and two-candle revision, in enlarged crops without color adjustments."
category: Luz
order: 10
status: published
---

The eastern hut's light now comes from two candles, each with wax, a wick, a holder and a volumetric flame. The rear window also opens into the same room, with stone walls, a ceiling and a plank floor. This revision replaces the generic source and closed rear wall described in the [previous record](/devlog/interior-cabana-sombras-terreno/).

The change was published on October 7, 2026, at 20:02, São Paulo time, in [commit cdeaaab0d](https://github.com/douglasopan/diablo-3d/commit/cdeaaab0d208bf0da4239b98c287619508e308a5). It remains restricted to the optional Meshy review profile; it does not complete Tristram's interiors or the other environments in Diablo 1.

## Openings that pass through the wall

Both windows have cutouts in the interior wall, stone tunnels and wooden dividers. Their physical boundaries also govern the passage of light. Twenty-sided polygons follow the openings, preventing an invisible rectangle from allowing rays through the masonry's opaque corners.

The front opening was measured in the imported model. The rear opening is an artistic inference explicitly approved by the project lead: Diablo offers no original rear view that proves this design. The revision opens the interior wall behind the existing gap in the candidate, preserving its stone border. This distinction between reference and design decision is part of the new [standard for openings and fire lighting](https://github.com/douglasopan/diablo-3d/blob/cdeaaab0d208bf0da4239b98c287619508e308a5/docs/BUILDING-OPENINGS.md).

![Rear window with fire sources off and on, using the same camera.](/assets/captures/cabin-rear-fire.webp)

*The light reveals the interior through the physical opening. The door remains closed, and original collision keeps the player outside.*

## Fire with a small variation

The sources use linear RGB `(1, 0.665, 0.094)`, a radius of four units and different intensities. Each candle varies smoothly by up to ±6%, with its own phase. This flicker uses visual time and a stable identity for each source without consuming the game's randomness.

The flame separates an amber body, a small core and a darker tip. Its emission is calculated separately from the light reaching surfaces. This reduces the very bright yellow area of the previous emitter. A small bright highlight can still appear after palette conversion; the result does not represent a perfect match with the original pixels.

Materials receive lighting before final color conversion. Bounded caches avoid repeating palette lookups for every pixel. The [lighting reference](https://github.com/douglasopan/diablo-3d/blob/cdeaaab0d208bf0da4239b98c287619508e308a5/docs/TRISTRAM-LIGHTING.md) records the parameters and their limitations.

## Checking front, back and time

The actual comparison toggles the sources and their emissions, with the same camera and time. At the front, 112 pixels changed, of which 111 were classified as warmed. At the back, all 73 changed pixels were warmed. No pixels belonging to other objects changed in these tests.

The temporal test freezes the scene at 0, 3 and 17 seconds. Summing the differences against time zero records 19 changed pixels after palette conversion, again without changes outside the hut. These are results from this test scene, not quality measurements for all models.

![Hut before and after the revision at rotations of minus five, zero and plus five degrees.](/assets/captures/cabin-fire-orbits.webp)

Mathematical lighting checks and the GOG, shareware and Meshy scenes passed. The 5,783 exterior triangles, their UVs and the converted model file were preserved. Four exterior roof and wall regions retained exactly the same pixels, and the shared exterior lighting profile did not change.

## A standard to build on

![Before and after comparison from four angles, showing the front and back of the hut.](/assets/captures/cabin-fire-angles.webp)

The window position and materials still differ from the reference. The small opening limits how clearly the planks can be seen with the normal camera. Dividers and interior objects do not yet cast individual shadows from these point sources, and the wall system does not cover arbitrary openings in sloped roofs.

The standard begins with this hut and must be validated against the references for each building. The goal remains all of Diablo 1 in 3D; Tristram is the first stage, followed by the procedural levels and the remaining characters, monsters and effects. The [roadmap](https://github.com/douglasopan/diablo-3d/blob/cdeaaab0d208bf0da4239b98c287619508e308a5/docs/ROADMAP.md) describes this sequence without promising a completion date.
