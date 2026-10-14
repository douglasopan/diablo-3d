---
title: "Local3 checkpoint: 3D Warrior, HUD and a preserved build"
date: 2026-10-14
description: "We preserved the current Diablo3D checkpoint, with downloadable binaries and source, TP/FP Warrior in the local setup and HUD R4. Performance and runtime distribution remain in progress."
slug: checkpoint-local3-warrior-hud-gpu
image: /assets/captures/local3-warrior-tp-hud.webp
image_alt: "3D Warrior with sword and shield in Tristram, alongside the character panel and HUD; automated capture of the local3 checkpoint."
category: Protótipo
order: 26
status: published
---

## Preserving this point before moving forward

We preserved **local3, dated October 14, 2026**, before continuing the next fixes. The executable, corresponding source and hashes identify the delivered build. The scene and private resources used in the test setup are also frozen locally; they are not part of the public download.

The goal remains **all of Diablo 1 in 3D**, with first and third person. This version covers Tristram and the technical pilot of the Cathedral's first standard floor. It does not complete the campaign, artwork or performance work.

## The Warrior is now rendered in 3D in the game

In the qualified local setup, the **Warrior wearing light armor** uses a 3D body in third person and eyes, arms and hands from the **same rig and pose** in first person. The camera does not introduce a separate animation clock: native logic still controls the actions.

The Short Sword and Buckler use their existing models. Unequipping an item leaves no residual weapon. Equipment without an attachment can use the corresponding inventory sprite; that alternative on the GPU still needs its own qualification. Classes, armor, actions and levels outside the supported scope retain native presentation. Grips, framing and finish remain subject to review.

![Arms, sword and shield viewed from the Warrior rig in the automated local3 test.](/assets/captures/local3-warrior-fp.webp)

This image comes from the **automated renderer test**, not a recording of gameplay using a physical mouse. It shows the feature in the private setup with sealed runtimes; it does not promise that those resources are included in the public technical ZIP.

## The HUD follows movement and resizing

HUD R4 includes corrections to panel size, content and clickable areas, along with focus, overlap, closing through the X, the map and clock. Tests covered closing and reopening, size limits and composition with the world.

In supported contexts, **Ctrl + left mouse button** moves a module and **Ctrl + Shift + left mouse button** resizes it. **Ctrl + right mouse button** resets the module; adding Shift resets them all. Dragged items and open modals interrupt HUD editing. Transparency affects the background while keeping text and items readable.

## Fixed Tristram, the Cathedral and controls

The local setup loads a **sealed persistent scene** and four cardinal corridors tested in staging. This does not automatically replace the town Douglas is editing in Godot. Saving or exporting in the editor also does not install a new scene into an open session.

**K** switches first and third person; **Home** returns to third person for the session; **F4** switches between 3D and original presentation. Bindings can be remapped and are blocked in appropriate modals. The third-person test followed SDL commands, packets and native logic: click walking and E interaction with a nearby door. It does not cover every campaign interaction.

## What was verified

The final source gates recorded:

| Scope | Result |
| --- | --- |
| CPU Warrior | 2,301 checks and 49 images |
| Third-person commands | 158 checks across five cases |
| HUD | 29,263 checks and 154 images |
| Local diagnostics | 8,272 checks across six cases |
| Radeon RX 570 | 33,393 checks, five readbacks and one real hidden Present |

Tests include Town/Cathedral1, equipment, poses, arms, doors and preservation of depth and picking. They use automated fixtures and captures from the final source. They do not certify physical keyboard and mouse input, multiplayer, the entire campaign or artistic acceptance. The old camera text visible in some captures is historical; the controls above are authoritative.

## Performance remains our next priority

This publication is not presented as a performance fix. A stationary hidden measurement at **1280 × 720, RX 570, AA off and VSync on** recorded means of **214.85 ms in Tristram** and **176.02 ms in the Cathedral**. Initial frames also have a high cost.

These are renderer times, not measured gameplay FPS. CPU scene recording was identified as an important bottleneck; the following fixes are not part of this checkpoint. Earlier process exits still have no proven cause. Preserving this version gives the next result an identified comparison baseline.

## Download: what is public and what remains local

[**Download the local3 technical checkpoint and corresponding source**](https://github.com/douglasopan/diablo-3d/releases/tag/v0.1.0-checkpoint.local3).

The **4,303,106-byte** ZIP preserves the candidate's program and scripts. **It is not a complete installer.** It excludes DIABDAT.MPQ, saves, packs, private maps/models and R17 assets. Its launcher requires those sealed runtimes and compatible assets: **DIABDAT.MPQ alone is insufficient**. Instructions and SHA256 hashes accompany the release.

Users who already have the local setup must create a new profile through explicit preparation, preserving earlier saves and preferences. For conventional public installation, the [older R17](https://github.com/douglasopan/diablo-3d/releases/tag/v0.1.0-early-access.r17) remains available separately. It retains sprite heroes and must not be confused with the new Warrior in the local3 setup.

Checkpoint source: `24080e8900d695f9c88f9d3ad417917499d8103f`. Executable: `1.6.0-dev-Release-24080e8`, SHA256 `0f4d9290c0589cbee3eb6c22791f373d4a1b67dd618b97e3bfb5a86637f9bd6c`. Use the corresponding-source file attached to the release; GitHub's automatic archive follows the website revision, not the private integrated source.

## Publishing this milestone and continuing

We will continue focused performance fixes and camera and interface review on this baseline. Procedural surroundings, an additional central stash, new effects and Panvia integration remain outside this delivery.

Douglas Pan directs the project. Codex is used for code development, text and translations; AI-generated resources are identified and still require review. DevilutionX, Blizzard and contributor licenses, authorship and credits are preserved. Publication is free and non-commercial; original game data is not distributed.
