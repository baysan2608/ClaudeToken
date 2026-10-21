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
DIST = ROOT / os.environ.get("DIST", "dist")
SITE_URL = os.environ.get("SITE_URL", "https://www.closetoskin.com").rstrip("/")
NOINDEX = os.environ.get("NOINDEX") == "1"
# The live checkout (checked 21 Oct 2026) only returns shipping rates for Norway, where delivery is free.
# Set INTERNATIONAL=1 once international shipping zones are enabled in Shopify to restore the export messaging.
INTERNATIONAL = os.environ.get("INTERNATIONAL") == "1"
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

# Unique closing copy per landing page (SEO + help with choosing).
L = lambda href, text: f'<a class="link" href="{href}">{text}</a>'
COLLECTION_COPY = {
    "camisoles": f"<h2>How to wear a wool &amp; silk camisole</h2><p>A camisole is the most discreet base layer: thin straps disappear under shirts, knitwear and dresses, and the fine knit adds warmth without bulk. Choose {L('/collections/silk-underwear/', 'pure silk')} for a light, smooth layer in spring and summer, or {L('/collections/wool-silk-underwear/', 'wool &amp; silk')} for warmth from autumn to spring.</p><p>Want more coverage on the shoulders? Try a {L('/collections/tank-tops/', 'tank top')} or a {L('/collections/short-sleeve-tops/', 'short-sleeve top')}.</p>",
    "tank-tops": f"<h2>Tank tops: extra coverage, same light feel</h2><p>Wider shoulder straps make a tank top warmer than a camisole and easier to wear on its own. It sits neatly under blouses, jumpers and jackets, and works as an everyday base layer all year.</p><p>For the coldest days, step up to a {L('/collections/long-sleeve-tops/', 'long-sleeve top or turtleneck')}.</p>",
    "short-sleeve-tops": f"<h2>The in-between layer</h2><p>Short sleeves add warmth to the upper arms while staying invisible under knitwear and jackets, which makes them ideal for offices, travel and changeable weather.</p><p>Compare with our {L('/collections/camisoles/', 'camisoles')} and {L('/collections/long-sleeve-tops/', 'long-sleeve tops')}, or read the {L('/pages/wool-silk-guide/', 'wool &amp; silk guide')}.</p>",
    "long-sleeve-tops": f"<h2>The warmest everyday layer</h2><p>A long-sleeve top in wool &amp; silk is the classic Nordic base layer: thin enough to wear under a shirt or jumper, warm enough for winter. Turtlenecks and mock necks add warmth at the neck and look polished on their own.</p><p>Pair with {L('/collections/leggings-briefs/', 'matching leggings')} for a complete base layer, or add a {L('/collections/cardigans-boleros/', 'cardigan or bolero')} on top.</p>",
    "bodysuits": f"<h2>Why choose a bodysuit?</h2><p>A bodysuit stays tucked in all day, so there is no riding up and no extra fabric at the waistband. That means a smooth line under trousers and skirts and warmth where you need it most.</p><p>Prefer separates? See our {L('/collections/tank-tops/', 'tank tops')} and {L('/collections/long-sleeve-tops/', 'long-sleeve tops')}.</p>",
    "cardigans-boleros": f"<h2>Soft layers to wear on top</h2><p>Our cardigans, boleros and gilets are knitted in the same fine wool &amp; silk as our underwear, so they layer smoothly over a {L('/collections/camisoles/', 'camisole')} or {L('/collections/tank-tops/', 'tank top')} without bulk. A bolero warms the shoulders and arms; a cardigan gives full coverage.</p>",
    "leggings-briefs": f"<h2>Complete the base layer</h2><p>Wool &amp; silk leggings add warmth under trousers in winter and are comfortable on their own at home. Culotte briefs and shorts in silk and wool &amp; silk match our {L('/collections/camisoles/', 'camisoles')} and {L('/collections/long-sleeve-tops/', 'tops')}.</p>",
    "silk-underwear": f"<h2>Why pure silk?</h2><p>Silk is one of the lightest natural fibres. It feels smooth and cool to the touch, and stays comfortable in warm and cool weather alike, which makes it ideal under fine blouses and for summer. Our pure silk lines come in camisoles, tank tops, long-sleeve tops and culotte briefs.</p><p>Need more warmth? Choose {L('/collections/wool-silk-underwear/', 'wool &amp; silk')}. Read more in our {L('/pages/wool-silk-guide/', 'guide')}.</p>",
    "wool-silk-underwear": f"<h2>The best of both fibres</h2><p>Wool insulates and manages moisture, so you stay warm without overheating. Silk adds softness and takes away the itch some people feel from pure wool. Our blends (85% wool / 15% silk and 70% wool / 30% silk) make thin, second-skin layers for everyday wear from autumn to spring.</p><p>Find your style: {L('/collections/camisoles/', 'camisoles')}, {L('/collections/long-sleeve-tops/', 'long-sleeve tops')} or {L('/collections/bodysuits/', 'bodysuits')}.</p>",
    "handbags": f"<h2>What is the tubular method?</h2><p>Instead of weaving flat panels and sewing them together, the leather is woven by hand in a continuous tube. The result is a bag with no side seams: stronger, softer and more refined. Every bag is made in Italy and finished with gold-tone hardware.</p><p>Complete the look with handmade {L('/collections/accessories/', 'Venetian jewellery')}.</p>",
    "accessories": f"<h2>About Maria Calderara</h2><p>Maria Calderara is an Italian designer whose jewellery is handmade in Venice. Her long necklaces, bold bracelets, earrings and rings are made by hand in small numbers: pieces to wear every day and keep for years.</p><p>Browse by type: {L('/collections/necklaces/', 'necklaces')}, {L('/collections/bracelets/', 'bracelets')}, {L('/collections/earrings/', 'earrings')} and {L('/collections/rings/', 'rings')}.</p>",
}

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


def composition(raw):
    """'85% Wool 15% Silk' -> '85% wool · 15% silk' (set as a care label in CSS)."""
    return " · ".join(f"{n}% {fibre.lower()}" for n, fibre in re.findall(r"(\d+)\s*%\s*([A-Za-z]+)", raw or ""))


# Warmth follows wool content, the way Nordic base-layer brands grade their fabrics.
WARMTH = ["light", "medium", "warm", "warmest"]
WARMTH_NOTE = {
    "warmest": "Our warmest blend: 85% wool, 15% silk. For cold days and anyone who feels the cold.",
    "warm": "70% wool, 30% silk: warm without bulk, for every day from autumn to spring.",
    "medium": "Cotton, wool & silk: soft and breathable, for wearing all year.",
    "light": "Pure silk: light and smooth, for warm days and under fine blouses.",
}


def warmth(raw):
    if not raw:
        return None
    m = re.search(r"(\d+)\s*%\s*wool", raw, re.I)
    wool = int(m.group(1)) if m else 0
    return "warmest" if wool >= 80 else "warm" if wool >= 60 else "medium" if wool > 0 else "light"


def parse_underwear(title, mat=""):
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
        line = digits[:3]
        prefix = {"Pure silk": "Silk", "Wool & silk": "Wool & Silk"}.get(mat, mat)
        return f"{prefix} {style} {line}".strip(), style, group, line
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
            name, style, group, model = parse_underwear(p["title"], mat)
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
            "material_raw": p["material"], "material": mat, "comp": composition(p["material"]), "warmth": warmth(p["material"]), "facts": facts if cat != "underwear" else [], "desc": [d for d in desc if d],
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
    # eager="high" marks the one image most likely to be the largest paint
    load = 'fetchpriority="high" loading="eager"' if eager == "high" else 'loading="eager"' if eager else 'loading="lazy"'
    c = f' class="{cls}"' if cls else ""
    return (f'<img src="{img_url(im, ws[min(1, len(ws)-1)])}" srcset="{srcset}" sizes="{sizes}" width="{ws[-1]}" height="{h}" '
            f'alt="{e(alt)}" {load} decoding="async"{c}>')


ART = {"hero": (1550, 1350), "knit": (320, 320), "lace": (510, 600), "weave": (700, 640), "bag": (1370, 1700)}
ART_W = {"hero": (900, 1500), "knit": (480,), "lace": (700,), "weave": (700,), "bag": (900,)}


def art_src(name, w=None):
    return f"/assets/img/{name}-{w or ART_W[name][-1]}.webp"


def art(name, alt, sizes, eager=False):
    ws = ART_W[name]
    w, h = ART[name]
    srcset = ", ".join(f"{art_src(name, x)} {x}w" for x in ws)
    load = 'fetchpriority="high" loading="eager"' if eager else 'loading="lazy"'
    return (f'<img src="{art_src(name)}" srcset="{srcset}" sizes="{sizes}" width="{ws[-1]}" height="{round(ws[-1] * h / w)}" '
            f'alt="{e(alt)}" {load} decoding="async">')


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
    "chat": '<svg viewBox="0 0 24 24" fill="none" stroke-width="1.4" aria-hidden="true"><path d="M4 5h16v11H9l-5 4V5Z"/><path d="M8 9h8M8 12h5"/></svg>',
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
    mats += '<a href="/pages/wool-silk-guide/">Which fibre is right for me?</a>'
    jewel = "".join(f'<a href="/collections/{s}/">{v[1]}</a>' for s, v in JEWELLERY_TYPES.items())
    return f'''<a class="skip" href="#main">Skip to content</a>
<div class="announce">{"".join(f"<span>{e(x)}</span>" for x in PERKS)}</div>
<header class="header"><div class="wrap">
<button class="icon-btn menu-toggle" type="button" data-open-nav aria-label="Open menu" aria-expanded="false">{ICONS["menu"]}</button>
<a class="logo" href="/" aria-label="{BRAND} home">{logo()}</a>
<nav class="nav" aria-label="Main">
<button class="icon-btn nav-close" type="button" data-close aria-label="Close menu">{ICONS["close"]}</button>
{a("/collections/bestsellers/", "Bestsellers")}
<div class="has-menu">{a("/collections/underwear/", "Underwear")}<button class="sub-toggle" type="button" aria-expanded="false" aria-label="Underwear categories"></button><div class="menu"><strong>By style</strong>{styles}<strong>By fibre</strong>{mats}</div></div>
{a("/collections/handbags/", "Handbags")}
<div class="has-menu">{a("/collections/accessories/", "Jewellery")}<button class="sub-toggle" type="button" aria-expanded="false" aria-label="Jewellery categories"></button><div class="menu"><strong>By type</strong>{jewel}</div></div>
{a("/collections/gifts/", "Gifts")}
{a("/pages/wool-silk-guide/", "Guide")}
<div class="nav-extra"><a href="/pages/about-us/">Our story</a><a href="/pages/shipping-returns/">Shipping &amp; returns</a><a href="/pages/contact/">Contact us</a><a href="mailto:{EMAIL}">{EMAIL}</a></div>
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
<div><h2>Shop</h2><ul><li><a href="/collections/underwear/">Underwear</a></li><li><a href="/collections/silk-underwear/">Pure silk</a></li><li><a href="/collections/wool-silk-underwear/">Wool &amp; silk</a></li><li><a href="/collections/handbags/">Handbags</a></li><li><a href="/collections/accessories/">Jewellery</a></li><li><a href="/collections/gifts/">Gifts</a></li><li><a href="/collections/all/">Shop all</a></li></ul></div>
<div><h2>Help</h2><ul><li><a href="/pages/about-us/">Our story</a></li><li><a href="/pages/shipping-returns/">Shipping &amp; returns</a></li><li><a href="/pages/wool-silk-guide/">Fit &amp; care guide</a></li><li><a href="/pages/contact/">Contact us</a></li><li><a href="/pages/terms-conditions/">Terms &amp; conditions</a></li></ul></div>
<div><h2>Contact</h2><ul><li><a href="mailto:{EMAIL}">{EMAIL}</a></li><li>Mon–Fri, 08:00–16:00 CET</li><li>{ADDRESS["street"]}<br>{ADDRESS["zip"]} {ADDRESS["city"]}, Norway</li></ul></div>
</div>
<div class="legal"><span>© 2026 Closetoskin · Operated by Sommerfugl Consult, org. no. 813 728 362</span><span>Prices in NOK incl. Norwegian VAT</span></div>
</div></footer>
<div class="overlay" aria-hidden="true"></div>
<aside class="drawer" aria-label="Shopping bag"><header><h2>Your bag</h2><button class="icon-btn" id="cart-close" type="button" data-close aria-label="Close bag">{ICONS["close"]}</button></header>
<div class="lines" id="cart-lines"></div>
<footer><div class="sum"><span>Subtotal</span><span id="cart-total">NOK 0</span></div><p class="muted" style="margin:0;font-size:.85rem">Free delivery in Norway · VAT included{" · tax-free outside Norway" if INTERNATIONAL else ""}</p>
<a class="btn btn-block" id="checkout" href="#">Secure checkout</a></footer></aside>
<div class="search" role="dialog" aria-label="Search products"><div class="search-bar"><label class="sr-only" for="q">Search products</label><input id="q" type="search" placeholder="Search camisoles, silk, bags…" autocomplete="off"><button class="icon-btn" type="button" data-close aria-label="Close search">{ICONS["close"]}</button></div><ul class="results" id="results"></ul></div>'''


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
<meta name="theme-color" content="#f6f5f2">
<link rel="icon" href="/favicon.svg" type="image/svg+xml">
<link rel="preconnect" href="{CDN}">
<link rel="preload" href="/assets/fonts/hanken-var.woff2" as="font" type="font/woff2" crossorigin>
<link rel="preload" href="/assets/fonts/bodoni-400.woff2" as="font" type="font/woff2" crossorigin>
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
    meta = p["material"] or ("Made in Italy" if p["cat"] == "handbags" else "Handmade in Venice" if "Calderara" in p["name"] else "")
    alt = p["name"] + (f", {meta.lower()}" if meta else ", jewellery")
    label = p["comp"] or ("Hand-woven leather · Italy" if p["cat"] == "handbags" else "Handmade in Venice" if "Calderara" in p["name"] else "Jewellery")
    imgs = picture(ims[0], alt, "(max-width:700px) 50vw, (max-width:1024px) 33vw, 300px", (360, 540, 720), eager) if ims else ""
    hover = f' data-hover="{img_url(ims[1], 540)}"' if len(ims) > 1 else ""
    sw = "".join(f'<i style="background:{COLOURS.get(c, "#bbb")}" title="{e(LABEL_FIX.get(c, c))}"></i>' for c in p["colours"][:6])
    ncol = len(p["colours"])
    tag = f'<span class="tag">{ncol} colours</span>' if ncol > 1 else ""
    filt = " ".join(x for x in [p["group"] or "", "silk" if p["material"] == "Pure silk" else "woolsilk" if p["material"] else ""] if x)
    cols = " ".join(re.sub(r"\W", "", LABEL_FIX.get(c, c)).lower() for c in p["colours"])
    return (f'<article class="card" data-reveal data-f="{filt}" data-c="{cols}" data-p="{p["price"]:.0f}" data-d="{p["created"][:10]}" data-i="{p["i"] if pos is None else pos}">'
            f'<a href="{p["url"]}"><span class="media"{hover}>{imgs}{tag}</span><h3>{e(p["name"])}</h3></a>'
            f'<div class="meta">{e(label)}</div><div class="price">{money(p["price"])}</div>{f"<div class=swatches>{sw}</div>" if sw else ""}</article>')


ORG_LD = {
    "@context": "https://schema.org", "@type": "OnlineStore", "@id": SITE_URL + "/#org", "name": BRAND, "url": SITE_URL + "/",
    "logo": "https://www.closetoskin.com/cdn/shop/files/CTS_Logo_Final.png", "email": EMAIL, "foundingDate": "2009",
    "address": {"@type": "PostalAddress", "streetAddress": ADDRESS["street"], "postalCode": ADDRESS["zip"], "addressLocality": ADDRESS["city"], "addressCountry": ADDRESS["country"]},
    "sameAs": SOCIAL,
    "hasMerchantReturnPolicy": {"@type": "MerchantReturnPolicy", "applicableCountry": "NO", "returnPolicyCategory": "https://schema.org/MerchantReturnFiniteReturnWindow",
                                "merchantReturnDays": 14, "returnMethod": "https://schema.org/ReturnByMail", "returnFees": "https://schema.org/ReturnFeesCustomerResponsibility"},
}

PERKS = (["Free delivery in Norway", "Tax-free shopping outside Norway", "14-day returns & size exchanges"] if INTERNATIONAL
         else ["Free delivery in Norway", "14-day returns & size exchanges", "Oslo · since 2009"])
SHIP_META = "Free delivery in Norway, 14-day returns" + (", tax-free abroad" if INTERNATIONAL else "")
SHIP_LD = {"@type": "OfferShippingDetails", "shippingRate": {"@type": "MonetaryAmount", "value": 0, "currency": "NOK"},
           "shippingDestination": {"@type": "DefinedRegion", "addressCountry": "NO"},
           "deliveryTime": {"@type": "ShippingDeliveryTime",
                            "handlingTime": {"@type": "QuantitativeValue", "minValue": 0, "maxValue": 1, "unitCode": "DAY"},
                            "transitTime": {"@type": "QuantitativeValue", "minValue": 2, "maxValue": 5, "unitCode": "DAY"}}}
FAQ = [
    ("How long does delivery take?", "Delivery within Norway is free on every order and takes 2–5 business days with Posten. Orders ship Monday to Friday; weekend orders ship the next business day."
     + (" International orders ship with UPS: 2–5 days within Europe, 4–10 days to the rest of the world." if INTERNATIONAL else "")),
    ("Can I shop tax-free from outside Norway?", "Yes. Our prices include 25% Norwegian VAT. Customers outside Norway shop tax-free, but are responsible for any import duties and taxes in their own country.")
    if INTERNATIONAL else
    ("Do you ship outside Norway?", f"Our online checkout currently delivers within Norway. For orders to other countries, email {EMAIL} and we'll help you."),
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


# --------------------------------------------------------------------------- layer finder (shared with app.js)

FINDER_COLD = [("yes", "Yes, always"), ("some", "Sometimes"), ("no", "No, I run warm")]
FINDER_PREF = {"yes": ["warmest", "warm", "medium", "light"], "some": ["warm", "medium", "warmest", "light"], "no": ["light", "medium", "warm", "warmest"]}
FINDER_COVER = [("camisoles", "Thin straps"), ("tank-tops", "Wider straps"), ("short-sleeve-tops", "Short sleeves"),
                ("long-sleeve-tops", "Long sleeves"), ("bodysuits", "Bodysuit")]


def finder_items(products):
    return [{"n": p["name"], "u": p["url"], "i": img_url(p["images"][0], 540) if p["images"] else "", "p": p["price"],
             "g": p["group"], "w": p["warmth"], "c": p["comp"]} for p in products if p["cat"] == "underwear"]


def finder_pick(items, cold, cover):
    """Same ranking as app.js: closest warmth to the answer first, then price."""
    pref = FINDER_PREF[cold]
    return sorted((x for x in items if x["g"] == cover), key=lambda x: (pref.index(x["w"]), x["p"]))[:3]


def mini_card(x):
    return (f'<article class="card"><a href="{x["u"]}"><span class="media"><img src="{x["i"]}" alt="" width="540" height="675" loading="lazy" decoding="async"></span>'
            f'<h3>{e(x["n"])}</h3></a><div class="meta">{e(x["c"])}</div><div class="price">{money(x["p"])}</div></article>')


TILE_PICK = {"camisoles": "arianna_ss-top", "tank-tops": "fiordaliso_sl", "short-sleeve-tops": "emily_mm", "long-sleeve-tops": "ilary-ml-neckline",
             "bodysuits": "sara_ml_body", "cardigans-boleros": "marie-_ml_cardigan", "leggings-briefs": "ilary-fuseaux"}

CARE_ICONS = (
    '<svg viewBox="0 0 32 32" aria-hidden="true"><path d="M4 11l3 15h18l3-15"/><path d="M4 11c2-2 4-2 6 0s4 2 6 0 4-2 6 0 4 2 6 0"/><text x="16" y="23" text-anchor="middle" font-size="8" fill="currentColor" stroke="none" font-family="sans-serif">30</text></svg>'
    '<svg viewBox="0 0 32 32" aria-hidden="true"><path d="M16 5l13 22H3z"/><path d="M7 7l18 20M25 7L7 27"/></svg>'
    '<svg viewBox="0 0 32 32" aria-hidden="true"><path d="M5 5h22v22H5z"/><path d="M10 16h12"/></svg>'
    '<svg viewBox="0 0 32 32" aria-hidden="true"><path d="M5 5h22v22H5z"/><circle cx="16" cy="16" r="7"/><path d="M6 6l20 20M26 6L6 26"/></svg>')


# --------------------------------------------------------------------------- pages

def build_home(products, best):
    by = {p["handle"]: p for p in products}
    hero_sizes = "(max-width:860px) 100vw, 62vw"
    hero = f'''<section class="hero">
<div class="hero-media">{art("hero", "Woman in an ivory fine-knit camisole with a lace neckline", hero_sizes, eager=True)}</div>
<div class="hero-copy"><span class="eyebrow">Wool · Silk · Oslo since 2009</span>
<h1>The layer you <em>forget</em> you’re wearing</h1>
<p class="lead">Fine-knit camisoles, tops and bodysuits in wool &amp; silk and pure silk. Warm without bulk, and smooth against the skin.</p>
<div class="hero-cta"><a class="btn" href="/collections/underwear/">Shop underwear <span class="arrow" aria-hidden="true">→</span></a><a class="btn btn-ghost" href="#find-your-layer">Find your layer</a></div>
<ul class="ticks"><li>Free delivery in Norway</li><li>14-day size exchanges</li><li>Natural fibres</li></ul></div>
</section>'''
    facts = ["85% wool · 15% silk", "70% wool · 30% silk", "100% silk", "48% cotton · 44% wool · 8% silk",
             "Hand-woven leather · made in Italy", "Handmade in Venice", "Oslo · since 2009"]
    run = "".join(f"<span>{e(x)}</span>" for x in facts)
    strip = (f'<div class="labelstrip" role="note" aria-label="Our fibres: {e(", ".join(facts))}"><div class="track">'
             f'<div style="display:inline-flex" aria-hidden="true">{run}</div><div style="display:inline-flex" aria-hidden="true">{run}</div></div></div>')
    usp = f'''<div class="wrap usp">
<div>{ICONS["leaf"]}<span><strong>Natural fibres</strong>Wool, silk and cotton, worn close to the skin</span></div>
<div>{ICONS["truck"]}<span><strong>Free delivery in Norway</strong>2–5 days with Posten, on every order</span></div>
<div>{ICONS["return"]}<span><strong>14-day returns</strong>Easy size exchanges</span></div>
<div>{ICONS["chat"]}<span><strong>Personal service</strong>Real people in Oslo, Mon–Fri</span></div></div>'''
    tiles = ""
    for slug, (sing, plural, intro) in UNDERWEAR_STYLES.items():
        p = by.get(TILE_PICK.get(slug)) or next(x for x in products if x["group"] == slug)
        n = sum(1 for x in products if x["group"] == slug)
        tiles += (f'<a class="tile" data-reveal href="/collections/{slug}/"><span class="img">{picture(p["images"][0], "", "(max-width:700px) 62vw, 280px", (360, 540))}</span>'
                  f'<span class="name">{e(plural)}<small>{n}</small></span></a>')
    items = finder_items(products)
    default = finder_pick(items, "some", "camisoles")
    cold_btns = "".join(f'<button type="button" data-cold="{k}" aria-pressed="{str(k == "some").lower()}">{e(v)}</button>' for k, v in FINDER_COLD)
    cover_btns = "".join(f'<button type="button" data-cover="{k}" aria-pressed="{str(k == "camisoles").lower()}">{e(v)}</button>' for k, v in FINDER_COVER)
    fdata = json.dumps({"items": items, "pref": FINDER_PREF, "notes": WARMTH_NOTE,
                        "fibre": {"light": "pure silk", "medium": "cotton, wool & silk", "warm": "70/30 wool & silk", "warmest": "85/15 wool & silk"},
                        "names": {k: UNDERWEAR_STYLES[k][1].lower() for k, _ in FINDER_COVER}}, ensure_ascii=False).replace("</", "<\\/")
    # Calderara pieces photographed on white (most of the catalogue has grey bars baked into the photo)
    jewels = [by[h] for h in ("1s051", "1s403b", "1s125n", "1s055b")]
    body = f'''{hero}
{strip}
{usp}
<section class="section"><div class="wrap">
<div class="section-head" data-reveal><div><span class="eyebrow">Bestsellers</span><h2>The ones people <em>come back</em> for</h2></div><a class="link" href="/collections/bestsellers/">Shop all bestsellers</a></div>
<div class="grid">{"".join(card(p) for p in best[:4])}</div></div></section>
<section class="section band"><div class="wrap">
<div class="section-head" data-reveal><div><span class="eyebrow">Underwear</span><h2>Shop by layer</h2></div><a class="link" href="/collections/underwear/">All {sum(1 for p in products if p["cat"] == "underwear")} styles</a></div>
<div class="scroller">{tiles}</div></div></section>
<section class="section silk" id="find-your-layer"><div class="wrap finder">
<div data-reveal><span class="eyebrow">Find your layer</span><h2>Which layer is <em>right</em> for you?</h2>
<p class="muted">Warmth follows wool content: the more wool, the warmer the layer. Silk keeps it light and smooth.</p>
<fieldset><legend>Do you get cold easily?</legend><div class="chips">{cold_btns}</div></fieldset>
<fieldset><legend>How much coverage do you like?</legend><div class="chips">{cover_btns}</div></fieldset></div>
<div class="finder-out" aria-live="polite"><p id="finder-note">{e(WARMTH_NOTE[default[0]["w"]])}</p>
<div class="grid" id="finder-results">{"".join(mini_card(x) for x in default)}</div>
<p style="margin-top:20px"><a class="link" id="finder-more" href="/collections/camisoles/">See all camisoles</a></p></div>
</div><script type="application/json" id="finder-data">{fdata}</script></section>
<section class="section"><div class="wrap">
<div class="section-head" data-reveal><div><span class="eyebrow">Up close</span><h2>Made to be <em>felt</em></h2></div><a class="link" href="/pages/wool-silk-guide/">Read the wool &amp; silk guide</a></div>
<div class="textures">
<div class="texture" data-reveal><div class="img">{art("knit", "Close-up of a navy wool and silk knit", "(max-width:860px) 50vw, 400px")}</div><span class="label">85% wool · 15% silk</span><h3>Fine wool &amp; silk</h3><p>A thin, close knit that keeps you warm and disappears under your clothes.</p></div>
<div class="texture" data-reveal><div class="img">{art("lace", "Close-up of a lace strap on an ivory camisole", "(max-width:860px) 50vw, 400px")}</div><span class="label">The details</span><h3>Soft lace edges</h3><p>Lace trims on selected camisoles and tops, soft enough to wear all day.</p></div>
<div class="texture" data-reveal><div class="img">{art("weave", "Close-up of hand-woven dark brown leather", "(max-width:860px) 100vw, 400px")}</div><span class="label">The leather</span><h3>Woven by hand in Italy</h3><p>Woven in one continuous tube, so our bags have no side seams.</p></div>
</div></div></section>
<section class="section dark"><div class="wrap split">
<div data-reveal><span class="eyebrow">Handbags</span><h2>Woven by hand. <em>Seamless</em> by design.</h2><p class="muted">Each bag is woven from soft leather in Italy using the tubular method: one continuous weave, no side seams, finished with gold-tone hardware. From the evening pochette to the everyday shopper.</p>
<a class="btn" href="/collections/handbags/">Shop handbags · from {money(min(p["price"] for p in products if p["cat"] == "handbags"))}</a></div>
<div data-reveal>{art("bag", "Dark brown hand-woven leather shopper", "(max-width:860px) 100vw, 50vw")}</div></div></section>
<section class="section"><div class="wrap">
<div class="section-head" data-reveal><div><span class="eyebrow">Jewellery</span><h2>Handmade in <em>Venice</em></h2></div><a class="link" href="/collections/accessories/">Shop jewellery</a></div>
<div class="grid">{"".join(card(p) for p in jewels)}</div></div></section>
<section class="section silk"><figure class="quote wrap" data-reveal><span class="eyebrow">Our story</span>
<blockquote>“That feeling of silk against the body is what makes us feel <em>at home.</em>”</blockquote>
<figcaption><span class="label">The founders · Oslo, since 2009</span><p style="margin:14px 0 0"><a class="link" href="/pages/about-us/">Read our story</a></p></figcaption></figure></section>
<section class="section"><div class="wrap"><div class="section-head" data-reveal><div><span class="eyebrow">Good to know</span><h2>Questions, answered</h2></div><a class="link" href="/pages/shipping-returns/">Shipping &amp; returns</a></div>{faq_html(FAQ[:4])}</div></section>
<section class="section dark"><div class="wrap split" style="align-items:center">
<div><h2>Letters from <em>Oslo</em></h2><p class="muted">New colours, restocks and the occasional private sale. No spam, unsubscribe anytime.</p></div>
<form class="inline-form" method="post" action="{SITE_URL}/contact#newsletter"><input type="hidden" name="form_type" value="customer"><input type="hidden" name="utf8" value="✓"><input type="hidden" name="contact[tags]" value="newsletter">
<label class="sr-only" for="nl">Email</label><input id="nl" type="email" name="contact[email]" required placeholder="Your email" autocomplete="email"><button class="btn" type="submit">Subscribe</button></form></div></section>'''
    site_ld = {"@context": "https://schema.org", "@type": "WebSite", "name": BRAND, "url": SITE_URL + "/", "publisher": {"@id": SITE_URL + "/#org"}}
    srcset = ", ".join(f"{art_src('hero', w)} {w}w" for w in ART_W["hero"])
    page("/", "Wool & Silk Underwear and Woven Leather Bags | Closetoskin",
         f"Fine wool & silk and pure silk underwear: camisoles, long-sleeve tops and bodysuits, plus hand-woven Italian leather bags. {SHIP_META}.",
         body, "/", [ORG_LD, site_ld], SITE_URL + art_src("hero"), (art_src("hero", 900), srcset, hero_sizes))


def build_gifts(products, best):
    nav, bld = crumbs([("Home", "/"), ("Gifts", None)])
    best_ids = {p["handle"] for p in best}
    # briefs and leggings are left out: they don't make easy gifts
    pool = sorted((p for p in products if p["group"] != "leggings-briefs"), key=lambda p: (p["handle"] not in best_ids, p["cat"] != "underwear", p["i"]))
    statement = sorted((p for p in products if p["price"] > 1500), key=lambda p: (p["cat"] != "handbags", -p["price"]))
    tiers = [("Under NOK 500", "Camisoles and tops in wool & silk and pure silk: a gift that gets worn every day.", [p for p in pool if p["price"] < 500]),
             ("NOK 500 to 1,500", "Long-sleeve layers and cardigans, plus handmade Venetian earrings and bracelets.",
              sorted((p for p in pool if 500 <= p["price"] <= 1500), key=lambda p: (p["cat"] == "accessories" and "Calderara" not in p["name"], p["cat"] != "underwear"))),
             ("Statement gifts", "Hand-woven Italian leather bags and sculptural jewellery by Maria Calderara.", statement)]
    sections = "".join(f'''<section class="section" style="padding-top:28px"><div class="section-head" data-reveal><div><h2>{e(t)}</h2><p class="muted" style="margin:0">{e(d)}</p></div></div>
<div class="grid">{"".join(card(p) for p in items[:8])}</div></section>''' for t, d, items in tiers)
    body = f'''{nav}<div class="wrap"><header class="col-head"><span class="eyebrow">Gift ideas</span><h1>Gifts, by <em>budget</em></h1>
<p class="lead">Natural-fibre layers, hand-woven bags and handmade Venetian jewellery. Free delivery in Norway and 14-day exchanges.</p></header>{sections}</div>'''
    shown = [p for _, _, items in tiers for p in items[:8]]
    item_ld = {"@context": "https://schema.org", "@type": "CollectionPage", "name": "Gifts", "url": SITE_URL + "/collections/gifts/",
               "mainEntity": {"@type": "ItemList", "numberOfItems": len(shown), "itemListElement": [
                   {"@type": "ListItem", "position": i + 1, "url": SITE_URL + p["url"], "name": p["name"]} for i, p in enumerate(shown)]}}
    page("/collections/gifts/", "Gift Ideas in Silk, Wool & Leather | Closetoskin",
         "Gift ideas from Closetoskin: wool & silk and pure silk underwear under NOK 500, Venetian jewellery and hand-woven Italian leather bags. Free delivery in Norway.",
         body, "/collections/gifts/", [bld, item_ld], img_url(statement[0]["images"][0], 1200))


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
{tool}<h2 class="sr-only">Products</h2><div class="grid" id="grid">{"".join(card(p, "high" if k == 0 else k < 4, k) for k, p in enumerate(items))}</div>
{f'<section class="section seo-copy">{seo}</section>' if seo else '<div style="height:80px"></div>'}</div>'''
    item_ld = {"@context": "https://schema.org", "@type": "CollectionPage", "name": h1, "description": desc, "url": SITE_URL + path,
               "mainEntity": {"@type": "ItemList", "numberOfItems": len(items), "itemListElement": [
                   {"@type": "ListItem", "position": i + 1, "url": SITE_URL + p["url"], "name": p["name"]} for i, p in enumerate(items)]}}
    og = img_url(items[0]["images"][0], 1200) if items and items[0]["images"] else None
    pre = None
    if items and items[0]["images"]:
        im = items[0]["images"][0]
        pre = (img_url(im, 540), ", ".join(f"{img_url(im, w)} {w}w" for w in (360, 540, 720)), "(max-width:700px) 50vw, (max-width:1024px) 33vw, 300px")
    page(path, title, desc, body, crumb[1][1] if len(crumb) > 2 else path, [bld, item_ld], og, pre)


def build_collections(products, best):
    best_ids = {p["handle"] for p in best}
    # featured order: bestsellers, then named lines (lifestyle photography), then numbered styles; bottoms last
    under = sorted((p for p in products if p["cat"] == "underwear"),
                   key=lambda p: (p["handle"] not in best_ids, p["group"] == "leggings-briefs", p["model"].isdigit(), p["model"], p["i"]))
    bags = [p for p in products if p["cat"] == "handbags"]
    jew = [p for p in products if p["cat"] == "accessories"]
    # put the most giftable / recognisable pieces first
    jew.sort(key=lambda p: (0 if "Calderara" in p["name"] else 1, -p["price"]))
    home = ("Home", "/")
    style_pills = [("All underwear", "/collections/underwear/")] + [(v[1], f"/collections/{s}/") for s, v in UNDERWEAR_STYLES.items()]
    seo_under = '''<h2>Wool &amp; silk underwear for every season</h2>
<p>Closetoskin underwear is knitted from natural fibres that work with your body: wool to keep you warm and dry, silk for a smooth, light feel against the skin. Choose a thin-strap camisole as an invisible base layer, a tank top for more coverage, or a long-sleeve top and turtleneck for the coldest days.</p>
<p>Every piece comes in sizes S, M and L with a close, second-skin fit, in timeless colours like black, ivory, ash grey and powder. Not sure where to start? Read our <a class="link" href="/pages/wool-silk-guide/">wool &amp; silk guide</a>.</p>'''
    build_collection("/collections/underwear/", "Wool & silk underwear", "Wool & Silk Underwear for Women | Closetoskin",
                     f"Shop {len(under)} styles of wool & silk and pure silk underwear: camisoles, tank tops, long-sleeve tops, bodysuits and cardigans in S–L. {SHIP_META}.",
                     "Fine-knit layers in wool & silk, pure silk and cotton blends: warm, breathable and soft enough to wear all day.",
                     under, [home, ("Underwear", None)], style_pills, seo=seo_under)
    for slug, (sing, plural, intro) in UNDERWEAR_STYLES.items():
        items = [p for p in under if p["group"] == slug]
        build_collection(f"/collections/{slug}/", plural, f"{plural} in Wool & Silk | Closetoskin",
                         f"{intro} {len(items)} styles in S–L. {SHIP_META}.",
                         intro, items, [home, ("Underwear", "/collections/underwear/"), (plural, None)], style_pills, seo=COLLECTION_COPY.get(slug, ""))
    for slug, (h1, intro) in MATERIALS.items():
        items = [p for p in under if (p["material"] == "Pure silk") == (slug == "silk-underwear")]
        build_collection(f"/collections/{slug}/", h1, f"{h1} for Women | Closetoskin",
                         f"{intro} {len(items)} styles from camisoles to long-sleeve tops. {SHIP_META}.",
                         intro, items, [home, ("Underwear", "/collections/underwear/"), (h1, None)], style_pills, seo=COLLECTION_COPY.get(slug, ""))
    build_collection("/collections/handbags/", "Hand-woven leather handbags", "Hand-Woven Italian Leather Handbags | Closetoskin",
                     "Seamless hand-woven leather handbags made in Italy using the tubular method: shopper, hobo, pochette and mini messenger. Free size exchanges, 14-day returns.",
                     "Woven by hand in Italy using the tubular method, so every bag is seamless. Soft leather, gold-tone hardware, made to last for years.",
                     bags, [home, ("Handbags", None)], filters=False, seo=COLLECTION_COPY["handbags"])
    jpills = [("All jewellery", "/collections/accessories/")] + [(v[1], f"/collections/{s}/") for s, v in JEWELLERY_TYPES.items()]
    build_collection("/collections/accessories/", "Jewellery", "Maria Calderara Jewellery – Handmade in Venice | Closetoskin",
                     "Necklaces, bracelets and earrings by Italian designer Maria Calderara, handmade in Venice, plus pearl and solitaire studs and pendants.",
                     "Sculptural necklaces, bracelets and earrings by Italian designer Maria Calderara, handmade in Venice, alongside fine pearl and solitaire pieces.",
                     jew, [home, ("Jewellery", None)], jpills, seo=COLLECTION_COPY["accessories"])
    for slug, (sing, plural, intro) in JEWELLERY_TYPES.items():
        items = [p for p in jew if p["group"] == slug]
        build_collection(f"/collections/{slug}/", plural, f"{plural} – Handmade Jewellery | Closetoskin", intro, intro, items,
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
    slides = "".join(picture(im, f"{alt_base} – view {k + 1}", "(max-width:900px) 100vw, 55vw", (540, 720, 900, 1200), "high" if k == 0 else False) for k, im in enumerate(ims))
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
    pjson = {"name": p["name"], "url": p["url"], "price": p["price"], "img": img_url(ims[0], 200) if ims else "", "options": [o["name"] for o in p["options"]], "labels": labels, "variants": variants}
    # accordion
    details_list = "".join(f"<li>{e(f)}</li>" for f in p["facts"]) if p["facts"] else ""
    desc_html = "".join(f"<p>{e(d)}</p>" for d in p["desc"]) + (f"<ul>{details_list}</ul>" if details_list else "")
    mat_block = ""
    if p["cat"] == "underwear":
        mat_block = f'''<details><summary>Material &amp; care</summary><div class="body"><p><strong>Composition:</strong> {e(p["material_raw"])}</p>
<p>Hand wash in cool water with a mild wool or silk detergent. Don't wring; dry flat away from direct heat. Always check the care label.</p></div></details>
<details><summary>Size &amp; fit</summary><div class="body"><p>Close, second-skin fit in S, M and L. Between sizes or prefer a looser layer? Size up. Exchanges to another size are free of hassle within 14 days.</p></div></details>'''
    acc = f'''<div class="acc"><details open><summary>Description</summary><div class="body">{desc_html}</div></details>{mat_block}
<details><summary>Delivery &amp; returns</summary><div class="body"><p>Free delivery within Norway in 2–5 business days with Posten{"; 2–5 days within Europe and 4–10 days worldwide with UPS" if INTERNATIONAL else ""}. Orders ship Monday–Friday. Returns and size exchanges within 14 days of delivery. <a class="link" href="/pages/shipping-returns/">Full policy</a></p></div></details></div>'''
    chips = "".join(f'<span class="chip">{e(c)}</span>' for c in [p["material"], "Made in Italy" if p["cat"] == "handbags" else "", "Handmade in Venice" if "Calderara" in p["name"] else ""] if c)
    code = f'<div class="code">Style {e(p["code"])}</div>' if p["code"] != p["name"] else ""
    # related: same model line first, then same group
    line = [x for x in products if x is not p and p["model"] and x["model"] == p["model"]]
    same = [x for x in products if x is not p and x["group"] == p["group"] and x not in line]
    rel = (line + same + [x for x in products if x["cat"] == p["cat"] and x is not p and x not in line and x not in same])[:4]
    care = ""
    if p["cat"] == "underwear":
        steps = "".join(f'<span class="{"on" if w == p["warmth"] else "fill" if WARMTH.index(w) < WARMTH.index(p["warmth"]) else ""}">{w.capitalize()}</span>' for w in WARMTH)
        care = f'''<div class="care"><span class="label">Composition</span><div class="comp">{e(p["comp"])}</div>
<div class="warmth"><span class="label">Warmth, by wool content: {p["warmth"]}</span><div class="scale" aria-hidden="true">{steps}</div></div>
<div class="icons-row" role="img" aria-label="Care: hand wash at 30 degrees, do not bleach, dry flat, do not tumble dry">{CARE_ICONS}</div>
<small>General care for wool and silk. Always follow the label in the garment.</small></div>'''
    elif p["cat"] == "handbags":
        care = '<div class="care"><span class="label">Made of</span><div class="comp">Hand-woven leather · made in Italy</div></div>'
    elif "Calderara" in p["name"]:
        care = '<div class="care"><span class="label">Made by</span><div class="comp">Maria Calderara · handmade in Venice</div></div>'
    complete = ""
    if p["cat"] == "underwear":
        def layer_pick(group):
            pool = sorted((x for x in products if x["group"] == group and x is not p and x not in rel), key=lambda x: (x["warmth"] != p["warmth"], x["i"]))
            return pool[0] if pool else None
        groups = [g for g in ("cardigans-boleros", "leggings-briefs", "camisoles", "long-sleeve-tops") if g != p["group"]][:3]
        layer = [x for x in (layer_pick(g) for g in groups) if x]
        layer.append(next(x for x in products if x["cat"] == "accessories" and x["group"] == "bracelets" and "Calderara" in x["name"]))
        complete = (f'<section class="section" style="padding-top:8px"><div class="section-head" data-reveal><div><span class="eyebrow">Wear it with</span><h2>Complete the <em>layer</em></h2></div></div>'
                    f'<div class="grid">{"".join(card(x) for x in layer)}</div></section>')
    rel_title = (f"More from the {p['model']} line" if not p["model"].isdigit() else f"More from line {p['model']}") if line else "You may also like"
    body = f'''{nav}<div class="wrap"><div class="pdp">
<div><div class="gallery">{f'<div class="thumbs">{thumbs}</div>' if thumbs else ''}<div class="slides">{slides}</div></div><div class="dots" aria-hidden="true">{dots}</div></div>
<div class="buy">{code}<h1>{e(p["name"])}</h1><div>{chips}</div>
<div class="price" data-price>{money(p["price"])}</div><div class="tax">Incl. 25% VAT · free delivery in Norway{" · tax-free abroad" if INTERNATIONAL else ""}</div>
<form id="buy" onsubmit="return false">{fs}
<div class="atc"><button class="btn btn-block" id="atc-main" type="submit" data-atc>Add to bag — {money(p["price"])}</button><span id="stock" class="stock">{"In stock, ready to ship" if p["available"] else "Currently sold out"}</span></div></form>
<ul class="assure"><li>{ICONS["truck"]}<span>Free delivery in Norway, 2–5 business days</span></li><li>{ICONS["return"]}<span>14-day returns &amp; size exchanges</span></li><li>{f'{ICONS["globe"]}<span>Tax-free shopping outside Norway</span>' if INTERNATIONAL else f'{ICONS["chat"]}<span>Questions about fit? <a class="link" href="mailto:{EMAIL}">Email our Oslo team</a></span>'}</li></ul>
{care}{acc}</div></div>
<section class="section" style="padding-top:24px"><div class="section-head"><h2>{e(rel_title)}</h2><a class="link" href="{trail[-1][1]}">View all</a></div><div class="grid">{"".join(card(x) for x in rel)}</div></section>{complete}</div>
<div class="sticky-atc" aria-hidden="true"><div><strong>{e(p["name"])}</strong><span data-price>{money(p["price"])}</span></div><button class="btn" type="button" data-atc tabindex="-1">Add to bag</button></div>
<script type="application/json" id="pdata">{json.dumps(pjson, ensure_ascii=False)}</script>'''
    offers = [{"@type": "Offer", "url": f"{SITE_URL}{p['url']}?variant={v['id']}", "priceCurrency": "NOK", "price": v["price"],
               "availability": "https://schema.org/" + ("InStock" if v["available"] else "OutOfStock"), "itemCondition": "https://schema.org/NewCondition",
               "name": v["title"] if v["title"] != "Default Title" else p["name"], "seller": {"@id": SITE_URL + "/#org"}, "shippingDetails": SHIP_LD} for v in p["variants"]]
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
    desc = f"{p['name']}{mat.lower()}" + (f" in {cols}" if cols and not mat else f". {cols}" if cols else "") + f". {money(p['price'])}. {SHIP_META}."
    pre = None
    if ims:
        ws = [w for w in (540, 720, 900, 1200) if w <= max(ims[0]["w"], 360)] or [540]
        pre = (img_url(ims[0], ws[min(1, len(ws) - 1)]), ", ".join(f"{img_url(ims[0], w)} {w}w" for w in ws), "(max-width:900px) 100vw, 55vw")
    page(p["url"], title, desc[:300], body, cat_url, [bld, prod_ld], img_url(ims[0], 1200) if ims else None, pre)


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
    page("/pages/about-us/", "Our Story – Natural Basics from Oslo | Closetoskin",
         "Closetoskin was founded in Oslo in 2009 to make modern basics in natural wool, silk, cotton and cashmere: comfortable, timeless and made to last.",
         about, "/pages/about-us/", [bld, ORG_LD])
    # Shipping & returns
    nav, bld = crumbs([("Home", "/"), ("Shipping & returns", None)])
    ship = f'''{nav}<article class="prose wrap"><h1>Shipping &amp; returns</h1>
<p class="lead">Free delivery in Norway, fast dispatch from Oslo and 14 days to change your mind.</p>
<h2>Delivery</h2>
<table><thead><tr><th>Destination</th><th>Carrier</th><th>Delivery time</th><th>Cost</th></tr></thead><tbody>
<tr><td>Norway</td><td>Posten</td><td>2–5 business days</td><td>Free</td></tr>{'<tr><td>Europe</td><td>UPS International Economy</td><td>2–5 business days</td><td>At checkout</td></tr><tr><td>Rest of the world</td><td>UPS International Economy</td><td>4–10 business days</td><td>At checkout</td></tr>' if INTERNATIONAL else ''}</tbody></table>
{'' if INTERNATIONAL else f'<p>Ordering from outside Norway? Our online checkout currently delivers within Norway. Email <a class="link" href="mailto:{EMAIL}">{EMAIL}</a> and we will help you.</p>'}
<p>Orders ship Monday to Friday; weekend orders ship the next business day. In Norway, Posten delivers to your local pick-up point and notifies you by SMS or email.</p>
<h2>Prices, VAT &amp; duties</h2>
<p>All prices are in NOK and include 25% Norwegian VAT.{" Customers outside Norway shop tax-free. Import duties and local taxes, where they apply, are paid by the customer." if INTERNATIONAL else " Delivery within Norway is always free."}</p>
<h2>Returns &amp; exchanges</h2>
<ul><li>Return or exchange within <strong>14 days</strong> of delivery.</li><li>Items must be in perfect condition with tags and seal attached.</li><li>Size exchanges are processed when we receive your return, subject to stock.</li><li>Refunds go back to the original payment method. Return shipping is paid by the customer.</li><li>The same policy applies during sales.</li></ul>
<p>Send returns to: <strong>Closetoskin, {ADDRESS["street"]}, {ADDRESS["zip"]} {ADDRESS["city"]}, Norway</strong>. Need help? Email <a class="link" href="mailto:{EMAIL}">{EMAIL}</a>.</p>
<h2>FAQ</h2>{faq_html(FAQ)}</article>'''
    page("/pages/shipping-returns/", "Free Delivery in Norway, Shipping & Returns | Closetoskin",
         "Free delivery on every order in Norway, in 2–5 business days with Posten. 14-day returns and size exchanges. All prices include VAT.",
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
    page("/pages/wool-silk-guide/", "Wool & Silk Underwear Guide: Fit & Care | Closetoskin",
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
    shutil.copytree(ROOT / "src/img", ad / "img")
    css = re.sub(r"/\*.*?\*/", "", (ROOT / "src/styles.css").read_text(), flags=re.S)
    (ad / "styles.css").write_text(re.sub(r"\n+", "", css))
    js = (ROOT / "src/app.js").read_text().splitlines()
    (ad / "app.js").write_text("\n".join(l.strip() for l in js if l.strip() and not l.strip().startswith("//")))
    (DIST / "_redirects").write_text("\n".join([
        "/pages/photo-gallery /collections/all/ 301",
        "/pages/returns-exchanges /pages/shipping-returns/ 301",
        "/collections/frontpage /collections/bestsellers/ 301",
        "/collections/frontpage/ /collections/bestsellers/ 301",
        "/collections/:c/products/:h /products/:h/ 301",
        "/policies/refund-policy /pages/shipping-returns/ 301",
        "/policies/shipping-policy /pages/shipping-returns/ 301",
        "/policies/* /pages/terms-conditions/ 301",
    ]) + "\n")
    (DIST / "_headers").write_text("/assets/*\n  Cache-Control: public, max-age=31536000, immutable\n"
                                   "/*\n  X-Content-Type-Options: nosniff\n  Referrer-Policy: strict-origin-when-cross-origin\n"
                                   "  Permissions-Policy: camera=(), microphone=(), geolocation=()\n" + ("  X-Robots-Tag: noindex\n" if NOINDEX else ""))
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
    build_gifts(products, best)
    for p in products:
        build_product(p, products)
    build_pages(products)
    write_sitemap()
    print(f"built {len(SITEMAP)} indexable pages -> {DIST}")
