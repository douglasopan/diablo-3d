# Reference sources and imported candidates

Evidence reviewed on 07 October 2026. This document records sources and import checkpoints; it does not approve an asset or enable a new runtime model.

**Resumo em português.** A arte e a simulação do Diablo original continuam sendo a referência de posição, composição, escala e colisão. Belzebub pode fornecer referências suplementares identificadas, mas maior resolução de tela não comprova sprites ou texturas redesenhados. O modelo “Diablo”, de Vasian-Digital3D no Sketchfab, está listado como baixável sob CC BY 4.0, com rig e uma animação. Seu arquivo ainda não foi obtido nesta auditoria; três materiais e cerca de 60 mil triângulos exigem um caminho de personagem animado, além do importador estático de cabanas.

## The canonical reference remains original Diablo

Bind a reference to its asset identity in [the catalog](ASSET-CATALOG.md) and [registry](../assets/registry.json), edition, native artwork family and actual instance. For current Tristram work, the original map, SOL flags, actor footpoints, triggers and closed-house collision remain authoritative. A screenshot from another engine or mod does not replace those coordinates or rules.

Compare actual original-renderer captures and forced 3D captures at the same calibrated camera, actor pose and object scale. Retain the whole silhouette and ground contact, then inspect nearby angles and a complete orbit. The Home backend is useful for switching to the original image; it cannot validate hidden geometry, materials or an imported model. An accepted interpretation of an unseen back or interior should be recorded as such, rather than presented as recovered original 3D information.

Visual openings and simulation access are separate. A measured window may expose an interior while the original closed door and player collision remain unchanged. Follow [building openings](BUILDING-OPENINGS.md) and [Tristram lighting](TRISTRAM-LIGHTING.md) for physical apertures, fire sources and comparisons.

## Belzebub: official scope and installed-package audit

The project's [official download page](https://mod.diablo.noktis.pl/download) lists Belzebub v1.045, public beta, 32 MB, dated 10 September 2014. Its [official features page](https://mod.diablo.noktis.pl/features) describes increased resolution and panoramic screens, new Barbarian/Assassin classes, locations, quests, bosses and gameplay changes. It runs through its own `Belzebub.exe` and requires the original game data. The [GOG listing](https://www.gog.com/en/game/diablo_1_hd_mod_belzebub) identifies the separately distributed mod.

These pages do not specify replacement sprite dimensions or promise a general redraw of the game's textures. Framebuffer size, interface scaling, filtering and a larger screenshot can change presentation without adding source-art detail. Compare decoded source art before labeling a reference HD.

The user supplied an already installed GOG copy. A read-only package audit compared its base archive with the separate original GOG installation, then probed the mod's `Data/game_data/data.mpq` and `patch.mpq`. No installation files or game state were changed.

| Installed evidence | Result |
|---|---|
| Both `DIABDAT.MPQ` copies | 517,501,282 bytes; identical SHA-256 `63fb47d9c76484024c7640d90ab6b7ec5e13f567a7e1a917b6c03a6631d3f2b0` |
| Extra archive sizes | `data.mpq`: 4,312,049 bytes; `patch.mpq`: 126,043 bytes |
| Extra archive coverage | 235 / 32 occupied file blocks; neither includes `(listfile)`. 10,568 candidate names found 158 / 1 matching names, all readable. This is a partial name inventory, not exhaustive content recovery. |
| Town imagery queried | No extra-archive entry for native `town.cel`, `towns.cel` or `town.pal`; their base archive is identical. |
| Town structure | `town.min` preserves every original byte and adds one 32-byte piece; `town.til` preserves its original prefix and adds 32 bytes. `town.sol` changes eight original flags and adds one. These structure/collision files do not establish higher-resolution textures. |
| Hero and boss imagery queried | No extra-archive replacement for 30 light-armored/unarmed Warrior, Rogue and Sorcerer action sheets or the six native Diablo action sheets. Unknown or alternate entry names remain outside this coverage. |
| Additional references found | Griswold `smithn.cel` and Ogden `twnfn.cel` expand from one 16-frame list to eight 16-frame directional groups, all still 96×96. Each mod's SouthWest group preserves all 16 original RGBA frames exactly under the original town palette. The seven supplemental views can guide local modeling; they are mod additions, not recovered original views or higher-resolution pixels. Six queried PNGs are interface/icon artwork. |

The local audit keeps the source reads, hashes, archive probes, 51 selected original reference extracts and decoded contact sheets under `diagnostics/belzebub-reference-audit`, outside the public source checkout. Decoding preserves RLE opacity, including opaque palette-index zero, and uses the unchanged town palette without scaling or translations. Do not upload that folder's extracted art. The unchanged town artwork is still the canonical source for the current cabin. Changes in lighting or resolution in Belzebub's renderer cannot be substituted for source-art detail.

The official feature changes require explicit source binding. Do not assume a mod's class, NPC, quest location or screenshot coordinate maps directly to the original edition. A changed Tristram layout has not been established by the pages reviewed here. Check the particular object and layout before borrowing shape or material evidence.

The site carries an all-rights-reserved notice. This audit has not found a permissive license for redistributing Belzebub artwork. Keep source archives and extracted references local while recording their provenance; a public mod download alone is not an asset redistribution license. Unidentified archive entries and any additional permissions remain audit items.

| Evidence to record | What it resolves |
|---|---|
| Official version, package SHA-256, relevant archive/file identity | Which mod edition produced the reference |
| Asset dimensions and corresponding original asset | Whether detail is changed, enlarged, filtered or redrawn |
| Same-object screenshots with resolution/filter/camera settings | Whether a visible difference belongs to art or presentation |
| Native asset family, instance and edition mapping | Whether shape/layout can guide the current model |
| Supplied asset terms and attribution | What can accompany a public contribution |

## Sketchfab candidate: “Diablo”

The user supplied [this model page](https://sketchfab.com/3d-models/diablo-fbd864f53f514b179a8d21579ca915f0). The [public model API](https://api.sketchfab.com/v3/models/fbd864f53f514b179a8d21579ca915f0) and the page's Model Information panel were inspected directly.

| Published field | Observed value |
|---|---|
| Title / uploader | Diablo / [Vasian-Digital3D](https://sketchfab.com/Vasian-Digital3D) |
| Model UID | `fbd864f53f514b179a8d21579ca915f0` |
| Published date | 04 November 2025 |
| License | Creative Commons Attribution, linked to [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Download flag | `isDownloadable: true`; the page exposes Download 3D Model |
| Geometry | API `faceCount: 60174`, `vertexCount: 30595`; page labels these 60.2k triangles / 30.6k vertices |
| Materials / textures | 3 / 9, metalness PBR |
| Rig / morph geometry | Rigged geometries: Yes / morph geometries: 3 |
| Animations | 1; the viewer labels it “Motion” |
| UVs / vertex colors | Both listed as present |
| Listed formats / size | Autodesk FBX; converted glTF, GLB, USDZ / 28 MB |
| Description | Empty; game edition and underlying asset provenance are not stated |

Only public metadata was saved locally at `diagnostics/sketchfab-diablo-candidate/model-metadata.json`. Its SHA-256 is `48da51ac4d1db62174ed22ff9dca21724452eacda2374f9976b7d26c4f247e24`. The public thumbnail and normal model page were viewed in the browser. No source model, protected viewer payload or texture archive was extracted during this audit.

Sketchfab's [official download documentation](https://sketchfab.com/developers/download-api/downloading-models) requires an authenticated account to request a downloadable archive. Its [Download API](https://sketchfab.com/developers/download-api) supplies converted model formats, rather than the original FBX. Obtain a source through the site's official download flow or authenticated Download API; the public flag does not supply an anonymous archive URL. The source-file inspection remains pending.

The published CC BY 4.0 terms permit adaptation and sharing with attribution, a license link and an indication of changes. [Sketchfab's integration guidelines](https://sketchfab.com/developers/download-api/guidelines) require author and source attribution to follow the asset. Record the uploader's license declaration and any supplied notices with the download. The empty description does not establish the source game or resolve the provenance of every component; the [CC deed](https://creativecommons.org/licenses/by/4.0/) also states that its license may not supply every permission needed for a use.

An attribution record can start as:

> “Diablo” by Vasian-Digital3D, supplied through Sketchfab, model `fbd864f53f514b179a8d21579ca915f0`, CC BY 4.0. Source: https://sketchfab.com/3d-models/diablo-fbd864f53f514b179a8d21579ca915f0. License: https://creativecommons.org/licenses/by/4.0/. Changes: [list the actual conversion, geometry, material and animation changes].

The preview shows a red muscular demon with gold abdominal plates, large horns and back spikes. This is a visual observation, not verification that its design matches Diablo I or identifies another installment. Compare original Diablo frames, proportions, silhouette, horns, coloring and motion before assigning a canonical boss identity. One published animation does not establish usable walk, attack, hit, death and special-action clips; the skeleton, tracks, durations and deformation still require source inspection.

## Import checkpoint: static town model versus animated boss

The current [town importer](../Source/engine/render/town_model_import.hpp) reads `D3DMESH1`: static triangles with position/UV and one RGB texture, at most 20,000 triangles and 2048 pixels per texture side. It has no skeleton, animation tracks, skin weights, morph targets or multiple-material records. Loading visual geometry preserves the assigned native map/collision metadata; it does not create a monster behavior implementation.

The current [3D view](../Source/engine/render/town_view.cpp) is enabled for town levels. A dungeon boss also needs a dungeon rendering path. The existing [monster state interface](../Source/monster.h) distinguishes `Stand`, `Walk`, `Attack`, `GotHit`, `Death` and `Special`; a catalog animation count alone cannot supply those bindings.

[The GLB inspector](../tools/inspect_glb.py) flattens a static scene and explicitly reports that it does not evaluate animation. [The town exporter](../tools/export_town_model.py) requires one material and rejects geometry beyond the runtime limit. Therefore this candidate cannot pass through that route unchanged. A static preview conversion would discard its animation and should be labeled a preview, rather than a working boss integration.

For a boss candidate, use these separate checkpoints:

1. **Acquire and inspect.** Retain the official source, download/license snapshot, hashes, material inventory, skeleton/skin/morph data and actual animation names/durations. Verify required texture channels and deformation through complete motion.
2. **Compare and prepare.** Map the candidate to original boss references. Prepare measured scale, origin and direction conventions. If reducing geometry or consolidating materials, preserve the full master and compare silhouette, UVs and deformation after conversion.
3. **Add an animated visual format.** Define bounded joints, weights, tracks and materials, or an explicitly sampled animation format with a measured memory budget. Use native simulation time and directions. The current static town file cannot encode this data.
4. **Bind native monster states.** Preserve the engine's stand/walk/attack/hit/death/special timing, damage events, footpoint, picking, collision, AI, saves and multiplayer behavior. A visual clip must follow simulation state rather than introduce new gameplay timing.
5. **Validate in the game.** Exercise every mapped state and direction, rotations, deformation, lighting, texture seams, occlusion, reload and malformed-data fallback. Compare native-camera frames as well as 360 views and measure actual runtime cost. Catalog information or a high-resolution thumbnail is not this validation.

These are proposed import stages. No animated-boss importer or source download was implemented by this research.

## Provenance alongside the existing workflow

Use a sidecar reference manifest per existing catalog asset ID. This can accompany a contribution now without changing the engine, generator or registry schema. Record:

- Source kind: original game, supplemental mod, catalog model, generated interpretation or authored contribution; title, author, version, official URL, license and date checked.
- Local source-file hashes and the reference object's original identity/layout. Keep private archive paths and extracted art out of the public repository.
- Capture dimensions, crop bounds, camera, pose/frame, filtering and palette/lighting settings. Record scaling, background removal and other transformations explicitly.
- Intended use: shape, material, lighting, animation or unseen-side interpretation; which facets are canonical and which are inferred or approved design changes.
- Ordered generation inputs, their hashes and roles. Retain the original composition as the primary input when matching it; a generated back view remains a generated interpretation even when used as an input to a later model.
- Derived model/texture hashes, conversion settings, license/attribution chain and separate native-camera, orbit and simulation evidence.

The relevant integration points are:

| Existing file | Current responsibility | Reference manifest relationship |
|---|---|---|
| [ASSET-CATALOG.md](ASSET-CATALOG.md), [registry.json](../assets/registry.json) | Stable identities, review status and source bindings | Attach the source record to the existing asset; avoid duplicating a model identity |
| [MESHY-WORKFLOW.md](MESHY-WORKFLOW.md) | Original composition, inferred views, local candidates and review | Declare each input's source and intended evidence |
| [meshy_assets.py](../tools/meshy_assets.py), [meshy_multiview.py](../tools/meshy_multiview.py) | Input hashes, request options, task/cost metadata | Add semantic provenance in a sidecar; a file hash alone does not state rights or edition |
| [inspect_glb.py](../tools/inspect_glb.py), [render_glb_views.py](../tools/render_glb_views.py) | Source geometry/material inspection and explicit projections | Link the downloaded source and derivatives to the inspection record |
| [export_town_model.py](../tools/export_town_model.py) | Explicit static fit and bounded runtime conversion | Carry source, license and modification records with the export manifest |
| [compare_native_views.py](../tools/compare_native_views.py), [town_view_smoke.cpp](../tools/town_view_smoke.cpp) | Unregistered reference comparisons and runtime checks | Name the canonical source and exact fixtures separately from supplementary previews |

Reference quality, license status, technical compatibility and artistic approval are separate records. The original-camera and native-collision requirements continue to apply when a new reference source is introduced.
