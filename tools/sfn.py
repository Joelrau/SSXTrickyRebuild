"""SSX Tricky bitmap fonts (DATA/FONTS/*.SFN) -> PNG atlas + JSON metrics.
Layout (little endian):
    0x00 "FNTS"
    0x04 u32  size of the file minus the kerning table (unused here)
    0x08 u16  ?              (0xC8 for MENU/TITLE, 0x65 for SMLFONT)
    0x0A u16  glyph count
    0x0C u16  ?              (9)
    0x0E u16  record layout  (1: 12-byte glyph records, 0: 11-byte packed records)
    0x10 u16  0
    0x12 u8   0
    0x13 u8   line height    (MENU 16, SMLFONT 8, TITLE 27)
    0x14 u32  glyph table offset (0x20)
    0x18 u32  kerning table offset (0 = none)
    0x1C u32  bitmap offset
  glyph record:
    u16 code (UCS-2), u8 w, u8 h, u16 x, u16 y (atlas pixel rect), u8 advance, s8 x offset,
    s8 y offset (from the top of the line), [u8 pad when layout = 1]
  bitmap:
    u32 1, u16 width, u16 height, 8 bytes 0, then width*height 4-bit coverage values (low nibble first)
  kerning:
    u32 count, then count * (u16 left, u16 right, s8 adjust, u8 0, u16 0)
    python tools/sfn.py extracted/DATA/FONTS converted/frontend/fonts
writes <NAME>.png (white, coverage in alpha) and <NAME>.json for each font.
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import struct
import sys

from PIL import Image


def read_sfn(path):
    with open(path, 'rb') as f:
        d = f.read()
    if d[:4] != b'FNTS':
        raise ValueError('%s: not an SFN font' % path)
    count, = struct.unpack_from('<H', d, 0x0A)
    layout, = struct.unpack_from('<H', d, 0x0E)
    line_height = d[0x13]
    glyph_off, kern_off, bmp_off = struct.unpack_from('<III', d, 0x14)
    rec = 12 if layout == 1 else 11
    glyphs = []
    for i in range(count):
        o = glyph_off + i * rec
        code, w, h, x, y, adv, xo, yo = struct.unpack_from('<HBBHHBbb', d, o)
        glyphs.append(dict(code=code, w=w, h=h, x=x, y=y, advance=adv, xoffset=xo, yoffset=yo))
    _, bw, bh = struct.unpack_from('<IHH', d, bmp_off)
    pix = d[bmp_off + 16: bmp_off + 16 + (bw * bh + 1) // 2]
    alpha = bytearray(bw * bh)
    for i in range(bw * bh):
        b = pix[i >> 1]
        v = (b >> 4) if (i & 1) else (b & 15)
        alpha[i] = v * 17
    kerning = []
    if kern_off:
        n, = struct.unpack_from('<I', d, kern_off)
        for i in range(n):
            l, r, k = struct.unpack_from('<HHb', d, kern_off + 4 + i * 8)
            kerning.append([l, r, k])
    img = Image.new('RGBA', (bw, bh), (255, 255, 255, 0))
    img.putalpha(Image.frombytes('L', (bw, bh), bytes(alpha)))
    return dict(line_height=line_height, width=bw, height=bh, glyphs=glyphs, kerning=kerning), img


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    for name in sorted(os.listdir(src)):
        if not name.upper().endswith('.SFN'):
            continue
        meta, img = read_sfn(os.path.join(src, name))
        base = os.path.join(dst, os.path.splitext(name)[0].upper())
        img.save(base + '.png')
        with open(base + '.json', 'w') as f:
            json.dump(meta, f, indent=1)
        print('%s: %d glyphs, %dx%d atlas, line %d' % (name, len(meta['glyphs']), meta['width'], meta['height'],
                                                       meta['line_height']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
