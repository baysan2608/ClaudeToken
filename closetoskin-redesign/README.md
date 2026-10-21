# Closetoskin redesign

A faster, SEO-focused rebuild of [closetoskin.com](https://www.closetoskin.com), built from the store's real catalogue. See **[AUDIT.md](AUDIT.md)** for the analysis, before/after scores and next steps.

## Run locally

```bash
python3 build.py                        # writes the static site to dist/
python3 -m http.server 8000 -d dist     # open http://localhost:8000
```

`NOINDEX=1 python3 build.py` builds a preview copy that search engines won't index. Use it for any staging URL.

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
