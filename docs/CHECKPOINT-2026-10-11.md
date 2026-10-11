# Versioned implementation checkpoint

Revision **2026.10.11-r1** · **11 October 2026, America/Sao_Paulo**. Documentation parent: `5c3d090fc7e103a356c910d5308d22e2342ace60`. This is a documentation-only publication. The containing Git commit identifies these documents; the binary hashes below identify different local builds. It does not publish or install a game binary.

Use [CODE-ARCHITECTURE.md](CODE-ARCHITECTURE.md) for paths/owners/lifecycle, [CODE-AUDIT.md](CODE-AUDIT.md) for verified gaps, and [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md) for the active execution queue. Historical component receipts remain useful but do not override this dated build table.

## Status vocabulary

**Planned** is a requirement/design. **Prepared** means files or a patch exist. **Implemented in candidate** means code exists in that source. **Compiled** means a specific binary linked. **Tested** always specifies its gate and limits. **Installed/delivered** means a particular launcher uses that payload. **Visually approved** is a separate human decision, with its scope. None implies all the others.

## Build identities

| Version / source | Real state | Contents and limits |
| --- | --- | --- |
| Habitual Windows **b775** | Installed locally through `Iniciar-Tristram.cmd`; three EXE aliases agree | Responsive HUD R4, own TP/FP camera/input, Town/horizon and normal floor-1 Cathedral pilot. Meshy OFF rejects 14 known bindings; Ogden is a native sprite. Load/reentry can refuse 3D. No full-game or sustained-FPS acceptance. |
| Experimental preview **337eba**, dungeon R3 | Delivered in its separate immutable version directory/profile | Rectified floors/primary walls and loader prerequisites on the same native simulation. No R4 E/stationary-attack change or new FP ceiling. User's visual/camera issues remain open. Profile uses Speed 20 instead of habitual 32; it has not been silently changed during testing. |
| Private normal candidate **ed0e0a**, dungeon R4 | Compiled; not installed | R3 plus bounded C1 E-interaction/LMB stationary-attack policy and a provisional FP ceiling. Fixture binaries below provide limited runtime evidence; the normal EXE itself has not passed a complete player session. |
| Clean C1 base **clean-c1-r1** | Source layers applied, **not compiled/executed from that checkout** | Detached upstream `dac104babfb6187415432f428ac2516747ffc154`; 186 declared Source deltas, retaining own UI/input/camera/Town. Not a completed modular rewrite or proof of a minimal dependency closure. Next profile explicitly preserves Speed 32. |
| Local browser **R12** | Previously exercised local Emscripten/WebGL2 bundle; frozen during testing | Meshy OFF, data import/IDBFS, Town/HUD and view switching exercised. Freezes/performance pending. Not updated with the dungeon R4/clean candidate; no public browser game release. |
| Old release/family/UI preparations | Preserved, on hold | `621ec3…`, broader families, prior final-release and other WIP do not become installed because a newer document mentions them. Level 2, other base families, special levels and Hellfire are not qualified by the floor-1 pilot. |

Full Windows EXE SHA-256 identities:

```text
b775:  b775621618640cc3f2e3a6f3c440c92d90cb67933a7a3a938d6b89b8c71d12f3
R3:    337eba48556513d00ad6fc88d5c7f6a58620c41a1ece3f532182fac034acc057
R4:    ed0e0ad8566d3c113810bcea50981769ef612eea262a2efd71a821a41e3d17fb
```

Preview R3 has 228 pinned payload files: EXE plus 227 support assets. Payload-manifest SHA-256 is `5f68e5ff871b4dffa0c9317e6c5497a9b36832df7cf45485518fffb03025c53c`. User configuration and newly created test saves are separate mutable profile data; they are not distributed.

## Functional state by feature

| Feature | Installed / delivered | Candidate or remaining work |
| --- | --- | --- |
| Own camera, WASD/native walking and native UI ownership | Retained in b775 and R3 | Separate persistent state exists. Lifecycle/camera-refusal investigation and full physical input matrix remain open. No reference movement clock imported. |
| Normal Cathedral materials | Bounded pilot in b775; rectified floor/primary wall adaptation delivered in R3 | R4/clean visual adaptation incomplete: common masonry, independent ceiling, arches/grates, stair/detail composition and lighting comparison. |
| Candelabra false corner pillars and opaque unseen walls | Defects previously reported; bounded prior fixes exist | Must be checked in comparable native/3D scenes. Do not infer final correctness from pure checks or partial screenshot improvements. |
| E nearby interaction; LMB standing held attack | Not in b775/R3 | R4 implementation exists; TP/FP empty-floor DOWN/UP parsed-command checks passed. E loot/doors/triggers, enemy/ranged targets, full held native-tick cadence, UI/focus matrix pending. |
| UI/HUD/menu/music/language | Existing responsive main HUD and settings retained | New panel/automap interactions and unified Q/E/Tab/M/J design are build-qualified preparations/design, not all delivered defaults. Approved inventory transparency stays intact. |
| Meshy content | OFF for 14 exact known bindings | Masters and historical revisions preserved, not deleted/regenerated. New models and distance LOD not automatically connected. |
| HD from DamnationX | **No integration into b775/R3/R4/clean runtime** | Owner-supplied archive extracted privately: 2,206 files, including 2,194 WebP atlases. Private 213-floor material/adapter preparation uncompiled. Full decoded atlases would be about 25.7 GiB; no all-pack eager upload. Work deferred behind Cathedral. |
| Shift run / stamina | No new implementation delivered | Deferred. Existing native Run in Town is a different option; do not use it to conceal the Speed-profile discrepancy. |
| Town attack animation / harmless friendly targets | Newly authorized requirement, **not implemented** | Native command handlers and attack-art loading currently exclude Town. Separate animation from damage/projectile policy; preserve friendly NPC immunity, E/talk, native cadence and no new PvP. |
| Expanded multiplayer / proximity voice / new surface maps | Not implemented | Future work after full-game stabilization, per roadmap. Current helper build is NONET; provisional horizon is decorative. |

## Evidence already executed

These receipts are private diagnostics using a synthetic test hero/profile and the owner's local original data. No user save, MPQ or extracted media is included here. A fixture EXE is different from the normal game EXE.

| Gate / fixture | Result and identity | What it does not prove |
| --- | --- | --- |
| Rectified material gate R2 | **69,638 checks**; `3e8138ab680d2f86e3bda8381b3f3ba5c2077db0c65dccae24248520a4e2d28e` | No visual acceptance, native play, GPU or FPS. |
| Native material/loader R3 | **32,114 checks**; `9826e007068e91626d0a376a2fc827be0b7469214cac2b1ef893c8eff2ef5a3e` | Real loader and CPU drawings across NEW/full load/Town reentry/1→2→1, four orbit angles per observed state. Hero selection/physical play excluded; level 2 stays native; no close-FP appearance approval. |
| Combined native input R5 | **40,037 checks**; `99331316dbb651945f90f139666ff76a15ad44a3b89e927553c9a5bd5e742b94` | Real loaded C1, native join completion, TP/FP actual DOWN/UP and native command parsing; interpolation/release guards. Not the complete E/target/focus/UI/combat matrix. |
| Native hardware gate R2 | Passed four TP/FP cold/warm frames on **Radeon RX 570**; `7f5d40c6d7a9099dca043fb9aef00c3dd6405a6b5581692b3a24243e1c33f9e1` | Hardware used, WARP off, no CPU raster fallback in those frames. TP cold included initialization (~1,176 ms); warm world frames ~6 ms at 640×480. Not sustained gameplay FPS, all camera positions or artistic quality. |

Presentation witnesses in those bounded gates preserved observed native map/occupancy/RNG and private file bytes. They are not exhaustive proofs of every simulation field, save format or campaign path. The documentation update itself launches no game, builds no game and changes no runtime profile.

## Current execution order

**New regression report incorporated before publication:** missing map marks and increased 3D refusal/previous-view behavior are open blockers. A read-only [provenance audit](CODE-AUDIT.md#reported-regression-and-provenance) verified intact payload and unchanged own automap sources, found independent profile differences, and identified missing first-refusal telemetry. This is not proof that the camera/map works, that a wrong EXE caused the earlier failure, or that exploration was lost. Recover earlier capabilities in the clean base before promotion; no human save/config was changed by this audit.

The owner-authorized exception prioritizes **real code documentation/audit and a clean, isolated Cathedral visual adaptation**. General game fronts and the prior final release stay on hold. Preserve our camera/input/UI/Town and native simulation; reference floor/wall methods are adapted into our renderer rather than importing the author's engine loop.

After this documentary checkpoint, continue the same Cathedral queue: complete materials/geometry/details/light and persistent lifecycle contracts; validate NEW/load/reentry and the full interaction/visual/performance matrix; then deliver one separately versioned, isolated candidate/profile. HD and Shift/stamina preparation remain deferred. Publishing these documents is not permission to promote unknown Source WIP or replace the tested preview.
