"""Unpack EA BIG archives (C0FB and BIGF variants) as used by SSX Tricky.
Usage: python eabig.py <archive.BIG | directory> <out_dir>
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import struct
import sys
from pathlib import Path


def refpack_decompress(data: bytes) -> bytes:
    """Decompress EA RefPack (QFS) data. Returns data unchanged if not RefPack."""
    if len(data) < 5 or data[1] != 0xFB or (data[0] & 0x3E) != 0x10:
        return data
    flags = data[0]
    pos = 2
    if flags & 0x01:   # compressed-size field present
        pos += 4 if flags & 0x80 else 3
    size_len = 4 if flags & 0x80 else 3
    out_size = int.from_bytes(data[pos:pos + size_len], "big")
    pos += size_len

    out = bytearray()
    while pos < len(data):
        b0 = data[pos]
        if b0 < 0x80:
            b1 = data[pos + 1]
            pos += 2
            lit = b0 & 0x03
            copy = ((b0 & 0x1C) >> 2) + 3
            offset = ((b0 & 0x60) << 3) + b1 + 1
        elif b0 < 0xC0:
            b1, b2 = data[pos + 1], data[pos + 2]
            pos += 3
            lit = (b1 & 0xC0) >> 6
            copy = (b0 & 0x3F) + 4
            offset = ((b1 & 0x3F) << 8) + b2 + 1
        elif b0 < 0xE0:
            b1, b2, b3 = data[pos + 1], data[pos + 2], data[pos + 3]
            pos += 4
            lit = b0 & 0x03
            copy = ((b0 & 0x0C) << 6) + b3 + 5
            offset = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
        elif b0 < 0xFC:
            pos += 1
            lit = ((b0 & 0x1F) << 2) + 4
            copy = 0
            offset = 0
        else:
            pos += 1
            lit = b0 & 0x03
            out += data[pos:pos + lit]
            break

        out += data[pos:pos + lit]
        pos += lit
        start = len(out) - offset
        for i in range(copy):  # byte-wise: source may overlap destination
            out.append(out[start + i])

    if len(out) != out_size:
        raise ValueError(f"RefPack size mismatch: got {len(out)}, expected {out_size}")
    return bytes(out)


def read_entries(data: bytes):
    """Yield (name, offset, size) for each file in a C0FB or BIGF archive."""
    magic = data[:4]
    if data[:2] == b"\xC0\xFB":
        _hdr_len, count = struct.unpack_from(">HH", data, 2)
        pos = 6
        for _ in range(count):
            offset = int.from_bytes(data[pos:pos + 3], "big")
            size = int.from_bytes(data[pos + 3:pos + 6], "big")
            pos += 6
            end = data.index(b"\0", pos)
            yield data[pos:end].decode("ascii"), offset, size
            pos = end + 1
    elif magic in (b"BIGF", b"BIG4"):
        count, _hdr_len = struct.unpack_from(">II", data, 8)
        pos = 16
        for _ in range(count):
            offset, size = struct.unpack_from(">II", data, pos)
            pos += 8
            end = data.index(b"\0", pos)
            yield data[pos:end].decode("ascii"), offset, size
            pos = end + 1
    else:
        raise ValueError(f"not a BIG archive (magic {magic!r})")


def unpack(archive: Path, out_dir: Path) -> int:
    data = archive.read_bytes()
    n = 0
    for name, offset, size in read_entries(data):
        payload = refpack_decompress(data[offset:offset + size])
        dest = out_dir / name.replace("\\", "/").lstrip("/")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(payload)
        n += 1
    return n


def main():
    src, out_root = Path(sys.argv[1]), Path(sys.argv[2])
    archives = sorted(src.rglob("*.BIG")) if src.is_dir() else [src]
    for archive in archives:
        rel = archive.relative_to(src).with_suffix("") if src.is_dir() else Path(archive.stem)
        try:
            n = unpack(archive, out_root / rel)
            print(f"{n:5d} files  {archive.name}")
        except Exception as e:
            print(f"  FAILED  {archive}: {e}")


if __name__ == "__main__":
    main()
