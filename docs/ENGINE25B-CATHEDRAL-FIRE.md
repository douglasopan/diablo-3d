# Engine25B — Cathedral fire diffuse and geometric shadows

Private candidate validated on16October2026. Source `aa8a84a945d37084f95c82e72011b854c8d5a308caf89f2981942ef8845ebd54` derives from delivered25A `22a64e1b…`; this describes `diagnostics/engine25b-cathedral-fire-shadow-20261016-r1/candidate-source`, not the main working tree or a combined23/24release. `HANDOFF.md` and `DELIVERY.json` in that directory bind source, normal executable, separate fixture, assets, profiles and receipts.

## Ownership and sequence

`town_view.cpp` owns the frame flag, diagnostic controls, current source intensities and completed CPU field. `cathedral_fire_lighting.inc` prepares it after native/art texture preparation and before GPU Begin. It validates actual native brazier/skull fires, chooses up to4 near the native player and uses the exact flame anchors already drawn by25A. Render-time flicker is deterministic and does not consume native RNG.

A cache miss calls the actual structural draw traversal with `CollectingCathedralFireCasters` set. `DrawTriangle` intercepts world vertices/UVs before camera clipping, rasterization, GPU recording or picking. It keeps actual art suppression/replacement/thin-wall/stair surgery and coverage masks; skips camera cutaway/frustum; forces authored/door resident meshes through their corresponding transformed triangle path. Objects, actors, native billboards and particles are excluded. No SOL box or old collision shell stands in for a visual aperture. Collection restores host draw state and releases its borrowed masks/scratch before returning.

`town_point_shadow.hpp` owns no native state. It validates/builds a bounded BVH and six64² axial-depth faces per source transactionally. Opaque hits use the same UV repeat/clamp/outside-opacity policy as the draw. Maximum field384KiB,131072triangles,512masks,32MiBcore scratch and fixed ray-work limits. Failure clears the whole field. Sampling uses distance/normal attenuation and same-face clamped3×3PCF with explicit depth/slope bias. Reflected original unlit albedo is added in linear RGB; index/alpha/depth/pick remain independent.

GPU setter `TownGpuSetFirePointLighting` runs afterBegin and before geometry. It validates/copies the completed atlas to t11, uploads source constants to b3 and caches immutable depth by identity/revision/content. Both projected and resident paths bind that same field. Only optant structural C1 materials set flag16384. Enabling resident vertex normals is limited to that lighting path; existing palette/index contracts remain intact.

## Lifecycle

- Frame start, native fallback and refused publication clear readiness/current sources/publication. GPU Begin/discard clear the binding and frame eligibility.
- CPU cache identity includes live epoch, geometry revision, selected source slots/poses/radii, region selection, visibility and transparency, admitted resource resets and frozen door progress. It excludes camera pose, native brightness and flicker energy. No borrowed texture or native object reference is retained in a completed map.
- Full resource/native-material reset frees maps/key; full GPU reset frees atlas/buffer/context/device.25A CPU shader bytecode cache retains its existing bounded policy. Preferences are preserved; diagnostic toggles reset separately and follow the existing Smooth3DLighting setting.
- `GetTownViewCathedralFireState` only marks publication when the current world/picking frame is valid. Shadow collection must remain before any future selectable-bounds tracking.

## Evidence and limits

Original guard unchanged: core24739, cacheCPU42, cachehardware24, point-lightGPU4697 and native89161checks; MPQ-only installation/reuse/changed-scene refusal also passed. Native checks include4frozen C1 views before/afterTown↔C1 and actual hardware world readbacks960×640 in4views, comparing lightOFF/shadowsON/shadowsOFF and full indexed/depth/picking/native witnesses.36captures and inspected comparison are retained.264assets/defaults/cena/helper and audio bytes remain25A-exact; PE/PDB, ZIP CRC/hashes and DouglasPan tags verified. Current25 INI/saves/stash were copied byte-exact into a separate profile at delivery.

These are bounded render/readback tests, not sustained FPS or full GPU SDL/UI gameplay qualification. Actor/loose-prop/particle shadows, all other fire families, native doors exercised through actual open/close transitions, artistic360°approval, physical penumbra, GI and new PBR are outside this evidence.64²PCF has a one-texel face-edge limitation.23/24were not merged; their eventual integration requires a new source/asset closure and its own gates.
