# Items and new HUD — media provenance

The bilingual article `itens-e-novo-hud`, dated 8 October 2026, displays nine generated item concepts, one generated HUD concept and one contextual offscreen HUD composition. These roles remain explicit in both languages and in the image viewer. This is an article gallery; these concepts are not classified as gameplay captures in the site's capture inventory.

[devlog-media.json](devlog-media.json) records every original SHA-256, public derivative SHA-256, dimensions, role and source. All eleven derivatives are lossless WebP at the original resolution. Their complete decoded pixels were compared with the originals. No image was cropped, composited, retouched or generated again for this article.

## Item concepts

The immutable source is commit [`f0102dc1b0c9075fa98906b3f38d82c09418644b`](https://github.com/douglasopan/diablo-3d/commit/f0102dc1b0c9075fa98906b3f38d82c09418644b), specifically [`docs/items-study/concept-art.json`](https://github.com/douglasopan/diablo-3d/blob/f0102dc1b0c9075fa98906b3f38d82c09418644b/docs/items-study/concept-art.json) and the nine PNGs in `assets/concept-art/items/r1/`.

The article includes Short Sword, Dagger, Falchion, Scimitar, Broad Sword, Potion of Healing, Potion of Mana, Gold and Scroll. Each is a candidate multiview concept under review. The source manifest declares artistic approval false, model/icon not generated and runtime installation false. Generated thumbnails within the boards are concept previews, not final game icons. Hidden faces and thickness remain proposals to reconcile during review and modeling.

The frozen catalog has 168 base records (166 named), 111 combined unique identities, 170 declared cursors (136 main and 34 Hellfire) and 228 proposed design/family/variant units. Nine units have linked candidate concepts; 219 remain pending. Units are not mandatory independent meshes. A future equipped/ground/inventory pipeline using the same accepted master remains a proposal.

Only generated output images are copied into the website. Licensed local reference images, extracted sprites, MPQ/CLX files and private reference paths are excluded.

## HUD concept and technical composition

- `hud-native-16x9-v1.webp` is a lossless copy of the generated concept `docs/hud-study/images/07-hud-native-16x9-v1.png`, pinned to the source commit above. The board itself labels its conceptual status. It is not a game capture or proof that every detail is implemented.
- `hud-hd-offscreen-fullhd.webp` comes from the integrator's frozen `final-production-03/home-1920-fill50.png` diagnostic. Its original SHA-256 is `98b20463c2a1b656c9df156e8db3a476bb83b5b8e3d623d93269c440eba30f51`, and its bytes match the same case in `final-production-01` and `final-production-02`. The full 1920×1080 composition retains the reference world and real code/assets; no isolated original game sprite or texture is distributed. Visual inspection found no private paths or personal data.

The technical receipt records production hooks, 278 checks and 13 presented cases, with interaction rectangles, artwork and private fixture files preserved. It explicitly declares `gameWindowCapture=false`, `runningGame=false` and `visualApproval=false`. Private profiles, models, logs and complete private receipts are not copied into the public website.

The six-piece author skin remains a technical candidate. The final R3 comparison independently confirms all 15 PNGs byte-identical and RGBA pixel-identical to the first run, including compact logical GPU presentation at Full HD. This is finite composition evidence, not a sustained FPS measurement.

The separate installation receipt `installed-20261008T120858Z` confirms installation on 8 October 2026. Three executable aliases have SHA-256 `a6e78ae4301b2107572bfa54a90e060c1d7286640f3e83314c72c0c3a24e752e`; 39 profile files and the launcher were preserved, with no game running during installation. These facts were checked against the receipt and installed bytes. The screenshot alone does not prove installation. The integrator retains responsibility for the separate game-source commit. Final artistic acceptance and real-use evaluation remain pending. Godot previews, offscreen compositions and installed builds are separate states.

## Historical reuse in the Gameplay-page fix article

The bilingual `hud-paginas-jogabilidade` article reuses the existing `hud-hd-offscreen-fullhd.webp` unchanged as historical context. Its alternative text and caption state that the composition predates the Gameplay-page fix and does not show the new page-transition tests. No private diagnostic capture, new media file or evidence-inventory entry is published. The installed correction and its finite CPU/software-SDL tests are documented as text, pinned to game commit `1e28d7b5a5cc90f4c40d9d2dfb59ee61fd71e8b1`.
