---
title: "A lit window and ground without the old shadow"
date: 2026-10-07
description: "The optional Meshy hut profile received a physical interior and yellow light; eight terrain tiles were reviewed to remove painted shadows."
slug: interior-cabana-sombras-terreno
image: /assets/captures/cabin-interior-final.webp
image_alt: "Hut comparison: original reference, model before and model after the lit interior and removal of painted terrain shadows."
category: Luz
order: 9
status: published
---

The eastern hut's window now reveals yellow light coming from a physical room. The nearby terrain was also revised to remove fragments of the old painted shadow, allowing the shadow calculated from geometry to take that role.

The work was published on October 7, 2026, at 19:33, São Paulo time, in [commit 63e5e749](https://github.com/douglasopan/diablo-3d/commit/63e5e749d73c213aecf4b77b400aa04acc5a0598). The interior belongs to the optional Meshy review profile. The final captures compare this state with the previous exterior calibration; the first lighting attempt has its own results, separated below.

## A room behind the opening

The interior has stone walls, a ceiling, a floor of 12 planks over a closed base and a small volumetric lamp. A stone tunnel and wooden dividers follow the window's depth. The interior rear wall closes the duplicated opening present in the generated model.

The opening was measured in the imported model and receives a 20-sided polygon that fits within it. Lighting uses this same boundary, blocking rays at the stone corners. This corrects a leak permitted by a rectangular opening. The lamp emits its own light and illuminates the floor and walls with a warm point source in linear RGB.

The Meshy file, exterior triangles, UVs and source texture remain intact. No new paid generation was needed. The door stays closed, and native collision prevents the player from entering. The [lighting reference](https://github.com/douglasopan/diablo-3d/blob/63e5e749d73c213aecf4b77b400aa04acc5a0598/docs/TRISTRAM-LIGHTING.md) documents this scope.

## The first test and final run

The comparison toggles the point source and its emission in the actual renderer, keeping the camera and scene unchanged. The numbers belong to this fixture:

| Run | Intensity | Changed pixels | Warmed pixels | Changes outside the hut |
| --- | ---: | ---: | ---: | ---: |
| First local test | 2.5 | 117 | 66 | 0 |
| Final diagnostic | 6.0 | 116 | 116 | 0 |

The intensity adjustment was accompanied by correction of the opening's boundaries. The final run also checks the opaque rear wall and the ray that previously passed through the stone corner.

![Window crop from the actual lamp-off versus lamp-on comparison.](/assets/captures/cabin-lamp-final.webp)

*The test shows the local lighting change. Plank details are not readable through the small window at normal scale; this image does not provide a complete view of the room.*

## Removing the right shadow

Cleanup expanded from four to eight audited hut ground tiles. Fixed masks bound the pixels that receive clean terrain from donor tiles. Independent inspection checked all eight tiles and 80 control samples in GOG, shareware and the Meshy profile, without differences outside the masks.

Transparent pixels, opaque black, original coverage, the map and collision are preserved. The dark transition between dirt and grass beside the well was inspected and retained: erasing every dark region would also erase legitimate terrain details.

![Exterior terrain: grass with painted shadow before, and clean terrain with geometric shadow after.](/assets/captures/cabin-floor-final.webp)

*Before, fragments of the painted shadow remained beside the walls. After, the terrain receives clean grass and retains the shadow calculated from geometry. This is the hut's exterior ground; the wooden interior floor is a different component.*

## Checking rotation and preserving the reference

The final runs passed lighting and scene tests with GOG data, shareware and the separate Meshy profile. Each produced 50 views and checked 24 Home views matching the original backend. Home validates that return, while the images below use active geometry.

![Before and after comparison at rotations of minus five, zero and plus five degrees.](/assets/captures/cabin-orbits-final.webp)

![Hut comparison before and after from four main angles.](/assets/captures/cabin-angles-final.webp)

The imported window still differs from the reference in position and material. Topology, textures and other interiors need review; the tests do not approve the model's complete fidelity. Trees, rocks and characters still do not cast shadows through the architecture's shadow map. Day/night, horizon and fog remain future work in the [roadmap](https://github.com/douglasopan/diablo-3d/blob/63e5e749d73c213aecf4b77b400aa04acc5a0598/docs/ROADMAP.md), with Tristram as the priority.

## Next revision: fire sources

The later direction requested fire sources with multiple emitters and subtle flicker, as well as a physical, visible opening in the rear window. The two-candle revision was published in `cdeaaab0d` and is documented in [Two candles, two windows and the same room](/devlog/velas-janelas-cabana/).

The captures and tests above continue to document the historical emitter in `63e5e749` and the closed rear wall at that stage. Evidence of the new source and open window belongs to the next record.
