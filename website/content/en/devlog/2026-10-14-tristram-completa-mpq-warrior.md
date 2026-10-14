---
title: "Complete local3 Tristram, requiring only the original MPQ"
date: 2026-10-14
description: "The new distribution automatically prepares the same selected Tristram scene from DIABDAT.MPQ, with first- and third-person Warrior. GitHub release notes and instructions are bilingual."
slug: tristram-completa-mpq-warrior
image: /assets/captures/local3-warrior-tp-hud.webp
image_alt: "Historical automated local3 capture: 3D Warrior and HUD in the selected Tristram scene; not a new public-installation capture."
category: Protótipo
order: 28
status: published
---

## The same Tristram scene, in the download

FULL3 included the Warrior and execution files but omitted the persistent Tristram scene from the local setup. That did not satisfy the request to publish the current complete checkpoint.

The snapshot contained embedded original Diablo artwork. The old launcher merely copied those private files. The new package includes an internal preparation component that recovers original materials from **the player's DIABDAT.MPQ**, without distributing that artwork.

[Download the corrected build](/download/), extract the entire ZIP and run **Iniciar-Diablo3D.cmd**. Select the original MPQ and wait for first-run preparation. No Python, compiler, external converter or additional download is required.

## What is preserved

Preparation reproduced **all 19 selected scene files byte for byte**, checking the original cardinal R2 hashes. Geometry, positions, UVs, materials, identities and edits are unchanged. The real native loader admitted 14 architecture objects, 25,743 fixed objects, 1,349 textures and four routes, without substituting an architecture fallback.

Warrior Light with Short Sword and Buckler retains first/third person in Town and Cathedral 1. K switches cameras, Home restores third person, and F4 switches 3D/native. HUD R4, the local3 game executable and Speed32 remain the same revision. No performance patch was applied for this publication.

## Verified preparation and English instructions

We tested the extracted ZIP, first preparation, profile reuse, preservation of settings/persistent data, and refusal of a changed scene without overwriting it. The separate profile is `%LOCALAPPDATA%/Diablo3D-local3-Tristram/profile`. MPQ compatibility is checked: original data that cannot reproduce the frozen baseline is refused.

[English instructions are available on GitHub](https://github.com/douglasopan/diablo-3d/blob/main/docs/TRISTRAM-PUBLIC-PACKAGE.en.md), along with bilingual release notes and the bundled readme. Corresponding source and dependencies are available to contributors.

These checks created no window, physical input, gameplay session or save. They establish exact scene recovery and native loading; performance and manual gameplay testing remain pending. The article image is a historical automated local3 capture with the same scene and Warrior revision, not a new capture of this installation. The goal remains the whole Diablo 1 game in3D, still in development.
