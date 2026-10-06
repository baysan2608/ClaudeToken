#!/usr/bin/env python3
"""Fourfold - draws the touch HUD geometry printed by ffg_layout_dump, one PNG per screen (needs Pillow).
Usage: <build>/ffg_layout_dump > layouts.json && python3 draw_layouts.py layouts.json OUT_DIR
Each image shows the safe area, the stick / camera split, the stick ghost, every control (fill = drawn size, thin ring =
hit radius), the technique aim ring, the cancel zone, the sub-element ring petals, the gesture petal anchors and a
10 mm scale bar. It also prints every control's size in mm and flags hit targets under 9 mm."""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

NAMES = ['ATTACK', 'GUARD', 'EVADE', 'TECH', 'TARGET', 'PAUSE', 'E0', 'E1', 'E2', 'E3', 'CANCEL']
COLORS = [(255, 140, 60), (90, 170, 255), (120, 230, 160), (220, 120, 255), (230, 230, 230), (200, 200, 200),
          (170, 120, 70), (255, 90, 50), (70, 160, 255), (200, 230, 255), (255, 60, 60)]
MIN_HIT_MM = 9.0


def font(size, bold=False):
    path = '/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf' % ('-Bold' if bold else '')
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.load_default()


def draw(screen, out_dir):
    w, h = int(screen['w']), int(screen['h'])
    ppm = screen['ppm']
    img = Image.new('RGB', (w, h), (28, 32, 40))
    d = ImageDraw.Draw(img, 'RGBA')
    big, small = font(max(16, int(h / 40)), True), font(max(12, int(h / 55)))
    ux, uy, uw, uh = screen['usable']
    d.rectangle([ux, uy, ux + uw, uy + uh], outline=(90, 90, 110), width=3)
    d.line([screen['split'], 0, screen['split'], h], fill=(80, 80, 90), width=2)
    sx, sy, sr = screen['stick']
    d.ellipse([sx - sr, sy - sr, sx + sr, sy + sr], outline=(200, 200, 200, 120), width=4)
    tx, ty = screen['controls'][3][0], screen['controls'][3][1]
    a = screen['aim']
    d.ellipse([tx - a, ty - a, tx + a, ty + a], outline=(220, 120, 255, 90), width=2)
    small_hits = []
    sizes = {}
    for i, (x, y, r, hr) in enumerate(screen['controls']):
        c = COLORS[i]
        sizes[NAMES[i]] = round(2 * r / ppm, 1)
        if NAMES[i] == 'CANCEL':
            d.ellipse([x - r, y - r, x + r, y + r], outline=c + (200,), width=3)
        else:
            d.ellipse([x - hr, y - hr, x + hr, y + hr], outline=c + (70,), width=2)
            d.ellipse([x - r, y - r, x + r, y + r], fill=c + (70,), outline=c + (230,), width=4)
            if 2 * hr < MIN_HIT_MM * ppm - 0.5:
                small_hits.append(NAMES[i])
        tw = d.textlength(NAMES[i], font=small)
        d.text((x - tw / 2, y - small.size / 2), NAMES[i], fill=(255, 255, 255), font=small)
    for (x, y, rw, rh) in screen['ring']:
        d.rounded_rectangle([x, y, x + rw, y + rh], radius=rh / 2, outline=(255, 255, 255, 140), width=2)
    for (x, y, _align, is_guard) in screen['petals']:
        d.ellipse([x - 6, y - 6, x + 6, y + 6], fill=(90, 200, 255) if is_guard else (255, 220, 80))
    d.rectangle([40, h - 60, 40 + 10 * ppm, h - 50], fill=(255, 255, 255))
    d.text((40, h - 60 - big.size * 1.5), '10 mm   %s   %.2f px/mm   %dx%d' % (screen['name'], ppm, w, h),
           fill=(255, 255, 255), font=big)
    if small_hits:
        d.text((40, 40), 'HIT TARGET < 9 mm: ' + ', '.join(small_hits), fill=(255, 80, 80), font=big)
    img.thumbnail((1600, 1600))
    path = os.path.join(out_dir, screen['name'] + '.png')
    img.save(path)
    print('%-28s mm %s%s' % (screen['name'], sizes, ('  SMALL: ' + ','.join(small_hits)) if small_hits else ''))
    return not small_hits


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    screens = json.load(open(sys.argv[1]))
    os.makedirs(sys.argv[2], exist_ok=True)
    ok = all([draw(s, sys.argv[2]) for s in screens])
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
