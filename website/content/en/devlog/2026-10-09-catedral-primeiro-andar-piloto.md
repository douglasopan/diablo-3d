---
title: "Cathedral: a 3D pilot reaches the first floor"
date: 2026-10-09
description: "The installed technical pilot draws nine nearby regions on the Cathedral's standard first floor. Simple geometry, GPU recovery and preserved native data; artwork and review in a play session remain pending."
slug: catedral-primeiro-andar-piloto
image: /assets/captures/cathedral-pilot-gpu-world.png
image_alt: "Offscreen technical frame of the Cathedral pilot, with floors and walls in simple colors and native actors as sprites; this is not a capture of a gameplay session using the installed version."
category: Geometria
order: 21
status: published
---

The reconstruction is beginning to move beyond Tristram into the dungeons. The **Cathedral's standard first floor** has received a technical pilot that assembles floors and walls from the live map of the same game session. It covers **nine regions near the player**; the other floors and quest levels continue to use the original presentation.

The delivery in [commit c5e8cb7f](https://github.com/douglasopan/diablo-3d/commit/c5e8cb7fdcf79ceb6ed368b77bfa2ae55446a91e) was installed on **October 9, 2026, at 15:54, Brasília time**, through the same **Iniciar-Tristram.cmd** launcher. This technical pilot is accessible through the existing hooks for normal entry and toggling with **F4**. Its installation does not amount to validation of entry, movement or combat with physical devices.

## Map geometry, with temporary artwork

The pilot uses the native map and seed to assemble the nearby area. The game remains responsible for simulation, collision and commands; the addition is visual. Floors and walls have geometry and **simple technical colors**, with palette and transparency support on the GPU path. Final textures, artistic composition and complete coverage still need work.

The images below come from the technical gate before installation: partial initialization, a stationary scene and native actors staged as sprites. They illustrate the pilot and recovery reference, **without constituting a gameplay session, artistic approval or a fidelity comparison using the same camera**. The count of nine regions comes from the pilot's contract, not from a count that can be made in this frame.

<figure><a href="/assets/captures/cathedral-pilot-gpu-world.png" data-lightbox="true"><img src="/assets/captures/cathedral-pilot-gpu-world.png" alt="Technical Cathedral pilot in an offscreen GPU frame: a checkerboard floor, flat translucent walls, a door in a temporary color and native actors as sprites." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>Pilot — offscreen technical frame after immediate GPU recovery. Geometric floors and walls, simple colors and actors still represented as sprites; final artwork and gameplay remain pending.</figcaption></figure>

<figure><a href="/assets/captures/cathedral-pilot-native-reference.png" data-lightbox="true"><img src="/assets/captures/cathedral-pilot-native-reference.png" alt="Native reference of the Cathedral test state, with original scenery and staged actors, used to check the complete return to the original renderer." width="640" height="480" loading="lazy" decoding="async"></a><figcaption>Reference — native presentation of the test state, used as a recovery control. Offscreen capture without a HUD or a gameplay session using physical input; this is not an artistic before-and-after comparison.</figcaption></figure>

Both PNGs were published **without conversion, cropping or retouching**, preserving their bytes and original **640×480** resolution. The gate's seven outputs form three distinct groups; these two sources avoid publishing redundant copies. The reference frame does not represent every intermediate failure in the test.

## GPU recovery and preserving the game session

The final gate used a **real Radeon RX 570, without WARP**, with **157 harness checks, 188 contextual checks and 93 allocation checks**. These groups have different scopes; they are not a count of gameplay tests. The gate accepted nine quest values in an inactive context and rejected the same nine in an active context, while also recording 42 rejections of invalid inputs by the consumers. This keeps the pilot outside the quest levels it does not support.

One frame traversed the real GPU world-drawing path. In **four injected failures**, the fixture checked the complete return to the native viewport and immediate GPU recovery, in three warm cases and one cold case. The partial frame is discarded before the image and selection are published. These checks support recovery in the exercised cases; they are not a prolonged performance assessment.

The **290,243-byte** witness remained identical before and after, with the map, SOL, RNG and transparency list preserved. Ogden and the Tristram selections remain in the installation. The **48 original profile files** retained their contents, sizes and timestamps; only the launcher's derived receipt was renewed. The three executables received identical bytes, with backups, without terminating processes or incurring new costs.

The installed executable has **6,525,440 bytes**, SHA-256 `87a441706d378d3583346023e53218c936304897900b6f610ad576e9830e21e5`. The [versioned integration status](https://github.com/douglasopan/diablo-3d/blob/c5e8cb7fdcf79ceb6ed368b77bfa2ae55446a91e/docs/PROJECT-EXECUTION.md) distinguishes this installation from the earlier private prototypes.

## The next step takes place in a play session

The next step is to check entry with physical input, movement, combat and transitions, measure the performance budget and develop the pilot's artwork. The offscreen frames do not demonstrate AI, HUD, save/load during play, sustained FPS or final artistic quality. Partial approval of Ogden's appearance does not approve the Cathedral's artwork; the 3D Warrior remains in its private workstream and is not announced by this delivery.

The goal remains **all of Diablo 1 in 3D**. Tristram remains under review, the standard first floor has gained this technical area, and the other floors, Catacombs, Caves and Hell still require their own implementation. See the [roadmap](/roadmap/) and follow the [earlier tavern comparison](/devlog/taverna-sem-residuos/) to keep each stage tied to its evidence.
