---
title: "Diablo 1 First Person: a new Cathedral preview"
date: 2026-10-12
description: "Diablo3D has a local preview with an animated sarcophagus, first- and third-person cameras, and HUD updates. Download and testing are available on Panvia."
slug: primeira-pessoa-previa-nativa-catedral
image: /assets/banner.webp
image_alt: "Diablo3D identity banner; an illustration, not a capture of this preview."
category: Protótipo
order: 25
status: published
---

## A local preview with an identified version

On **October 12, 2026**, we delivered local preview **R3 / 4fe0f467cd56**. Diablo3D aims to reconstruct **all of Diablo 1 in 3D**, with first and third person. Current validation covers Tristram and the first normal Cathedral floor, not the entire campaign.

It follows native preview **d33d3aa9f3fc** and logo/button revision **42883d5f7f19**. Heroes and NPCs retain native sprites. This article documents the local R3 preview; check Panvia for the currently available download.

## The sarcophagus now opens in the game

The model supplied by Douglas has **a 1,362-triangle body and a 461-triangle lid**, with no mesh reduction. It occupies two cells using uniform scale.

The lid slides following native states and **stays in place once open**. Interaction, contents and collision remain native. Textures retain their resolution; the Cathedral applies its palette and lighting, without reproducing all of the GLB's PBR materials.

## Cameras and HUD

In third person, walls obstructing the camera are hidden visually while preserving hero collision. First person remains available. Validation covered 16 Cathedral angles; it does not approve every possible room or position.

Inventory and map windows can be dragged, and retain their positions when reopened. Inventory contents can also be enlarged, with pointer coordinates adjusted to match. This does not mean every window supports unrestricted resizing and stacking.

The project's logo keeps its animation and smooth transparency over the standard background. The level-up button and text open the character panel without spending points automatically. **Native item names** remain visible in 3D according to lighting and line of sight; this continues the earlier feature rather than introducing a new loot system.

## Controls in this preview

- **WASD or arrow keys:** camera-relative movement.
- **Mouse and K:** look and switch camera; **Home** restores third person.
- **I:** inventory; **Tab:** map; **Q:** quests; **B:** spellbook.
- **E:** dungeon interaction. Picking up a nearby item was verified; doors and triggers still need specific review.
- **Left mouse button:** stationary attack; holding follows native cadence and releasing stops it. **Right mouse button:** selected spell, with native repetition and consumption.
- **F4:** native or 3D presentation; **Esc:** menu.

These bindings belong to the version identified above. The alternative M/Q/J scheme is not its default yet.

## What the tests establish

**231 files** were verified and **111,564 bounded checks passed**, using a synthetic profile. Tests include NEW, save/load, reentry, GPU, UI and input within Tristram/Cathedral 1. The sarcophagus was drawn closed, partially open and open, with the open state repeated to check that the lid stays in place.

This is not a complete session with physical mouse input, visual approval of all artwork or sustained gameplay FPS. The earlier broad test found a defect returning from floor 2 to floor 1; **later floors remain unqualified**. New ceiling and arch requirements also remain pending.

## Artwork, AI and credits

Douglas directs the project. We use **AI in Codex to generate and review code and text**; concepts, assets and experiments also use AI generation, identified where applicable. Review and tests do not mean completion or absence of defects.

The project's own artwork remains in production. H6 was a **partial, private** HD experiment; the DamnationX pack is excluded from the public package. Original data and sprites remain separate, and the dungeon generator is a **study**, not a delivered feature.

We retain credits for [DevilutionX](https://github.com/diasurgical/devilutionX), Blizzard and the original authors. Material adaptation references the independent [Diablo First Person mod by plopsDOTio](https://forge.plops.io/t/diablo-first-person-devilutionx-mod-for-windows); its method and modifications are [documented in the repository](https://github.com/douglasopan/diablo-3d/blob/main/docs/DUNGEON-REFERENCE-ADAPTATION.md). These are distinct projects. Inherited licensing and notices continue to apply.

## Download and testing on Panvia

[**Download and testing on Panvia**](https://panvia.online/jogos/thepan-art/diablo-3d/).

The page tracks the launch; an available file is not confirmed yet. The public build will list its version, requirements and known issues. When testing, report the version, reproduction steps and a screenshot of the defect.
