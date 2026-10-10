---
title: "Two gameplay cameras and Cathedral continuity"
date: 2026-10-09
updated: 2026-10-10
description: "Cameras and Cathedral continuity, with an October 10 local update: grates, lighting and native item names in the 3D world. Gameplay and artwork remain under review."
slug: cameras-continuidade-catedral
image: /assets/banner.webp
image_alt: "Diablo 3D project identity banner; an illustration, not a capture of this delivery."
category: Protótipo
order: 24
status: published
---

## Installed through the usual launcher

The **October 9 update, 21:16 Brasília time**, is installed through the same launcher. [Commit 7e4a6c3b](https://github.com/douglasopan/diablo-3d/commit/7e4a6c3b3796a98f371b6a4817ad259c1bf91467) combines the two gameplay views and a fix for one reproduced cause of the Cathedral returning to the 2D renderer. Selected models, simulation, collision, saves and settings were preserved.

**K and the menu** offer third and first person. The mouse wheel moves closer into first person and back out; **Home restores third person within the session**, while **F4 switches native and 3D presentation**. Local Tristram architecture containing or obstructing the camera eye is temporarily hidden for the local player and reappears when it clears. The shared 1.7-unit height is provisional: calibration by class, trees, props and the Cathedral remain outside this initial hiding behavior.

## The corrected Cathedral cause

The transparency budget charged a full screen for each layer, even though the GPU already copied regions. Admission now adds up the boxes actually emitted, retaining the limits of **2,048 layers and 512 MiB**.

Across eight equivalent cases on a Radeon RX 570, the previous version published six GPU frames and refused two; the fix published all eight. The largest case reached **408 layers and 19,556,601 bytes**, without CPU rasterization or WARP. Color, depth and picking were preserved in the common outputs compared. This technical test uses a partially initialized scene and effective 1× AA; it is not a gameplay benchmark or proof that every cause of 2D fallback is resolved.

The pilot remains limited to nine nearby regions on the first normal floor. Physical continuity review, opaque-frame updates, wall composition and final artwork remain pending. Other floors and quest levels retain native presentation.

## October 10 update: grates and item names

[Commit 29097acd](https://github.com/douglasopan/diablo-3d/commit/29097acdc9c30a9662006f469ab11c38a7dc5ae6) was installed on **October 10, 00:50:47 Brasília time**, through the same **Iniciar-Tristram.cmd**. Selected models, settings and saves were preserved. This is a local installation; distribution of a public build remains in preparation.

Native item names and selection now appear in the 3D world. The **Item highlighting** action, left Alt by default, can be remapped. It and the **Show Item Labels** preference retain the game’s behavior. With a free cursor, selection through text uses native commands. In first person with the mouse captured, text does not receive cursor selection; the reticle follows its own path. A visible label does not allow collecting an item at a distance or through a wall.

Known RAW35/36 grates use their own columns and the native combination of opaque writing and transparency. The fix removes false visual walls at L1LIGHT supports, preserving objects, collision and commands. Pillars and arches receive masonry material; native lighting is prepared in a cache. Unlit grates may legitimately remain black. Heights, repeated bands, donor materials, doors and general artwork remain approximations.

The item-label gate passed **253 checks across 14 frames**, including three on the GPU. Walls and lighting passed across **12 frames**, six on the RX 570. First-person verification uses three CPU frames and simulated SDL services, without establishing physical input. The strict operator suite remains **UNSUPPORTED** because palette index 255 is absent from the four own columns examined; the known-domain gate passed. The separate synthetic unit covers all 256 values without fabricating that case in the actual scene. These results do not complete gameplay, in-game pickup, final artwork or sustained FPS.

## Next steps and video context

The [video devlog](/devlog/devlog-em-video/) records an earlier stage. It does not demonstrate this new installation. This publication adds no gameplay capture and makes no sustained-FPS claim.

The priority remains completing and stabilizing **all of Diablo 1 in 3D**. The [roadmap](/roadmap/) places future multiplayer, dependent on a persistent world and resource renewal, before surface expansion. The current build remains offline.
