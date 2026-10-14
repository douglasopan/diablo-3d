# Versioned implementation checkpoint

Revision **2026.10.11-r2** · **11 October 2026, America/Sao_Paulo**. This local operational increment follows documentation-only commit `cc768ac25adc2dc37bac0019a897ae236b6fdace` (r1; parent `5c3d090fc7e103a356c910d5308d22e2342ace60`). It records a separately delivered review candidate, not a public binary release or replacement of the habitual game. Git document revisions and binary hashes identify different artifacts.

Use [CODE-ARCHITECTURE.md](CODE-ARCHITECTURE.md) for paths/owners/lifecycle, [CODE-AUDIT.md](CODE-AUDIT.md) for verified gaps, and [PROJECT-EXECUTION.md](PROJECT-EXECUTION.md) for the active execution queue. Historical component receipts remain useful but do not override this dated build table.

## Status vocabulary

**Planned** is a requirement/design. **Prepared** means files or a patch exist. **Implemented in candidate** means code exists in that source. **Compiled** means a specific binary linked. **Tested** always specifies its gate and limits. **Installed/delivered** means a particular launcher uses that payload. **Visually approved** is a separate human decision, with its scope. None implies all the others.

## Build identities

| Version / source | Real state | Contents and limits |
| --- | --- | --- |
| Habitual Windows **b775** | Installed locally through `Iniciar-Tristram.cmd`; three EXE aliases agree | Responsive HUD R4, own TP/FP camera/input, Town/horizon and normal floor-1 Cathedral pilot. Meshy OFF rejects 14 known bindings; Ogden is a native sprite. Load/reentry can refuse 3D. No full-game or sustained-FPS acceptance. |
| Experimental preview **337eba**, dungeon R3 | Delivered in its separate immutable version directory/profile | Rectified floors/primary walls and loader prerequisites on the same native simulation. No R4 E/stationary-attack change or new FP ceiling. User's visual/camera issues remain open. Profile uses Speed 20 instead of habitual 32; it has not been silently changed during testing. |
| Private normal candidate **ed0e0a**, dungeon R4 | Compiled; not installed | R3 plus bounded C1 E-interaction/LMB stationary-attack policy and a provisional FP ceiling. Fixture binaries below provide limited runtime evidence; the normal EXE itself has not passed a complete player session. |
| Clean C1 base **clean-c1-r1**, normal EXE **4391a5**, gate **R26** | Freshly compiled; bounded native/hardware fixture passed; delivered as an isolated local review | Detached upstream `dac104babfb6187415432f428ac2516747ffc154`; 186 initial declared Source deltas, then 11 Source paths changed for diagnosed C1 defects. Retains own UI/input/camera/Town. Separate Speed 32 / FOV 80 / Zoom 1 profile. Normal EXE human gameplay, physical input and sustained FPS remain unqualified; not a completed modular rewrite or minimality proof. |
| Local browser **R12** | Previously exercised local Emscripten/WebGL2 bundle; frozen during testing | Meshy OFF, data import/IDBFS, Town/HUD and view switching exercised. Freezes/performance pending. Not updated with the dungeon R4/clean candidate; no public browser game release. |
| Old release/family/UI preparations | Preserved, on hold | `621ec3…`, broader families, prior final-release and other WIP do not become installed because a newer document mentions them. Level 2, other base families, special levels and Hellfire are not qualified by the floor-1 pilot. |

Full Windows EXE SHA-256 identities:

```text
b775:  b775621618640cc3f2e3a6f3c440c92d90cb67933a7a3a938d6b89b8c71d12f3
R3:    337eba48556513d00ad6fc88d5c7f6a58620c41a1ece3f532182fac034acc057
R4:    ed0e0ad8566d3c113810bcea50981769ef612eea262a2efd71a821a41e3d17fb
Clean: 4391a5b65af771947082e0c48c5db5a5ebd7ef155745afb63e50b774adaa83a2
```

Preview R3 has 228 pinned payload files: EXE plus 227 support assets. Payload-manifest SHA-256 is `5f68e5ff871b4dffa0c9317e6c5497a9b36832df7cf45485518fffb03025c53c`. User configuration and newly created test saves are separate mutable profile data; they are not distributed.

The clean R26 review is in `versions/cathedral-clean-4391a5b65af7-r26`, opened through **Iniciar-Review-Catedral.cmd**. Its verifier passed all 228 frozen payload files; the 227 support assets are unchanged from the preserved support set. Manifest SHA-256: `46dbf17ba4bf0728a5e603cd7348663e5c3e16dd8c090897eac0e65c0244a86e`. Launcher SHA-256: `731096b777e74fcc76c584f8c5f1d51f0cef765dd7c5174719eac130697e84a1`. The separate mutable profile contains only the fixture-generated **NativeLoadGate** hero and initial review configuration. No human save was read or copied, no MPQ was bundled, and neither b775 nor the R3 preview was overwritten.

## Functional state by feature

| Feature | Installed / delivered | Candidate or remaining work |
| --- | --- | --- |
| Own camera, WASD/native walking and native UI ownership | Retained in b775 and R3 | Separate persistent state exists. Lifecycle/camera-refusal investigation and full physical input matrix remain open. No reference movement clock imported. |
| Normal Cathedral materials | R26 separate review adds covered floor boundaries, common masonry, an independent stone FP ceiling and textured stairs | Native SOL, openings, map and simulation remain authoritative. Comparable native/3D and reference captures exist for bounded scenes; complete architectural and human visual acceptance remain pending. |
| Candelabra false corner pillars and opaque lower wall faces | R26 suppresses false wall/corner geometry at the verified native light-stand cell and supplies a closed volumetric support; solid masonry faces are opaque | Native light stand artwork/owner predicates are bounded to retail normal C1. Flame and body share native visibility; original animated flame coverage is anchored/scaled below the ceiling. The procedural body is an approximate proxy, not an approved final master. Review other rooms/cameras. |
| E nearby interaction; LMB standing held attack | Implemented in the separate R26 review; absent from b775/R3 | Real native E pickup in TP removed the ground item and transferred its exact identity to belt/inventory. Actual native ticks repeated standing attacks in TP/FP and stopped on release. Native inventory and simulated focus/UI recovery passed. Doors/triggers, enemy/ranged targets and the remaining physical input matrix are pending. |
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
| Clean native fixture **R26** | **245,750 checks passed**; fixture EXE `2e80ef9d601b50c9eb8dbbf7e912b9cb7a1e04e141282b9b831cd72206cee7a2`; normal EXE `4391a5…` compiled separately | Real loader, save/load, reentry, transitions, native command parser/simulation and hardware world frames; no normal StartGame/UI session or human gameplay. The fixed synthetic seed does not qualify every seed, room or target. |

Presentation witnesses in these bounded gates preserved observed native map/occupancy/RNG and private file bytes. They are not exhaustive proofs of every simulation field, save format or campaign path. The documentation edit itself launches no game and changes no runtime profile.

R26 includes actual NEW → Town → C1, C1 reentry, 1 → 2 → 1 and full save/load using original local data read-only and a new synthetic profile. Native map/item-marker pixels survive final GPU composition in the observed case. First unintentional refusal telemetry retains epoch/camera/input cause across recovery, including an injected allocation refusal; it does not identify the cause of every earlier human report. TP and FP each exercised 80 held attack ticks plus 40 released ticks, observing eight native repeat commands and no continued attack after release. E pickup exercised the native request/collect parser and legitimate inventory/belt sync, with native occupancy and active-item membership removed.

Radeon RX 570 world frames used hardware with WARP off and no CPU raster fallback in those frames. Warm world-pass timings were about 5.7–6.6 ms at 640×480; cold initialization was about 893 ms. These are **not sustained gameplay FPS**; the separate manual review starts at 1280×720. SDL focus conditions were injected for the fixture; physical Alt-Tab/click capture was not tested.

The original reference fills zero-piece/out-of-bounds cells with artificial far walls. In the observed angle-0 view, tile (44,46) has native piece 0, is not visible and lies outside the nine-region pilot. Keeping that void unrendered is an accepted methodological difference; do not widen visibility or reveal unexplored architecture merely to copy reference pixels. Own camera projection/eye height and native lighting are retained.

Private receipts: `clean-native-r1/GATE-R26.json` SHA-256 `5eeaa158bfc4367907ab96b38a2bb64caf72d1e698a38a3cf0716c8293dad315`; `qualified-r26/CHECKPOINT.json` archives the 735-file Source closure, build/gate identities and 1,704 protected-file checks; `DELIVERY-R26.json` records the actual launcher, payload and synthetic profile. Earlier failed fixture preparations and successful historical gates remain preserved; they do not independently qualify the final build.

## Current execution order

**Regression follow-up:** the earlier [provenance audit](CODE-AUDIT.md#reported-regression-and-provenance) remains historical evidence. R26 now checks composed native map marks, matching-frame input and first-refusal/recovery in a bounded synthetic case. The reported human map/camera behavior still needs confirmation in the delivered review; no wrong-EXE or lost-exploration cause has been established and no human save/config was changed.

The owner-authorized exception prioritizes **real code documentation/audit and a clean, isolated Cathedral visual adaptation**. General game fronts and the prior final release stay on hold. Preserve our camera/input/UI/Town and native simulation; reference floor/wall methods are adapted into our renderer rather than importing the author's engine loop.

The separately versioned review is now delivered. Next, review its normal gameplay and appearance, physical capture/UI, doors/triggers/targets and sustained performance, then fix reproduced defects in this same Cathedral queue. Human visual approval is pending. HD, Shift/stamina and other fronts remain deferred. Neither document publication nor fixture success promotes unknown WIP, replaces the habitual game/preview or qualifies the full campaign.
