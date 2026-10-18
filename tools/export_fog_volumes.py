"""Export the fog volumes (cFogObjMan / cPS2FogVolume) of every .pbd for the run-time fog renderer (World/SSXFogVolumes.h).
Usage:
    python tools/export_fog_volumes.py            # every track .pbd and the front end (ssxfe.pbd)
    python tools/export_fog_volumes.py ssxfe gari
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
import fog_volume as fv  # noqa: E402

OUT_DIR = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
MAGIC = b"SSXG"
VERSION = 2


def ltg_grid(path: Path) -> bytes:
    """The .ltg grid header and every cell / sub-cell (sub-cell +0x30 u16 particle-instance count and
    +0x48 its list offset, cell-relative)."""
    if not path.exists():
        return struct.pack("<3f3I", 0.0, 0.0, 10000.0, 0, 0, 0)
    d = path.read_bytes()
    u32 = lambda o: struct.unpack_from("<I", d, o)[0]
    u16 = lambda o: struct.unpack_from("<H", d, o)[0]
    nx, ny, nc, table = u32(0x2C), u32(0x30), u32(0x34), u32(0x4C)
    cells = []
    for c in range(nc):
        co = u32(table + 4 * c)
        if not co:
            continue
        b = bytearray(struct.pack("<I6f", c, *struct.unpack_from("<6f", d, co)))
        so = co + u32(co + 0x38)
        for k in range(16):
            s = so + 0x4C * k
            n, lst = u16(s + 0x30), u32(s + 0x48)
            empty = u32(s + 0x24) == 0 and u32(s + 0x28) == 0 and u16(s + 0x2C) == 0 and lst == 0
            b += struct.pack("<6fBH", *struct.unpack_from("<6f", d, s), 1 if empty else 0, n)
            b += struct.pack(f"<{n}I", *struct.unpack_from(f"<{n}i", d, co + lst)) if n else b""
        cells.append(bytes(b))
    head = struct.pack("<3f3I", *struct.unpack_from("<2f", d, 0x04), struct.unpack_from("<f", d, 0x28)[0], nx, ny,
                       len(cells))
    return head + b"".join(cells)


def export(path: Path, out_dir: Path) -> bool:
    models, inst = fv.load(str(path))
    if not all(0 <= i.model < len(models) for i in inst):
        print(f"{path.stem}: model index out of range; not written")
        return False
    b = bytearray(MAGIC + struct.pack("<3I", VERSION, len(models), len(inst)))
    for pts in models:
        b += struct.pack("<I", len(pts))
        for q in pts:
            b += struct.pack("<7f", *q.pos, *q.rgb, q.radius)
    for i in inst:
        b += struct.pack("<16fi6f", *i.matrix, i.model, *i.bbox_min, *i.bbox_max)
    b += ltg_grid(path.with_suffix(".ltg"))
    out = out_dir / f"{path.stem.lower()}.fog.bin"
    out.write_bytes(bytes(b))
    print(f"{path.stem}: {len(models)} models, {len(inst)} instances, "
          f"{sum(len(models[i.model]) for i in inst)} puffs -> {out.name}")
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tracks", nargs="*")
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    args = ap.parse_args()
    wanted = {t.lower() for t in args.tracks}
    ok = True
    for p in sorted((ROOT / "unpacked" / "MODELS").glob("*/data/models/*.pbd")):
        if p.stem.lower().endswith("_sky") or (wanted and p.stem.lower() not in wanted):
            continue
        ok &= export(p, args.out)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
