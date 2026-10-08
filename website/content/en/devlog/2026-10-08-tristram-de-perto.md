---
title: "Tristram up close: new cameras, quality and performance"
date: 2026-10-08
description: "Four cameras, model reviews, GPU fixes and a bilingual community: today's progress and the challenges revealed by the first-person view."
slug: tristram-de-perto
image: /assets/captures/tristram-camera-modes.webp
image_alt: "Technical comparison of Tristram in isometric, free-orbit, third-person and first-person modes, with CPU above and GPU below."
category: Protótipo
order: 13
status: published
---

Looking at Tristram from ground level changes how we assess the project. A house that works at a distance can reveal a distorted wall up close; a texture that seemed sufficient from the original camera starts to show repetition. The new cameras made this possible and helped guide today's work: preserving the models, understanding the cost of drawing them and improving the experience of playing and collaborating.

**The goal remains all of Diablo 1 in 3D, with every level.** Tristram is the first validation environment. The campaign, rules, collision and saves remain in DevilutionX; the other levels still use the original rendering.

## Four ways to view the same town

Isometric, free orbit, third person and first person have been integrated and installed in the local prototype, together with preferences for field of view, sensitivity and the horizon. The previous delivery is in [commit 38a280881](https://github.com/douglasopan/diablo-3d/commit/38a280881938c43e029227e678d28e1457d8b507). The opening image combines technical captures from the same run, with CPU on the top row and GPU below; it is neither a video nor a measurement of smoothness.

The horizon fills the background beyond the town, but remains provisional. Eye height needs calibration, and the camera still has no wall collision. These limits become especially clear when walking close to buildings.

The new remappable shortcut, **K**, now cycles through the four modes in the usual installation. **F4** retains the view toggle, and **Home** continues to open the original renderer's reference view. Returning to that reference helps compare the scene, but does not automatically approve the meshes.

## Why first person became slow

The [first GPU pilot](/devlog/renderizacao-gpu-tristram/) had already brought a noticeable improvement during play. First person in Full HD exposed another problem: the cache could exceed its budget and send rendering back to the CPU.

The investigation found a global lighting table duplicated for every texture. In the controlled case with nine textures, this duplication took up **288 MiB**; sharing the same table reduced that portion to **32 MiB**, preserving the meshes and textures. The fix passed the real scene at 1920×1080 and an 80° field of view, without falling back to the CPU and with color, depth, selection and shadows preserved in the comparison.

This fix was installed in the usual launcher on October 8, together with spatial culling and the menu revision, at [revision ae43f0470](https://github.com/douglasopan/diablo-3d/commit/ae43f0470134af6bb470c68d20fa37647c8a0ec4). It resolves the reproduced cache failure, but does not settle the performance problem: preparing the scene on the CPU remains expensive. An earlier profile from this round found about **173 ms** spent on that preparation, of which approximately **111 ms** went to the scene's smaller elements. These are development measurements, not the game's FPS.

## Doing less work without removing the scenery

Early culling of buildings outside the field of view has also been delivered. In the Full HD scene examined, visits to architectural geometry fell from **82,626 to 54,579**, preserving exactly the same color, depth, selection and shadow results with culling enabled and disabled on the same backend. Off-screen objects that cast shadows onto the visible area remain included. Ground traversal was also reorganized to avoid sorting the whole grid every frame.

The installed bytes passed **16 cases** covering four cameras, CPU/GPU and requested antialiasing at 960×540, plus first person in Full HD. Warm GPU frames uploaded no new textures or lighting tables. These tests establish the finite conditions examined; sustained smoothness still needs work and measurement during play.

In parallel, an experimental backend keeps indexed meshes on the GPU between frames. An isolated comparison of a 32,768-triangle grid passed and, in that specific test, the median time fell from **6.80 to 1.93 ms**. The adapter for the game's scene is not ready yet; this gain cannot be presented as performance already delivered to the player.

The independent cloud review reinforced this direction: also reduce projection, clipping and assembly on the CPU, while retaining geometry on the GPU. It examined the public code at revision `89e767d1` and the contracts between the workstreams, without access to the graphics card or private local files and without adding a competing implementation.

## Preserving the model before reducing detail

Moving the camera closer also made reviewing model conversion more urgent. Adria's master model, with **3,491,844 triangles**, was preserved, while the textured result has **5,134 triangles**. Transferring materials to the dense mesh now allows a visual comparison, but still shows patches and compression. This result is an authoring candidate, without artistic approval or installation in the game.

![Adria's geometric master on the left and reduced textured result on the right, in the Godot review tool.](/assets/captures/adria-master-textured-comparison.webp)

*Before the transfer: the master preserves the volumes, but has no UVs or texture. The reduced result on the right carries the materials, with visible loss of shape. The image is an authoring preview in Godot.*

![Experimental material transfer to the dense geometry on the left, compared with the reduced model on the right.](/assets/captures/adria-material-transfer-review.webp)

*In the experimental transfer, the volumes reappear on the left. Patches, UV compression and material variations still require review. This is neither an approved model nor an in-game capture.*

The new experimental model format preserves indices, normals, texture coordinates and PBR material information, with explicit identity and anchoring. A level-of-detail selector considers size and error as projected on screen; hysteresis prevents continuous switching between versions near a threshold. The proposal is to use identified derivatives when distance allows, preserving the master and quality close to the camera.

The format, selector and Godot review passed their isolated tests. The current runtime does not yet load this format or use the new selector. Visual review needs to determine which derivatives are acceptable before integration.

## Checking every building and its origin

The community inspector now organizes **130 unique GLB files**, their origins and their relationship to the **12 bindings** in the applied package, alongside the eastern hut. It helps compare the source file with what the game actually assembles, without confusing the broader catalog with the installed selection. The [external Godot editor](/devlog/editor-godot-arquitetura/) continues to support this work; the game remains in DevilutionX.

The Cathedral received an important local correction: its derivative now uses uniform scaling and anchors the threshold at the native entrance. Its 16,854 triangles, texture coordinates and atlas were preserved; the original GLB remained intact. This removes the flattening caused by different scales on each axis. The proportion between the tower and nave still does not exactly match the reference, so the correction does not represent full acceptance of the building.

Orientation, contact with the ground, openings and interior lighting remain separate issues in the house reviews. A model selected for comparison does not automatically gain approved status.

## Ground designed for a closer look too

The terrain's grid-like appearance in first person opened a new investigation. Two approaches are being compared: tiles with higher-definition materials, and a continuous visual surface divided into chunks, with textures anchored in world space and transitions between materials.

Six candidate materials were generated for review, guided by the dark grass, brown earth with a purplish tone and cool-toned banks in the references. The standalone masters do not yet establish how repetition, seams or appearance will hold up in the game. Dry grass, mud and bank substrate are also interpretations, without confirmation that they correspond to distinct families of the original terrain.

The native grid remains responsible for paths, collision and selection. Water, unclassified banks and unknown tiles remain preserved in the study. None of these materials was automatically applied to the usual installation.

The pilot produced **30 close and isometric images** and passed **13 checks**, preserving masks, opacity and regions without replacements. The comparison recommends visual chunks with world-anchored textures, but cutouts remain at native transitions. This is a study outside the runtime, which still needs a small in-game test before any application.

## Menus, HUD and music are part of the experience

The current HUD received a functional composition fix using the native artwork, retaining life, mana, the belt, the readied spell and existing controls, with a layout that can also be reviewed in Godot. **The new visual HUD requested by the author is not yet complete.** The latest feedback reopened the finishing work on the main screen, focusing on visual quality, readability and adaptation to the resolution. Tests of the previous composition do not establish that this new interface is complete. The game's functions will be preserved as this work continues.

The soundtrack already offers **Vanilla**, **Rock** or **Custom**, with choices for each environment. Tristram offers three local files and makes a new random choice at the end of each track when random mode is selected. Environments without replacements retain the original music.

The [music library](/musica/) contains five custom menu and Tristram files to **listen to and download for free**, with **Douglas Pan** credited on the page and in the MP3s' internal metadata. Tristram's third option appears to be another export of the first; the available files are not counted as distinct compositions. The original game music is not distributed in this library.

The latest menu revision is already installed. It removes options from the in-game menu that only work in the main menu, increases the number of rows according to the available space and uses larger native fonts. Previous, Next and Back now share a footer. The selection is preserved when the window is resized. The settings suite passed **59,452 checks**, accompanied by native screenshots at two resolutions; visual assessment during play remains with the author.

## A community in Portuguese and English

Discord received an English area with rooms for conversation, help, model review, asset contributions, bugs and performance, as well as ideas and modding. The Portuguese rooms, existing permissions and voice lobby were preserved. There are devlog channels for both languages, and the site continues to publish complete translations of the entries.

This structure helps anyone who wants to collaborate find a concrete task: compare proportions and materials, review a model from several angles, report a camera problem or test performance on their own computer. The review keeps candidate, partial approval and full acceptance as separate states.

Anyone who would like email updates can [leave their contact voluntarily](/novidades/), choosing Portuguese or English. Responses are private, and an unsubscribe request form is available. Signup is not required to access the site or download music.

The project also needs support to continue. Anyone who can help with development, art, testing, funding or generation credits can learn about ways to take part on [Participate](/participar/) and [Support](/apoiar/), or join the [Diablo 3D Discord](https://discord.gg/4YxQ7s69S). Meshy and other 3D generators, along with credits for ChatGPT and Claude, are among the needs identified by the project lead.

## What comes after this round

The cameras and review tools make defects easier to see; the next delivery needs to correct those defects and reduce the cost of drawing the scene without sacrificing the models. The cache fix, spatial culling and menus are installed. Persistent GPU meshes, LOD, the new HUD and materials remain in development.

Tristram still requires a complete visual review. After that milestone come the Cathedral's first procedural floor and the other environments, characters, monsters, objects and effects of Diablo 1. The new perspective expands what we can explore while keeping the commitment to the whole game.

The public sources for this round are pinned to the integrated revision: [GPU](https://github.com/douglasopan/diablo-3d/blob/ae43f0470134af6bb470c68d20fa37647c8a0ec4/docs/GPU-RENDERER.md), [cameras](https://github.com/douglasopan/diablo-3d/blob/ae43f0470134af6bb470c68d20fa37647c8a0ec4/docs/TRISTRAM-HORIZON-CAMERAS.md), [model inspector](https://github.com/douglasopan/diablo-3d/blob/ae43f0470134af6bb470c68d20fa37647c8a0ec4/docs/MODEL-INSPECTOR.md), [settings](https://github.com/douglasopan/diablo-3d/blob/ae43f0470134af6bb470c68d20fa37647c8a0ec4/docs/INGAME-SETTINGS.md) and [community](https://github.com/douglasopan/diablo-3d/blob/ae43f0470134af6bb470c68d20fa37647c8a0ec4/docs/DISCORD-COMMUNITY.md).
