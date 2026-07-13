#!/usr/bin/env python3
"""Generate the dashboard art for the ml-model-update demo.

Outputs (into ../media/):
  icons/state-<label>.png   one 256x256 transparent-background icon per ML
                            class label the firmware can report in ml.class
  ml-states-legend.png      a 1600x1000 legend explaining every model's
                            states, thresholds, and LED policy -- made to be
                            inserted into an IOTCONNECT dashboard as a graphic

Pure Pillow (pip install pillow). Everything is drawn at 4x and downsampled,
so edges are smooth. Rerunning regenerates identical art.
"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
MEDIA = HERE.parent / "media"
ICONS = MEDIA / "icons"

SS = 4          # supersampling factor
ICON = 256      # final icon size

# Validated palette (dataviz reference): shape + label always accompany color.
INK = "#0b0b0b"
MUTED = "#52514e"
FAINT = "#898781"
C_NIGHT = "#4a3aa7"   # violet-indigo  (dark / night)
C_DUSK = "#c98500"    # deep amber     (dim / dusk)
C_SUN = "#eda100"     # gold           (bright / day / sunny)
C_COOL = "#2a78d6"    # blue           (cool)
C_GOOD = "#0ca30c"    # green          (comfy / normal)
C_WARM = "#ec835a"    # orange         (warm)
C_HOT = "#d03b3b"     # red            (hot)
C_GLOOM = "#6b7280"   # slate          (gloomy)


def tint(hexcolor, alpha):
    h = hexcolor.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) + (alpha,)


def on_white(hexcolor, a):
    """Opaque blend of the color over white. ImageDraw REPLACES pixels rather
    than compositing, so translucent fills only work on the transparent icon
    canvases -- the legend (flattened to RGB) needs pre-blended colors."""
    h = hexcolor.lstrip("#")
    rgb = tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    return tuple(round(255 * (1 - a) + v * a) for v in rgb)


def canvas(size):
    img = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def badge(d, size, color):
    s = size * SS
    m = s * 0.04
    d.ellipse([m, m, s - m, s - m], fill=tint(color, 36))
    d.ellipse([m, m, s - m, s - m], outline=tint(color, 110), width=int(s * 0.012))


def gs(img, size):
    return img.resize((size, size), Image.LANCZOS)


# ---- glyphs (all take center cx,cy and radius r in supersampled px) --------

def glyph_sun(d, cx, cy, r, color, rays=8):
    import math
    d.ellipse([cx - r * .55, cy - r * .55, cx + r * .55, cy + r * .55], fill=color)
    for i in range(rays):
        a = math.tau * i / rays
        x1 = cx + math.cos(a) * r * .72
        y1 = cy + math.sin(a) * r * .72
        x2 = cx + math.cos(a) * r * 1.0
        y2 = cy + math.sin(a) * r * 1.0
        d.line([x1, y1, x2, y2], fill=color, width=int(r * .13))


def glyph_halfsun(d, cx, cy, r, color):
    import math
    # sun disc clipped by a horizon line
    d.pieslice([cx - r * .62, cy - r * .62 + r * .18, cx + r * .62, cy + r * .62 + r * .18],
               180, 360, fill=color)
    d.line([cx - r, cy + r * .18, cx + r, cy + r * .18], fill=color, width=int(r * .12))
    for a_deg in (210, 270, 330):
        a = math.radians(a_deg)
        x1 = cx + math.cos(a) * r * .78
        y1 = (cy + r * .18) + math.sin(a) * r * .78
        x2 = cx + math.cos(a) * r * 1.02
        y2 = (cy + r * .18) + math.sin(a) * r * 1.02
        d.line([x1, y1, x2, y2], fill=color, width=int(r * .12))


def glyph_moon(d, cx, cy, r, color, star=False):
    # crescent: disc minus offset transparent disc (punch via second pass)
    moon = Image.new("RGBA", (int(r * 4), int(r * 4)), (0, 0, 0, 0))
    md = ImageDraw.Draw(moon)
    c = r * 2
    md.ellipse([c - r * .75, c - r * .75, c + r * .75, c + r * .75], fill=color)
    md.ellipse([c - r * .75 + r * .5, c - r * .75 - r * .28,
                c + r * .75 + r * .5, c + r * .75 - r * .28], fill=(0, 0, 0, 0))
    d._image.paste(moon, (int(cx - c), int(cy - c)), moon)
    if star:
        sx, sy, sr = cx + r * .55, cy - r * .5, r * .16
        d.polygon([(sx, sy - sr), (sx + sr * .35, sy - sr * .35), (sx + sr, sy),
                   (sx + sr * .35, sy + sr * .35), (sx, sy + sr),
                   (sx - sr * .35, sy + sr * .35), (sx - sr, sy),
                   (sx - sr * .35, sy - sr * .35)], fill=color)


def glyph_thermo(d, cx, cy, r, color, level, waves=False):
    """level 0..1 = fill height. Stem + bulb thermometer."""
    stem_w = r * .34
    top = cy - r * .95
    bulb_r = r * .34
    bulb_cy = cy + r * .62
    # outline
    d.rounded_rectangle([cx - stem_w / 2, top, cx + stem_w / 2, bulb_cy],
                        radius=stem_w / 2, outline=FAINT, width=int(r * .07))
    d.ellipse([cx - bulb_r, bulb_cy - bulb_r, cx + bulb_r, bulb_cy + bulb_r],
              outline=FAINT, width=int(r * .07))
    # mercury
    fill_top = top + (1.0 - level) * (bulb_cy - top - r * .1)
    inner = stem_w * .45
    d.rounded_rectangle([cx - inner / 2, fill_top, cx + inner / 2, bulb_cy],
                        radius=inner / 2, fill=color)
    d.ellipse([cx - bulb_r * .72, bulb_cy - bulb_r * .72,
               cx + bulb_r * .72, bulb_cy + bulb_r * .72], fill=color)
    if waves:
        for off in (-1, 1):
            wx = cx + off * r * .78
            d.arc([wx - r * .16, cy - r * .55, wx + r * .16, cy - r * .05],
                  300 if off < 0 else 120, 60 if off < 0 else 240,
                  fill=color, width=int(r * .09))


def glyph_check(d, cx, cy, r, color):
    d.ellipse([cx - r * .8, cy - r * .8, cx + r * .8, cy + r * .8],
              outline=color, width=int(r * .14))
    d.line([cx - r * .38, cy + r * .02, cx - r * .08, cy + r * .34], fill=color,
           width=int(r * .16))
    d.line([cx - r * .08, cy + r * .34, cx + r * .44, cy - r * .3], fill=color,
           width=int(r * .16))


def glyph_cloud(d, cx, cy, r, color):
    d.ellipse([cx - r * .85, cy - r * .1, cx - r * .15, cy + r * .55], fill=color)
    d.ellipse([cx - r * .45, cy - r * .55, cx + r * .35, cy + r * .3], fill=color)
    d.ellipse([cx + r * .0, cy - r * .25, cx + r * .8, cy + r * .5], fill=color)
    d.rectangle([cx - r * .5, cy + r * .15, cx + r * .45, cy + r * .55], fill=color)


# ---- icon table -------------------------------------------------------------

def draw_icon(name, color, painter):
    img, d = canvas(ICON)
    badge(d, ICON, color)
    s = ICON * SS
    painter(d, s / 2, s / 2, s * 0.30, color)
    ICONS.mkdir(parents=True, exist_ok=True)
    gs(img, ICON).save(ICONS / f"state-{name}.png")
    print(f"  icons/state-{name}.png")


ICON_DEFS = [
    # ambient / nightlight (light-driven)
    ("dark",   C_NIGHT, lambda d, x, y, r, c: glyph_moon(d, x, y, r, c)),
    ("night",  C_NIGHT, lambda d, x, y, r, c: glyph_moon(d, x, y, r, c, star=True)),
    ("dim",    C_DUSK,  glyph_halfsun),
    ("dusk",   C_DUSK,  glyph_halfsun),
    ("bright", C_SUN,   glyph_sun),
    ("day",    C_SUN,   glyph_sun),
    # comfort / hot-alarm (temperature-driven)
    ("cool",   C_COOL,  lambda d, x, y, r, c: glyph_thermo(d, x, y, r, c, 0.25)),
    ("comfy",  C_GOOD,  lambda d, x, y, r, c: glyph_thermo(d, x, y, r, c, 0.55)),
    ("warm",   C_WARM,  lambda d, x, y, r, c: glyph_thermo(d, x, y, r, c, 0.8)),
    ("hot",    C_HOT,   lambda d, x, y, r, c: glyph_thermo(d, x, y, r, c, 1.0, waves=True)),
    ("normal", C_GOOD,  glyph_check),
    # fusion
    ("gloomy", C_GLOOM, glyph_cloud),
    ("sunny",  C_SUN,   glyph_sun),
]

# ---- legend graphic ---------------------------------------------------------

LEGEND_W, LEGEND_H = 1600, 1000

MODELS = [
    ("v1  ambient",    "classifies LIGHT",       "builtin",
     [("dark", "< 20 %"), ("dim", "20 – 60 %"), ("bright", "> 60 %")], [2]),
    ("v2  comfort",    "classifies TEMPERATURE", "pushed",
     [("cool", "< 22 °C"), ("comfy", "22 – 27 °C"), ("warm", "> 27 °C")], [2]),
    ("v3  nightlight", "classifies LIGHT",       "pushed",
     [("night", "< 20 %"), ("dusk", "20 – 60 %"), ("day", "> 60 %")], [0]),
    ("v4  hot-alarm",  "classifies TEMPERATURE", "pushed",
     [("normal", "< 26 °C"), ("warm", "26 – 29 °C"), ("hot", "> 29 °C")], [1, 2]),
    ("v5  fusion",     "classifies TEMP + LIGHT", "pushed",
     [("gloomy", "index < 0.32"), ("normal", "0.32 – 0.55"), ("sunny", "> 0.55")], [2]),
]

ICON_COLORS = {n: c for n, c, _ in ICON_DEFS}


def font(px, bold=False):
    f = "segoeuib.ttf" if bold else "segoeui.ttf"
    return ImageFont.truetype(f"C:/Windows/Fonts/{f}", px * SS)


def make_legend():
    W, H = LEGEND_W * SS, LEGEND_H * SS
    img = Image.new("RGBA", (W, H), "#ffffff")
    d = ImageDraw.Draw(img)
    d._image = img  # for glyph_moon paste

    def text(x, y, s, px, color=INK, bold=False, anchor="la"):
        d.text((x, y), s, font=font(px, bold), fill=color, anchor=anchor)

    M = 56 * SS
    text(M, 40 * SS, "ML Model States", 40, bold=True)
    text(M, 96 * SS, "SAM E54 + IO1 Xplained Pro  ·  ml-model-update  ·  "
                     "each model is a 124-byte data blob pushed from IOTCONNECT",
         19, MUTED)

    top = 150 * SS
    row_h = 148 * SS
    left_w = 400 * SS
    cell_w = 370 * SS
    icon_r = 40 * SS

    for i, (name, classifies, origin, states, led_idx) in enumerate(MODELS):
        y = top + i * row_h
        if i:
            d.line([M, y, W - M, y], fill="#e1e0d9", width=SS)
        cy = y + row_h / 2
        text(M, cy - 34 * SS, name, 27, bold=True)
        text(M, cy + 6 * SS, classifies, 17, MUTED)
        text(M, cy + 34 * SS, f"({origin})", 15, FAINT)

        for j, (label, rng) in enumerate(states):
            x = M + left_w + j * cell_w
            color = ICON_COLORS[label]
            # icon badge
            bx, by = x, cy - icon_r
            d.ellipse([bx, by, bx + 2 * icon_r, by + 2 * icon_r],
                      fill=on_white(color, 0.14), outline=on_white(color, 0.45),
                      width=SS)
            painter = dict((n, p) for n, _, p in ICON_DEFS)[label]
            painter(d, bx + icon_r, by + icon_r, icon_r * 0.62, color)
            tx = bx + 2 * icon_r + 18 * SS
            text(tx, cy - 30 * SS, label, 24, bold=True)
            text(tx, cy + 6 * SS, rng, 17, MUTED)
            if j in led_idx:
                pill_w, pill_h = 86 * SS, 26 * SS
                py = cy + 30 * SS
                d.rounded_rectangle([tx, py, tx + pill_w, py + pill_h],
                                    radius=pill_h / 2,
                                    fill=on_white("#eda100", 0.28))
                text(tx + pill_w / 2, py + pill_h / 2, "LED ON", 13,
                     "#7a5200", bold=True, anchor="mm")

    d.line([M, top + 5 * row_h, W - M, top + 5 * row_h], fill="#e1e0d9", width=SS)
    text(M, top + 5 * row_h + 24 * SS,
         "ml.class reports the active state  ·  the LED follows the active model's policy  ·  "
         "fusion index = mean of temp/50 °C and light/100 %",
         17, MUTED)

    MEDIA.mkdir(parents=True, exist_ok=True)
    img.resize((LEGEND_W, LEGEND_H), Image.LANCZOS).convert("RGB").save(
        MEDIA / "ml-states-legend.png")
    print("  media/ml-states-legend.png")


if __name__ == "__main__":
    for name, color, painter in ICON_DEFS:
        draw_icon(name, color, painter)
    make_legend()
