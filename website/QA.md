# Website verification · 2026-10-07

The final implementation follows the five revised imagegen boards in `design-references/v2/`, created before implementing the revised full-game direction. They cover every page, desktop/mobile, menu, empty search, filters, expanded images and support copy/FAQ states. Original boards are retained for comparison.

## Completed checks

- Eight main pages, ten complete articles and a custom 404 in each language: 38 HTML documents with one `h1` each, descriptions, canonical URLs, OpenGraph and valid JSON-LD. English articles contain the full records, not summaries.
- All local links, fragments and public images resolve. Sitemap includes all 36 indexable pages with language alternates; each language's RSS includes all ten published posts.
- Ten authoring tests pass: drafts, metadata, duplicate slugs, missing assets, path traversal, active/unsafe content, required English translations, matching evidence/editorial metadata, preserved section anchors/local links and complete heading structure.
- All 18 indexable pages opened at effective 320 px. One five-column article table initially overflowed and was corrected to scroll inside its own bounds; recheck passed. The ambient art label was removed from the masthead flex flow, restoring proper title margins.
- All eight main pages and two articles checked at 768 px; all main pages and the latest article at 1309 px. No document overflow or broken loaded images.
- Search `iluminacao` returns one result, an unmatched term displays the empty state, reset restores results and Luz displays three articles.
- Gallery Luz shows 13 captures. Dialog advances from 1/13 to 2/13 and Escape closes it. Mobile menu opens, closes with Escape and restores focus. No captured browser warnings/errors.
- Support copy action shows `E-mail copiado.` after an explicit click. Public email remains selectable without JavaScript. Clipboard fallback selects the email and gives manual copy instructions.
- System reduced-motion preference was active during browser QA: backgrounds correctly had no transform. Static review confirms scroll-driven parallax is bounded at ±46 px desktop/±14 px mobile, with requestAnimationFrame, passive listeners and no continuous loop. The system preference was preserved.
- Captures remain whole, including comparison labels and defects. Gallery sheets load lazily. All 37 selected images were inspected, converted to lossless WebP and decoded to verify original pixels; their original hashes are distinct.
- Only `website/public/` is copied to the publication artifact. Design boards, authoring scripts/posts, source/models, private inventories and QA screenshots are excluded. Fonts are local: the existing OFL notices remain and the supplied Diablo font retains its embedded metadata and source notice. Two decorative ambient illustrations are labelled and have prompts in `design-references/ATMOSPHERE-PROMPTS.md`.
- Scoped text scans found no credentials or private machine paths. No credential files were read. This workstream did not edit or build the game engine.
- Scope review confirms all Diablo 1 levels are the objective, Tristram is the current stage and dungeon reconstruction is planned. Latest recorded game version is `cdeaaab0d`; historic `63e5e749` evidence remains labelled.
- Support prefers direct Discord contact and exposes the authorized PayPal recipient. No payment API, fabricated recipient URL, Asaas link, QR code, fee guarantee or transferable-credit guarantee is included.

Screenshots remain in ignored `website/qa/`. GitHub Pages uses GitHub Actions; live deployment is verified after pushing the site commit.

## Bilingual reading and original branding

- All 18 indexable English pages and all 18 Portuguese equivalents checked at effective 320 px with the Diablo heading font. No document overflow or clipped visible headings after reducing mobile masthead sizes. All eight main English pages also checked at 768 px and 1309 px.
- The PT/EN selector opens the corresponding page. Selecting English and reopening the Portuguese root returns to English. Article contents links followed after load update the selector's fragment; switching to Portuguese preserves the same section ID. Explicit Portuguese choice is retained.
- English search `candles` returns `1 result`; an unmatched term displays `0 results`, the translated empty state and `Clear filters`. Copying the public PayPal email displays `Email copied.`. The translated gallery preserves every capture and source hash.
- The owner's original logo has no opaque rectangular background, with Mason letterforms already rendered in the artwork. Header and home hero now use the animated WebP directly in HTML, with no animation control. Footer keeps the static WebP. Headings use the local Cormorant Garamond serif; body text remains Manrope.
- Encoding verification decodes all 240 frames and compares every alpha channel with the original RGBA sequence. Frame count, infinite loop and total duration of 8,000 ms pass. The static fallback is lossless; animated colors use WebP quality 86. Source hash and output hashes are recorded in `branding.json`.
- The animation loads successfully in Chrome at 640×160. Following the owner's revised request, header and home hero animate automatically, including without JavaScript. Logo animation is independent of reduced-motion preference; parallax continues honoring it. The operating-system preference was preserved.
- Discord's ten informational channels have Portuguese and English guidance, with full-game scope and current-stage limitations preserved. Screenshot: `qa/discord-english.jpg`. No new bot, webhook credential or privilege was introduced for this bilingual update.

## Readability correction

- Restored Cormorant Garamond for every heading after the owner rejected Diablo Heavy's decorative cross-shaped letters. Diablo WebFont remains an unused historical source; current CSS does not request it.
- Removed the animation toggle, its labels, wrapper and JavaScript. Both requested placements use the same cached animated image resource and its original eight-second loop.
- Reviewed Portuguese desktop/mobile home, English mobile home, devlog and the latest complete article. The serif loads, headings remain readable, no document overflow occurs and the header/home images are loaded from the animated source, with zero animation controls.
- Styles and scripts have content-derived version parameters so a subsequent page load requests revised files instead of reusing an old cached heading font or animation control.

## GPU renderer record — October 8, 2026

- Added the complete Portuguese/English GPU article, bringing the artifact to 38 indexable pages plus two 404 pages, with 11 full articles per language. Build, ten authoring checks and artifact verification pass; both RSS feeds, metadata, local links and fragments include the new record.
- Current home, technology, roadmap and devlog wording now follows public game commit `c8329403e`. Historical articles and all 37 gallery captures retain their original context. The reused v4 article image is explicitly labelled historical, with no CPU/GPU comparison claim; no new local capture or private file was added.
- Reviewed the new article and its results table on desktop and at effective 320 px, plus both language versions of the revised technology blocks. No document overflow or clipped headings. Numeric table values keep their units together. Switching PT to EN from the results section preserves the original section anchor.
- English interface review covers all new visible nodes and labels, including roadmap text split by evidence links. GPU remains optional and Windows-specific; the public default is off. World drawing time includes GPU readback and excludes UI/SDL, so the 5.2 factor is kept separate from in-game frame rate. CPU fallback, synchronous readback and CPU shadow-map construction remain explicit.

## First gameplay feedback — October 8, 2026

- Updated the existing GPU article in both languages, retaining its slug, publication date and five section anchors. The owner's literal Portuguese report, “melhorou muito!!”, and its explicitly identified English translation record perceived performance improvement after testing the game. Sustained FPS and complete artistic approval remain pending.
- The existing controlled Full HD result is shown consistently as 154.00 ms CPU and 29.58 ms GPU, using the precision reconfirmed by the project lead. No new benchmark or FPS result is claimed.
- Added a devlog highlight linked to the same article; home cards, metadata and RSS use its revised description. Checked the highlight in both languages at effective 320 px, the Portuguese desktop article report and the complete English report. No document overflow; both highlight links open the existing article.
- Build, ten authoring checks and verification pass with 40 HTML files, 11 articles per language and the same 37 captures. No duplicate article, new local capture or private asset was published. The engine's door-window correction remains outside this publication.

## External Godot editor record — October 8, 2026

- Added a complete Portuguese/English article about public game commit `e17b77f03`, bringing the artifact to 40 indexable pages plus two 404 pages and 12 articles per language. Ten authoring checks, the build and artifact verification pass, including both RSS feeds, metadata, local URLs and section fragments. All 37 curated captures remain unchanged.
- Updated the devlog highlight, technology panels and roadmap to describe the delivered external static-architecture editor and validated export bridge. The earlier GPU gameplay report remains linked. The game continues in DevilutionX; terrain/collision editing, authored-light export and a complete Godot port are outside this delivery. The cabin with lighting/fire remains protected by the initial export format.
- Reviewed the Portuguese desktop article, both article tables and language switching at effective 320 px, and the new technology blocks on desktop/mobile. No document overflow or clipped headings. Switching languages from the validation section retains its canonical section fragment. The editor guide points to the documented game revision.
- An independent factual review clarified launcher checks before game launch, architectural triangle counts and synthetic exporter fixtures. The synthetic well replacement is identified as an integration test; the complete C++ regression duration is not a frame-performance result. The door-window correction does not imply a regenerated model or complete artistic approval.
- The article reuses an already public image with an explicit historical caption. No new game screenshot, local model, texture, private scene or payment/API credential was added to the publication.

## Background music — October 8, 2026

- Added the owner's explicitly supplied track and a compact self-hosted Plyr 3.8.5 player to all Portuguese/English pages. Play/pause, mute and volume controls work; the owner also confirmed playback during the review. MIT attribution remains with the vendor files and the native audio controls remain the fallback.
- The published MP3 is 233.32 seconds and 5,484,096 bytes. Embedded artwork and private tags were removed without recompressing the audio. Full decoding passes and decoded PCM matches the supplied original. The master remains unchanged.
- Browser checks cover actual playback, looping enabled, the initial autoplay-blocked prompt, mute/unmute, saved volume, pause across language changes, playing across navigation and restoration after Back. A paused-page metadata/seek race was corrected by preserving the pending position until seek completion; the final check resumed from 186.73 seconds to 192.95 seconds after switching languages and pressing Play.
- The player fits at effective 320 px and desktop width, with no document overflow. Controls and state messages are translated. Keyboard shortcuts are limited to the focused player. Space at the end of the document keeps footer content accessible.
- Build and artifact verification pass with the same 42 HTML documents, 12 articles per language and 37 captures. The new audio file and vendor license are explicitly allowed by path; no broad audio-folder publication exception was added. Playback preferences use local browser storage, and automatic playback remains subject to browser policy.


## Discord follow actions — October 9, 2026

The owner requested a visible Discord follow action before distributing the final devlog video. Home now puts **Acompanhe via Discord / Follow our progress on Discord** above the existing devlog and optional support actions. The primary gold action links directly to the existing official Discord invite; it does not open another dialog.

The shared desktop navigation places Discord with its real brand icon after Gallery. Mobile places exactly three direct links between the animated logo and Menu, in this order: Discord, the verified official r/Diablo3D community, and GitHub. Each icon has a localized accessible link name and a 44 × 44 px touch area. The existing language selector, menu behavior, optional community invitation and footer links remain available. The new icons are official Bootstrap Icons under the MIT license, with the full notice distributed in the HTML; there is no external icon runtime.

Reviewed Portuguese and English home at desktop width 1280 and mobile widths 320, 360 and 390 px after layout settled. The document has no horizontal overflow, the header controls do not overlap, the Discord action is centered above the two previous actions, and the logo remains animated. Opening and closing the mobile menu leaves all three social links visible. Static anchors retain their real destinations without requiring the community JavaScript handler. These checks establish layout and link markup; they do not measure traffic, membership growth or conversion.

The existing suite passed 37 tests with one Node-dependent test skipped on this Windows runtime; the build and artifact verification passed with 66 HTML documents, 22 articles per language and the same 51 curated captures. No extra popup, analytics provider, tracking event, contact collection, payment integration or claim of a public game build was added. The final devlog video and social posts remain private until the site deployment is verified; their publication also requires the separate video handoff and credits.
