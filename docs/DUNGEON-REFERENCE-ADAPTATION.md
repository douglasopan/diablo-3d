# Dungeon reference adaptation — in progress

Douglas Pan authorized this bounded adaptation on 11 October 2026. The public project remains non-commercial and free of charge. The habitual build remains b775; an experimental R3 floor/wall preview was delivered in a separate immutable snapshot/profile. The [2026.10.11-r1 checkpoint](CHECKPOINT-2026-10-11.md) distinguishes that preview, the private R4 candidate and the uncompiled clean C1 base. This publication changes documentation only and publishes no game binary.

## Attribution and source

We thank **plopsDOTio / plops.io**, author of the independent Diablo First Person mod, for making its source available. [Official distribution and source](https://forge.plops.io/t/diablo-first-person-devilutionx-mod-for-windows), [author announcement](https://www.reddit.com/r/Diablo1/comments/1x1wtjz/diablo_1_in_first_person_original_automap_butcher/).

The reference declares DevilutionX 1.6.0-dev base `bf574c9b74f1d718b0bcee15d648b20a8bfc927c`. Drelvak's video inspired that independent implementation; it is not a source of code incorporated here. DevilutionX contributor credits, Blizzard/original Diablo authorship and third-party notices remain in force.

Verified official package SHA-256:

- Source 0.3.2: `9cae7ba13a467371235a8d3563dd41db27cfc8f0c3424016dcddbc5c39d80857`.
- Library sources: `f7b4f93186c9a1ab564d9659fd10c695dd52a2fbf50f95bb1249b11be0f0032f`.
- Windows package: `58f8e9d8c4e23e3a08aa290fc676a27a4258873301f5b99b1299a1d18714c015`.
- Reference `Source/engine/render/first_person.cpp`: `585fdb560e3ca056b3875f533dfdd543e3beb31b7453ea6ef5d16f851dda3bb5`.

Packages were inspected without executing the reference binaries. A matching ZIP hash identifies the downloaded package; it does not independently prove how its executable was compiled.

## Modified adaptation and license

This is **modified software**. The reference's Sustainable Use License 1.0 is identical to this project's [LICENSE.md](../LICENSE.md). It permits derivative works subject to its terms: distribution must be free of charge and non-commercial, terms and notices must accompany copies, and modifications must be marked prominently. It is not MIT or an OSI-approved license. [The reference modification notice is preserved](credits/diablo-first-person/MODIFICATION-NOTICE.txt).

The candidate adapts the square-floor and complete-wall-face sampling method from `GetTexture`. It retains our native decoder, independent coverage for palette values 0 and 255, scene triangles, cache budgets, native lighting and D3D11 submission. It does not import the reference's raw CEL decoder or software rasterizer. Any missing solid-wall artwork filled by a masonry sample remains a visual reconstruction, not original 3D geometry. Doors, grates, arches, objects and caps are excluded from that solid-face fill.

No proprietary MPQ, extracted texture atlas, original music or personal save is included in these documents. Existing third-party library distribution obligations remain applicable to eventual binaries/source kits.

## Bounded execution

| Component | Adaptation | Preserved authority / limit |
| --- | --- | --- |
| Floors | Native diamond sampled as a square material on the existing quad | Physical floor vertices, native map and SOL collision |
| Primary walls | Full-height rectified material rather than one repeated fragment | Apertures and object-specific paths; reconstruction is approximate |
| Dungeon lifecycle | Prepare only after loader data is ready; epoch-scoped caches | Seeds, RNG, saves, load/reentry and native transitions |
| Interaction | E for native nearby interaction; held world-left for stationary attack | Our movement, camera, UI, native commands, reach and combat cadence |
| Renderer | Existing scene and D3D11 pipeline | No transplant of CPU renderer or per-frame reference buffers |

Tristram geometry, reference movement/camera/UI and a compass are outside this immediate adaptation. The separate clean base retains our own camera, input, movement, UI and Town source layers. Complete Cathedral materials, geometry, details and lighting remain the immediate milestone; moving files alone does not complete modularization. Other development fronts and the previous final-release candidate remain paused; HD and Shift/stamina preparations are deferred.

## Evidence and remaining work

Private candidate checks, not a release claim:

- Pure floor/wall coordinate and independent coverage tests: **69,638 checks passed**.
- Actual native loader and CPU floor/wall draw: **32,114 checks passed**, four camera angles per observed Cathedral state. NEW, full save/load, Town reentry and level 1 → 2 → 1 were exercised using a newly created private profile. Native map/occupancy/RNG observations and private save bytes were preserved by presentation.
- The loader test excludes the hero-selection UI and physical gameplay. Unsupported floor 2 remains outside the current pilot; a successful transition does not claim it renders in 3D.
- Separate combined native input fixture R5 passed **40,037 checks** for loaded C1 TP/FP DOWN/UP, native command parsing and interpolation/release guards. The fixture now processes the native join-level completion rather than testing input while the player is still changing levels. It does not qualify E loot/doors, enemies/ranged targets, full held-combat cadence or the focus/UI matrix.
- Separate native hardware fixture R2 passed four cold/warm TP/FP frames on **Radeon RX 570**, with hardware used, WARP disabled and no CPU raster fallback in those frames. It does not establish sustained gameplay FPS or complete visual acceptance.
- R4 includes a provisional FP ceiling using floor material; reference common masonry/independent ceiling and complete doorway/arch/stair/detail/lighting adaptation remain pending. Other families and browser changes are unqualified. Fixture hashes, build status and limits are in the dated checkpoint.

A material-specific candidate reused two previously reproduced loader prerequisites: saved ground items may legitimately have zero animation timing; unused corpse slots may refer to artwork freed during a level change. The visual fingerprint accepts that native item state and reads only corpse slots referenced by the live map. It does not modify native item/corpse state or reset the camera.

The next work is complete Cathedral visual adaptation and persistent level/frame/camera/input contracts on the isolated clean candidate, followed by the full functional/visual/performance matrix. Installation and binary publication require validation; preparation, pure tests and isolated hardware frames alone are insufficient. The [code map](CODE-ARCHITECTURE.md) and [audit](CODE-AUDIT.md) describe current owners and gaps.
