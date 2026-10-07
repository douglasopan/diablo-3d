# Tristram asset catalog and reservations

**Finish Tristram first.** This catalog coordinates whole reusable objects and exposes coverage gaps before work begins. It is not a complete Diablo/Hellfire library, an art dump, or a claim that current reconstruction is artistically accepted.

**Resumo em português:** escolha um ID abaixo, abra uma issue para reservar esse objeto e espere a confirmação de um mantenedor antes de modelar. Uma família reutilizável pode ter várias instâncias; não crie modelos concorrentes para cada árvore, pedra, direção da câmera ou pedaço de uma casa. Concluiremos Tristram antes de ampliar o catálogo. A expansão para outros mapas virá depois que o primeiro andar procedural da Catedral funcionar.

The machine-readable companion is [`assets/registry.json`](../assets/registry.json). It contains authored IDs, source bindings, count provenance, reservation fields and validation profiles. It does not ship original pixels, map arrays or extracted game assets. Start in the [Tristram coordination issue](https://github.com/douglasopan/diablo-3d/issues/1). Use the [contribution guide](CONTRIBUTING.md), [3D issue form](https://github.com/douglasopan/diablo-3d/issues/new?template=3d_asset.yml), [build/validation guide](BUILDING-D3D.md) and [official Discord](https://discord.gg/4YxQ7s69S).

## Reserve before modeling

1. Search the ID in this catalog, the registry and [existing issues](https://github.com/douglasopan/diablo-3d/issues?q=is%3Aissue). Check parent/component dependencies and existing reservations. An empty registry claim field alone does not prove that an issue is free.
2. Open **one reservation issue for one reusable model family or a clearly scoped discovery/system task**. Put the exact catalog ID in the issue, name variants/components, and cite any earlier claim. Request a new ID through a discovery issue when the source object is still unidentified.
3. A maintainer confirms the scope, assigns the owner, and records the issue under the catalog ID. The project labels are `asset:claimed`, `asset:review` and `asset:accepted`; labels and issue assignment are the live reservation queue. These labels exist and are maintained manually; no automatic reservation system is implied.
4. Begin modeling after that confirmation. Keep one whole object, its variants, backfaces, animation and attached props under the same issue. A reusable door/window/barrel kit must coordinate with its parent claims; it must not become a second house submission. Alternative proposals belong in the existing issue unless a reviewer explicitly opens a separate experiment.
5. Submit provenance, source/variant mapping, native-angle **forced-geometry** comparison and 360-degree evidence in a PR referencing the reservation. The reviewer updates status and approved revision. Source closure tests alone do not approve the art.
6. If a claim stalls, discuss it in the issue and agree on a handoff or release. A reviewer may unassign/reopen it **after discussion**. There is no stale-claim timer or automatic reservation removal.

Registry claim fields start empty. Never put private contact information, service keys or signed asset-download URLs there. A model file path is recorded only when a lawful, reviewable asset is actually added.

## Status and scope

| Status | Meaning |
| --- | --- |
| `needed` | No validated complete model for this scope, or identification work is required. |
| `prototype` | Procedural proxy, native layer, generic relief or another runtime representation exists. Artistic acceptance is still pending. |
| `review` | A reservation's submission is undergoing visual, simulation and provenance review. |
| `accepted` | A reviewer recorded an approved revision and required evidence. |

**Approved manually authored models: 0.** The current entries are prototypes or work needed; none is marked accepted. `verified` identifies a source binding, not artistic fidelity. `category` identifies a runtime layer/library task without an exhaustive object list. `unidentified` means semantic whole-object grouping is not yet established. A null count is unknown or state-dependent.

The recorded retail/shareware snapshot has **14 architectural scene groups assembled into 12 whole objects / 9 reusable architecture families**, **93 tree instances / 6 families**, and **501 rock instances / 6 exact stencil patterns**. The rock audit covers 1,607 native source cells; 19 filler groups are hidden inside closed architecture. These are instances and renderer groups, not hundreds of model requests. Older captures limited to a smaller map region counted 419 rocks. The full renderer-grid snapshot is the 501-instance reference.

The recorded resident fixture has 11 towners: eight individually named residents plus three cows, sharing nine model families. The wounded townsman and Hellfire residents are conditional and were not in that fixture. Animation frames, viewing directions and seeded branch shapes do not count as separately reserved models.

## Whole architecture

Source: [`BuildScene` / `FinishBuilder`](../Source/engine/render/town_scene.cpp). Native piece ranges below describe **source artwork families**, not walkable physical bounds. Keep the original collision, positions and triggers.

| Stable ID | Complete object and reuse | Current source key | Status |
| --- | --- | --- | --- |
| `tristram.arch.tavern` | Ogden's tavern, main + attached wing; one issue | 2 `Tavern` scene groups; fringe 397–452 | prototype |
| `tristram.arch.smithy` | Main smithy + open work bay/forge; one issue | 2 `Smithy` groups; fringe 900–959 | prototype |
| `tristram.arch.house-common` | Shared common-house family: Gillian, northern house, Farnham; 3 measured layouts | `House`; fringe 453–487 | prototype |
| `tristram.arch.house-pepin` | Pepin's hip-roof house | `House`; fringe 488–541 | prototype |
| `tristram.arch.house-adria` | Adria's open single-slope hut, posts and foundation | `AddWitchHut`; fringe 601–642 | prototype |
| `tristram.arch.cabin` | One stone-cabin family, west short + east long layouts | `AddCabin`; fringe 849–874 | prototype |
| `tristram.arch.well` | Rim, inner wall, bottom and water; clean/poisoned quest states | `AddWell`; town `FillTile(60,70,71/342)` | prototype |
| `tristram.arch.cathedral-exterior` | Town cathedral, nave, side roofs, bell tower, spire, buttresses, apse and steps | `AddCathedral`; family 700–834 | prototype |
| `tristram.arch.catacombs-entrance` | Small mausoleum/warp, body, roof, crest and entry; open/closed state plan | `AddCrypt` / kind `Crypt`; family 1170–1197 | prototype |
| `tristram.arch.caves-entrance` | Complete Caves town warp and open/closed dressing | Native trigger (17,69) | needed |
| `tristram.arch.hell-entrance` | Complete Hell town warp and open/closed dressing | Native trigger (41,80) | needed |

**Naming warning:** the current renderer's `TownSceneKind::Crypt` / `AddCrypt` covers the **Catacombs entrance at (49,21)**. [`TownWarp1List` and `InitTownTriggers`](../Source/levels/trigs.cpp) establish that binding. Hellfire's actual crypt/grave uses `TownCryptList` 1330–1337 and trigger (36,24); it has a different catalog ID. Do not model these as the same object.

The cathedral's town trigger at (25,29), including adjacent tile (25,30), must remain accessible. A common house's source-art rectangle is not its solid wall volume. Adria's facade and the smithy bay are deliberately open; “complete 3D” does not mean sealing gameplay openings.

Conditional Hellfire town stubs: `tristram.arch.hive-entrance` (80,62; `TownOpenHive/TownCloseHive`) and `tristram.arch.hellfire-crypt-entrance` (36,24; `TownOpenGrave/TownCloseGrave`). Both are needed and unvalidated in the retail/shareware snapshot. They do not establish a full Hellfire inventory.

## Reusable tree and rock families

One tree includes its native MIN fragments **and** any delayed special-sprite columns. A single column is not a separate tree. Trees wholly painted by MIN remain whole trees. Source: [vegetation stencils](../Source/engine/render/town_vegetation.cpp) and [family enum](../Source/engine/render/town_vegetation.hpp).

| Stable ID | Source stencil / anchor → trunk piece | Foliage | Status |
| --- | --- | --- | --- |
| `tristram.tree.bare` | `BareTreeCells`, 127 → 130 | no | prototype |
| `tristram.tree.autumn` | `AutumnTreeCells`, 155 → 156 | yes | prototype |
| `tristram.tree.broad` | `BroadTreeCells`, 211 → 212 | yes | prototype |
| `tristram.tree.riverside` | `RiversideTreeCells`, 357 → 358 | yes | prototype |
| `tristram.tree.small-bare` | `SmallBareTreeCells`, 167 → 167 | no; MIN only | prototype |
| `tristram.tree.small-forked` | `SmallForkedTreeCells`, 179 → 179 | no; MIN only | prototype |

The two small-tree stencils accept specific verified alternate cells. Do not use a broad piece-range flood fill or infer an authored variant count from those alternates. Variant proposals and procedural seeds stay under the existing family reservation until source/art review justifies child IDs.

Rock source: [six `NativeProps` stencils](../Source/engine/render/town_props.cpp). The layout must match **every** cell before the current renderer groups/replaces its art. Preserve native collision, including walkable painted edge fragments.

| Stable ID | Source stencil | Piece IDs / cells per instance | Status |
| --- | --- | --- | --- |
| `tristram.rock.paired` | `PairedRock` | 219,220 / 2 | prototype |
| `tristram.rock.small-a` | `SmallRockA` | 221 / 1 | prototype |
| `tristram.rock.small-b` | `SmallRockB` | 222 / 1 | prototype |
| `tristram.rock.angled` | `AngledRock` | 223,224,225 / 3 | prototype |
| `tristram.rock.wide` | `WideRock` | 226,227,228,229 / 4 | prototype |
| `tristram.rock.large` | `LargeRock` | 230,231,232,233 / 4 | prototype |

An irregular 2×2 boulder is one model, not four boxes. Architecture filler rocks are drawing exclusions inside their parent house; they do not create a seventh rock family or alter the map. Per-family instance totals are not separately claimed by this catalog.

## Residents and playable heroes

Source identities: [towners TSV](../assets/txtdata/towners/towners.tsv), [towner types/presence](../Source/towners.cpp), [class enum](../Source/tables/playerdat.hpp) and [runtime directional actor dispatch](../Source/engine/render/town_view.cpp). Reserve the **complete character**, its animations and attached clothing/props. Eight directional source images are observations of one body.

| Stable ID | Native identity | Current representation / status |
| --- | --- | --- |
| `tristram.actor.griswold` | `TOWN_SMITH` | inferred single-view segmented body; prototype |
| `tristram.actor.pepin` | `TOWN_HEALER` | inferred single-view segmented body; prototype |
| `tristram.actor.ogden` | `TOWN_TAVERN` | inferred single-view segmented body; prototype |
| `tristram.actor.cain` | `TOWN_STORY` | inferred single-view segmented body; prototype |
| `tristram.actor.farnham` | `TOWN_DRUNK` | inferred single-view segmented body; prototype |
| `tristram.actor.adria` | `TOWN_WITCH` | inferred single-view segmented body; prototype |
| `tristram.actor.gillian` | `TOWN_BMAID` | inferred single-view segmented body; prototype |
| `tristram.actor.wirt` | `TOWN_PEGBOY` | inferred single-view segmented body; prototype |
| `tristram.actor.cow` | `TOWN_COW` | one family / 3 native instances; eight-view reconstruction; prototype |
| `tristram.actor.wounded-townsman` | `TOWN_DEADGUY` | conditional Butcher-quest body/states; needed |
| `tristram.hero.warrior` | `HeroClass::Warrior` | real eight-view hull mechanically tested; prototype |
| `tristram.hero.rogue` | `HeroClass::Rogue` | shared runtime path, class-specific art/animation validation needed |
| `tristram.hero.sorcerer` | `HeroClass::Sorcerer` | shared runtime path, class-specific art/animation validation needed |

The warrior test does not approve every armor, weapon, action or frame. Each class reservation needs a planned appearance/animation matrix; its count is currently unknown. Feet must meet the ground, actor shadows must stay on the floor, and native-visible heads/limbs must not disappear behind incorrect roofs.

Conditional Hellfire reservation stubs, all needed: `tristram.actor.lester` / `TOWN_FARMER`, `tristram.actor.celia` / `TOWN_GIRL`, `tristram.actor.nut` / `TOWN_COWFARM`, and `tristram.hero.monk`, `tristram.hero.bard`, `tristram.hero.barbarian`. Their enums and code paths exist; neither their presence nor complete appearance coverage was validated by the recorded fixtures.

## Components, terrain, loose scenery, items and effects

These IDs reserve **scoped coordination/discovery work**, not an invented exhaustive list of game assets.

| Stable ID | Ownership and coverage | Status |
| --- | --- | --- |
| `tristram.component.doors` | Coordinate a reusable door kit with parent houses/cabins; preserve native openings/collision | prototype category |
| `tristram.component.windows` | Parent windows/front projection/cabin relief; style inventory still incomplete | prototype category |
| `tristram.component.barrels` | Cabin barrel belongs to its parent; other containers need identification | prototype category |
| `tristram.terrain.native-ground` | Native floor layer, first two microframes; grass/dirt/path/bank/water semantic IDs not yet curated | prototype category |
| `tristram.terrain.river-crossings` | Identify water/banks/crossings and states; preserve native traversal | needed category |
| `tristram.discovery.fences-walls` | Standalone fences, walls and railings: no verified complete stencil catalog | needed / unidentified |
| `tristram.discovery.ruins-graves` | Residual ruin/grave/masonry fragments beyond already identified entrances | needed / unidentified |
| `tristram.discovery.loose-decor` | Remaining debris/loose decor/unknown scenery, identify before authoring | needed / unidentified |
| `tristram.item.ground-visuals` | Runtime dropped-item relief/picking; curate actual appearance families and variants next | prototype category |
| `tristram.effects.spells-portals` | Runtime missile/spell/portal billboards; complete volumetric/VFX families needed | needed category |
| `tristram.effects.actor-shadows` | Current painted ground decals; geometry-cast dynamic shadows remain future system work | prototype category |

Do not infer semantic object identity from a SOL flag, one decoded sprite, a material color or a rectangular map crop. Generic relief/fallback rendering is **coverage to inspect**, not proof that a fence/ruin/decor asset has a complete 3D model. UI artwork, sounds, inventory screens and dungeon monsters are outside the current Tristram asset inventory.

## Registry checks

Run `python tools/check_asset_registry.py` before submitting registry changes. It uses only the Python standard library and checks the versioned structure, unique IDs/source keys, existing source paths, valid dependencies/profiles, coherent claim fields and unique issue reservations. A custom registry path can be passed as its positional argument. It also checks local links and ID coverage in this catalog.

The checker does not query GitHub: it cannot discover an unrecorded issue reservation, enforce assignment remotely or approve an asset. Review existing issues first and have the maintainer update the registry when accepting a claim. Keep `claimIssue` and `claimOwner` paired. A reservation issue cannot own two different catalog IDs; coordinate dependencies through linked issues. Review/accepted submissions need an assigned claim. Acceptance additionally needs reviewer, approved revision, provenance and an existing file path for authored art.

## Validation and expansion gates

For every complete asset, compare **forced mesh rendering** with the **real native backend** at the original camera, then inspect the entire silhouette and small ±5° orbits plus eight 360° directions. Home's native backend dispatch is not mesh fidelity evidence. Validate backfaces/undersides, plausible inferred unseen surfaces, normals/UVs, finite nondegenerate geometry, appropriate closure, ground contact, picking/occlusion, animation and variant states. Keep original map, collision, player/NPC positions, triggers and random seed unchanged. The registry defines the detailed validation profiles.

Require lawful provenance for independently authored art and record importer/generator settings where relevant. Proprietary native captures and game archive extracts remain local. Existing proxies use a user's own game data at runtime; that does not make extracted textures redistributable.

Expansion order:

1. Identify residual Tristram scenery, complete required retail/shareware objects and state variants, and record visual/gameplay acceptance. Conditional edition gaps stay explicit.
2. Make the **first procedural Cathedral dungeon level** work from the live native map and existing seed. Reuse agreed modular assets, preserving collision and triggers, and validate it before moving to other level families.
3. Then extend the catalog from verified source inventories for further Diablo/Hellfire content. Do not preannounce an exhaustive full-game list or reserve hundreds of unidentified future objects now.

This catalog changes through reviewed source/asset work. Keep stable IDs when renaming display labels; deprecate/redirect a split or merged ID with a reviewer-approved mapping rather than silently duplicating reservations.
