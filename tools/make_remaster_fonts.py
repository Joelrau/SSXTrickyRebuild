"""High-resolution UI fonts for the Remastered look, written to converted/frontend/fonts/remaster/<NAME>.png / .json.

    python tools/make_remaster_fonts.py

TITLE is drawn with Exo 2 Black Italic, MENU and SMLFONT with Rajdhani SemiBold (tools/fonts, SIL Open Font License),
keeping the disc font's baseline and character set; a character the font lacks falls back to the disc glyph, enlarged,
smoothed and re-cut at a clean edge. The JSON has the converted fonts' format and units
(positions in the disc font's pixels, as decimals), so the layout code is unchanged. Game-derived: stays git-ignored.
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONTS = ROOT / "converted" / "frontend" / "fonts"
OUT = FONTS / "remaster"
RAJDHANI = (ROOT / "tools" / "fonts" / "Rajdhani-SemiBold.ttf", None)
EXO2_BLACK = (ROOT / "tools" / "fonts" / "Exo2-Italic[wght].ttf", [900])
ATLAS_W = 2048
PAD = 3


def rebuild_glyph(atlas, g, k, sigma=0.5, edge=0.5, soft=0.12):
    """A disc glyph at k x its size: (L image, x offset, y offset) in glyph pixels from the glyph's own origin."""
    x, y, w, h = g["x"], g["y"], g["w"], g["h"]
    p = 2
    cell = Image.new("L", (w + 2 * p, h + 2 * p), 0)
    cell.paste(atlas.crop((x, y, x + w, y + h)), (p, p))
    big = cell.resize((cell.width * k, cell.height * k), Image.BICUBIC).filter(ImageFilter.GaussianBlur(sigma * k))
    v = np.asarray(big, dtype=float) / 255.0
    if v.max() < 0.9:
        v = v / max(v.max(), 1e-3)
    im = Image.fromarray((np.clip((v - (edge - soft / 2)) / soft, 0, 1) * 255).astype("uint8"))
    box = im.getbbox()
    if not box:
        return None, 0.0, 0.0
    return im.crop(box), box[0] / k - p, box[1] / k - p


def load_ttf(ttf, size):
    path, axes = ttf
    font = ImageFont.truetype(str(path), size)
    if axes:
        font.set_variation_by_axes(axes)
    return font


def ttf_for_cap(ttf, cap_px):
    b = load_ttf(ttf, 200).getbbox("H", anchor="ls")
    return load_ttf(ttf, round(200 * cap_px / (b[3] - b[1])))


def ttf_glyph(font, code, notdef):
    ch = chr(code)
    if bytes(font.getmask(ch)) == notdef and code != 32:
        return None
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
    if x1 <= x0 or y1 <= y0:
        return Image.new("L", (1, 1), 0), 0, 0, font.getlength(ch)
    im = Image.new("L", (x1 - x0 + 4, y1 - y0 + 4), 0)
    ImageDraw.Draw(im).text((2 - x0, 2 - y0), ch, font=font, fill=255, anchor="ls")
    return im, x0 - 2, y0 - 2, font.getlength(ch)


def write(name, cells, kx, ky):
    """cells: (code, image or None, xoff, yoff, advance) in logical units; packs the atlas and writes the PNG / JSON."""
    x = y = PAD
    row = 0
    placed = []
    for code, im, xo, yo, adv in cells:
        w, h = im.size if im else (0, 0)
        if x + w + PAD > ATLAS_W:
            x, y, row = PAD, y + row + PAD, 0
        placed.append((code, im, x, y, w, h, xo, yo, adv))
        x += w + PAD
        row = max(row, h)
    height = y + row + PAD
    alpha = Image.new("L", (ATLAS_W, height), 0)
    glyphs = []
    for code, im, px, py, w, h, xo, yo, adv in placed:
        if im:
            alpha.paste(im, (px, py))
        glyphs.append({"code": code, "x": px / kx, "y": py / ky, "w": w / kx, "h": h / ky, "xoffset": xo, "yoffset": yo,
                       "advance": adv})
    atlas = Image.merge("RGBA", (Image.new("L", alpha.size, 255),) * 3 + (alpha,))
    atlas.save(OUT / f"{name}.png", optimize=True)
    (OUT / f"{name}.json").write_text(json.dumps({"width": ATLAS_W / kx, "height": height / ky, "glyphs": glyphs}))
    print(f"{name}: {len(glyphs)} glyphs, atlas {ATLAS_W} x {height}")


def load(name):
    j = json.loads((FONTS / f"{name}.json").read_text())
    return j["glyphs"], Image.open(FONTS / f"{name}.png").convert("RGBA").getchannel("A")


def rebuilt_cell(atlas, g, k):
    im, xo, yo = rebuild_glyph(atlas, g, k) if g["w"] > 1 else (None, 0.0, 0.0)
    return g["code"], im, g["xoffset"] + xo, g["yoffset"] + yo, g["advance"]


def make_ttf(name, ttf, k, aspect, cap_scale):
    """aspect = the font's ScaleX / ScaleY in the game: logical widths are divided by it so the letters end up true."""
    glyphs, atlas = load(name)
    h = next(g for g in glyphs if g["code"] == ord("H"))
    baseline = h["yoffset"] + h["h"]
    font = ttf_for_cap(ttf, h["h"] * cap_scale * k)
    notdef = bytes(font.getmask("\uffff"))
    kx = k * aspect
    cells = []
    for g in glyphs:
        made = ttf_glyph(font, g["code"], notdef)
        if made is None:
            print(f"  {name}: U+{g['code']:04X} not in {ttf[0].name}, rebuilt from the disc glyph")
            cells.append(rebuilt_cell(atlas, g, k))
            continue
        im, x0, y0, adv = made
        cells.append((g["code"], im, x0 / kx, baseline + y0 / k, adv / kx))
    write(name, cells, kx, k)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    make_ttf("TITLE", EXO2_BLACK, 6, 1.4 / 1.3, 1.0)   # SSXFe::TitleScaleX / TitleScaleY
    make_ttf("MENU", RAJDHANI, 6, 1.8 / 1.4, 0.95)   # SSXFe::MenuScaleX / MenuScaleY
    make_ttf("SMLFONT", RAJDHANI, 8, 1.0, 1.0)


if __name__ == "__main__":
    main()
