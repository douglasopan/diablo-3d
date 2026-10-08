---
title: "A D3D identity and a catalog for collaboration"
date: 2026-10-07
description: "The animation supplied by the author and the Tristram catalog establish the project's identity and organize collaboration around object families."
slug: identidade-catalogo-colaboracao
image: /assets/banner.webp
image_alt: "Official Diablo 3D banner supplied for the project's visual identity."
category: Comunidade
order: 6
---

The project needs to make both its progress and ways to participate visible. On October 7, 2026, two commits expanded that foundation: a catalog to coordinate Tristram objects and the D3D animation supplied by the author for startup, the main menu and pause screen.

[Commit 80c291fcb](https://github.com/douglasopan/diablo-3d/commit/80c291fcb3bead7741722d8f78c84a0fc25e1a94), published at 16:05, added the catalog, registry and structure checks. [Commit 8f031fa3b](https://github.com/douglasopan/diablo-3d/commit/8f031fa3b8ebbe713ddf80a6383ed8a289347232), at 16:06, integrated the animation. These times are in São Paulo. They are publication dates verified in the Git history.

## Supplied artwork finds its place in the game

The received animation contains 240 frames at 30 frames per second, with an eight-second cycle. The importer adapts its dimensions, palette and coverage to the software renderer without redrawing the artwork. Each screen uses its own asset, suited to the available area.

The cycle is calculated over 8,000 milliseconds. This avoids accumulating the error that would result from rounding each 30 fps interval to 33 milliseconds. The pause screen also uses the game's active palette rather than assuming it is the same as the main menu palette.

The supplied files and adaptations have recorded provenance. The [animation document](https://github.com/douglasopan/diablo-3d/blob/8f031fa3b8ebbe713ddf80a6383ed8a289347232/docs/ANIMATED-LOGO.md) distinguishes this independent material from the earlier experiment that reconstructed an alternative from native artwork, kept locally. The full banner identifies the project's pages; the D3D symbol identifies icons and avatars.

Animation and installed-asset diagnostics passed. The recorded run did not carry out a complete manual review of the open interface. The change also does not alter the town's geometry, whose earlier audit remains separate.

## Reserve a family, build an object

The catalog provides stable IDs for buildings, trees, rocks, inhabitants and identification tasks. A tree can have several instances on the map. A house can include components and variants. These cases should be coordinated as families and complete objects, avoiding competing requests for each fragment or camera direction.

The process begins by consulting the [catalog](https://github.com/douglasopan/diablo-3d/blob/80c291fcb3bead7741722d8f78c84a0fc25e1a94/docs/ASSET-CATALOG.md), registry and existing issues. The contributor requests a reservation for an ID and waits for the maintainer's confirmation before modeling. Confirmation defines the scope and responsible person; review records evidence and the approved revision.

Statuses distinguish needed work, prototypes, review and acceptance. In the documented snapshot, **zero manually authored models have been accepted**. An existing procedural representation remains a prototype, and an identified source does not prove artistic fidelity.

## Evidence and provenance accompany each contribution

An object must preserve composition, scale, doors, windows, ground contact and position. Comparison should use forced meshes from the original perspective, followed by small rotations and a complete orbit. Home uses the original backend and cannot approve the reconstruction on its own.

The proposal also states authorship, license and asset provenance. Game files, extractions, saves, derived models that redistribute this artwork and credentials stay outside the repository. Authorized contextual development screenshots are presented in this devlog with their stage identified.

The code retains the [Sustainable Use License](https://github.com/douglasopan/diablo-3d/blob/8f031fa3b8ebbe713ddf80a6383ed8a289347232/LICENSE.md), with free, noncommercial distribution. D3D is an independent fan project with no affiliation with Blizzard. The [official Discord](https://discord.gg/4YxQ7s69S) and [issues](https://github.com/douglasopan/diablo-3d/issues) are the coordination points. The shared priority is to complete Tristram before the Cathedral's first procedural level.
