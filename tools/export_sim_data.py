"""Export the rider-simulation data the Unreal game reads at run time.
Usage:
    python tools/export_sim_data.py                    # every track under unpacked/MODELS
    python tools/export_sim_data.py alaska snow        # selected tracks
    python tools/export_sim_data.py --surfaces <csv>   # surface table from a CSV instead of the executable
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import csv
import struct
import sys
from pathlib import Path

import numpy as np

import pbd
import pbd2gltf

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))
import exe_tables  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"

PATCH_MAGIC = b"SSXP"
SURF_MAGIC = b"SSXS"
VERSION = 1
FORMAT_FLAG_GAME_SPACE = 1
TESS = 10   # the game tessellates u, v = k/9


def grid_bounds(p: pbd.Patch):
    ks = np.linspace(0.0, 1.0, TESS)
    pts = np.array([p.evaluate(u, v) for u in ks for v in ks])
    return pts.min(axis=0), pts.max(axis=0)


def export_track(pbd_path: Path, out_dir: Path) -> Path:
    level = pbd.load(pbd_path)
    count = len(level.patches)
    wmin = np.full(3, np.inf)
    wmax = np.full(3, -np.inf)
    records = bytearray()
    for k, p in enumerate(level.patches):
        gmin, gmax = grid_bounds(p)
        bmin = np.minimum(np.array(p.bbox_min), gmin)
        bmax = np.maximum(np.array(p.bbox_max), gmax)
        wmin, wmax = np.minimum(wmin, bmin), np.maximum(wmax, bmax)
        flag_word = p.flag_word   # the 32-bit word (an s16 read never goes negative: bit 31 is in the high half)
        coeffs = [c for xyz in p.coeffs for c in xyz]
        records += struct.pack("<48f", *coeffs)
        records += struct.pack("<6f", *bmin, *bmax)
        records += struct.pack("<4i", int(p.surface_type), int(flag_word), k, 0)
    start = pbd2gltf.find_start(level)
    if start is None:
        spos, sfwd, valid = (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), 0
    else:
        spos, sfwd, valid = tuple(start[0]), tuple(start[1]), 1
    if count == 0:
        wmin = wmax = np.zeros(3)
    header = PATCH_MAGIC + struct.pack("<3I", VERSION, count, FORMAT_FLAG_GAME_SPACE)
    header += struct.pack("<12f", *wmin, *wmax, *spos, *sfwd)
    header += struct.pack("<8I", valid, 0, 0, 0, 0, 0, 0, 0)
    assert len(header) == 96
    out = out_dir / f"{pbd_path.stem.lower()}.patches.bin"
    out.write_bytes(header + records)
    print(f"{out.name}: {count} patches, start {'%.0f %.0f %.0f' % spos if valid else 'none'}")
    return out


def export_surfaces(csv_path, out_dir: Path) -> Path:
    """Surface physics table (built by the executable at 0x256188; 20 records x 25 words)."""
    rows = {}
    if csv_path:
        with open(csv_path, encoding="utf-8") as fh:
            for row in csv.DictReader(fh):
                rows[int(row["surface_id"])] = [float(row[f"f{i}"]) for i in range(25)]
    else:
        rows = {sid: [float(v) for v in r] for sid, r in enumerate(exe_tables.surface_table())}
    count = max(rows) + 1
    data = SURF_MAGIC + struct.pack("<3I", VERSION, count, 25)
    for sid in range(count):
        data += struct.pack("<25f", *rows.get(sid, rows.get(1)))
    out = out_dir / "surfaces.bin"
    out.write_bytes(data)
    print(f"{out.name}: {count} surface records from {csv_path or 'SLUS_203.26'}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tracks", nargs="*", help="track names (default: all non-sky .pbd files)")
    ap.add_argument("--surfaces", type=Path, default=None)
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    wanted = {t.lower() for t in args.tracks}
    for level in sorted((ROOT / "unpacked" / "MODELS").rglob("*.pbd")):
        stem = level.stem.lower()
        if stem.endswith("_sky") or (wanted and stem not in wanted):
            continue
        export_track(level, args.out)
    export_surfaces(args.surfaces, args.out)


if __name__ == "__main__":
    main()
