---
title: "One atmosphere across every screen: the R2 interface study"
date: 2026-10-08
description: "Sixty new concepts, three interface components and editable prototypes to give Diablo 3D's screens visual continuity, with references and limitations identified."
slug: interfaces-diablo-r2
image: /assets/studies/hud-r2/previews/p03-combined.webp
image_alt: "Candidate character and inventory concept for the R2 study; illustration under review, not a game capture."
category: Protótipo
order: 15
status: published
---

Opening the inventory, talking to a resident or adjusting the audio is also part of Diablo's atmosphere. The R2 study extends the project's visual direction to these situations, with proposals organized by screen, available references and three editable examples in Godot. The images presented here are candidate concepts; the new panels have not yet been integrated into the game.

## One direction for the whole game

The goal remains to rebuild **all of Diablo 1 in 3D**. Tristram is the first validation stage, while the interface study considers the screens and states that accompany the wider experience: entry screens, characters, equipment, spells, quests, dialogue, trading and settings.

The direction aims to retain dark stone, aged iron, ivory text and amber highlights. Frames and ornaments need to share space with readable names, values and descriptions. The background should support this atmosphere consistently, without competing for attention with the information the player is looking for.

The main menu reuses the earlier direction. The **main HUD already installed and the credits remain preserved**; this batch addresses the other interfaces. The [study documentation](https://github.com/douglasopan/diablo-3d/blob/40fcbae1f5173636545f85a556e956a3e3a6f988/docs/hud-study/r2/README.md) explains the organization and next steps.

## 65 archetypes, 223 states

The catalog brings together **65 archetypes and 223 tracked states**. An archetype is a shared structure: an options list can have different content and conditions without needing an independent design for every combination. These numbers do not represent 223 distinct menus.

The proposals cover the 63 archetypes that need artwork. **60 new compositions** were produced: 18 for entry and navigation, 12 for settings, 21 for panels and nine for trading. Some sheets present multiple variants. With three components without text and the reused menu, the catalog contains 64 visual assets.

The groups include hero and session selection, audio and graphics adjustments, keyboard and controller bindings, character and inventory, stash and gold, spellbook, maps, quests, conversations, shops and notices. Each proposal is linked to the actions and conditions recorded in the [catalog](https://github.com/douglasopan/diablo-3d/blob/40fcbae1f5173636545f85a556e956a3e3a6f988/docs/hud-study/r2/catalog.json).

The study includes conditional states for Hellfire, networking, shareware, controllers, touch and development tools. Their presence in the catalog does not mean they are all available in the current build.

## Concepts and references play different roles

An illustration helps assess composition, materials and hierarchy. A native reference allows us to check what the interface actually needs to show and do. The gallery identifies both roles so that a generated proposal is not confused with the original.

There are **14 native image references**: one historical window capture and 13 technical compositions produced outside the game window. Some already show backgrounds modified by the project; they are not all unmodified reproductions of the original Diablo. A reference may also cover only some variants of an archetype.

The other **51 archetypes still need native captures**. In these cases, the comparison identifies the gap and points to the existing functions in the code. A complete visual comparison remains pending.

For publication, 60 concepts and 14 references were converted to lossless WebP. The [receipt for the 74 conversions](https://github.com/douglasopan/diablo-3d/blob/40fcbae1f5173636545f85a556e956a3e3a6f988/docs/hud-study/r2/publication-media.json) records identical RGBA pixels and dimensions. There was no cropping, resizing or retouching; the original masters remained intact.

## From drawings to editable controls

Three examples moved from illustrated compositions to separate Godot scenes: **settings, inventory and trading**. Their frames use the three kits without text; names, values, buttons and cells are editable elements. This lets the artwork guide the presentation while the content remains separate.

The inventory preserves **seven equipment slots and a 10×4-cell backpack**. The trading example uses a **10×9-cell shop**, in addition to the backpack. Settings presents 18 rows and a horizontal footer. The geometric items and displayed attributes are local examples.

The [prototype receipt](https://github.com/douglasopan/diablo-3d/blob/40fcbae1f5173636545f85a556e956a3e3a6f988/docs/hud-study/r2/Godot-HANDOFF.json) records **819 passed checks** of structure, geometry and local actions. These tests did not produce Godot renders or captures, nor did they validate physical input or the final appearance. Buying only opens a demonstration confirmation; confirming or canceling closes the preview, without a transaction.

Saving these scenes does not change gameplay, settings or saves. Future integration still needs to connect each control to the native behavior.

## What still needs correction

Two sheets make clear why an image cannot determine interface rules on its own. **P08** draws approximately a 10×9 stash and a 10×3 backpack; the correct contracts are **10×10 and 10×4**. The **c-05** sheet presents an **11×8** shop, while the Godot prototype already uses the **correct 10×9** grid. These illustrated grids are blocked from direct promotion.

Letters, numbers, items and chat examples also need to become real content. The central ornaments on the frames need review when their proportions change, and readability must be checked at different sizes and in different languages. Structural validation does not replace that visual assessment.

The next step is to review each family, complete the missing references and adjust the components before integrating one family at a time. Concept coverage is organized; artistic approval and complete comparison remain open.

In another part of the project, Griswold and Ogden appear in the two public videos below, in a fast 360° turntable. These are models in development: the presentation is not gameplay and does not demonstrate game integration or artistic approval.

<div data-model-showcase="public"></div>

## Complete R2 study gallery

The gallery brings together the 64 visual assets and 14 available references, with their identifiers and limitations. Look at control placement, text readability and consistency of materials. For the stash and shop proposals, also consider the grid discrepancies described above: they are part of the pending review.

<div data-study-gallery="hud-r2"></div>
