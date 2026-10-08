---
title: "Project credits and less work per frame"
date: 2026-10-08
description: "The menu brings Diablo 3D support and credits together, while props and trees reuse geometry on the GPU, preserving models, textures and gameplay."
slug: creditos-e-geometria-gpu
image: /assets/captures/project-credits-main-pt.webp
image_alt: "Main menu in Portuguese with Project Support and Credits; native interface produced in an offscreen diagnostic without starting a game."
category: Ferramentas
order: 16
status: published
---

Development moved forward in two parts of the experience: recognizing who builds Diablo 3D and reducing repeated work when drawing Tristram. The menu now organizes support and credits in one place. In the engine, props and trees reuse their geometry on the graphics card in perspective camera modes.

Both changes were published in [commit fc9642982](https://github.com/douglasopan/diablo-3d/commit/fc9642982da2b74b37af366f74c75e966aad4578) and installed in the usual launcher on **October 8, at 15:01, Brasília time**. Models and textures were preserved. The images in this record show the native interface in an offscreen diagnostic: they are neither gameplay captures nor a new full artistic approval.

## One place for support and credits

The main menu brings the previous entries together under **Project Support and Credits**. The submenu keeps **Support** and **Show Credits**, adds **Diablo 3D Credits** and offers a way back. The original authors' content and credits remain preserved.

<figure><a href="/assets/captures/project-credits-submenu-pt.webp" data-lightbox="true"><img src="/assets/captures/project-credits-submenu-pt.webp" alt="Submenu in Portuguese with Support, Show Credits, Diablo 3D Credits and Back, in a native offscreen diagnostic." width="1920" height="1080" loading="lazy" decoding="async"></a><figcaption>Native submenu in Portuguese, produced by the offscreen diagnostic. The complete capture was published without cropping or retouching; it does not represent a game in progress.</figcaption></figure>

The new screen identifies **Authorship and direction: Douglas Pan**, restates the goal of rebuilding **all of Diablo 1 in 3D**, with Tristram as the first stage, and invites collaboration through art, code, testing and suggestions. GitHub, the website and Discord have selectable links.

<figure><a href="/assets/captures/project-credits-author-pt.webp" data-lightbox="true"><img src="/assets/captures/project-credits-author-pt.webp" alt="Diablo 3D Credits in Portuguese, with Douglas Pan, the goal of rebuilding the whole game and community links; native offscreen diagnostic." width="1920" height="1080" loading="lazy" decoding="async"></a><figcaption>Project credits in Portuguese. The native interface recognizes Douglas Pan and preserves the authorship, licenses and credits inherited from Diablo and DevilutionX.</figcaption></figure>

The flow works in Brazilian Portuguese and English. Returning restores selection to the same item; navigating between these screens does not start another music track. The long title adjusts to the available width, and the Godot menu scene follows the main menu's five actions.

<figure><a href="/assets/captures/project-credits-author-en.webp" data-lightbox="true"><img src="/assets/captures/project-credits-author-en.webp" alt="Diablo 3D Credits in English, with authorship and direction by Douglas Pan and community links; native offscreen diagnostic." width="1920" height="1080" loading="lazy" decoding="async"></a><figcaption>The same screen in English, also produced in the native offscreen diagnostic. Both languages are part of the installed delivery.</figcaption></figure>

**1,193 native checks** passed, with **26 captures** in both languages at three resolutions, as well as **103 Godot checks**. The cases include focus, keyboard input, clicking, reduced width, simulated links and browser-opening failure; they did not open real addresses. Physical gamepad input and audio playback were not tested in this round. The [editor guide](https://github.com/douglasopan/diablo-3d/blob/fc9642982da2b74b37af366f74c75e966aad4578/docs/GODOT-EDITOR.md) records the contract and limitations.

## Geometry stays on the graphics card

Even with rasterization on the GPU, repeated work remained: preparing and sending the volumes' faces every frame. The new path stores the geometry of props and trees in **immutable indexed buffers that remain on the GPU**. The card reuses these data while the camera changes their projection.

The geometry cache has an independent **256 MiB** budget. In the measured scenes, warm frames did not need to refresh the cached data: zero uploads of resident geometry, texels or LUT data. This does not mean zero uploads for the entire scene: objects that remain on the projected path still have a cost every frame.

This optimization does not reduce models or textures, or change lighting, collision or saves. It also does not install LOD, new materials or denser masters. Its current scope is **props and trees in perspective**. Imported buildings, individual scenery objects and actors continue to use the previous projected path.

## Less time to draw the world

The final isolated comparison used **1920×1080, a Radeon RX 570, antialiasing off and 1× sampling**. Each result is the median of **four alternating AB/BA pairs**, comparing the projected GPU path with the new resident path under the same conditions.

| Camera | Projected GPU path per frame | GPU with resident volumes |
| --- | ---: | ---: |
| Third person | 229.183 ms | 77.821 ms |
| First person | 214.149 ms | 68.647 ms |

These times measure **only drawing the world**, including reading data back from the GPU, and exclude simulation, interface and SDL presentation. **They are not sustained gameplay FPS and do not demonstrate that a 60 FPS target has been met.** They compare two GPU paths in this round; they do not replace the [historical CPU-versus-GPU measurement](/devlog/renderizacao-gpu-tristram/).

Readback remains synchronous and has its own cost. Despite the overall gain, the median of this cost in first person increased from **9.057 to 15.607 ms**. Preparing buildings also remains among the bottlenecks to investigate.

## Preserving image, selection and state

In the final Full HD round, colors and central identifiers matched exactly in both cameras, and depth remained within the **0.002** tolerance. The complete shadow map, native state and simulation's random number generator were preserved.

An earlier round checked four camera modes at **960×540**, with antialiasing off and on. One edge pixel in third person without antialiasing was classified by comparing the **same pixel** in a projected reference with a subpixel offset limited to **1/256 per axis**. Color, identity and depth had to match together; the other cases had exact colors and central identifiers.

Resident isometric rendering was left out of the integration after a clipping discrepancy that has not yet been classified. It retains the validated projected path, without widening the tolerance to enable the new path. The final synthetic suite passed **1,768 checks**. The [renderer documentation](https://github.com/douglasopan/diablo-3d/blob/fc9642982da2b74b37af366f74c75e966aad4578/docs/GPU-RENDERER.md) details these tests.

## Installation and next steps

Installation took place with no game open. The **40 files in the usual profile** kept their hashes, sizes and timestamps; subsequent preparation changed only its expected receipt. Full artistic review remains with the author.

The next performance step is to measure actual gameplay and investigate the remaining costs of architecture, scenery and GPU readback. Extending the resident path requires demonstrating the same preservation for each family before enabling it. The [execution guide for this revision](https://github.com/douglasopan/diablo-3d/blob/fc9642982da2b74b37af366f74c75e966aad4578/docs/PROJECT-EXECUTION.md) maintains the queue and dependencies.

The goal remains every level of Diablo 1. Tristram is the first validation environment; the other levels still use the original renderer. Anyone who wants to participate can learn about [ways to collaborate](/participar/) and [support development](/apoiar/).
