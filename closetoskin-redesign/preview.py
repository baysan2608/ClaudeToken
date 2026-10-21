#!/usr/bin/env python3
"""Build a private review copy of the site for sharing as a claude.ai Artifact.

Differences from the production build (build.py):
- every page carries a "preview" banner; checkout and forms are switched off
- search engines are told to stay away (noindex)
- links are relative, and CSS, JS and fonts are inlined, because the viewer serves the site
  from a sub-path and blocks other hosts
- product photos are bundled in preview/img/ instead of loaded from the Shopify CDN

    python3 preview.py        # writes preview/ (index.html is the entry page)
"""
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).parent
OUT = ROOT / "preview"
IMG_WIDTH = 900
CDN_RE = re.compile(r'https://cdn\.shopify\.com/s/files/1/0571/3733/2289/([^"?\s]+)\?(?:v=[^&"]*&(?:amp;)?)?width=\d+')
FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
         '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Cormorant+Garamond:wght@500&family=Jost:wght@300..700&display=swap">')
BANNER = ('<div class="preview-bar" role="note">Closetoskin redesign preview · not the live store · '
          'checkout and forms are turned off</div>')
BANNER_CSS = (".preview-bar{background:#7a5c45;color:#fff;font-size:.78rem;letter-spacing:.03em;text-align:center;"
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

    # the viewer allows Google Fonts, which serves the same two typefaces, so pages stay light
    css = re.sub(r"@font-face\{[^}]*\}", "", (OUT / "assets/styles.css").read_text())
    css = css.replace('"Cormorant",', '"Cormorant Garamond",')
    assert "/assets/fonts/" not in css and '"Cormorant Garamond"' in css
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
        s = re.sub(r'<link rel="stylesheet" href="/assets/styles\.css[^"]*">', lambda m: FONTS + f"<style>{css}</style>", s)
        s = re.sub(r'<script src="/assets/app\.js[^"]*" defer></script>\n?', "", s)
        s = s.replace("</body>", f'<script>window.CTS_ROOT="{prefix}";window.CTS_PREVIEW=1;</script><script>{js}</script>\n</body>')
        for _ in range(2):
            s = re.sub(r'(<form[^>]*?)\s(?:method|action)="[^"]*"', r"\1", s)
        s = re.sub(r'\b(href|src|data-hover)="/([^"/][^"]*|)"', lambda m: f'{m.group(1)}="{local_link(m.group(2), prefix)}"', s)
        s = s.replace("<body>\n", "<body>\n" + BANNER + "\n", 1)
        if depth == 0 and page.name == "index.html":
            # the viewer wraps the entry page in its own document skeleton
            s = re.sub(r"<title>.*?</title>", "<title>Closetoskin Redesign Preview</title>", s, count=1)
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

    for leftover in ("assets", "404.html", "_redirects", "_headers", "robots.txt", "sitemap.xml"):
        p = OUT / leftover
        shutil.rmtree(p) if p.is_dir() else p.unlink(missing_ok=True)
    files = [p for p in OUT.rglob("*") if p.is_file()]
    size = sum(p.stat().st_size for p in files)
    print(f"preview: {len(pages)} pages, {len(images)} images, {len(files)} files, {size / 1e6:.1f} MB -> {OUT}")


if __name__ == "__main__":
    main()
