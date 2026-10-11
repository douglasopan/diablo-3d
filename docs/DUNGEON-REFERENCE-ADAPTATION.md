# Dungeon reference adaptation — in progress

Douglas Pan authorized this bounded adaptation on 11 October 2026. The public project remains non-commercial and free of charge. No new game build has been installed or published by this work.

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

Tristram geometry, reference movement/camera/UI and a compass are outside this immediate adaptation. Other development fronts and the previous final-release candidate remain paused.

## Evidence and remaining work

Private candidate checks, not a release claim:

- Pure floor/wall coordinate and independent coverage tests: **69,638 checks passed**.
- Actual native loader and CPU floor/wall draw: **32,114 checks passed**, four camera angles per observed Cathedral state. NEW, full save/load, Town reentry and level 1 → 2 → 1 were exercised using a newly created private profile. Native map/occupancy/RNG observations and private save bytes were preserved by presentation.
- The loader test excludes the hero-selection UI and physical gameplay. Unsupported floor 2 remains outside the current pilot; a successful transition does not claim it renders in 3D.
- Walls/materials candidate compiled and linked; GPU draw, first-person ceiling imagery, E/held-attack gameplay, doorway/arch fidelity, all dungeon families and browser validation remain pending. No sustained FPS measurement or complete campaign qualification.

A material-specific candidate reused two previously reproduced loader prerequisites: saved ground items may legitimately have zero animation timing; unused corpse slots may refer to artwork freed during a level change. The visual fingerprint accepts that native item state and reads only corpse slots referenced by the live map. It does not modify native item/corpse state or reset the camera.

The next gate is native first-/third-person rendering and interaction on the same candidate. Installation and publication require validation; preparation or pure tests alone are insufficient.
