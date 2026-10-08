---
title: "Items and a new HUD: artwork under review for Diablo 3D"
date: 2026-10-08
description: "Nine item concepts linked to the catalog and a new HUD with six original pieces: the visual proposals, completed tests and next steps."
slug: itens-e-novo-hud
image: /assets/concepts/items/r1/potion-of-healing-r1.webp
image_alt: "Candidate multiview concept sheet for the Potion of Healing, created to guide future modeling; this is neither a 3D model nor an installed icon."
category: Protótipo
order: 14
status: published
---

A sword needs to be recognizable in the inventory, in the character's hand and when it falls to the ground. This consistency guides Diablo 3D's new item workstream: organizing the game's identities, developing the concepts and, after review, building the model that will provide each object's different presentations.

The HUD's new visual finish has also progressed, with original artwork and a composition that retains the game's actions. This entry brings together the first nine item sheets and the interface candidate, distinguishing concepts, editor previews and technical test compositions.

**The scope includes every item in the project, following the goal of rebuilding all of Diablo 1 in 3D.** Weapons, shields, armor, helms, jewelry, potions, scrolls, gold, books and quest objects are part of the study. Tristram remains the game's first stage; the item catalog also considers the rest of the campaign and distinguishes Diablo and Hellfire data.

The nine images in this first batch are **candidate concepts under review**. They are generated sheets for studying the objects; they are not yet 3D models, final icons, approved artwork or assets installed in the game. The hidden sides proposed in the views need assessment, as does consistency between the views.

## A catalog to know what we are building

The [item production guide](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/ITEM-ASSET-CATALOG.md) connects the project's public data to design families and variants. The study records:

- **168 base records**, of which 166 have names and two are empty reserved slots.
- **111 unique identities** combined across the Diablo and Hellfire tables. The tables overlap; adding their rows would count several items twice.
- **170 declared item cursors**: 136 main cursors and 34 from Hellfire, associated with base items, uniques and dynamic appearances.
- **228 proposed design, family or variant units**, to organize production and asset sharing.

These 228 entries **do not require 228 independent meshes**. Scroll spells can share the physical object; books can use cover variants; potions can share the bottle when this preserves their identity. Affixes, charges and randomly generated attributes do not automatically multiply the models either.

The numbers describe the sources in this revision, including special records and availability conditions. They do not mean that every item appears in any given playthrough, or that their concepts or models are already complete. The [JSON catalog](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/items-study/catalog.json), [CSV production list](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/items-study/concept-slots.csv) and [coverage receipt](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/items-study/coverage.json) make it possible to check the relationships.

Local references have been organized to guide the first batch, without publishing licensed extractions. The nine artworks are linked to nine catalog units, and the local gallery and file integrity have been checked. The other **219 production units** remain pending; this queue also retains unresolved reference cases, such as Lightforge. It does not represent 219 independent models or a completed collection.

## The same object in the inventory, on the ground and equipped

In the native presentation, several pieces of equipment share designs or animation groups. The character's weapon selector works with broad categories, such as sword, axe, bow, mace and staff; it does not select an individual appearance for every weapon in the inventory. Different items can also share the same cursor. This structure helps explain why the reconstruction needs to take care of these relationships as well as produce attractive images.

The proposed production sequence is:

1. Identify the item and its reference, preserving differences between game profiles.
2. Create and review the concept, including silhouette, materials, scale and consistent views.
3. Produce a complete 3D master and review it from every angle.
4. Derive the ground presentation, equipped appearance where applicable and rendered inventory icon from that same master.
5. Integrate and check the set during play, preserving native slots, rules, animations and interaction.

The concept starts this sequence. A thumbnail drawn on the sheet is not yet the game's final icon, and a convincing image does not prove that the object has been modeled or integrated. The accepted master will need to preserve the same identity across all these presentations.

## First batch: potions, gold and a scroll

The first four sheets study consumables and familiar objects. Healing and Mana propose a consistent bottle design while retaining the difference between their contents. Gold begins with one presentation of the family, which still needs to cover the expected quantities. The scroll studies the physical object; spell variants will be addressed separately.

Open each image to examine the complete sheet. The captions are also shown in the enlarged view.

<div class="gallery-grid">
<figure class="gallery-item"><a href="/assets/concepts/items/r1/potion-of-healing-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/potion-of-healing-r1.webp" alt="Potion of Healing concept sheet, with several proposed views of the same bottle."></a><figcaption><strong>Potion of Healing · r1</strong> Candidate concept under review. Proposed views for future modeling; no generated 3D model, final icon, artistic approval or installation.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/potion-of-mana-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/potion-of-mana-r1.webp" alt="Potion of Mana concept sheet, studying the bottle's consistency with the healing potion."></a><figcaption><strong>Potion of Mana · r1</strong> Candidate concept under review. The proposal shares Healing's bottle design; no generated 3D model, final icon, artistic approval or installation.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/gold-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/gold-r1.webp" alt="Gold concept sheet, as an initial study of the coin family."></a><figcaption><strong>Gold · r1</strong> Candidate concept under review. Initial family study, without completing every gold quantity; no generated 3D model, final icon, artistic approval or installation.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/scroll-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/scroll-r1.webp" alt="Scroll concept sheet, with proposed views of the physical rolled-up scroll."></a><figcaption><strong>Scroll · r1</strong> Candidate concept under review. Generic physical object, with spell variants still undefined; no generated 3D model, final icon, artistic approval or installation.</figcaption></figure>
</div>

## First batch: five blades

Short Sword, Dagger, Falchion, Scimitar and Broad Sword begin the weapon study. Each has its own identity in the catalog. The review needs to compare views, silhouettes and proportions before turning these proposals into models or deciding what can be shared.

<div class="gallery-grid">
<figure class="gallery-item"><a href="/assets/concepts/items/r1/short-sword-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/short-sword-r1.webp" alt="Candidate Short Sword concept sheet for studying the future model."></a><figcaption><strong>Short Sword · r1</strong> Candidate concept under review; no generated 3D model, final icon, artistic approval or installation.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/dagger-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/dagger-r1.webp" alt="Candidate Dagger concept sheet for studying the future model."></a><figcaption><strong>Dagger · r1</strong> Candidate concept under review; no generated 3D model, final icon, artistic approval or installation.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/falchion-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/falchion-r1.webp" alt="Candidate Falchion concept sheet for studying the future model."></a><figcaption><strong>Falchion · r1</strong> Candidate concept under review; no generated 3D model, final icon, artistic approval or installation.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/scimitar-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/scimitar-r1.webp" alt="Candidate Scimitar concept sheet for studying the future model."></a><figcaption><strong>Scimitar · r1</strong> Candidate concept under review; no generated 3D model, final icon, artistic approval or installation.</figcaption></figure>
<figure class="gallery-item"><a href="/assets/concepts/items/r1/broad-sword-r1.webp" data-lightbox="true"><img src="/assets/concepts/items/r1/broad-sword-r1.webp" alt="Candidate Broad Sword concept sheet for studying the future model."></a><figcaption><strong>Broad Sword · r1</strong> Candidate concept under review; no generated 3D model, final icon, artistic approval or installation.</figcaption></figure>
</div>

## A new HUD with original artwork and preserved controls

The HD finish began with **six original pieces**: the central frame, an unlabeled button, a dark inset, the sculpted orb support and the red and blue liquids. They form a candidate interface with size and positioning adapted to the resolution. The layout can be edited in the external Godot tool; exporting its arrangement is an explicit step, separate from saving the scene.

<figure><a href="/assets/concepts/hud/hud-native-16x9-v1.webp" data-lightbox="true"><img src="/assets/concepts/hud/hud-native-16x9-v1.webp" alt="HUD concept sheet 07, with orbs, belt, information, prepared spell and the six utility buttons."></a><figcaption>HUD visual direction: a generated concept under review. This sheet is not a game capture and does not prove that every detail has been implemented.</figcaption></figure>

The contract preserves **13 interaction regions** and their original commands. Life and mana, **eight belt slots**, the prepared spell, information and warnings still come from the game, as do multiplayer states. The six utility buttons retain their existing actions; the new artwork adds no functions. Labels use the native runtime font, while the Godot preview uses a system font: the editor study does not establish a final typographic finish during play.

## What the HUD tests show

The final revision was compiled and exercised in **13 technical compositions**, including empty, half-full and full orbs, a pressed button, five information lines, multiplayer states and compact, Full HD and ultrawide resolutions. The run recorded **278 checks**, with interaction regions and artwork files preserved.

<figure><a href="/assets/captures/hud-hd-offscreen-fullhd.webp" data-lightbox="true"><img src="/assets/captures/hud-hd-offscreen-fullhd.webp" alt="Offscreen technical composition at 1920 by 1080, with the reference world and candidate HD HUD showing half-full life and mana."></a><figcaption>Offscreen technical composition of the installed revision, using the real code and assets. This is not a gameplay capture; final artistic assessment remains pending.</figcaption></figure>

The optimization revision limits layer preparation and uploads to the regions actually used by the interface, including experience. The final comparison preserved the pixels in all 15 test files against the first run, including the GPU case with a compact logical interface presented at Full HD. This verifies preservation of those results; it is not a sustained FPS measurement.

**The new HUD version was installed in the usual launcher on 8 October**, after validation, with profile files, settings, saves and the launcher preserved. A concept, a Godot preview, a technical composition and an installed build remain different states: installation was confirmed by its receipt, while the image above came from the offscreen test. Final artistic assessment and a check during actual use with the project owner remain pending. The [visual study](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/hud-study/README.md) preserves the proposals. The [image provenance for this entry](https://github.com/douglasopan/diablo-3d/blob/main/website/DEVLOG-MEDIA.md) separately identifies generated artwork and the technical composition.

## Follow, review and expand

The [art manifest](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/items-study/concept-art.json) records the relationship between each concept and production unit, its revision, file and provenance. Generated files are kept in the [public concept folder](https://github.com/douglasopan/diablo-3d/tree/f0102dc1b0c9075fa98906b3f38d82c09418644b/assets/concept-art/items/r1). The full catalog remains much larger than this initial batch: the next families will follow the same identification, review and care with reuse.

Anyone who wants to collaborate can help check proportions, readability, materials and consistency between views. A useful comparison identifies the item and revision, shows the problem and explains the suggested change. See [how to participate](/participar/) or join the [Diablo 3D Discord](https://discord.gg/4YxQ7s69S), with spaces in Portuguese and English.

The item images show only generated concept artwork. Local licensed references and extracted sprites are not distributed; the HUD image retains its technical composition context and label. The dedicated item team continues the catalog and subsequent batches, while HUD integration and GPU and performance work remain with their respective owners. For the nine items in this batch, the next step is concept review, before modeling and integration into the game.
