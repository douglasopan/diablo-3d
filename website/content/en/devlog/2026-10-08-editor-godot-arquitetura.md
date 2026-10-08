---
title: "Godot joins the review of Tristram's architecture"
date: 2026-10-08
description: "An external editor for inspecting architecture, saving the scene and exporting static models to DevilutionX, with validation, a review profile and explicit limits."
slug: editor-godot-arquitetura
image: /assets/captures/cabin-fire-angles.webp
image_alt: "Historical hut comparison from four angles, published before this delivery. This image does not show the Godot editor or the door-window correction."
category: Ferramentas
order: 12
status: published
---

The project now has an external Godot editor for inspecting and working on Tristram's static architecture. The scene can be saved and reopened, and compatible models can follow an explicit export path to the game's C++ loader. The delivery is in [commit e17b77f03](https://github.com/douglasopan/diablo-3d/commit/e17b77f0370df87732e227b7fa479d9b56c44280).

The game remains in DevilutionX, with its simulation, collision and saves. The editor assists with object review; porting all of Diablo 1 to Godot remains a subject for future evaluation. The image above is historical and serves only as context for the preserved hut. No new local screenshots, textures or models have been added to the site.

## The scene the game actually assembles

The snapshot brings the triangles actually assembled by the game into the editor, including the cutouts and interior added by the executable. The validated scene contains **14 architectural groups**, **11,361 architectural triangles** and a ground reference with **12,544 cells grouped into 503 meshes**. These counts describe the prepared scene and do not represent newly approved models.

There are separate modes for the edited result, the inherited snapshot and the source GLB with PBR materials that Godot can display. The source model can be compared with the assembled geometry, but importing its GLB does not reconstruct the game's attached interior, lighting and fire components in that model.

Each instance retains its identity, variant, revision and hashes of the actual bytes. Orbiting, panning and zooming the camera help inspect its faces. The interior cutaway reveals the room without removing geometry from the saved scene or export. Saving and reopening preserve the work; rebuilding from the snapshot is a separate action that makes a copy of the previously saved scene.

## Ground and collision as fixed references

The ground helps check how the buildings fit, and the collision overlay shows blocked native cells. Both layers are locked and excluded from the architecture export.

Moving, rotating or scaling a mesh in the editor does not move the game's collision, doors, NPCs or triggers. One scene unit corresponds to one native cell. The camera and preview aspect-ratio tools help inspect the whole scene, but do not replace comparison from the game's original perspective and through a full 360-degree orbit.

## From editing to the review profile

Importing a GLB as a reference is separate from choosing to replace an instance in the game. The person responsible must mark the replacement and specify its own revision before exporting. The first format accepts static meshes and basic opaque materials, with base color and an albedo texture when present; incompatible features cause an error instead of silently disappearing.

The package is checked by the C++ loader before being applied to **perfil-godot-review**, with a backup, receipt and hashes. Before launching the game, the launcher also checks the executable, manifest and models against that receipt. The usual profile remains preserved. Saving, exporting, applying and giving artistic approval remain distinct decisions.

Integration was demonstrated with a synthetic 12-triangle replacement linked to the well in a disposable workspace. This box tests the bridge and is not a new artistic revision of the well. The [editor guide](https://github.com/douglasopan/diablo-3d/blob/e17b77f0370df87732e227b7fa479d9b56c44280/docs/GODOT-EDITOR.md) describes the workflow and its prerequisites.

## The hut with lighting and fire remains protected

The initial format does not yet carry the eastern hut's lighting and fire behavior. It can be inspected, but replacing it is blocked in this version to preserve the existing features. Seeing PBR materials in the reference also does not mean that all those materials can be exported to the game.

This delivery includes a bounded correction in DevilutionX: the small existing window in the hut's door now passes through the interior wall that was blocking it. The selected D3D file, the door's subdivisions and collision were preserved; the door remains closed. The openings and fire pipeline advanced to v3 without generating another model. This technical correction remains part of the whole object's visual review.

## What was validated

The October 8, 2026 run used Godot 4.7.2, the prepared scene and synthetic fixtures to test the export contract. The published results are:

| Check | Result |
| --- | --- |
| Exporter | 185 checks passed with synthetic fixtures |
| Actual Godot cycle | 21 checks, including saving, reopening and exporting |
| Python application and C++ loader | 15 checks in a disposable workspace |
| PowerShell 5.1 launchers | 8 fixtures with launches intercepted, without opening game sessions |
| Full C++ regression | No failures; 140,704 ms for the test suite to run |

Checks covered hashes, limits, identity, the atlas, receipts and rejection of incompatible inputs, as well as preservation of review settings and saves. The normal and quality local executables received the same bytes as the validated candidate, with the usual profile intact. The regression duration is the time taken to run the tests, not a measurement of performance per frame.

## The next step remains object review

The [bridge contract](https://github.com/douglasopan/diablo-3d/blob/e17b77f0370df87732e227b7fa479d9b56c44280/docs/GODOT-BRIDGE.md) limits this version to static architecture and basic materials. Authored lights, animation, collision and terrain editing, and a complete migration of the game remain outside this delivery. The technical tests do not complete Tristram or give the artwork full approval.

The next step is to review the hut with the project lead, record defects through a full 360-degree orbit and develop the interior and lighting format before allowing this baseline to be replaced. The [review execution guide](https://github.com/douglasopan/diablo-3d/blob/e17b77f0370df87732e227b7fa479d9b56c44280/docs/PROJECT-EXECUTION.md) retains this sequence. The goal remains to reconstruct every level of Diablo 1 in 3D, beginning with Tristram.
