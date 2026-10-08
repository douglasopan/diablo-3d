# D3D website

Static development journal in Brazilian Portuguese and complete English for the whole of Diablo 1 in 3D, including every level. Tristram is the current prototype stage. Portuguese pages use the project root; English equivalents use `/en/`. See [the publishing guide](../docs/DEVLOG.md) and [capture provenance](CAPTURE-INVENTORY.md).

- `build.py` reads versioned articles and generates all HTML at build time.
- `content/en/devlog/` contains a complete English Markdown translation of every published article in `docs/devlog/`, with the same filename. Titles, descriptions, alternative text and full bodies are translated; canonical slugs, dates, update dates, images, categories, order and publication status match the original.
- `public/` is the only directory copied into the Pages artifact.
- `evidence.json` describes curated, visually audited real captures.
- `translations.py` supplies complete English interface strings and the `EVIDENCE_EN` gallery overlay. The overlay translates titles, alternative text and captions without changing capture files, source references, original hashes or duplicate provenance.
- `design-references/v2/` contains five revised imagegen boards covering every page, desktop/mobile and interaction states, plus exact prompts. The original four boards are preserved for comparison. These are design concepts, excluded from deployment.
- `verify.py` checks the actual output, including local links/fragments, metadata, feeds, assets and deduplication.
- `serve.py` provides a local preview with the production path prefix.

The default URL is `https://douglasopan.github.io/diablo-3d`. Set `SITE_URL` when deliberately changing the host or project prefix; every local URL, canonical and feed uses it.

No client-only content fetching, third-party analytics, externally fetched fonts or runtime framework. Menu, filters and the image dialog progressively enhance the static HTML. Captures are kept in full in thumbnails and the viewer, including defects and before/after labels. Original game data and derived model files are excluded. Ambient illustrations are labelled separately. Parallax honors reduced-motion preferences.

The PT/EN selector links to the equivalent page and preserves article section anchors. A selection is remembered locally when browser storage is available. On a Portuguese page without a saved choice, an English primary browser language opens the English equivalent. Direct `/en/` links work independently, and both languages remain readable without JavaScript. Each language has its own HTML, metadata and RSS; canonical and alternate-language links identify the corresponding pages.

Every new published article requires its full English translation before the bilingual build can pass. Keep heading levels, count and order aligned with Portuguese: the publisher maps English heading IDs to the original anchors so section links survive switching languages. Preserve the evidence links, numerical results, historical context and limitations; a translation must not turn planned work into completed features.

The identity uses the owner's original transparent Diablo 3D animation: 240 RGBA PNG frames at 640×160, 30 fps and an eight-second cycle, converted to animated WebP with alpha preserved. Header and footer use the static WebP; the home hero offers the animation with a play/pause control. The static image is the fallback without JavaScript and the initial state when reduced motion is requested. The Mason lettering is already part of the original logo artwork; no Mason font file is embedded.

Headings use the user-supplied Diablo WebFont, while body text stays in Manrope. Existing Cormorant and Manrope font files, source records and OFL texts remain in `public/assets/fonts/`.

The support page prefers direct contact with the authorized Discord handle, exposes the authorized PayPal recipient with a copy action, and opens the official PayPal site. It processes no payments and embeds no payment API or credentials. No Asaas account link was supplied; that method is coordinated by contact. Fees are described as dependent on the payment service/method, not on API usage. Platform credit assistance is coordinated rather than promising transferable balances.

Only website, devlog and Pages workflow paths belong to this task; engine changes remain in their own workstream.
