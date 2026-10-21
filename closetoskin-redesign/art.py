#!/usr/bin/env python3
"""Art-directed crops for version 3, made from the store's own high-resolution photos.

Downloads the three ~1900px homepage photos from the Shopify CDN, crops them inside their
baked-in rounded corners, applies one gentle colour treatment so they sit together, and
writes WebP files to src/img/. Run it again only if the source photos change.
"""
import io
import subprocess
from pathlib import Path
from PIL import Image, ImageEnhance

ROOT = Path(__file__).parent
OUT = ROOT / "src/img"
CDN = "https://cdn.shopify.com/s/files/1/0571/3733/2289/"
SRC = {8: CDN + "files/Image_new_1_Asset_8.png", 1: CDN + "files/Image_new_1_Asset_1.png", 7: CDN + "files/Image_new_1_Asset_7.png",
       "ilary": CDN + "products/30.png?width=800"}  # Ilary long-sleeve top, 85% wool / 15% silk
# name: (source asset, crop box in source pixels, output widths)
CROPS = {
    "hero": (8, (300, 50, 1850, 1400), (900, 1500)),
    "knit": ("ilary", (150, 300, 470, 620), (480,)),
    "lace": (8, (1150, 160, 1660, 760), (700,)),
    "weave": (1, (760, 1060, 1460, 1700), (700,)),
    "bag": (1, (330, 120, 1700, 1820), (900,)),
}


def grade(im):
    """One quiet treatment for every image: a touch less saturation, a little more depth."""
    im = ImageEnhance.Color(im).enhance(0.9)
    im = ImageEnhance.Contrast(im).enhance(1.05)
    r, g, b = im.split()
    r = r.point(lambda v: min(255, v * 1.012))
    b = b.point(lambda v: v * 0.985)
    return Image.merge("RGB", (r, g, b))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sources = {}
    for name, (asset, box, widths) in CROPS.items():
        if asset not in sources:
            raw = subprocess.run(["curl", "-sSf", SRC[asset]], check=True, capture_output=True).stdout
            sources[asset] = Image.open(io.BytesIO(raw)).convert("RGB")
        im = grade(sources[asset].crop(box))
        for w in widths:
            h = round(im.height * w / im.width)
            im.resize((w, h), Image.LANCZOS).save(OUT / f"{name}-{w}.webp", quality=82, method=6)
        print(name, im.size)
    hero = grade(sources[8].crop(CROPS["hero"][1]))
    strip = hero.crop((0, 0, 24, hero.height)).resize((1, 1), Image.BOX).getpixel((0, 0))
    print("hero wall colour #%02x%02x%02x" % strip)


if __name__ == "__main__":
    main()
