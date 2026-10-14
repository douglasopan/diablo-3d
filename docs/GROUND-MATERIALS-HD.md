# Ground materials: private Tristram pilot

Updated 8 October 2026. **Recommendation: keep the native gameplay grid and give the ground a continuous visual material system, divided into small render chunks.** World-space XZ UVs provide detail; audited material masks preserve paths and banks; exact native coverage remains a separate input. Higher-resolution images alone do not resolve the current point sampling or the UV reset at each piece.

This is the explicitly authorized ground-material front, following the report of very low quality near the first-person camera and the subsequent request to compare a continuous surface against “a lot of squares.” It is a bounded G2 preparation/pilot while G1 review remains partial, not a competing roadmap or permission to apply assets. The main integration chat owns the execution guide, GPU integration, shared build, profiles and Git. No runtime selection is changed here.

## Delivery and ownership

New tools only: `tools/ground_material_reference.py`, `ground_material_validate.py`, `ground_material_pilot.py`, `ground_material_compare.py`, `ground_material_package.py`. This document owns the ground-material contract. Outputs are local under the already ignored `diagnostics/ground-materials-hd/20261008-r1/`; no original or generated media is added to the public assets.

Reuse registry IDs `tristram.terrain.native-ground` and `tristram.terrain.river-crossings`. Candidate family labels below are private material hypotheses, not new exhaustive catalog IDs. Selection and acceptance remain distinct under [ASSET-APPROVAL.md](ASSET-APPROVAL.md).

The front does not edit `town_view.*`, `town_gpu.*`, `town_scene.*`, models, options, CMake, launchers, profiles or another front's source. The GPU cache/buffer work, model/LOD format work and first-person profiling remain with their existing owners. No Meshy/Tripo generation, credit purchase or engine build was performed.

## What exists in the renderer

These observations are from the local source on 8 October; concurrent GPU work can change limits. Confirm them again when integrating.

| Current fact | Local source |
| --- | --- |
| Ground art is the first two `DPieceMicros` microframes, decoded as 64×32 pixels with explicit opacity | `town_view.cpp`, `BuildTileTexture`, around line 884 |
| Ground is a planar inverse projection of the full bitmap rectangle; alpha keeps the native stair-step diamond edge | `town_view.cpp`, `DrawGround`, around line 1661 |
| Native UV spans 0..1 on every piece, with diagonal axes of length √2 world units; source densities are about 45.25 and 22.63 texels/unit along those axes | `town_view.cpp`, `Quad`, around line 804 |
| CPU samples integer texels; GPU uses `Texture2D<uint>.Load`, one mip, and no anisotropic material sampling | `town_view.cpp`, texture sampler; `town_gpu.cpp`, shader/upload |
| Perspective-correct UV interpolation already exists | `town_view.cpp`, triangle rasterizer; `town_gpu.cpp`, perspective shader |
| Palette index zero can be opaque; the independent mask decides coverage | `town_view.cpp`, texture/opacity sampler |
| Imported albedo is lit in linear light then mapped to palette; native artwork contains painted lighting | [TRISTRAM-LIGHTING.md](TRISTRAM-LIGHTING.md) |
| Normal/roughness maps are not consumed by the current ground material | Current material/shader contract |

One native map unit is one `dPiece` cell coordinate. It is **not a proven meter**. At the native camera, `screenX=32(X−Z)` and `screenY=16(X+Z)−32·height`. The full legacy quad is:

```text
worldX = tileX − 1.46875 + coverageU + coverageV
worldZ = tileZ − 0.46875 − coverageU + coverageV
coverageU = (worldX − tileX − worldZ + tileZ + 1) / 2
coverageV = (worldX − tileX + worldZ − tileZ + 1.9375) / 2
```

Do not replace it with an ideal mathematical diamond without a coverage-equivalence test. A past idealized edge produced cracks. Preserve the ordering/ownership of overlapping coplanar bases, ground picking, and piece zero.

## Architecture comparison

| Choice | Close view and seams | Sampling and memory | Draw submission and preservation |
| --- | --- | --- | --- |
| Higher-resolution texture for each existing piece/atlas | Adds source detail, but retains per-piece UV resets and many repeated compositional units. Tile borders still need matching. A carefully packed atlas needs gutters at every mip | Requires decoded-color mips and filtering too. Enlarging hundreds of unique pieces spends memory on repeated material detail. Existing index/LUT data cannot be interpolated as colors | Lowest migration cost, good compatibility/reference fallback. An atlas can reduce bindings, but does not itself reduce geometry or draw calls. Native coverage remains separately required |
| **Small continuous ground chunks, XZ world UVs, material masks** | Preferred pilot direction. Detail crosses cell/chunk boundaries at stable scale. Native mask shapes keep roads and unknown regions. Repeat seams are at the master period, not every gameplay cell | Few resident masters plus compact per-chunk masks. Color mips, trilinear and bounded anisotropy. Separate coverage and ownership. Native holdout pixels remain exact | Persistent indexed chunks and stable shared material binding can reduce CPU work/submissions; measure with the GPU owner. Chunk size is a culling/update choice, not a new gameplay grid |
| Triplanar on actual relief | Useful candidate for steep authored banks/rock surfaces where XZ projection stretches. Adds no geometric relief by itself. On this flat plane it degenerates to a single dominant projection or unnecessary mixing | Three axis projections per contributing material before possible optimizations; normal/weight continuity needs review. Does not solve semantic boundary classification | Reserve for a later demonstrated slope/stretch defect. Do not apply it to the whole flat town or blur shore masks |

This is an engineering recommendation from the local contract and the isolated visual experiment. It is not a measured runtime performance result. No new renderer or virtual texturing is needed for this pilot: start with a small resident material set and measured chunk culling. Keep existing tile art as the compatibility fallback and exact original Home reference.

**Candidate geometry arrangement:** begin with 8×8 or 16×16 native cells per render chunk, persistent buffers and a stable material binding. A chunk may use a continuous planar mesh while coverage/owner fields reproduce the existing ground. Where exact overlap cannot be reproduced yet, batch the existing quads in that chunk with continuous UVs first. That intermediate step already decouples texture appearance from cell boundaries. Do not promise two triangles per chunk while still needing native partial coverage or per-piece ownership.

**Candidate texture budget, arithmetic only:** six 1024² RGBA8 layers with full power-of-two mips occupy 33,554,424 bytes, approximately 32 MiB, before masks/geometry/cache bookkeeping. Six 2048² layers would occupy approximately 128 MiB. At the same time, 503 unique 1024×512 RGBA8 piece textures with mips would be approximately 1.31 GiB. This last number illustrates duplicated material detail; it is not the current native memory cost, nor a required atlas design. Compression, only after supported/inspected, can change these totals.

The audit found a current per-texture cap of 4,194,304 texels and a 256 MiB GPU texture cache; verify after the cache front lands. A 4096² image exceeds that texel cap even though its edge dimension is legal. Do not consume the whole cache with ground or repeat the existing CPU fallback regression.

Microsoft documents filtering/addressing/LOD and anisotropy 1..16 in [D3D11_SAMPLER_DESC](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_sampler_desc). A texture array requires matching layer format, dimensions and mip count; that makes a common derived runtime size useful, while preserving differently sized masters separately. See [Direct3D 11 texture resources](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-resources-textures-intro). [DirectXTex filter flags](https://github.com/microsoft/DirectXTex/wiki/Filter-Flags) document explicit sRGB input/output handling for texture processing. These are transport/sampling references, not evidence that this engine already implements them.

## Reference audit and candidate masters

Original local game art was inspected before generation: the native whole-town isometric atlas, original floor RGBA pieces, a path/grass crop and a river/bank crop. The private reference manifest records input hashes, crops, transformations and provenance. The overview atlas is a composition reference with existing light/scenery overpaint, not a clean albedo capture.

Important defect in the editor reference: `ExportTownEditorGround` marks **every Solid or BlockMissile cell** as generic grass fallback, as well as replaced scenery. The actual renderer preserves native ground in solid cells not replaced by scenery. Therefore the editor export cannot establish river coverage or blocked material semantics. Raw floor overrides were used where the audited exports exist; fallback stays protected. No river or bridge was painted over in this pilot.

Observed native identities: near-black desaturated olive grass; mauve/gray-brown dirt; directional grass/dirt transition pieces; cold dark-blue water with stony muted margins. Distinct native “dry grass,” “mud” or “stone pavement” families have **not** been verified. Do not invent such bindings from a dark color, SOL flag or adjacent piece number.

Six new full-resolution masters were generated using the built-in imagegen tool, with the original references as material/palette guidance rather than edit targets. Prompts asked for top-down, opaque, seamless, clean basecolor with no painted light/shadow, no roads or shoreline shapes. Requested 2048² was **not** the returned size: all six real outputs are **RGB 1254×1254**. Keep their byte-exact originals. Runtime-size derivatives are 1024²; they are not replacement masters.

| Private candidate | Identity and current review note | Native assignment in pilot |
| --- | --- | --- |
| `grass-r1.png` | Sparse dark olive fibrous grass/soil; close blade identity still needs review | Explicit pure-grass occurrences only |
| `dirt-r1.png` | Compacted mauve-brown soil with small grit | Explicit pure-dirt occurrences only |
| `dry-r1.png` | Dry grass interpretation, visually close to grass candidate | None; possible variation, not verified native family |
| `mud-r1.png` | Damp fine-soil interpretation, visually close to dirt candidate | None; wetness/roughness not validated |
| `stone-r1.png` | Cold slate fragments in earth; not permission to pave existing paths | None |
| `bank-r1.png` | Fine silt/gravel substrate; **not water** | None; shore stencil pending |

Initial scale: period `[2,2]` native world units, origin `[0,0]`. Master density is **627 texels/world unit** on X and Z; the 1024² derivative is 512. Compare feature size near the camera and in the native view before fixing this scale. These numbers do not establish centimeters, grass species or measured physical roughness.

All six source/repeat contacts were visually inspected. Opposite-edge statistics did not trigger the advisory seam heuristic; this is not a tileability certificate. Large-scale repeating patches, grass fibers, and similarity between the soil variants remain artistic review items. No automatic edge repair, learned upscaling, normal generation or roughness inference was used.

**PBR status:** basecolor only. No normal, height, roughness or metallic map was created. Start with the planar geometric normal `(0,1,0)` and the existing engine light/shadow system. If a future normal is derived from a separately authored height field, retain that field and declare scale, tangent convention, method and amplitude. Deriving height from arbitrary color would be a heuristic, not physical validation. Roughness likewise requires authored or measured provenance and a shader that consumes it.

## Private comparable pilot

Entry point: `diagnostics/ground-materials-hd/20261008-r1/index.html`. Detailed viewer: `pilot-world/index.html`. Source prompts are in `prompts/`; originals in `masters/`; QA repetitions, seam crops and mips in `qa/`; hashes/provenance in `manifest.json`, `references/reference-manifest.json`, `pilot-input/pilot-manifest.json` and the comparison report.

Two actual exported regions were chosen rather than inventing a material classifier:

- Path: X55..66, Z67..77, 132 occurrences, of which 12 audited pure-dirt occurrences receive candidate detail.
- Grass: X40..47, Z72..79, 64 occurrences, of which 20 audited pure-grass occurrences receive candidate detail.

The other **164 occurrences** are native holdouts, including transition pieces, fallback and unknown materials. Native support masks are binary 64×32 alpha, restricted to explicit map occurrences. Water is not selected. Holding transitions native means their close detail remains low-resolution; this delivery must not be described as a complete HD town.

The comparator ray-samples the current floor projection and retains point-sampled native opacity exactly. It renders five fixed cameras: path isometric, path close, path closer detail, grass isometric and grass close. The original-angle orthographic view uses yaw45°/pitch35.2643897°, corresponding to the native ground projection ratio. It is an offline ground-only rendering, **not an actual original-backend or engine screenshot**. Scenery, palette quantization, engine lighting, triangle raster edge rules, interaction and runtime performance are outside it.

Each camera compares the same inputs in world UV and per-piece legacy UV modes, with nearest, linear-light trilinear and an explicitly approximate bounded anisotropic sampler. The latter demonstrates the footprint/LOD contract; it is not a D3D hardware benchmark or pixel-identity claim. A pixel slider keeps native on the left and candidate on the right.

The per-piece contrast maps the full square master to the original diagonal quad axes; its footprint/axis scale differs from the XZ period-two mapping. The report records both derivatives. It is a comparison of complete mapping choices, not a single-variable seam test or an equal-density performance benchmark.

**Observed result:** in the inspected close dirt and grass frames, selected support gains fine surface detail instead of enlarged native texels. The isometric path and all holdouts retain their original shapes. Abrupt interfaces between detailed pure pieces and untouched low-resolution transitions remain conspicuous: the pilot does **not** eliminate the visible patch boundaries of a partially classified town. Better audited transition masks and palette/light calibration are necessary before a complete continuous town is reviewable.

Validation: **13 synthetic contract checks passed**, including fallback protection, unsupported/alpha conservation, invalid-mask rejection, gain, different UV modes, inward feather and odd-sized linear mips. The real pilot rendered **5 cameras × 2 mappings × 3 samplers = 30 candidate frames** at 960×600. Every result reports zero changes outside support, zero unsupported opaque changes, zero alpha/coverage changes and zero protected-fallback changes. Source masters match their recorded hashes and remain unchanged. These are offline checks; no runtime frame-time, draw-call or game-input result is inferred.

For brightness comparison only, the pilot fits a scalar linear-luminance gain to pure native reference samples: grass `0.4144257725`, dirt `0.6922531068`. Those gains are recorded in the config. They compensate the mismatch between already-lit native art and clean basecolor in this unlit offline view; they are **not** new environment settings, a measured albedo recovery or approved engine light factors. Masters remain unchanged.

The prototype supports inward feathering within explicit support, but the actual pure-piece pilot uses width zero; native directional transition pieces stay unchanged. Future transition detail must use audited material weights across the original boundary, normalized and sampled consistently in world space. Never feather coverage or expand a land mask into water. At a chunk border, sample the same world field with a one-texel halo rather than independently normalizing each chunk.

## Sampling/material contract for integration

Keep these independent:

1. **Native coverage/ownership:** exact opacity and source ownership from live map state; palette zero remains a valid opaque color. Picking, collision and triggers remain native. Masks never create solid gameplay floor.
2. **Semantic weights:** audited material families/transitions, protected water and unsupported regions; versioned by piece/state and coordinates. No global color or SOL classifier. Legitimate quest/warp changes invalidate affected visual chunks.
3. **Detailed albedo:** repeated masters in XZ world coordinates. Read source sRGB as linear color for filtering, mip reduction, blending and lighting; map the final color to the game palette afterward. Do not interpolate integer palette indices or cached color codes.

Candidate mapping:

```text
detailUV = (worldXZ − materialOrigin) / materialPeriod
coverageUV = inverseNativeProjection(worldXZ, nativeTile)
weights = auditedMaterialField(worldXZ, livePieceState)
albedoLinear = sum(normalizedWeights * sampleDetailLinear(detailUV, derivatives))
colorLinear = engineLightAndShadow(albedoLinear, geometricNormal)
finalPaletteIndex = nearestPalette(encodeSRGB(colorLinear))
```

Unknown pixels follow the native already-lit path rather than being lit twice. How mixed/native boundaries are resolved is an integration fixture, not permission to multiply native art by the imported lighting curve. Separate native and albedo materials explicitly.

Provide linear-light mip chains down to 1×1 with dimensions `max(1,floor(previous/2))`; keep world derivatives across chunks and consistent addressing. A 1024² runtime layer gives eleven mip levels. Begin anisotropy at a tested bounded setting such as4 or8 and expose controls only after real behavior/budget/fallback exists. Avoid a negative LOD bias as a substitute for detail; evaluate temporal shimmer in movement.

Variant selection may use a deterministic visual hash of world coordinates/material revision, independent of simulation RNG. Preserve continuity when rotating or blending variants; validate derivative orientation and material scale. This first delivery contains swatch candidates and repeated sampling, not a runtime anti-repetition system.

## Reproduce without touching the game

Python 3.12, Pillow and NumPy were already available locally. All commands write only the private candidate directory. They do not launch a game or rebuild the shared engine.

```powershell
python tools/ground_material_reference.py --ground editor/godot/local/ground-reference.json --atlas <private-native-atlas.png> --output diagnostics/ground-materials-hd/20261008-r1/references
python tools/ground_material_validate.py "diagnostics/ground-materials-hd/20261008-r1/masters/*.png" --output diagnostics/ground-materials-hd/20261008-r1/qa --runtime-size 1024
python tools/ground_material_pilot.py --reference editor/godot/local/ground-reference.json --raw <private-raw-ground-shadow-sources-directory> --masters diagnostics/ground-materials-hd/20261008-r1/masters --output diagnostics/ground-materials-hd/20261008-r1/pilot-input
python tools/ground_material_compare.py --self-test --output diagnostics/ground-materials-hd/20261008-r1/comparator-self-test
python tools/ground_material_compare.py --reference diagnostics/ground-materials-hd/20261008-r1/pilot-input/reference.json --config diagnostics/ground-materials-hd/20261008-r1/pilot-input/config.json --output diagnostics/ground-materials-hd/20261008-r1/pilot-world --samplers nearest,trilinear,anisotropic --export-mips
python tools/ground_material_package.py diagnostics/ground-materials-hd/20261008-r1
```

Imagegen prompts and input hashes are preserved. Built-in output did not report a model identifier, so none is invented. Repeating a generative prompt is not byte reproducibility; deterministic preparation/comparison is reproducible from the preserved PNGs.

## Handoff to main / GPU / format-LOD owners

Concrete integration request, after review of this private set:

- Main: record this explicitly authorized front and its limited result in the execution guide; keep current selected assets and profiles. Schedule a **small isolated native-runtime** ground fixture, not a town-wide application.
- GPU: add an explicit decoded-color albedo material/sampler path, independent coverage, stable shared textures/arrays, linear mips and bounded anisotropy. Preserve RGB/ID/depth coherence and complete CPU fallback. Coordinate chunk/persistent-buffer transport with the existing buffer front; do not duplicate its implementation.
- Format/LOD: transport material revision/hash, source/dimensions/colorspace, origin/period, wrap/filter and mip policy, separate coverage/semantic fields, water exclusion and native-state invalidation. Keep masters and derived runtime sizes separate. D3DMESH1 alone cannot express this ground contract; do not hide it in an RGB atlas and claim the format supports it.
- Reference audit: classify directional transition and river/crossing stencils from raw native microframes plus actual state. Preserve the eight accepted ground-shadow cleanup masks; do not erase arbitrary dark terrain. Only then expand semantic coverage.

Runtime gate: same map/seed/actors/camera and selected models/light; actual forced-geometry native-angle, close, grazing and orbit captures; stationary plus motion shimmer; zero unsupported/coverage changes; exact opaque-black handling; unchanged native collision, water/crossings, ownership/IDs/depth and Home/F4; quest/warp states, reload and missing/invalid candidates; measured GPU-active frame stages, draw calls, texture/mip memory and cache headroom. The offline image pilot cannot close those gates.

No full-resolution native/reference media, generated masters, MPQs, saves, credentials or private diagnostics may be published with this document. Code/contracts and sanitized metadata can be reviewed separately. This set is **candidate review**, not artistic acceptance or an installed HD-ground feature.

## Coordination handoff — 8 October 2026

Dot coordinates the project; `DIABLOI 1 3D` (`01a1153a-cad1-7fb1-8326-225511ee918d`) remains the only integrator. This front's status is **private candidate delivery complete; artistic review and runtime integration pending**. No further generation, spend, application or renderer implementation is authorized by this handoff.

The shared checkout HEAD observed at handoff is `ae43f0470134af6bb470c68d20fa37647c8a0ec4`. The original generation-start HEAD was not recorded, so this is an observed handoff base, not a claim that the pilot ran against that engine commit. The six front-owned files are still untracked local candidates, not a commit or an integrated patch. Their exact hashes and artifact receipts are recorded in private `diagnostics/ground-materials-hd/20261008-r1/handoff-coordination.json`.

Front-owned files are this document and the five `tools/ground_material_*.py` tools listed above. Artifacts are the six 1254² masters, full prompts/reference provenance, explicit pilot inputs, source/runtime mip derivatives, 30 candidate frames, slider/gallery and comparison overview. Verified receipt SHA-256 values:

| Receipt | SHA-256 |
| --- | --- |
| Candidate package `manifest.json` | `fdcc417dc216affe2a33fb1966c68289f6cd1be9ce605f8acff0ac4db25c4297` |
| Real comparison `pilot-world/comparison-report.json` | `fac2ee957a811668e5728345d14cfe604621100ccbfe3a95b091519e8dc950ef` |
| Master QA `qa/report.json` | `260b09ba8759227a685a3d1e1ed8925775c647bffbbf0066385b1491629b7a64` |
| 13 synthetic checks `comparator-self-test/self-test.json` | `4dcf288aa31dbd4b6673b4f96c365b1becd14596d0cb7cd2af0edc06d9ac8ad8` |

Handoff recheck confirmed all six master hashes and that the comparison report names the current comparator tool hash. It did not rerun the engine or change prior receipts. The shared GPU source still declares a 256 MiB texture budget, 4,194,304-texel payload cap, one mip at upload and integer point loads; only read access was used to confirm these facts.

Budget request remains a **proposal**, not a reservation or approved runtime setting: six resident 1024² RGBA8 layers with full eleven-level mips ≈32 MiB, 512 texels/native world unit at a two-unit period, plus separately budgeted masks, coverage, LUT/cache bookkeeping and geometry. Preserve the 1254² masters at 627 texels/unit. Do not promote 2048² layers (≈128 MiB for six with mips) without measured headroom alongside existing architecture. The pilot currently uses only grass/dirt; runtime residency of all six is not required by this receipt.

Outstanding dependencies: visual review of candidate colors/blades and source-scale fit; audited transition and river/crossing masks; actual decoded-color ground material with linear mips/filtering before palette mapping; persistent chunk/batch transport coordinated with the GPU owner; measured runtime memory, frame stages, coverage/ID/depth, native state changes and fallback. The detailed/native boundaries remain visible in the restricted pilot. No normals or roughness exist, and no runtime PBR, water system, chunk draw-count reduction or FPS improvement is claimed.
