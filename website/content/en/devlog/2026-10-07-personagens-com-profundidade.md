---
title: "Characters with depth, identity still under review"
date: 2026-10-07
description: "The warrior, townspeople and cows gained volume in v4. Available sources, inferences and test coverage remain explicit."
slug: personagens-com-profundidade
image: /assets/captures/v4-actors.webp
image_alt: "Hero comparison in refined v4: the original backend and forced mesh at rotations of minus five, zero and plus five degrees."
category: Personagens
order: 5
---

Giving a character depth requires preserving their identity, ground contact and position in the game. Head, clothing, equipment and silhouette need to remain recognizable as the camera moves. The number of available views also changes what reconstruction can observe and what it must infer.

This article of October 7, 2026 records the v4 character stage, published in [commit 4cb265c01](https://github.com/douglasopan/diablo-3d/commit/4cb265c012925936645c926e3e59426351b4c6c5). The main fixture uses the warrior. A shared code path for other classes is not equivalent to a review of all their appearances and animations.

The opening comparison comes from `v4-refined-final-gog`, a later run identified as refined v4. It shows the original reference and forced mesh at rotations close to the native angle. This source is distinct from the initial audit described in this record.

## Eight views of one body

The warrior has eight directions in the original animation. Reconstruction combines these observations to form a body with depth and apply the artwork from the corresponding direction to its faces. Cows also provide eight views and follow this path.

The diagnostics use the actual frame and animation loaded in the fixture and check that all eight directions are available. The mesh must have depth, valid triangles, closed components and consistent orientation. Reconstruction must be deterministic: repeating the same input and direction must produce the same result.

The eight images still belong to one character. They do not represent eight models for contributors, nor do they prove that every combination of armor, weapon, action and frame has been checked. That coverage needs an appearance and animation matrix for each class, as explained in the [character catalog](https://github.com/douglasopan/diablo-3d/blob/80c291fcb3bead7741722d8f78c84a0fc25e1a94/docs/ASSET-CATALOG.md).

## When only one view exists

The other townspeople have a single original view. Their bodies receive rounded heads, torsos and limbs, or continuous clothing, fitted to that image's outline. Depth and the back are inferred. The result requires a review of anatomy, materials and identity around the full orbit.

The recorded scene uses 11 inhabitants: eight named townspeople and three cows, sharing nine families. The wounded townsman and conditional Hellfire inhabitants were not in this fixture. The existence of enums or code paths for them does not establish visual validation that was never performed.

In the single-view fallback, cows preserve the relief of their own outline. Giving them a generic body with human anatomy would be a mistake in interpreting the source. Recognizing the character's actual type is part of the same care used to group trees and rocks.

## Feet, shadows and the original position

The body is brought into contact with the ground through a rigid translation that preserves the frontal projection. Tests check the minimum height and texture orientation after this adjustment. The visual position of the foot is not a reason to move the actor in the simulation.

The painted ground shadow is kept separate from body geometry. The mask removes the lower, exterior black region identified as shadow while preserving enclosed and upper dark details. The original pixels remain intact; separation happens during image processing at runtime.

This compatibility shadow is still not a dynamic shadow produced by a scene light. Even after architectural shadows were introduced, characters, trees and rocks still did not participate in that system. The [later prototype status](https://github.com/douglasopan/diablo-3d/blob/f673fc7090f21afc2f09a4fff3bd44a028943315/docs/TRISTRAM-STATUS.pt-BR.md) retains this limitation.

## Selection and occlusion matter too

The warrior was selectable at all ten angles tested in open space. Near the hut, the building can still hide him. Selection, ground contact and occlusion must follow the geometry without changing inventory, walking or dialogue.

Models and characters remain under review. This stage makes that review possible in volume and from multiple angles, with enough evidence to identify what still needs correction.
