# D3D website

Static, Portuguese-first development journal for the complete Diablo 1 in 3D. Tristram is the current prototype stage. See [the publishing guide](../docs/DEVLOG.md) and [capture provenance](CAPTURE-INVENTORY.md).

- `build.py` reads versioned articles and generates all HTML at build time.
- `public/` is the only directory copied into the Pages artifact.
- `evidence.json` describes curated, visually audited real captures.
- `design-references/v2/` contains five revised imagegen boards covering every page, desktop/mobile and interaction states, plus exact prompts. The original four boards are preserved for comparison. These are design concepts, excluded from deployment.
- `verify.py` checks the actual output, including local links/fragments, metadata, feeds, assets and deduplication.
- `serve.py` provides a local preview with the production path prefix.

The default URL is `https://douglasopan.github.io/diablo-3d`. Set `SITE_URL` when deliberately changing the host or project prefix; every local URL, canonical and feed uses it.

No client-only content fetching, third-party analytics, externally fetched fonts or runtime framework. Menu, filters and the image dialog progressively enhance the static HTML. Captures are kept in full in thumbnails and the viewer, including defects and before/after labels. Original game data and derived model files are excluded. Ambient illustrations are labelled separately; fonts are self-hosted with their licenses. Parallax honors reduced-motion preferences.

The support page prefers direct contact with the authorized Discord handle, exposes the authorized PayPal recipient with a copy action, and opens the official PayPal site. It processes no payments and embeds no payment API or credentials. No Asaas account link was supplied; that method is coordinated by contact. Fees are described as dependent on the payment service/method, not on API usage. Platform credit assistance is coordinated rather than promising transferable balances.

Only website, devlog and Pages workflow paths belong to this task; engine changes remain in their own workstream.
