# closetoskin.com: audit & redesign

Audited 21 Oct 2026 with headless Chromium and Lighthouse 12 (mobile, simulated throttling), plus the store's own `/products.json`.

## Scorecard

| | Live site (home / product) | Redesign (home / product) |
|---|---|---|
| Lighthouse performance | 80 / 72 | **99 / 99** |
| Lighthouse SEO | 85 / 85 | **100 / 100** |
| Lighthouse accessibility | 92 / 92 | **99 / 100** |
| Largest contentful paint | 2.4 s / 4.1 s | 2.1 s / 2.0 s |
| Total blocking time | 550 ms / 480 ms | 40 ms / 0 ms |
| Layout shift (CLS) | 0.112 / 0 | 0 / 0 |
| Page weight | 1,180 KB / 1,132 KB | 341 KB / 213 KB |

The redesign was measured on a local server. Its images still load from the real Shopify CDN, so the numbers are close to what production hosting will give, but not identical.

## What the live site is

- Shopify, **Dawn theme v6.0.2** (2022, never updated). Behind a Cloudflare bot check.
- **107 products**: 56 underwear (NOK 245–1,040), 46 jewellery (NOK 675–8,250), 5 handbags (NOK 2,300–7,100).
- Norwegian business (Oslo, Sommerfugl Consult). English only, prices in NOK.

## SEO: main problems

1. **Homepage title is just "Closetoskin".** No keyword like "wool silk underwear" appears anywhere in the title or the description.
2. **No meta description** on the homepage or any collection page. 68 of 107 product pages have none either. Social share text is literally "Closetoskin", and the share image is the logo.
3. **Empty H1 on the homepage. No H1 at all on collection pages.**
4. **49 of 107 product names are internal codes**, for example `1R058c`, `Emily_SS` and `909 04 ML`. Nobody searches for those. **68 of 107 products have no description.** The product type is empty for every product.
5. **16 of 22 homepage images have no alt text.**
6. **Collections have no descriptions.** There are no pages for style or material searches such as "silk camisole" or "wool silk long sleeve top". There is no editorial content at all.
7. "Photo Gallery" sits in the main menu. It is a thin page with nothing to buy and no ranking value.
8. There is no Norwegian-language version, although the business is Norwegian (for example "ull og silke undertøy").

## Design & UX: main problems

- **The hero doesn't sell the main product.** It shows two low-res photos of necklaces on mannequins, with no headline and one "Shop all" button. Underwear is more than half the catalogue and is not in the hero.
- **The copy is vague and has errors.** "A concept, a philosophy, a lifestyle…" appears twice. "As children of the caribbean seas our greatest passion is to float in these water…" contains grammar mistakes. All headings are lowercase.
- **Trust information is hidden.** Fast delivery, 14-day returns and tax-free shopping outside Norway are all real benefits, but they only appear deep in collapsed policy text.
- **Product pages are thin.** The name is a code, there is no description, the "Care Guide" tab is empty, and there are no cross-sells within a product line. One colour is misspelled "Ivony".
- Photography is inconsistent: studio shots, warm interiors and mannequins mixed together.
- There are no reviews or social proof.

## Pricing observations

- Odd prices like NOK 1,063, 2,565 and 3,038 look like currency conversions. Rounding them (1,050 / 2,550 / 2,990) reads as more deliberate.
- No free-shipping threshold and no bundles. The 4–5 styles per product line (e.g. Emily camisole, tank, short-sleeve, long-sleeve) are a natural "buy 2 / buy 3" offer.
- Shipping cost isn't shown before checkout.
- Customers pay return shipping. Free exchanges would be a strong selling point for a product where fit is uncertain.

## What the redesign does

**SEO**
- 131 pages: home, 17 collections, 107 product pages and 5 content pages.
- Every page has a unique, keyword-led title and meta description, one H1, a canonical URL, Open Graph data and breadcrumbs.
- **New landing pages that rank for real searches:**
  - 7 style pages (camisoles, tank tops, long-sleeve tops & turtlenecks, …)
  - 2 material pages (pure silk, wool & silk)
  - 4 jewellery type pages
  - a wool & silk guide
- **Readable product names**, decoded from the Italian codes (SS = camisole, SL = tank, MM = short sleeve, ML = long sleeve). `Emily_SS` becomes "Emily Camisole". The original code is kept on the page for anyone who searches by style number.
- Structured data:
  - `OnlineStore` with the return policy
  - `Product` with an offer per variant (price, stock, material, colour)
  - `BreadcrumbList`, `CollectionPage`/`ItemList`, `FAQPage`, `Article`
- Same `/products/…` and `/collections/…` paths as Shopify, so moving needs no redirects for existing pages (static hosts add the trailing slash automatically). Includes `sitemap.xml` and `robots.txt`.

**Conversion**
- The hero has a clear promise and two calls to action. Delivery, returns and tax-free shopping appear in the announcement bar, the trust row and on every product page.
- Shop-by-style tiles, colour swatches and a second image on hover. Filters by material and colour, plus sorting. Instant product search.
- Product pages have colour and size pickers tied to the real Shopify variants, with sold-out handling. They also have a sticky "Add to bag" bar on mobile, "More from the Emily line" cross-sells, and material & care / size & fit / delivery sections.
- The cart drawer checks out through the **real Shopify checkout** using cart permalinks (`/cart/{variant}:{qty}`).
- FAQ, a rewritten "Our story" page, and newsletter and contact forms that post to the Shopify store.

**Performance**: no framework, under 9 KB of JavaScript (3 KB gzipped), self-hosted fonts (~50 KB), and responsive WebP images from the Shopify CDN with fixed dimensions (no layout shift).

## Content integrity

All factual claims come from the live site: delivery times, carriers, 14-day returns, tax-free outside Norway, founding year, materials per product, "Made in Italy" bags and Venetian jewellery. **No reviews, ratings or discounts were invented.**

Product descriptions for underwear are written per style and material. The care advice is general wool/silk guidance and tells people to follow the care label. Product types for 11 undescribed jewellery items were identified from their photos.

## Recommended next steps (owner decisions)

1. **Collect reviews** (e.g. Judge.me) and add `aggregateRating` once real reviews exist.
2. Decide on a **free-shipping threshold** (e.g. NOK 1,000) and/or **free size exchanges**, then add it to the announcement bar.
3. Add **Vipps and Klarna**. The terms page names PayPal as the payment gateway, and Norwegian shoppers expect Vipps.
4. Add a **Norwegian (nb) version** with `hreflang`.
5. Do one **consistent photo shoot**, especially for jewellery (currently on mannequins) and the numbered underwear lines.
6. Fill in real size charts (bust/waist in cm) per line.
