#!/usr/bin/env python3
"""Build a private review copy of the site for sharing as a claude.ai Artifact.

Differences from the production build (build.py):
- every page carries a "preview" banner; checkout and forms are switched off
- search engines are told to stay away (noindex)
- links are relative, and CSS, JS and fonts are inlined, because the viewer serves the site
  from a sub-path and blocks other hosts
- product photos are bundled in preview/img/ instead of loaded from the Shopify CDN

    python3 preview.py                  # writes preview/ (index.html is the entry page)
    python3 preview.py preview-v3 "v3"  # a separately published copy, labelled as version 3
"""
import base64
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).parent
OUT = ROOT / (sys.argv[1] if len(sys.argv) > 1 else "preview")
VERSION = sys.argv[2] if len(sys.argv) > 2 else ""
IMG_WIDTH = 900
CDN_RE = re.compile(r'https://cdn\.shopify\.com/s/files/1/0571/3733/2289/([^"?\s]+)\?(?:v=[^&"]*&(?:amp;)?)?width=\d+')
BANNER = ('<div class="preview-bar" role="note">Closetoskin redesign preview{version} · not the live store · '
          'checkout and forms are turned off</div>')
BANNER_CSS = (".preview-bar{background:#285a5e;color:#fff;font-size:.78rem;letter-spacing:.03em;text-align:center;"
              "padding-block:8px;padding-left:max(16px,env(safe-area-inset-left,0px));padding-right:max(16px,env(safe-area-inset-right,0px))}")


def flat(path):
    return re.sub(r"[^A-Za-z0-9._-]", "_", path) + ".webp"


def local_link(path, prefix):
    if path == "":
        return prefix + "index.html"
    return prefix + path + ("index.html" if path.endswith("/") else "")


def main():
    if OUT.exists():
        shutil.rmtree(OUT)
    subprocess.run([sys.executable, str(ROOT / "build.py")], env={**os.environ, "NOINDEX": "1", "DIST": OUT.name}, check=True)

    # embed the exact font files the site ships (the viewer blocks font files from anywhere but Google Fonts),
    # so the preview has the same cuts, weights and hierarchy as the real build
    css = (OUT / "assets/styles.css").read_text()
    for font in (OUT / "assets/fonts").glob("*.woff2"):
        data = base64.b64encode(font.read_bytes()).decode()
        css = css.replace(f"url(/assets/fonts/{font.name})", f"url(data:font/woff2;base64,{data})")
    assert "/assets/fonts/" not in css, "a font was not embedded"
    css += BANNER_CSS
    js = (OUT / "assets/app.js").read_text()

    images = set()
    pages = [p for p in OUT.rglob("*.html") if p.name != "404.html"]
    for page in pages:
        depth = len(page.relative_to(OUT).parts) - 1
        prefix = "../" * depth
        s = page.read_text()
        images.update(m.group(1) for m in CDN_RE.finditer(s))
        s = CDN_RE.sub(lambda m: "/img/" + flat(m.group(1)), s)
        s = re.sub(r'\s(?:srcset|sizes)="[^"]*"', "", s)
        s = re.sub(r'<link rel="(?:preload|preconnect)"[^>]*>\n?', "", s)
        s = re.sub(r'<link rel="stylesheet" href="/assets/styles\.css[^"]*">', lambda m: f"<style>{css}</style>", s)
        s = re.sub(r'<script src="/assets/app\.js[^"]*" defer></script>\n?', "", s)
        s = s.replace("</body>", f'<script>window.CTS_ROOT="{prefix}";window.CTS_PREVIEW=1;</script><script>{js}</script>\n</body>')
        for _ in range(2):
            s = re.sub(r'(<form[^>]*?)\s(?:method|action)="[^"]*"', r"\1", s)
        s = re.sub(r'\b(href|src|data-hover)="/([^"/][^"]*|)"', lambda m: f'{m.group(1)}="{local_link(m.group(2), prefix)}"', s)
        s = s.replace("<body>\n", "<body>\n" + BANNER.format(version=f" ({VERSION})" if VERSION else "") + "\n", 1)
        if depth == 0 and page.name == "index.html":
            # the viewer wraps the entry page in its own document skeleton
            s = re.sub(r"<title>.*?</title>", f"<title>Closetoskin Redesign {VERSION or 'Preview'}</title>", s, count=1)
            for tag in ("<!doctype html>\n", '<html lang="en">\n', "<head>\n", "</head>\n", "<body>\n", "</body>\n", "</html>",
                        '<meta charset="utf-8">\n', '<meta name="viewport" content="width=device-width,initial-scale=1">\n'):
                s = s.replace(tag, "", 1)
        page.write_text(s)

    index = OUT / "search-index.json"
    s = index.read_text()
    images.update(m.group(1) for m in CDN_RE.finditer(s))
    index.write_text(CDN_RE.sub(lambda m: "/img/" + flat(m.group(1)), s))

    (OUT / "img").mkdir()

    def fetch(path):
        dest = OUT / "img" / flat(path)
        subprocess.run(["curl", "-sSf", "-H", "Accept: image/webp", "-o", str(dest),
                        f"https://cdn.shopify.com/s/files/1/0571/3733/2289/{path}?width={IMG_WIDTH}"], check=True)
    with ThreadPoolExecutor(8) as pool:
        list(pool.map(fetch, sorted(images)))

    # art-directed images in assets/img stay; CSS, JS and fonts are inlined or served by Google Fonts
    for leftover in ("assets/styles.css", "assets/app.js", "assets/fonts", "404.html", "_redirects", "_headers", "robots.txt", "sitemap.xml"):
        p = OUT / leftover
        shutil.rmtree(p) if p.is_dir() else p.unlink(missing_ok=True)
    files = [p for p in OUT.rglob("*") if p.is_file()]
    size = sum(p.stat().st_size for p in files)
    print(f"preview: {len(pages)} pages, {len(images)} images, {len(files)} files, {size / 1e6:.1f} MB -> {OUT}")


if __name__ == "__main__":
    main()
