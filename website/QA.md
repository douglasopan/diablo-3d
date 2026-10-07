# Website verification · 2026-10-07

The final implementation follows the five revised imagegen boards in `design-references/v2/`, created before implementing the revised full-game direction. They cover every page, desktop/mobile, menu, empty search, filters, expanded images and support copy/FAQ states. Original boards are retained for comparison.

## Completed checks

- Eight main pages, ten complete articles and a custom 404: 19 HTML documents with one `h1` each, descriptions, canonical URLs, OpenGraph and valid JSON-LD.
- All local links, fragments and public images resolve. Sitemap includes all 18 indexable pages; RSS includes all ten published posts.
- Six authoring tests pass: drafts, metadata, duplicate slugs, missing assets, path traversal and active/unsafe content.
- All 18 indexable pages opened at effective 320 px. One five-column article table initially overflowed and was corrected to scroll inside its own bounds; recheck passed. The ambient art label was removed from the masthead flex flow, restoring proper title margins.
- All eight main pages and two articles checked at 768 px; all main pages and the latest article at 1309 px. No document overflow or broken loaded images.
- Search `iluminacao` returns one result, an unmatched term displays the empty state, reset restores results and Luz displays three articles.
- Gallery Luz shows 13 captures. Dialog advances from 1/13 to 2/13 and Escape closes it. Mobile menu opens, closes with Escape and restores focus. No captured browser warnings/errors.
- Support copy action shows `E-mail copiado.` after an explicit click. Public email remains selectable without JavaScript. Clipboard fallback selects the email and gives manual copy instructions.
- System reduced-motion preference was active during browser QA: backgrounds correctly had no transform. Static review confirms scroll-driven parallax is bounded at ±46 px desktop/±14 px mobile, with requestAnimationFrame, passive listeners and no continuous loop. The system preference was preserved.
- Captures remain whole, including comparison labels and defects. Gallery sheets load lazily. All 37 selected images were inspected, converted to lossless WebP and decoded to verify original pixels; their original hashes are distinct.
- Only `website/public/` is copied to the publication artifact. Design boards, authoring scripts/posts, source/models, private inventories and QA screenshots are excluded. Fonts are local with licenses; two decorative ambient illustrations are labelled and have prompts in `design-references/ATMOSPHERE-PROMPTS.md`.
- Scoped text scans found no credentials or private machine paths. No credential files were read. This workstream did not edit or build the game engine.
- Scope review confirms all Diablo 1 levels are the objective, Tristram is the current stage and dungeon reconstruction is planned. Latest recorded game version is `cdeaaab0d`; historic `63e5e749` evidence remains labelled.
- Support prefers direct Discord contact and exposes the authorized PayPal recipient. No payment API, fabricated recipient URL, Asaas link, QR code, fee guarantee or transferable-credit guarantee is included.

Screenshots remain in ignored `website/qa/`. GitHub Pages uses GitHub Actions; live deployment is verified after pushing the site commit.
