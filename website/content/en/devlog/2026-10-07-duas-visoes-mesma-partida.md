---
title: "One game, two ways to see Tristram"
date: 2026-10-07
description: "A retrospective on D3D's first local diagnostic run: switching views, rotating the camera and preserving the original Diablo game."
slug: duas-visoes-mesma-partida
image: /assets/captures/v1-center.webp
image_alt: "Contextual screenshot from the first local diagnostic run of Tristram's 3D view."
category: Protótipo
order: 1
---

D3D's first step was to introduce another way of drawing Tristram within the same game. The town, the character and the rules still belong to the game. The visual change must follow that state, allow comparisons and return the player to the original view whenever they want.

This article is a retrospective published on October 7, 2026. The images represent the first local set of diagnostics, preserved in `diagnostics/gog`. That run was recorded at 05:37 on the same day. This time identifies the local evidence; it does not represent a separate release. The code from the early stages was published with the v4 prototype in [commit 4cb265c01](https://github.com/douglasopan/diablo-3d/commit/4cb265c012925936645c926e3e59426351b4c6c5), at 15:29, São Paulo time.

## Switching the rendering while preserving the game

The F4 key switches rendering in Tristram without reloading the map. The first test sequence checked 24 mode switches, confirming that the map, player and camera target remained unchanged. This established a basic condition for the project: a change in presentation must not create a different game session or interfere with the existing rules.

This separation also defines the scope of the prototype. The 3D view applies to the town. Upon entering the Cathedral, the game retains its original rendering. Inventory, movement, collision and dialogue continue to be calculated by DevilutionX. The approach is to reconstruct all of Tristram first and develop this method before extending it to the procedural levels.

The documented Windows build uses `NONET=ON` and runs offline. More players, a shared hub and proximity voice remain subjects for future research. The checks at this stage concern the local game; they do not establish a new networking implementation.

The prototype is based on DevilutionX 1.6.0-dev, at [commit dac104bab](https://github.com/diasurgical/devilutionX/commit/dac104babfb6187415432f428ac2516747ffc154). The history before that base belongs to the upstream engine. D3D's contribution begins with its renderer and tools; preserving that distinction is also part of this retrospective.

## Drawing and selecting the same scene

A camera that changes the projection also changes where the player sees each object. The initial diagnostics therefore checked selection of walkable ground, the player and townspeople at their actual positions. They also rejected clicks outside the world area and verified that rendering did not overwrite the interface rows.

When the camera rotates, the previous selection must be invalidated. The test confirmed this update and the mapping between ground coordinates and their screen projection. Asset reloading was exercised separately: releasing and rebuilding the cache should produce the same image again in the same state.

![Rotated view from the first local Tristram diagnostic run.](/assets/captures/v1-rotated.webp)

*The rotation exposes the state of that first experiment. This screenshot is development evidence, not a promotional image of a finished town.*

## What this stage demonstrates

This run shows that the new presentation could coexist with the native game, respond to rotation and maintain consistent selection. It does not complete the reconstruction of houses, trees, rocks or characters. An image that reads well from one angle can still reveal insufficient volume when the camera moves.

Automated captures must also be distinguished from the experience in an open game session. The diagnostics run without a window and check specific renderer contracts. Walking, talking and using the controls in the game remain part of the review. The [documented prototype status](https://github.com/douglasopan/diablo-3d/blob/4cb265c012925936645c926e3e59426351b4c6c5/docs/TRISTRAM-STATUS.pt-BR.md) records these limitations.

The next stage starts treating buildings as complete objects and expands the camera controls. The same rule follows this progress: improve the geometry while preserving the simulation that already works.
