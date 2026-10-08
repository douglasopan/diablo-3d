---
title: "Comparing without hiding the differences"
date: 2026-10-07
description: "The v3 stage corrected ground tiles and house boundaries and separated returning to the original backend from actual comparisons of reconstructed meshes."
slug: comparar-sem-esconder-diferencas
image: /assets/captures/v3-forced.webp
image_alt: "Tavern comparison in v3: the original backend and a forced 3D mesh from the same perspective."
category: Ferramentas
order: 3
---

Comparing the reconstruction with Diablo requires two images of the same place, with the same camera and game state. It also requires knowing which renderer produced each image. This distinction became central in v3: returning to the original image and checking mesh fidelity are different forms of verification.

This article was published on October 7, 2026 as a retrospective. The `diagnostics/v3-gog` run was recorded at 08:38 that day. The stage was consolidated with the v4 prototype in [commit 4cb265c01](https://github.com/douglasopan/diablo-3d/commit/4cb265c012925936645c926e3e59426351b4c6c5). There is no independent public release for every number in these local runs.

## The painted area is not the wall boundary

An image of a house includes its roof, projected sides, shadows and nearby details. Using this entire rectangle as the building's physical body can make the geometry occupy a place where the game allows walking. The character still follows native collision but appears to pass through the rendered house.

V3 separated these responsibilities. Closed walls were checked against walkable cell centers, actors' foot positions and step segments permitted by the map. The aim is to keep the house's body within blocked areas while preserving the visual composition of the facade and roof.

The workshop, Adria's hut and the Cathedral entrance have important openings. Reconstruction must preserve these passages. Closing every surface indiscriminately could produce a mechanically consistent mesh and a scene that is wrong for the game.

## Ground tile zero exists

Another correction addressed a ground tile that had been interpreted as missing content. Index zero is real, walkable ground near the eastern hut. Discarding it left black holes in the terrain.

The diagnostics began checking that this region was drawn and remained selectable. The correction changes neither the map nor its collision: it correctly recognizes data that was already present. It is an example of how a small mistake in interpreting the source can create a large visual difference.

![Cathedral exterior in v3, with a forced mesh from the native perspective.](/assets/captures/v3-low.webp)

*Screenshot from `diagnostics/v3-gog/calibrated-cathedral.png`. The filename identifies a forced-mesh comparison at the native camera anchor. The scene shows the Cathedral exterior in Tristram; it does not represent a completed first procedural level.*

## Home and the forced mesh

At the position restored by Home, the prototype uses the original backend itself, including its draw order and selection rules. If the pixels match on this path, returning to the original rendering is working. That result does not measure the quality of the reconstructed geometry.

To measure that, the tools force the mesh renderer at the same native angle. V3 checked projection of ground centers at 121 points and recorded separate images of the original backend, the Home reset and the forced geometry. The modified camera also uses selection from the geometry path.

The later v4 comparison was explicit: all 24 Home views matched the reference, while none of the 24 forced-mesh views matched completely. The results are described in the [prototype status](https://github.com/douglasopan/diablo-3d/blob/4cb265c012925936645c926e3e59426351b4c6c5/docs/TRISTRAM-STATUS.pt-BR.md).

## Using differences to guide the review

A high percentage of matching pixels can be dominated by ground and background. An error in a window, a roof or a townsperson's body occupies a smaller area and can still be decisive in recognizing the object. Reports need to be read alongside screenshots and relevant crops.

The tool compares RGB at the same coordinates, including true black, without moving or resizing images to hide differences. Small rotations to either side of the original angle and a full orbit add another kind of evidence: they reveal surfaces that a frontal comparison cannot judge on its own.
