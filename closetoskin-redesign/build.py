#!/usr/bin/env python3
"""Static site generator for the Closetoskin redesign.

Reads data/catalog.json (a snapshot of the live store's /products.json plus the
per-product material field) and writes a fast, SEO-ready static site to dist/.

    python3 build.py                 # production build (canonical = SITE_URL)
    NOINDEX=1 python3 build.py       # preview build (adds noindex to every page)
"""
import hashlib
import html
import json
import os
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).parent
DIST = ROOT / "dist"
SITE_URL = os.environ.get("SITE_URL", "https://www.closetoskin.com").rstrip("/")
NOINDEX = os.environ.get("NOINDEX") == "1"
CDN = "https://cdn.shopify.com"
BRAND = "Closetoskin"
EMAIL = "info@closetoskin.com"
ADDRESS = {"street": "Nordengveien 23b", "zip": "0755", "city": "Oslo", "country": "NO"}
SOCIAL = ["https://www.instagram.com/closetoskin_/", "https://www.facebook.com/closetoskin"]

e = lambda s: html.escape(str(s), quote=True)


# --------------------------------------------------------------------------- catalog

UNDERWEAR_STYLES = {
    # slug: (singular, plural collection title, intro)
    "camisoles": ("Camisole", "Camisoles", "Thin-strap camisoles in wool & silk and pure silk: the invisible base layer under shirts, knitwear and dresses."),
    "tank-tops": ("Tank Top", "Tank tops & sleeveless tops", "Wider straps for extra coverage and warmth. Sleeveless wool & silk layers that sit neatly under blouses and jumpers."),
    "short-sleeve-tops": ("Short-Sleeve Top", "Short-sleeve tops", "Short-sleeve tops in fine wool & silk: a little extra warmth that stays discreet under knitwear and jackets."),
    "long-sleeve-tops": ("Long-Sleeve Top", "Long-sleeve tops & turtlenecks", "Our warmest everyday layers: long-sleeve tops, mock necks and turtlenecks in wool & silk and pure silk."),
    "bodysuits": ("Bodysuit", "Bodysuits", "Bodysuits that stay tucked in all day for a smooth line under trousers and skirts."),
    "cardigans-boleros": ("Cardigan", "Cardigans, boleros & gilets", "Light, soft wool & silk layers to wear over camisoles and tops, at home or out."),
    "leggings-briefs": ("Leggings", "Leggings, shorts & briefs", "Leggings, shorts and culotte briefs in wool & silk to match your tops."),
}
JEWELLERY_TYPES = {
    "necklaces": ("Necklace", "Necklaces & pendants", "Long statement necklaces handmade in Venice by Maria Calderara, plus fine pendants and chains."),
    "bracelets": ("Bracelet", "Bracelets", "Sculptural bracelets by Italian designer Maria Calderara, handmade in Venice."),
    "earrings": ("Earrings", "Earrings", "Handmade Venetian earrings by Maria Calderara, plus pearl and solitaire studs."),
    "rings": ("Ring", "Rings", "Sculptural rings by Italian designer Maria Calderara, handmade in Venice."),
}
MATERIALS = {
    "silk-underwear": ("Pure silk underwear", "Underwear in 100% silk: light, smooth and comfortable in warm and cool weather alike."),
    "wool-silk-underwear": ("Wool & silk underwear", "Wool keeps you warm and manages moisture; silk makes it feel smooth and soft against the skin. Our wool & silk blends are made for everyday wear, all year round."),
}
# Accessories that have no description on the live store, identified from their product photos.
ACCESSORY_TYPES = {
    "1R010mm": "Maria Calderara Earrings", "1S105J": "Maria Calderara Necklace", "1272H-P": "Pearl Stud Earrings",
    "1273H": "Stud Earrings", "1278H": "Stud Earrings", "1V129": "Statement Necklace", "1v106": "Beaded Necklace",
    "3581H": "Pendant Necklace", "3582H-P": "Pearl Pendant", "3583H": "Pendant Necklace", "V100/42H": "Fine Chain Necklace",
}
BAG_NAMES = {
    "Mini messenger": "Mini Messenger Bag in Woven Leather",
    "Medium shopper in woven leather": "Medium Woven Leather Shopper",
    "Woven leather handbag": "Woven Leather Handbag",
    "Pochette in woven leather": "Woven Leather Pochette",
    "Hobo -Woven leather handbag": "Woven Leather Hobo Bag",
}
COLOURS = {
    "Black": "#1f1d1c", "Ivory": "#f3eee2", "Ivony": "#f3eee2", "Grey": "#8d8a86", "Ash-Grey": "#a7a39d", "Ocean": "#2f4f6b",
    "Raisins": "#5b2a3a", "Powder": "#e8cfc6", "Pomegranate": "#8e2433", "Taupe": "#8b7d6b", "Silver": "#c4c4c4",
    "Gold": "#c9a85a", "Camel": "#b5895a", "Dark Brown": "#4a3426", "Beige": "#d9c7a7",
}
LABEL_FIX = {"Ivony": "Ivory", "Small": "S", "Medium": "M", "Large": "L"}

STYLE_COPY = {
    "Camisole": "Thin straps and a close, smooth fit make it an invisible base layer under shirts, knitwear and dresses.",
    "Tank Top": "Wider shoulder straps give extra coverage and warmth, and it layers neatly under blouses and jumpers.",
    "Sleeveless Top": "A sleeveless cut that layers neatly under blouses, dresses and jumpers.",
    "Sleeveless Turtleneck": "A sleeveless turtleneck that keeps your neck warm without bulk under jackets and cardigans.",
    "Short-Sleeve Top": "Short sleeves add a little extra warmth while staying discreet under knitwear and jackets.",
    "Long-Sleeve Top": "Full-length sleeves make it the warmest everyday layer. Wear it under knitwear in winter or on its own.",
    "Long-Sleeve Turtleneck": "A close-fitting turtleneck that works as a warm base layer or a refined top on its own.",
    "Long-Sleeve Mock-Neck Top": "A softly raised mock neck adds warmth while staying light enough to layer.",
    "Long-Sleeve Round-Neck Top": "A clean round neckline and long sleeves: a versatile top to layer or wear alone.",
    "Long-Sleeve Bodysuit": "A long-sleeve bodysuit that stays tucked in all day for a smooth line under trousers and skirts.",
    "Tank Bodysuit": "A sleeveless bodysuit that stays tucked in all day for a smooth line under trousers and skirts.",
    "Cardigan": "A light, soft cardigan to wear over camisoles and tops, at home or out.",
    "Bolero": "A cropped bolero that adds warmth to the shoulders and arms without bulk.",
    "Gilet": "A sleeveless gilet that adds a soft, warm layer over tops and camisoles.",
    "Leggings": "Close-fitting leggings for warmth under trousers, or on their own at home.",
    "Shorts": "Soft, close-fitting shorts to match your tops.",
    "Culotte Briefs": "Soft culotte briefs to match your tops.",
}
MATERIAL_COPY = {
    "Wool & silk": "Wool keeps you warm and manages moisture, while silk adds a smooth, soft feel next to the skin.",
    "Pure silk": "Pure silk is light and smooth, and comfortable in warm and cool weather alike. You barely notice you are wearing it.",
    "Cotton, wool & silk": "Cotton gives softness and breathability, wool adds warmth and a touch of silk makes it feel smooth against the skin.",
}


def material_label(raw):
    r = raw.lower()
    if not r:
        return ""
    if "cotton" in r:
        return "Cotton, wool & silk"
    if "wool" in r:
        return "Wool & silk"
    if "silk" in r:
        return "Pure silk"
    return raw


def parse_underwear(title):
    """Turn store codes like 'Emily_SS' or '909 04 ML' into readable names + a style group."""
    t = title.replace("Am_lie", "Amélie").replace("_", " ").replace("-", " ")
    toks = t.split()
    low = [x.lower() for x in toks]
    has = lambda *w: any(x in low for x in w)
    if has("culotte"): style, group = "Culotte Briefs", "leggings-briefs"
    elif has("fuseaux"): style, group = "Leggings", "leggings-briefs"
    elif has("short"): style, group = "Shorts", "leggings-briefs"
    elif has("cardigan"): style, group = "Cardigan", "cardigans-boleros"
    elif has("bolero"): style, group = "Bolero", "cardigans-boleros"
    elif has("gilet"): style, group = "Gilet", "cardigans-boleros"
    elif has("body"): style, group = ("Long-Sleeve Bodysuit" if has("ml") else "Tank Bodysuit"), "bodysuits"
    elif has("turtleneck"): style, group = ("Sleeveless Turtleneck", "tank-tops") if has("sm") else ("Long-Sleeve Turtleneck", "long-sleeve-tops")
    elif has("lupetto"): style, group = "Long-Sleeve Mock-Neck Top", "long-sleeve-tops"
    elif has("neckline"): style, group = "Long-Sleeve Round-Neck Top", "long-sleeve-tops"
    elif has("ss"): style, group = "Camisole", "camisoles"
    elif has("sl"): style, group = "Tank Top", "tank-tops"
    elif has("sm"): style, group = "Sleeveless Top", "tank-tops"
    elif has("mm"): style, group = "Short-Sleeve Top", "short-sleeve-tops"
    elif has("ml"): style, group = "Long-Sleeve Top", "long-sleeve-tops"
    else: raise ValueError(f"unknown style: {title}")
    if toks[0][0].isdigit():
        digits = "".join(x for x in toks if x.isdigit())
        code = f"{digits[:3]}-{digits[3:5]}" if len(digits) >= 5 else digits
        return f"{style} {code}", style, group, None
    model = toks[0].capitalize() if toks[0].islower() else toks[0]
    return f"{model} {style}", style, group, model


def clean_body(raw):
    """Live descriptions use '*' as bullets and have a few typos; turn them into a tidy list."""
    text = re.sub(r"<[^>]+>", " ", raw or "")
    text = html.unescape(re.sub(r"\s+", " ", text)).strip()
    for a, b in [("tubolar", "tubular"), ("Wi dth", "Width"), ("cmHeight", "cm · Height"), ("cmDepth", "cm · Depth"),
                 ("t.moro", "dark brown"), (", ,", ","), ("Height:", "Height: "), ("Depth:", "Depth: "), ("Width:", "Width: ")]:
        text = text.replace(a, b)
    text = re.sub(r"\s+", " ", text)
    parts = [p.strip(" .") for p in re.split(r"\s?\*\s?|\. (?=[A-Z])", text) if p.strip(" .")]
    return parts


def load_catalog():
    data = json.loads((ROOT / "data/catalog.json").read_text())
    products = []
    for i, p in enumerate(data["products"]):
        cat = p["tags"][0] if p["tags"] else "accessories"
        mat = material_label(p["material"])
        facts = clean_body(p["body_html"])
        style = group = model = None
        if cat == "underwear":
            name, style, group, model = parse_underwear(p["title"])
            desc = [f"The {name} is a fine-knit layer made to be worn close to the skin. {STYLE_COPY[style]}", MATERIAL_COPY.get(mat, "")]
        elif cat == "handbags":
            name = BAG_NAMES.get(p["title"], p["title"])
            desc = ["Hand-woven leather made in Italy using the tubular method, which gives a seamless construction."]
        else:
            body = " ".join(facts)
            kind = next((k for k in ("Necklace", "Bracelet", "Earrings", "Ring") if k.lower() in body.lower()), None)
            if p["title"] in ACCESSORY_TYPES:
                base = ACCESSORY_TYPES[p["title"]]
            elif kind:
                base = f"Maria Calderara {kind}"
            else:
                base = p["title"]
            name = base if base == p["title"] else f"{base} {p['title']}"
            low = base.lower()
            group = "earrings" if "earring" in low else "bracelets" if "bracelet" in low else "rings" if "ring" in low else "necklaces"
            desc = ["Handmade in Venice by Italian designer Maria Calderara." if "Calderara" in base else "A refined piece to complete your look, chosen by Closetoskin."]
        opts = [o for o in p["options"] if o["name"] != "Title"]
        colour_opt = next((o for o in opts if o["name"].lower().startswith("colo")), None)
        colours = colour_opt["values"] if colour_opt else []
        prices = [float(v["price"]) for v in p["variants"]]
        products.append({
            "i": i, "handle": p["handle"], "code": p["title"], "name": name, "cat": cat, "group": group, "style": style, "model": model,
            "material_raw": p["material"], "material": mat, "facts": facts if cat != "underwear" else [], "desc": [d for d in desc if d],
            "options": opts, "colours": colours, "variants": p["variants"], "price": min(prices),
            "available": any(v["available"] for v in p["variants"]), "images": p["images"], "created": p["created_at"],
            "url": f"/products/{p['handle']}/",
        })
    names = [p["name"] for p in products]
    dupes = {n for n in names if names.count(n) > 1}
    assert not dupes, f"duplicate product names: {dupes}"
    by_handle = {p["handle"]: p for p in products}
    best = [by_handle[h] for h in data["bestsellers"] if h in by_handle]
    return products, best


# --------------------------------------------------------------------------- html helpers

def img_url(im, w):
    return f"{im['src']}?v={im['v']}&width={w}" if im.get("v") else f"{im['src']}?width={w}"


def picture(im, alt, sizes, widths=(360, 540, 720, 900, 1200), eager=False, cls=""):
    if not im:
        return ""
    ws = [w for w in widths if w <= max(im["w"], 360)] or [widths[0]]
    srcset = ", ".join(f"{img_url(im, w)} {w}w" for w in ws)
    h = round(ws[-1] * im["h"] / im["w"])
    load = 'fetchpriority="high" loading="eager"' if eager else 'loading="lazy"'
    c = f' class="{cls}"' if cls else ""
    return (f'<img src="{img_url(im, ws[min(1, len(ws)-1)])}" srcset="{srcset}" sizes="{sizes}" width="{ws[-1]}" height="{h}" '
            f'alt="{e(alt)}" {load} decoding="async"{c}>')


def money(n):
    return "NOK " + (f"{n:,.0f}" if float(n).is_integer() else f"{n:,.2f}")


ICONS = {
    "search": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true"><circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/></svg>',
    "bag": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true"><path d="M5 8h14l-1 12H6L5 8Z"/><path d="M9 8V6a3 3 0 0 1 6 0v2"/></svg>',
    "menu": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true"><path d="M4 7h16M4 12h16M4 17h16"/></svg>',
    "close": '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true"><path d="M6 6l12 12M18 6 6 18"/></svg>',
    "leaf": '<svg viewBox="0 0 24 24" fill="none" stroke-width="1.4" aria-hidden="true"><path d="M5 19c0-8 5-13 14-14-1 9-6 14-14 14Z"/><path d="M5 19 13 11"/></svg>',
    "truck": '<svg viewBox="0 0 24 24" fill="none" stroke-width="1.4" aria-hidden="true"><path d="M3 7h11v9H3zM14 10h4l3 3v3h-7"/><circle cx="7" cy="17.5" r="1.5"/><circle cx="17" cy="17.5" r="1.5"/></svg>',
    "return": '<svg viewBox="0 0 24 24" fill="none" stroke-width="1.4" aria-hidden="true"><path d="M9 14 4 9l5-5"/><path d="M4 9h10a6 6 0 0 1 0 12h-3"/></svg>',
    "globe": '<svg viewBox="0 0 24 24" fill="none" stroke-width="1.4" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c3 3.5 3 14.5 0 18M12 3c-3 3.5-3 14.5 0 18"/></svg>',
}


def logo():
    return '<span>close</span><b>to</b><span>skin</span>'


def header(active):
    def a(href, label):
        cur = ' aria-current="page"' if active == href else ""
        return f'<a href="{href}"{cur}>{label}</a>'
    styles = "".join(f'<a href="/collections/{s}/">{v[1]}</a>' for s, v in UNDERWEAR_STYLES.items())
    mats = "".join(f'<a href="/collections/{s}/">{v[0]}</a>' for s, v in MATERIALS.items())
    jewel = "".join(f'<a href="/collections/{s}/">{v[1]}</a>' for s, v in JEWELLERY_TYPES.items())
    return f'''<a class="skip" href="#main">Skip to content</a>
<div class="announce"><span>Tax-free shopping outside Norway</span><span>Delivery in 2–5 days across Norway &amp; Europe</span><span>14-day returns &amp; size exchanges</span></div>
<header class="header"><div class="wrap">
<button class="icon-btn menu-toggle" type="button" data-open-nav aria-label="Open menu">{ICONS["menu"]}</button>
<a class="logo" href="/" aria-label="{BRAND} home">{logo()}</a>
<nav class="nav" aria-label="Main">
<button class="icon-btn nav-close" type="button" data-close aria-label="Close menu">{ICONS["close"]}</button>
<div class="has-menu">{a("/collections/underwear/", "Underwear")}<div class="menu"><strong>By style</strong>{styles}<strong>By material</strong>{mats}</div></div>
{a("/collections/handbags/", "Handbags")}
<div class="has-menu">{a("/collections/accessories/", "Jewellery")}<div class="menu"><strong>By type</strong>{jewel}</div></div>
{a("/pages/wool-silk-guide/", "Wool &amp; Silk Guide")}
{a("/pages/about-us/", "Our Story")}
</nav>
<div class="icons">
<button class="icon-btn" type="button" data-open-search aria-label="Search">{ICONS["search"]}</button>
<a class="icon-btn" href="{SITE_URL}/cart" data-open-cart aria-label="Shopping bag">{ICONS["bag"]}<span class="badge" data-cart-count></span></a>
</div></div></header>'''


def footer():
    return f'''<footer class="footer"><div class="wrap">
<div class="cols">
<div><a class="logo" href="/">{logo()}</a><p>Underwear in natural wool, silk and cotton, hand-woven Italian leather bags and handmade Venetian jewellery. From Oslo since 2009.</p>
<p><a href="{SOCIAL[0]}" rel="noopener">Instagram</a> · <a href="{SOCIAL[1]}" rel="noopener">Facebook</a></p></div>
<div><h2>Shop</h2><ul><li><a href="/collections/underwear/">Underwear</a></li><li><a href="/collections/silk-underwear/">Pure silk</a></li><li><a href="/collections/wool-silk-underwear/">Wool &amp; silk</a></li><li><a href="/collections/handbags/">Handbags</a></li><li><a href="/collections/accessories/">Jewellery</a></li><li><a href="/collections/all/">Shop all</a></li></ul></div>
<div><h2>Help</h2><ul><li><a href="/pages/shipping-returns/">Shipping &amp; returns</a></li><li><a href="/pages/wool-silk-guide/">Fit &amp; care guide</a></li><li><a href="/pages/contact/">Contact us</a></li><li><a href="/pages/terms-conditions/">Terms &amp; conditions</a></li></ul></div>
<div><h2>Contact</h2><ul><li><a href="mailto:{EMAIL}">{EMAIL}</a></li><li>Mon–Fri, 08:00–16:00 CET</li><li>{ADDRESS["street"]}<br>{ADDRESS["zip"]} {ADDRESS["city"]}, Norway</li></ul></div>
</div>
<div class="legal"><span>© 2026 Closetoskin · Operated by Sommerfugl Consult, org. no. 813 728 362</span><span>Prices in NOK incl. Norwegian VAT</span></div>
</div></footer>
<div class="overlay" aria-hidden="true"></div>
<aside class="drawer" aria-label="Shopping bag"><header><h2>Your bag</h2><button class="icon-btn" id="cart-close" type="button" data-close aria-label="Close bag">{ICONS["close"]}</button></header>
<div class="lines" id="cart-lines"></div>
<footer><div class="sum"><span>Subtotal</span><span id="cart-total">NOK 0</span></div><p class="muted" style="margin:0;font-size:.85rem">Shipping calculated at checkout. Customers outside Norway shop tax-free.</p>
<a class="btn btn-block" id="checkout" href="#">Secure checkout</a></footer></aside>
<div class="search" role="dialog" aria-label="Search products"><label class="sr-only" for="q">Search products</label><input id="q" type="search" placeholder="Search camisoles, silk, bags…" autocomplete="off"><ul class="results" id="results"></ul></div>'''


def page(path, title, desc, body, active="", ld=(), og_image=None, preload=None):
    canon = SITE_URL + path
    robots = '<meta name="robots" content="noindex">' if NOINDEX else '<meta name="robots" content="index,follow,max-image-preview:large">'
    og = f'<meta property="og:image" content="{e(og_image)}">' if og_image else ""
    pre = f'<link rel="preload" as="image" href="{e(preload[0])}" imagesrcset="{e(preload[1])}" imagesizes="{e(preload[2])}" fetchpriority="high">' if preload else ""
    lds = "".join(f'<script type="application/ld+json">{json.dumps(x, ensure_ascii=False, separators=(",", ":"))}</script>' for x in ld)
    doc = f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{e(title)}</title>
<meta name="description" content="{e(desc)}">
<link rel="canonical" href="{canon}">
{robots}
<meta property="og:site_name" content="{BRAND}"><meta property="og:type" content="{'product' if path.startswith('/products/') else 'website'}"><meta property="og:title" content="{e(title)}"><meta property="og:description" content="{e(desc)}"><meta property="og:url" content="{canon}">{og}
<meta name="twitter:card" content="summary_large_image">
<meta name="theme-color" content="#f7f3ee">
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="preconnect" href="{CDN}">
<link rel="preload" href="/assets/fonts/jost-var.woff2" as="font" type="font/woff2" crossorigin>
<link rel="preload" href="/assets/fonts/cormorant-500.woff2" as="font" type="font/woff2" crossorigin>
{pre}
<link rel="stylesheet" href="/assets/styles.css?v={ASSET_V['css']}">
<script src="/assets/app.js?v={ASSET_V['js']}" defer></script>
{lds}
</head>
<body>
{header(active)}
<main id="main">
{body}
</main>
{footer()}
</body>
</html>'''
    out = DIST / path.lstrip("/") / "index.html" if path.endswith("/") else DIST / path.lstrip("/")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(re.sub(r"\n\s*\n", "\n", doc))
    if not NOINDEX and path != "/404.html":
        SITEMAP.append(path)


def crumbs(items):
    lis = "".join(f'<li><a href="{u}">{e(n)}</a></li>' if u else f'<li aria-current="page">{e(n)}</li>' for n, u in items)
    ld = {"@context": "https://schema.org", "@type": "BreadcrumbList", "itemListElement": [
        {"@type": "ListItem", "position": i + 1, "name": n, **({"item": SITE_URL + u} if u else {})} for i, (n, u) in enumerate(items)]}
    return f'<nav class="crumbs wrap" aria-label="Breadcrumb"><ol>{lis}</ol></nav>', ld


def card(p, eager=False, pos=None):
    ims = p["images"][:2]
    alt = f"{p['name']}" + (f" in {p['material'].lower()}" if p["material"] else "")
    imgs = "".join(picture(im, alt if k == 0 else "", "(max-width:700px) 50vw, (max-width:1024px) 33vw, 300px", (360, 540, 720), eager and k == 0) for k, im in enumerate(ims))
    sw = "".join(f'<i style="background:{COLOURS.get(c, "#bbb")}" title="{e(LABEL_FIX.get(c, c))}"></i>' for c in p["colours"][:6])
    meta = p["material"] or ("Made in Italy" if p["cat"] == "handbags" else "Handmade in Venice" if "Calderara" in p["name"] else "")
    ncol = len(p["colours"])
    tag = f'<span class="tag">{ncol} colours</span>' if ncol > 1 else ""
    filt = " ".join(x for x in [p["group"] or "", "silk" if p["material"] == "Pure silk" else "woolsilk" if p["material"] else ""] if x)
    cols = " ".join(re.sub(r"\W", "", LABEL_FIX.get(c, c)).lower() for c in p["colours"])
    return (f'<article class="card" data-f="{filt}" data-c="{cols}" data-p="{p["price"]:.0f}" data-d="{p["created"][:10]}" data-i="{p["i"] if pos is None else pos}">'
            f'<a href="{p["url"]}"><span class="media">{imgs}{tag}</span><h3>{e(p["name"])}</h3></a>'
            f'<div class="meta">{e(meta)}</div><div class="price">{money(p["price"])}</div>{f"<div class=swatches>{sw}</div>" if sw else ""}</article>')


ORG_LD = {
    "@context": "https://schema.org", "@type": "OnlineStore", "@id": SITE_URL + "/#org", "name": BRAND, "url": SITE_URL + "/",
    "logo": "https://www.closetoskin.com/cdn/shop/files/CTS_Logo_Final.png", "email": EMAIL, "foundingDate": "2009",
    "address": {"@type": "PostalAddress", "streetAddress": ADDRESS["street"], "postalCode": ADDRESS["zip"], "addressLocality": ADDRESS["city"], "addressCountry": ADDRESS["country"]},
    "sameAs": SOCIAL,
    "hasMerchantReturnPolicy": {"@type": "MerchantReturnPolicy", "applicableCountry": "NO", "returnPolicyCategory": "https://schema.org/MerchantReturnFiniteReturnWindow",
                                "merchantReturnDays": 14, "returnMethod": "https://schema.org/ReturnByMail", "returnFees": "https://schema.org/ReturnFeesCustomerResponsibility"},
}

FAQ = [
    ("How long does delivery take?", "Orders ship Monday to Friday. Delivery takes 2–5 days within Norway and Europe and 4–10 days to the rest of the world. Norwegian orders ship with Posten, international orders with UPS."),
    ("Can I shop tax-free from outside Norway?", "Yes. Our prices include 25% Norwegian VAT. Customers outside Norway shop tax-free, but are responsible for any import duties and taxes in their own country."),
    ("What is your return and exchange policy?", "You can return or exchange items within 14 days of delivery, as long as they are in perfect condition with tags and seal attached. We happily exchange to a different size, subject to stock. Return shipping is paid by the customer."),
    ("What are your underwear made of?", "Our underwear is made from natural fibres: wool & silk blends (85/15 and 70/30), pure 100% silk, and a cotton, wool & silk blend. The exact composition is listed on every product page."),
    ("How do I choose my size?", "Our underwear comes in S, M and L with a close, second-skin fit. If you are between sizes or prefer a looser layer, choose the larger size. You can always exchange within 14 days."),
    ("How do I care for wool and silk?", "Hand wash in cool water with a mild wool or silk detergent, don't wring, and dry flat away from direct heat. Always follow the care label on the garment."),
]


def faq_html(items):
    return '<div class="faq">' + "".join(f'<details><summary>{e(q)}</summary><div class="body">{e(a)}</div></details>' for q, a in items) + "</div>"


def faq_ld(items):
    return {"@context": "https://schema.org", "@type": "FAQPage", "mainEntity": [
        {"@type": "Question", "name": q, "acceptedAnswer": {"@type": "Answer", "text": a}} for q, a in items]}


# --------------------------------------------------------------------------- pages

def build_home(products, best):
    hero_im = {"src": f"{CDN}/s/files/1/0571/3733/2289/files/Image_new_1_Asset_8.png", "v": "1667813786", "w": 1897, "h": 1906}
    bag_im = {"src": f"{CDN}/s/files/1/0571/3733/2289/files/Image_new_1_Asset_1.png", "v": "1667812724", "w": 1897, "h": 1906}
    ring_im = {"src": f"{CDN}/s/files/1/0571/3733/2289/files/Image_new_1_Asset_7.png", "v": "1667813753", "w": 1897, "h": 1905}
    hero_sizes = "(max-width:860px) 100vw, 50vw"
    hero_srcset = ", ".join(f"{img_url(hero_im, w)} {w}w" for w in (540, 720, 900, 1200))
    by = {p["handle"]: p for p in products}
    tiles = [("camisoles", "arianna_ss-top"), ("long-sleeve-tops", "ilary-ml-neckline"), ("bodysuits", "sara_ml_body"), ("cardigans-boleros", "marie-_ml_cardigan")]
    tile_html = ""
    for slug, h in tiles:
        p = by.get(h) or next(x for x in products if x["group"] == slug)
        n = sum(1 for x in products if x["group"] == slug)
        tile_html += f'<a class="tile" href="/collections/{slug}/">{picture(p["images"][0], "", "(max-width:860px) 50vw, 25vw", (360, 540, 720))}<span>{UNDERWEAR_STYLES[slug][1]}<small>{n}</small></span></a>'
    calderara = [p for p in products if p["cat"] == "accessories" and "Calderara" in p["name"]]
    jewels = [next(p for p in calderara if p["group"] == g) for g in ("bracelets", "necklaces", "earrings", "rings")]
    usp = f'''<div class="wrap usp">
<div>{ICONS["leaf"]}<span><strong>Natural fibres</strong>Wool, silk and cotton, worn close to the skin</span></div>
<div>{ICONS["truck"]}<span><strong>Fast delivery</strong>2–5 days across Norway &amp; Europe</span></div>
<div>{ICONS["return"]}<span><strong>14-day returns</strong>Easy size exchanges</span></div>
<div>{ICONS["globe"]}<span><strong>Tax-free abroad</strong>No Norwegian VAT outside Norway</span></div></div>'''
    body = f'''<section class="hero">
<div class="hero-copy"><span class="eyebrow">Wool · Silk · Cotton</span>
<h1>Wool &amp; silk underwear, made to live close to your skin</h1>
<p class="lead">Fine-knit camisoles, tops and bodysuits in natural wool, silk and cotton. Warm without bulk, soft enough to forget you're wearing them. Plus hand-woven Italian leather bags and handmade Venetian jewellery.</p>
<div class="hero-cta"><a class="btn" href="/collections/underwear/">Shop underwear</a><a class="btn btn-ghost" href="/collections/bestsellers/">See bestsellers</a></div>
<ul class="ticks"><li>Tax-free outside Norway</li><li>2–5 day delivery in Europe</li><li>14-day size exchanges</li></ul></div>
<div class="hero-media">{picture(hero_im, "Woman wearing an ivory camisole with a lace neckline", hero_sizes, (540, 720, 900, 1200), eager=True)}</div>
</section>
{usp}
<section class="section"><div class="wrap">
<div class="section-head"><div><span class="eyebrow">Bestsellers</span><h2>Most-loved layers</h2></div><a class="link" href="/collections/bestsellers/">Shop all bestsellers</a></div>
<div class="grid">{"".join(card(p) for p in best[:4])}</div></div></section>
<section class="section band"><div class="wrap">
<div class="section-head"><div><span class="eyebrow">Underwear</span><h2>Shop by style</h2></div><a class="link" href="/collections/underwear/">All {sum(1 for p in products if p["cat"] == "underwear")} styles</a></div>
<div class="tiles">{tile_html}</div></div></section>
<section class="section"><div class="wrap split">
{picture(by["am_lie_ml"]["images"][0], "Grey long-sleeve wool and silk top", "(max-width:860px) 100vw, 50vw", (540, 720, 900))}
<div><span class="eyebrow">Why wool &amp; silk</span><h2>Warm in winter, light in spring, soft all year</h2>
<ul class="facts"><li><strong>Wool</strong> insulates and manages moisture, so you stay warm without overheating.</li><li><strong>Silk</strong> is light and smooth, and takes away the itch some people feel from pure wool.</li><li><strong>Together</strong> they make a thin, second-skin layer that disappears under your clothes.</li></ul>
<a class="btn" href="/collections/wool-silk-underwear/">Shop wool &amp; silk</a> <a class="link" style="margin-left:14px" href="/pages/wool-silk-guide/">Read the guide</a></div></div></section>
<section class="section dark"><div class="wrap split">
<div><span class="eyebrow">Handbags</span><h2>Hand-woven in Italy. Seamless by design.</h2><p class="muted">Each bag is woven by hand from soft leather using the tubular method, which leaves it without a single seam. Finished with gold-tone hardware, from the evening pochette to the everyday shopper.</p>
<a class="btn" href="/collections/handbags/">Shop handbags · from {money(min(p["price"] for p in products if p["cat"] == "handbags"))}</a></div>
{picture(bag_im, "Dark brown hand-woven leather shopper bag", "(max-width:860px) 100vw, 50vw", (540, 720, 900))}</div></section>
<section class="section"><div class="wrap">
<div class="section-head"><div><span class="eyebrow">Jewellery</span><h2>Handmade in Venice by Maria Calderara</h2></div><a class="link" href="/collections/accessories/">Shop jewellery</a></div>
<div class="grid">{"".join(card(p) for p in jewels)}</div></div></section>
<section class="section band"><div class="wrap split">
<div><span class="eyebrow">Our story</span><h2>Beautiful things that are natural and long-lasting</h2>
<p>Closetoskin was founded in Oslo in 2009 by a group of friends who met as students in London. We grew up by the Caribbean sea, and the feeling of silk against the body is what feels like home to us.</p>
<p>Our idea is simple: modern basics in natural materials (wool, silk, cotton and cashmere) that are comfortable, timeless and made to last.</p>
<a class="link" href="/pages/about-us/">Read our story</a></div>
{picture(ring_im, "Gold-tone sculptural statement ring", "(max-width:860px) 100vw, 50vw", (540, 720, 900))}</div></section>
<section class="section"><div class="wrap"><div class="section-head"><div><span class="eyebrow">Good to know</span><h2>Questions, answered</h2></div><a class="link" href="/pages/shipping-returns/">Shipping &amp; returns</a></div>{faq_html(FAQ[:4])}</div></section>
<section class="section dark"><div class="wrap split" style="align-items:center">
<div><h2>Join the Closetoskin letter</h2><p class="muted">New colours, restocks and the occasional private sale. No spam, unsubscribe anytime.</p></div>
<form class="inline-form" method="post" action="{SITE_URL}/contact#newsletter"><input type="hidden" name="form_type" value="customer"><input type="hidden" name="utf8" value="✓"><input type="hidden" name="contact[tags]" value="newsletter">
<label class="sr-only" for="nl">Email</label><input id="nl" type="email" name="contact[email]" required placeholder="Your email" autocomplete="email"><button class="btn" type="submit">Subscribe</button></form></div></section>'''
    site_ld = {"@context": "https://schema.org", "@type": "WebSite", "name": BRAND, "url": SITE_URL + "/", "publisher": {"@id": SITE_URL + "/#org"}}
    page("/", "Wool & Silk Underwear, Woven Leather Bags & Jewellery | Closetoskin",
         "Shop fine wool & silk and pure silk underwear: camisoles, long-sleeve tops and bodysuits. Plus hand-woven Italian leather bags. 2–5 day delivery, tax-free outside Norway.",
         body, "/", [ORG_LD, site_ld], img_url(hero_im, 1200), (img_url(hero_im, 720), hero_srcset, hero_sizes))


def build_collection(path, h1, title, desc, intro, items, crumb, pills=None, filters=True, seo=""):
    nav, bld = crumbs(crumb)
    pill_html = ""
    if pills:
        pill_html = '<div class="pills">' + "".join(f'<a class="pill" href="{u}"{" aria-current=page" if u == path else ""}>{e(n)}</a>' for n, u in pills) + "</div>"
    colours = []
    for p in items:
        for c in p["colours"]:
            c = LABEL_FIX.get(c, c)
            if c not in colours:
                colours.append(c)
    tool = ""
    if filters and len(items) > 4:
        cbtn = "".join(f'<button class="pill" type="button" data-filter="c:{re.sub(r"[^a-z]", "", c.lower())}" aria-pressed="false"><i style="display:inline-block;width:12px;height:12px;border-radius:50%;margin-right:7px;border:1px solid rgba(0,0,0,.2);background:{COLOURS.get(c, "#bbb")}"></i>{e(c)}</button>' for c in colours) if len(colours) > 1 else ""
        mats = {p["material"] for p in items if p["material"]}
        mbtn = ('<button class="pill" type="button" data-filter="f:silk" aria-pressed="false">Pure silk</button><button class="pill" type="button" data-filter="f:woolsilk" aria-pressed="false">Wool &amp; silk</button>') if len(mats) > 1 else ""
        tool = f'''<div class="toolbar"><div class="filters">{mbtn}{cbtn}</div><div><span id="count" class="muted" style="margin-right:12px">{len(items)} products</span>
<label class="sr-only" for="sort">Sort by</label><select id="sort"><option value="">Featured</option><option value="asc">Price: low to high</option><option value="desc">Price: high to low</option><option value="new">Newest</option></select></div></div>'''
    else:
        tool = '<div style="height:28px"></div>'
    body = f'''{nav}<div class="wrap"><header class="col-head"><h1>{e(h1)}</h1><p class="lead">{e(intro)}</p>{pill_html}</header>
{tool}<div class="grid" id="grid">{"".join(card(p, k < 4, k) for k, p in enumerate(items))}</div>
{f'<section class="section seo-copy">{seo}</section>' if seo else '<div style="height:80px"></div>'}</div>'''
    item_ld = {"@context": "https://schema.org", "@type": "CollectionPage", "name": h1, "description": desc, "url": SITE_URL + path,
               "mainEntity": {"@type": "ItemList", "numberOfItems": len(items), "itemListElement": [
                   {"@type": "ListItem", "position": i + 1, "url": SITE_URL + p["url"], "name": p["name"]} for i, p in enumerate(items)]}}
    og = img_url(items[0]["images"][0], 1200) if items and items[0]["images"] else None
    page(path, title, desc, body, crumb[1][1] if len(crumb) > 2 else path, [bld, item_ld], og)


def build_collections(products, best):
    best_ids = {p["handle"] for p in best}
    # featured order: bestsellers, then named lines (lifestyle photography), then numbered styles; bottoms last
    under = sorted((p for p in products if p["cat"] == "underwear"),
                   key=lambda p: (p["handle"] not in best_ids, p["group"] == "leggings-briefs", p["model"] is None, p["model"] or "", p["i"]))
    bags = [p for p in products if p["cat"] == "handbags"]
    jew = [p for p in products if p["cat"] == "accessories"]
    # put the most giftable / recognisable pieces first
    jew.sort(key=lambda p: (0 if "Calderara" in p["name"] else 1, -p["price"]))
    home = ("Home", "/")
    style_pills = [("All underwear", "/collections/underwear/")] + [(v[1], f"/collections/{s}/") for s, v in UNDERWEAR_STYLES.items()]
    seo_under = '''<h2>Wool &amp; silk underwear for every season</h2>
<p>Closetoskin underwear is knitted from natural fibres that work with your body: wool to keep you warm and dry, silk for a smooth, light feel against the skin. Choose a thin-strap camisole as an invisible base layer, a tank top for more coverage, or a long-sleeve top and turtleneck for the coldest days.</p>
<p>Every piece comes in sizes S, M and L with a close, second-skin fit, in timeless colours like black, ivory, ash grey and powder. Not sure where to start? Read our <a class="link" href="/pages/wool-silk-guide/">wool &amp; silk guide</a>.</p>'''
    build_collection("/collections/underwear/", "Wool & silk underwear", "Wool & Silk Underwear for Women – Camisoles, Tops & Bodysuits | Closetoskin",
                     f"Shop {len(under)} styles of wool & silk and pure silk underwear: camisoles, tank tops, long-sleeve tops, bodysuits and cardigans in S–L. Tax-free outside Norway.",
                     "Fine-knit layers in wool & silk, pure silk and cotton blends: warm, breathable and soft enough to wear all day.",
                     under, [home, ("Underwear", None)], style_pills, seo=seo_under)
    for slug, (sing, plural, intro) in UNDERWEAR_STYLES.items():
        items = [p for p in under if p["group"] == slug]
        build_collection(f"/collections/{slug}/", plural, f"{plural} in Wool & Silk | Closetoskin",
                         f"{intro} {len(items)} styles in S–L. Delivery in 2–5 days across Europe, 14-day exchanges.",
                         intro, items, [home, ("Underwear", "/collections/underwear/"), (plural, None)], style_pills)
    for slug, (h1, intro) in MATERIALS.items():
        items = [p for p in under if (p["material"] == "Pure silk") == (slug == "silk-underwear")]
        build_collection(f"/collections/{slug}/", h1, f"{h1} for Women | Closetoskin",
                         f"{intro} {len(items)} styles from camisoles to long-sleeve tops. Tax-free outside Norway.",
                         intro, items, [home, ("Underwear", "/collections/underwear/"), (h1, None)], style_pills)
    build_collection("/collections/handbags/", "Hand-woven leather handbags", "Hand-Woven Italian Leather Handbags | Closetoskin",
                     "Seamless hand-woven leather handbags made in Italy using the tubular method: shopper, hobo, pochette and mini messenger. Free size exchanges, 14-day returns.",
                     "Woven by hand in Italy using the tubular method, so every bag is seamless. Soft leather, gold-tone hardware, made to last for years.",
                     bags, [home, ("Handbags", None)], filters=False)
    jpills = [("All jewellery", "/collections/accessories/")] + [(v[1], f"/collections/{s}/") for s, v in JEWELLERY_TYPES.items()]
    build_collection("/collections/accessories/", "Jewellery", "Maria Calderara Jewellery – Handmade in Venice | Closetoskin",
                     "Necklaces, bracelets and earrings by Italian designer Maria Calderara, handmade in Venice, plus pearl and solitaire studs and pendants.",
                     "Sculptural necklaces, bracelets and earrings by Italian designer Maria Calderara, handmade in Venice, alongside fine pearl and solitaire pieces.",
                     jew, [home, ("Jewellery", None)], jpills)
    for slug, (sing, plural, intro) in JEWELLERY_TYPES.items():
        items = [p for p in jew if p["group"] == slug]
        build_collection(f"/collections/{slug}/", plural, f"{plural} – Handmade Italian Jewellery | Closetoskin", intro, intro, items,
                         [home, ("Jewellery", "/collections/accessories/"), (plural, None)], jpills)
    build_collection("/collections/bestsellers/", "Bestsellers", "Bestselling Wool & Silk Underwear | Closetoskin",
                     "Our customers' favourite wool & silk camisoles, tops and bodysuits.", "The layers our customers come back for, season after season.",
                     best, [home, ("Bestsellers", None)], filters=False)
    allp = under + bags + jew
    build_collection("/collections/all/", "Shop all", "Shop All – Underwear, Handbags & Jewellery | Closetoskin",
                     "Browse every Closetoskin piece: wool & silk underwear, hand-woven Italian leather bags and handmade Venetian jewellery.",
                     "Every piece in one place: underwear, handbags and jewellery.", allp, [home, ("Shop all", None)],
                     [("Underwear", "/collections/underwear/"), ("Handbags", "/collections/handbags/"), ("Jewellery", "/collections/accessories/")])


def build_product(p, products):
    cat_name, cat_url = {"underwear": ("Underwear", "/collections/underwear/"), "handbags": ("Handbags", "/collections/handbags/")}.get(p["cat"], ("Jewellery", "/collections/accessories/"))
    trail = [("Home", "/"), (cat_name, cat_url)]
    if p["group"] in UNDERWEAR_STYLES:
        trail.append((UNDERWEAR_STYLES[p["group"]][1], f"/collections/{p['group']}/"))
    elif p["group"] in JEWELLERY_TYPES:
        trail.append((JEWELLERY_TYPES[p["group"]][1], f"/collections/{p['group']}/"))
    nav, bld = crumbs(trail + [(p["name"], None)])
    alt_base = p["name"] + (f", {p['material'].lower()}" if p["material"] else "")
    ims = p["images"] or []
    slides = "".join(picture(im, f"{alt_base} – view {k + 1}", "(max-width:900px) 100vw, 55vw", (540, 720, 900, 1200), k == 0) for k, im in enumerate(ims))
    thumbs = "".join(f'<button type="button" aria-label="Show image {k + 1}" aria-current="{str(k == 0).lower()}"><img src="{img_url(im, 160)}" alt="" width="72" height="90" loading="lazy"></button>' for k, im in enumerate(ims)) if len(ims) > 1 else ""
    dots = "".join(f'<i class="{"on" if k == 0 else ""}"></i>' for k in range(len(ims))) if len(ims) > 1 else ""
    # options
    fs = ""
    for i, o in enumerate(p["options"]):
        is_col = o["name"].lower().startswith("colo")
        lab = "Colour" if is_col else o["name"]
        opts = ""
        for k, v in enumerate(o["values"]):
            vid = f"o{i}-{k}"
            sw = f'<i style="background:{COLOURS.get(v, "#bbb")}"></i>' if is_col else ""
            opts += f'<input type="radio" name="o{i}" id="{vid}" value="{e(v)}"{" checked" if k == 0 else ""}><label for="{vid}">{sw}{e(LABEL_FIX.get(v, v) if is_col else v)}</label>'
        fs += f'<fieldset><legend>{e(lab)}: <span id="ov{i}"></span></legend><div class="opts">{opts}</div></fieldset>'
    labels = {v: LABEL_FIX.get(v, v) for o in p["options"] for v in o["values"] if v in LABEL_FIX and v not in ("Small", "Medium", "Large")}
    variants = [{"id": v["id"], "o": [v["option1"], v["option2"]][:len(p["options"])], "a": v["available"], "p": float(v["price"])} for v in p["variants"]]
    pjson = {"name": p["name"], "price": p["price"], "img": img_url(ims[0], 200) if ims else "", "options": [o["name"] for o in p["options"]], "labels": labels, "variants": variants}
    # accordion
    details_list = "".join(f"<li>{e(f)}</li>" for f in p["facts"]) if p["facts"] else ""
    desc_html = "".join(f"<p>{e(d)}</p>" for d in p["desc"]) + (f"<ul>{details_list}</ul>" if details_list else "")
    mat_block = ""
    if p["cat"] == "underwear":
        mat_block = f'''<details><summary>Material &amp; care</summary><div class="body"><p><strong>Composition:</strong> {e(p["material_raw"])}</p>
<p>Hand wash in cool water with a mild wool or silk detergent. Don't wring; dry flat away from direct heat. Always check the care label.</p></div></details>
<details><summary>Size &amp; fit</summary><div class="body"><p>Close, second-skin fit in S, M and L. Between sizes or prefer a looser layer? Size up. Exchanges to another size are free of hassle within 14 days.</p></div></details>'''
    acc = f'''<div class="acc"><details open><summary>Description</summary><div class="body">{desc_html}</div></details>{mat_block}
<details><summary>Delivery &amp; returns</summary><div class="body"><p>Ships Monday–Friday. 2–5 days within Norway &amp; Europe (Posten / UPS), 4–10 days worldwide. Returns and size exchanges within 14 days of delivery. <a class="link" href="/pages/shipping-returns/">Full policy</a></p></div></details></div>'''
    chips = "".join(f'<span class="chip">{e(c)}</span>' for c in [p["material"], "Made in Italy" if p["cat"] == "handbags" else "", "Handmade in Venice" if "Calderara" in p["name"] else ""] if c)
    code = f'<div class="code">Style {e(p["code"])}</div>' if p["code"] != p["name"] else ""
    # related: same model line first, then same group
    line = [x for x in products if x is not p and p["model"] and x["model"] == p["model"]]
    same = [x for x in products if x is not p and x["group"] == p["group"] and x not in line]
    rel = (line + same + [x for x in products if x["cat"] == p["cat"] and x is not p and x not in line and x not in same])[:4]
    rel_title = f"More from the {p['model']} line" if line else "You may also like"
    body = f'''{nav}<div class="wrap"><div class="pdp">
<div><div class="gallery">{f'<div class="thumbs">{thumbs}</div>' if thumbs else ''}<div class="slides">{slides}</div></div><div class="dots" aria-hidden="true">{dots}</div></div>
<div class="buy">{code}<h1>{e(p["name"])}</h1><div>{chips}</div>
<div class="price" data-price>{money(p["price"])}</div><div class="tax">Incl. Norwegian VAT · tax-free when shipped outside Norway</div>
<form id="buy" onsubmit="return false">{fs}
<div class="atc"><button class="btn btn-block" id="atc-main" type="submit" data-atc>Add to bag — {money(p["price"])}</button><span id="stock" class="stock">{"In stock — ships within 1 business day" if p["available"] else "Currently sold out"}</span></div></form>
<ul class="assure"><li>{ICONS["truck"]}<span>Delivery in 2–5 days across Norway &amp; Europe</span></li><li>{ICONS["return"]}<span>14-day returns &amp; size exchanges</span></li><li>{ICONS["globe"]}<span>Tax-free shopping outside Norway</span></li></ul>
{acc}</div></div>
<section class="section" style="padding-top:24px"><div class="section-head"><h2>{e(rel_title)}</h2><a class="link" href="{trail[-1][1]}">View all</a></div><div class="grid">{"".join(card(x) for x in rel)}</div></section></div>
<div class="sticky-atc" aria-hidden="true"><div><strong>{e(p["name"])}</strong><span data-price>{money(p["price"])}</span></div><button class="btn" type="button" data-atc tabindex="-1">Add to bag</button></div>
<script type="application/json" id="pdata">{json.dumps(pjson, ensure_ascii=False)}</script>'''
    offers = [{"@type": "Offer", "url": f"{SITE_URL}{p['url']}?variant={v['id']}", "priceCurrency": "NOK", "price": v["price"],
               "availability": "https://schema.org/" + ("InStock" if v["available"] else "OutOfStock"), "itemCondition": "https://schema.org/NewCondition",
               "name": v["title"] if v["title"] != "Default Title" else p["name"], "seller": {"@id": SITE_URL + "/#org"}} for v in p["variants"]]
    prod_ld = {"@context": "https://schema.org", "@type": "Product", "name": p["name"], "url": SITE_URL + p["url"], "mpn": p["code"], "sku": p["code"],
               "brand": {"@type": "Brand", "name": "Maria Calderara" if "Calderara" in p["name"] else BRAND},
               "description": " ".join(p["desc"] + p["facts"]), "image": [img_url(im, 1200) for im in ims],
               "category": cat_name, "offers": offers}
    if p["material_raw"]:
        prod_ld["material"] = p["material_raw"]
    if p["colours"]:
        prod_ld["color"] = ", ".join(LABEL_FIX.get(c, c) for c in p["colours"])
    mat = f" in {p['material']}" if p["material"] else ""
    title = f"{p['name']}{mat} | Closetoskin"
    if len(title) > 70:
        title = f"{p['name']} | Closetoskin"
    cols = ", ".join(LABEL_FIX.get(c, c) for c in p["colours"][:4])
    desc = f"{p['name']}{mat.lower()}" + (f" in {cols}" if cols and not mat else f". {cols}" if cols else "") + f". {money(p['price'])}. 2–5 day delivery in Europe, 14-day returns, tax-free outside Norway."
    page(p["url"], title, desc[:300], body, cat_url, [bld, prod_ld], img_url(ims[0], 1200) if ims else None)


def build_pages(products):
    under = [p for p in products if p["cat"] == "underwear"]
    by = {p["handle"]: p for p in products}
    # About
    nav, bld = crumbs([("Home", "/"), ("Our story", None)])
    about = f'''{nav}<article class="prose wrap"><span class="eyebrow">Since 2009 · Oslo</span><h1>Our story</h1>
<p class="lead">Closetoskin began with a passion for beautiful things that are natural and long-lasting.</p>
<p>We are a group of friends who studied together in London some 30 years ago. We grew up by the Caribbean sea, where our greatest joy was floating in the water. That feeling of silk against the body is what makes us feel at home, and it is the feeling we wanted to bring to everyday clothing.</p>
<p>We founded Closetoskin in 2009 for people who want to feel comfortable <em>and</em> look modern: urban, busy and discerning, yet unwilling to compromise on comfort or value.</p>
{picture(by["ilary-ml-neckline"]["images"][0], "Woman wearing a navy long-sleeve wool and silk top", "(max-width:760px) 100vw, 740px", (540, 720, 900))}
<h2>Natural materials, by choice</h2>
<p>We care about nature and want to do our share. That is why our collections are built on natural materials: wool, silk, cotton and cashmere. Fibres that breathe, regulate temperature and feel good against the skin.</p>
<h2>What sets us apart</h2>
<ul><li><strong>Natural materials of good quality.</strong> Every garment lists its exact composition.</li><li><strong>Simple design.</strong> Modern basics you'll reach for again and again.</li><li><strong>Superior craftsmanship and fit.</strong> From fine-knit underwear to hand-woven Italian leather.</li></ul>
<h2>It's about you</h2>
<p>It's about feeling wonderful in whatever you're wearing and whatever you're doing. It's about luxury, quality, simplicity and style, and about giving a gift that's remembered.</p>
<p><a class="btn" href="/collections/underwear/">Explore the collection</a></p></article>'''
    page("/pages/about-us/", "Our Story – Natural Underwear from Oslo since 2009 | Closetoskin",
         "Closetoskin was founded in Oslo in 2009 to make modern basics in natural wool, silk, cotton and cashmere: comfortable, timeless and made to last.",
         about, "/pages/about-us/", [bld, ORG_LD])
    # Shipping & returns
    nav, bld = crumbs([("Home", "/"), ("Shipping & returns", None)])
    ship = f'''{nav}<article class="prose wrap"><h1>Shipping &amp; returns</h1>
<p class="lead">Fast delivery from Oslo, tax-free shopping outside Norway and 14 days to change your mind.</p>
<h2>Delivery</h2>
<table><thead><tr><th>Destination</th><th>Carrier</th><th>Delivery time</th></tr></thead><tbody>
<tr><td>Norway</td><td>Posten</td><td>2–5 business days</td></tr><tr><td>Europe</td><td>UPS International Economy</td><td>2–5 business days</td></tr><tr><td>Rest of the world</td><td>UPS International Economy</td><td>4–10 business days</td></tr></tbody></table>
<p>Orders ship Monday to Friday; weekend orders ship the next business day. In Norway, Posten delivers to your local pick-up point and notifies you by SMS or email.</p>
<h2>Prices, VAT &amp; duties</h2>
<p>All prices are in NOK and include 25% Norwegian VAT. Customers outside Norway shop tax-free. Import duties and local taxes, where they apply, are paid by the customer.</p>
<h2>Returns &amp; exchanges</h2>
<ul><li>Return or exchange within <strong>14 days</strong> of delivery.</li><li>Items must be in perfect condition with tags and seal attached.</li><li>Size exchanges are processed when we receive your return, subject to stock.</li><li>Refunds go back to the original payment method. Return shipping is paid by the customer.</li><li>The same policy applies during sales.</li></ul>
<p>Send returns to: <strong>Closetoskin, {ADDRESS["street"]}, {ADDRESS["zip"]} {ADDRESS["city"]}, Norway</strong>. Need help? Email <a class="link" href="mailto:{EMAIL}">{EMAIL}</a>.</p>
<h2>FAQ</h2>{faq_html(FAQ)}</article>'''
    page("/pages/shipping-returns/", "Shipping, Returns & Tax-Free Shopping | Closetoskin",
         "Delivery in 2–5 days across Norway & Europe, 4–10 days worldwide. Tax-free outside Norway. 14-day returns and size exchanges.",
         ship, "", [bld, faq_ld(FAQ)])
    # Contact
    nav, bld = crumbs([("Home", "/"), ("Contact", None)])
    contact = f'''{nav}<article class="prose wrap"><h1>Contact us</h1>
<p class="lead">We're here Monday to Friday, 08:00–16:00 CET, and usually reply within one business day.</p>
<p>Email: <a class="link" href="mailto:{EMAIL}">{EMAIL}</a><br>Address: {ADDRESS["street"]}, {ADDRESS["zip"]} {ADDRESS["city"]}, Norway</p>
<form method="post" action="{SITE_URL}/contact#contact_form"><input type="hidden" name="form_type" value="contact"><input type="hidden" name="utf8" value="✓">
<div class="field"><label for="cn">Name</label><input id="cn" name="contact[name]" autocomplete="name"></div>
<div class="field"><label for="ce">Email *</label><input id="ce" type="email" name="contact[email]" required autocomplete="email"></div>
<div class="field"><label for="cm">Message</label><textarea id="cm" name="contact[body]" rows="5"></textarea></div>
<button class="btn" type="submit">Send message</button></form></article>'''
    page("/pages/contact/", "Contact Closetoskin – Customer Service", "Questions about sizes, orders or returns? Email info@closetoskin.com, Monday–Friday 08:00–16:00 CET.", contact, "", [bld])
    # Terms (live store wording, tidied)
    nav, bld = crumbs([("Home", "/"), ("Terms & conditions", None)])
    terms = f'''{nav}<article class="prose wrap"><h1>Terms &amp; conditions</h1>
<p>The Closetoskin web shop at closetoskin.com is operated by Sommerfugl Consult (org. no. 813 728 362). These terms govern the use of the web shop.</p>
<h2>Privacy</h2><p>Personal information you share with Closetoskin is used only to fulfil your order and give you the best service. We never share your details with third parties without your consent. You may request to see, correct or delete your information at any time by emailing {EMAIL}.</p>
<h2>Orders</h2><p>You'll receive an automatic email confirming your order. If an item turns out to be out of stock, we'll contact you and offer to cancel the order. Colours on screen may differ slightly from the actual product.</p>
<h2>Prices &amp; payment</h2><p>Prices are stated in NOK including 25% Norwegian VAT and excluding shipping. Customers outside Norway shop tax-free and are responsible for local import duties and taxes. Prices are fixed at the time you place your order. Parcels returned because of unpaid customs charges are refunded minus any costs incurred.</p>
<h2>Delivery</h2><p>See <a class="link" href="/pages/shipping-returns/">Shipping &amp; returns</a>. Closetoskin does not cover shipping costs for uncollected packages.</p>
<h2>Complaints</h2><p>We welcome feedback. If you wish to complain, email {EMAIL} stating clearly that it's a complaint, and we'll respond within a few working days.</p></article>'''
    page("/pages/terms-conditions/", "Terms & Conditions | Closetoskin", "Terms and conditions, privacy, prices, payment and delivery for the Closetoskin web shop.", terms, "", [bld])
    # Guide (content marketing / SEO)
    nav, bld = crumbs([("Home", "/"), ("Wool & silk guide", None)])
    n_ws = sum(1 for p in under if p["material"] in ("Wool & silk", "Cotton, wool & silk"))
    n_s = sum(1 for p in under if p["material"] == "Pure silk")
    guide = f'''{nav}<article class="prose wrap"><span class="eyebrow">Guide</span><h1>Wool &amp; silk underwear: how to choose, wear and care for it</h1>
<p class="lead">Why so many people in cold climates swear by wool &amp; silk, and how to find the right layer for you.</p>
<h2>Why wear natural fibres next to your skin?</h2>
<p>The layer closest to your skin does the most work: it has to keep you warm, move moisture away and still feel comfortable for hours. Natural fibres like wool and silk do this better than most synthetics. They breathe, they regulate temperature, and they feel soft rather than clammy.</p>
<h2>Wool, silk or a blend?</h2>
<table><thead><tr><th>Material</th><th>Feels</th><th>Best for</th></tr></thead><tbody>
<tr><td><a class="link" href="/collections/wool-silk-underwear/">Wool &amp; silk</a> (85/15, 70/30)</td><td>Warm, soft, no itch</td><td>Autumn and winter, everyday base layer</td></tr>
<tr><td><a class="link" href="/collections/silk-underwear/">Pure silk</a> (100%)</td><td>Light, smooth, cool to the touch</td><td>Spring and summer, under fine blouses</td></tr>
<tr><td>Cotton, wool &amp; silk</td><td>Soft and breathable</td><td>All year, sensitive skin</td></tr></tbody></table>
<p><strong>Wool</strong> insulates even when it is slightly damp and handles moisture well, which is why it is the classic base layer for Nordic winters. <strong>Silk</strong> is one of the lightest natural fibres and feels very smooth. Blending a little silk into wool keeps the warmth but gives a softer, itch-free feel. We have {n_ws} wool-blend styles and {n_s} pure silk styles.</p>
{picture(by["arianna_ss-top"]["images"][0], "Ivory wool and silk camisole with lace front", "(max-width:760px) 100vw, 740px", (540, 720, 900))}
<h2>Which style is right for you?</h2>
<ul><li><a class="link" href="/collections/camisoles/">Camisoles</a>: thin straps, invisible under shirts and dresses.</li>
<li><a class="link" href="/collections/tank-tops/">Tank tops</a>: wider straps, more coverage and warmth on the shoulders.</li>
<li><a class="link" href="/collections/short-sleeve-tops/">Short-sleeve tops</a>: a little extra warmth under knitwear.</li>
<li><a class="link" href="/collections/long-sleeve-tops/">Long-sleeve tops &amp; turtlenecks</a>: the warmest layer for the coldest days.</li>
<li><a class="link" href="/collections/bodysuits/">Bodysuits</a>: stay tucked in for a smooth line.</li>
<li><a class="link" href="/collections/cardigans-boleros/">Cardigans &amp; boleros</a>: soft layers to wear on top.</li></ul>
<h2>Finding your size</h2>
<p>Our underwear comes in S, M and L and is designed to fit close to the body, like a second skin, so it disappears under your clothes. If you are between sizes or want a looser layer, choose the larger size. You can exchange sizes within 14 days.</p>
<h2>How to care for wool &amp; silk</h2>
<ul><li>Hand wash in cool water (max 30 °C) with a mild wool or silk detergent.</li><li>Skip fabric softener and bleach.</li><li>Don't wring. Press out water gently in a towel.</li><li>Dry flat, away from radiators and direct sunlight.</li><li>Air between wears. Wool freshens up naturally, so you need to wash it less often.</li></ul>
<p>Always follow the care label on your garment.</p>
<p><a class="btn" href="/collections/underwear/">Shop all underwear</a></p></article>'''
    art = {"@context": "https://schema.org", "@type": "Article", "headline": "Wool & silk underwear: how to choose, wear and care for it",
           "author": {"@type": "Organization", "name": BRAND}, "publisher": {"@id": SITE_URL + "/#org"}, "datePublished": "2026-10-21",
           "image": img_url(by["arianna_ss-top"]["images"][0], 1200), "mainEntityOfPage": SITE_URL + "/pages/wool-silk-guide/"}
    page("/pages/wool-silk-guide/", "Wool & Silk Underwear Guide: Choosing, Sizing & Care | Closetoskin",
         "How to choose between wool & silk and pure silk underwear, which style suits you, how to find your size and how to wash wool and silk.",
         guide, "/pages/wool-silk-guide/", [bld, art])
    # 404
    page("/404.html", "Page not found | Closetoskin", "This page could not be found.",
         '<div class="prose wrap" style="text-align:center"><h1>Page not found</h1><p class="lead" style="margin:0 auto 1.5em">The page you\'re looking for has moved or no longer exists.</p><a class="btn" href="/collections/underwear/">Shop underwear</a></div>')


def build_assets(products, best):
    ad = DIST / "assets"
    (ad / "fonts").mkdir(parents=True, exist_ok=True)
    for f in (ROOT / "src/fonts").glob("*.woff2"):
        shutil.copy(f, ad / "fonts" / f.name)
    shutil.copy(ROOT / "src/styles.css", ad / "styles.css")
    shutil.copy(ROOT / "src/app.js", ad / "app.js")
    (DIST / "favicon.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="14" fill="#2b2420"/><text x="32" y="44" font-family="Georgia,serif" font-size="36" text-anchor="middle" fill="#f7f3ee">c</text></svg>')
    best_ids = {p["handle"] for p in best}
    idx = [{"n": p["name"], "u": p["url"], "i": img_url(p["images"][0], 120) if p["images"] else "", "p": p["price"],
            "m": p["material"] or {"handbags": "Woven leather, made in Italy"}.get(p["cat"], "Jewellery"), "b": p["handle"] in best_ids,
            "k": " ".join([p["name"], p["code"], p["material"], p["cat"], p["group"] or "", "jewellery" if p["cat"] == "accessories" else "", " ".join(p["colours"])]).lower()}
           for p in products]
    (DIST / "search-index.json").write_text(json.dumps(idx, ensure_ascii=False, separators=(",", ":")))
    robots = "User-agent: *\nDisallow: /\n" if NOINDEX else f"User-agent: *\nAllow: /\n\nSitemap: {SITE_URL}/sitemap.xml\n"
    (DIST / "robots.txt").write_text(robots)


def write_sitemap():
    urls = "".join(f"<url><loc>{SITE_URL}{u}</loc><lastmod>2026-10-21</lastmod></url>" for u in SITEMAP)
    (DIST / "sitemap.xml").write_text(f'<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">{urls}</urlset>\n')


def asset_versions():
    h = lambda f: hashlib.md5((ROOT / f).read_bytes()).hexdigest()[:8]
    return {"css": h("src/styles.css"), "js": h("src/app.js")}


SITEMAP = []
ASSET_V = asset_versions()

if __name__ == "__main__":
    if DIST.exists():
        shutil.rmtree(DIST)
    DIST.mkdir()
    products, best = load_catalog()
    build_assets(products, best)
    build_home(products, best)
    build_collections(products, best)
    for p in products:
        build_product(p, products)
    build_pages(products)
    write_sitemap()
    print(f"built {len(SITEMAP)} indexable pages -> {DIST}")
