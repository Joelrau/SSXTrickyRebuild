"""Convert EA SHPS (.ssh) PS2 texture containers to PNG.
Usage: python sshtex.py <file.ssh | directory> <out_dir>
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import struct
import sys
from pathlib import Path

from PIL import Image

from eabig import refpack_decompress

FMT_4BIT, FMT_8BIT, FMT_32BIT, FMT_16BIT, FMT_24BIT = 0x01, 0x02, 0x05, 0x06, 0x03
BLOCK_PALETTE, BLOCK_NAME = 0x21, 0x70


def ps2_alpha(a: int) -> int:
    """PS2 alpha runs 0..0x80; scale it to 0..0xFF."""
    return min(a * 2, 255)


def read_blocks(data: bytes, offset: int):
    """Yield (type, header_offset, block_size_or_None) for the chain of blocks of one image."""
    while True:
        btype = data[offset]
        nxt = int.from_bytes(data[offset + 1:offset + 4], "little")
        yield btype, offset, nxt or None
        if not nxt:
            return
        offset += nxt


def decode_palette(data: bytes, off: int) -> list:
    pw, ph = struct.unpack_from("<HH", data, off + 4)
    raw = data[off + 16:off + 16 + pw * ph * 4]
    return [(raw[i], raw[i + 1], raw[i + 2], ps2_alpha(raw[i + 3])) for i in range(0, len(raw), 4)]


def decode_image(data: bytes, offset: int, raw_alpha: bool = False):
    """Decode one image entry. Returns (PIL.Image, embedded_name_or_None)."""
    blocks = list(read_blocks(data, offset))
    fmt = data[offset]
    w, h = struct.unpack_from("<HH", data, offset + 4)
    pixel_end = offset + blocks[0][2] if blocks[0][2] else len(data)
    pixels = data[offset + 16:pixel_end]
    if fmt & 0x80:   # RefPack-compressed pixel data
        pixels = refpack_decompress(pixels)
        fmt &= 0x7F

    palette, name = None, None
    for btype, boff, _size in blocks[1:]:
        if btype == BLOCK_PALETTE or btype in (0x22, 0x23, 0x24, 0x2A):
            palette = decode_palette(data, boff)
        elif btype == BLOCK_NAME:
            end = data.index(b"\0", boff + 4)
            name = data[boff + 4:end].decode("ascii", "replace") or None

    img = Image.new("RGBA", (w, h))
    if fmt == FMT_8BIT:
        img.putdata([palette[p] for p in pixels[:w * h]])
    elif fmt == FMT_4BIT:
        idx = []
        for b in pixels[:(w * h + 1) // 2]:
            idx += (b & 0x0F, b >> 4)
        img.putdata([palette[p] for p in idx[:w * h]])
    elif fmt == FMT_32BIT:
        alpha = (lambda a: a) if raw_alpha else ps2_alpha
        img.putdata([(pixels[i], pixels[i + 1], pixels[i + 2], alpha(pixels[i + 3]))
                     for i in range(0, w * h * 4, 4)])
    elif fmt == FMT_24BIT:
        img.putdata([(pixels[i], pixels[i + 1], pixels[i + 2], 255) for i in range(0, w * h * 3, 3)])
    elif fmt == FMT_16BIT:
        px = []
        for i in range(0, w * h * 2, 2):
            v = pixels[i] | pixels[i + 1] << 8
            px.append(((v & 0x1F) << 3, (v >> 5 & 0x1F) << 3, (v >> 10 & 0x1F) << 3,
                       255 if v & 0x8000 else 0))
        img.putdata(px)
    else:
        raise ValueError(f"unsupported pixel format 0x{fmt:02x}")
    return img, name


def read_images(path: Path, raw_alpha: bool = False) -> list:
    """All images of one container, in index order: [(PIL.Image, name), ...]."""
    data = Path(path).read_bytes()
    if data[:4] != b"SHPS":
        raise ValueError("not an SHPS file")
    count = struct.unpack_from("<I", data, 8)[0]
    return [decode_image(data, struct.unpack_from("<4sI", data, 16 + i * 8)[1], raw_alpha) for i in range(count)]


def convert(path: Path, out_dir: Path) -> tuple:
    data = path.read_bytes()
    if data[:4] != b"SHPS":
        raise ValueError("not an SHPS file")
    count = struct.unpack_from("<I", data, 8)[0]
    out_dir.mkdir(parents=True, exist_ok=True)
    ok, failed = 0, 0
    for i in range(count):
        tag, off = struct.unpack_from("<4sI", data, 16 + i * 8)
        tag = tag.decode("ascii", "replace").strip("\0 ")
        try:
            img, name = decode_image(data, off)
            safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in (name or tag))
            img.save(out_dir / f"{i:03d}_{safe}.png")
            ok += 1
        except Exception as e:
            print(f"    image {i} ({tag}) failed: {e}")
            failed += 1
    return ok, failed


def main():
    src, out_root = Path(sys.argv[1]), Path(sys.argv[2])
    files = sorted(p for p in src.rglob("*") if p.suffix.lower() == ".ssh") if src.is_dir() else [src]
    total_ok = total_failed = 0
    for f in files:
        rel = f.relative_to(src).with_suffix("") if src.is_dir() else Path(f.stem)
        try:
            ok, failed = convert(f, out_root / rel)
        except Exception as e:
            print(f"  FAILED {f}: {e}")
            continue
        total_ok += ok
        total_failed += failed
    print(f"done: {total_ok} images written, {total_failed} failed, from {len(files)} files")


if __name__ == "__main__":
    main()
