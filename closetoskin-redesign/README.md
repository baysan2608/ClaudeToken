# Closetoskin redesign

Three versions exist:

| Version | What it is | Where |
|---|---|---|
| Today (v1) | The live Shopify store | https://www.closetoskin.com |
| Version 2 | Fast, SEO-ready rebuild | git commit `3e4933c` (local tag `v2-redesign`) |
| Version 3 | Editorial, fibre-led redesign (this code) | latest commit on this branch |

Version 3 adds:
- art-directed images from the brand's own photos (`art.py`)
- Bodoni Moda / Hanken Grotesk / IBM Plex Mono, self-hosted and subset
- a "Find your layer" guide, a warmth scale based on wool content, and care labels with composition and care symbols
- "Complete the layer" suggestions, Bestsellers and Gifts in the menu
- entrance and scroll motion that respects reduced-motion settings

See RESEARCH.md for the benchmark brief behind these choices.

A faster, SEO-focused rebuild of [closetoskin.com](https://www.closetoskin.com), built from the store's real catalogue. See **[AUDIT.md](AUDIT.md)** for the analysis, before/after scores and next steps.

## Run locally

```bash
python3 build.py                        # writes the static site to dist/
python3 -m http.server 8000 -d dist     # open http://localhost:8000
```

Build switches (environment variables):

- `NOINDEX=1` builds a preview copy that search engines won't index. Use it for any staging URL.
- `INTERNATIONAL=1` turns the export messaging back on (tax-free abroad, UPS delivery to Europe and worldwide). Only use it once international shipping zones are enabled in Shopify. Today the checkout only returns rates for Norway.
- `SITE_URL=…` sets the canonical domain. Default: `https://www.closetoskin.com`.

The build also writes `_redirects` (old Shopify URLs → new pages) and `_headers` (caching and security). Netlify and Cloudflare Pages read both automatically.

`screenshots/mobile-walkthrough.mp4` is a 50-second phone walkthrough: home, menu, collection, product, bag, search.

## How it fits together

- `data/catalog.json` is a snapshot of the store's `/products.json` (21 Oct 2026) plus each product's material.
- `build.py` generates every page, the structured data, `sitemap.xml`, `robots.txt` and the search index.
- `src/styles.css`, `src/app.js` and `src/fonts/` are the only front-end assets.
- Images are served by the existing Shopify CDN. Checkout goes to the existing Shopify checkout through cart permalinks. Newsletter and contact forms post to the Shopify store.

## Going live

Pick one:

1. **Recommended:** keep Shopify and port this design into a theme. Checkout, inventory and the admin stay as they are, and the URLs already match.
2. Host `dist/` on Netlify, Cloudflare Pages or similar, with checkout still on Shopify. Rebuild whenever products or prices change. `catalog.json` is a snapshot, not live stock.

Only deploy this with the brand owner's permission.

## Private review copy

`python3 preview.py` (or `python3 preview.py preview-v3 v3`) writes `preview/`, a self-contained review copy for sharing as a private claude.ai Artifact. It shows a "preview" banner on every page, turns off checkout and forms, uses relative links, inlines CSS and JS, embeds the exact font files, and bundles the product photos. `preview/` is git-ignored; rebuild it whenever the site changes.
