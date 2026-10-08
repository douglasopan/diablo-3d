---
title: "Reconstructing complete objects in Tristram"
date: 2026-10-07
description: "V4 grouped buildings, trees and rocks from the native scene and expanded the volume audit, with explicit visual limitations."
slug: objetos-inteiros-tristram
image: /assets/captures/v4-town.webp
image_alt: "Central Tristram in the v4-refined-final-gog run: architecture, a tree, the well and the hero in a screenshot of refined v4."
category: Geometria
order: 4
---

Tristram is drawn from fragments that together form houses, trees and rocks. Turning each fragment into an independent column preserves some map coverage, but can pull the object apart when the camera rotates. V4 began gathering these components into complete volumes, using verified patterns from the native scene.

This retrospective was published on October 7, 2026. The local `v4-final-gog` and `v4-final-shareware` diagnostics record the stage before the new animation and the Meshy and lighting revisions. The result was included in [commit 4cb265c01](https://github.com/douglasopan/diablo-3d/commit/4cb265c012925936645c926e3e59426351b4c6c5).

This article's opening image and rotated view come from the later `v4-refined-final-gog` run, identified as refined v4. They illustrate the state used in subsequent local reviews. The initial audit results below remain tied to the `v4-final` runs and are not automatically attributed to every later screenshot.

## Architecture with a front, back and base

Houses, the tavern, Griswold's workshop, Adria's hut, the well, the Cathedral exterior and the Catacombs entrance have continuous meshes. Exterior, interior and underside surfaces close their volumes. The frontal artwork is applied to the corresponding faces, respecting depth and occlusion; surfaces exposed by rotation receive their own materials.

This preserves a recognizable visual reference without repeating a door or window across every wall. Shapes and materials still need refinement. The original game provides a painted view, and reconstructing the back requires a coherent interpretation of that composition.

The [catalog published next](https://github.com/douglasopan/diablo-3d/blob/80c291fcb3bead7741722d8f78c84a0fc25e1a94/docs/ASSET-CATALOG.md) organizes the snapshot into 14 architectural groups, combined into 12 complete objects and nine reusable families. These numbers describe the renderer's organization; they do not mean that every model has artistic approval.

## Trees and rocks belong to families

The full native grid of 112 by 112 cells revealed 93 trees in six families. Grouping combines scene fragments with special images and includes small trees drawn only from MIN tiles. Trunks, branches and foliage receive thickness and closed volumes.

Rocks are identified by six exact tile patterns. The full run contains 501 groups and 1,607 source cells. Of those groups, 19 are hidden fill inside architecture. They remain available for auditing, but do not need to reappear through a closed wall. Exterior rocks are retained.

A rock made of four fragments is treated as one object rather than four boxes. Earlier counts of 419 rocks belonged to a smaller region; the difference in the total reflects the expanded snapshot. Map instances are not individual modeling requests either: a reusable family can serve several of them.

![Central Tristram rotated 90 degrees in the v4-refined-final-gog run.](/assets/captures/v4-rotated.webp)

*A 90-degree rotation of refined v4. The view shows the relationship between volumes that overlap in the native perspective. Hidden faces remain subject to review.*

## Auditing closure and preserving the game

The tests checked finite triangles, depth, edge orientation, closure, deterministic reconstruction and ground contact. The earlier final run audited 93 trees and 501 rock groups, produced 40 rotated views and checked 12,794 independent rays for the hut and well.

Those rays found no missing faces in the tested fixtures. This is mechanical evidence of coverage within a defined scope. A tree's silhouette, a house's proportions or a rock's depth can still be wrong even when their surfaces are closed.

Solid objects without an identified grouping still use closed relief per fragment. Ruins, fences and composite decorations need identification and review as assemblies. The [recorded v4 status](https://github.com/douglasopan/diablo-3d/blob/4cb265c012925936645c926e3e59426351b4c6c5/docs/TRISTRAM-STATUS.pt-BR.md) keeps this gap explicit. Completing these objects while preserving collision, paths and triggers is part of the priority to finish Tristram before moving on to the Cathedral's first procedural level.
