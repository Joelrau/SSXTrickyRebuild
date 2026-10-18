"""Export the rail splines (and the .ssf prop-instance records) the rider simulation reads at run time.
Usage:
    python tools/export_rails.py                 # every track under unpacked/MODELS
    python tools/export_rails.py gari mesa       # selected tracks
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import struct
import sys
from pathlib import Path

import numpy as np

import pbd

import effects as fx  # noqa: E402
from export_effects import TRACK_IDS  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"

RAIL_MAGIC = b"SSXR"
INST_MAGIC = b"SSXI"
VERSION = 1
FLAG_GAME_SPACE = 1
FLAG_ENABLE_APPROX = 2
RACE_MODE = 4

SPLINE_RECORD = 48
SEGMENT_RECORD = 112


def ssf_header(s: bytes) -> dict:
    """Header words used here."""
    h = struct.unpack_from("<19I", s, 0)
    return dict(n_irec=h[13], p_irec=h[14], n_inst=h[15], p_iidx=h[16], n_spl=h[17], p_spl=h[18])


def ssf_spline_table(s: bytes) -> list:
    h = ssf_header(s)
    return [struct.unpack_from("<II", s, h["p_spl"] + 8 * k) for k in range(h["n_spl"])]


def ssf_rail_effects(s: bytes) -> list:
    """Track-effect records {25, 16, spline, enable} in file order: (file offset, spline, enable)."""
    n = len(s) // 4
    w = np.frombuffer(s[:n * 4], "<u4")
    idx = np.nonzero((w[:-4] == 25) & (w[1:-3] == 16) & (w[3:-1] <= 1))[0]
    return [(int(i) * 4, int(w[i + 2]), int(w[i + 3])) for i in idx]


def rail_state(s: bytes, ssf_path: Path, stem: str):
    """(flags, surface, initial enabled, enabled after the race-mode PreRace scripts) per spline."""
    table = ssf_spline_table(s)
    initial = [(f >> 16) & 1 for f, _ in table]
    effects = ssf_rail_effects(s)
    track = TRACK_IDS.get(stem, -1)
    w = fx.World(fx.parse_ssf(ssf_path), RACE_MODE, track)
    for name in fx.flow_scripts("prerace", RACE_MODE, track):
        w.call_function(name)
    return table, initial, list(w.rail_enabled), effects


def ssf_instances(s: bytes):
    h = ssf_header(s)
    recs = s[h["p_irec"]:h["p_irec"] + 24 * h["n_irec"]]
    idx = struct.unpack_from(f"<{h['n_inst']}i", s, h["p_iidx"])
    return h["n_irec"], recs, idx


def export_track(pbd_path: Path, out_dir: Path):
    stem = pbd_path.stem.lower()
    ssf_path = pbd_path.with_suffix(".ssf")
    level = pbd.load(pbd_path)
    if not ssf_path.is_file():
        print(f"{stem}: no .ssf next to {pbd_path.name}, skipped", file=sys.stderr)
        return
    ssf = ssf_path.read_bytes()
    table, initial, enabled, effects = rail_state(ssf, ssf_path, stem)
    nspl, nseg = len(level.splines), len(level.spline_segments)
    if len(table) != nspl:
        print(f"{stem}: ssf spline count {len(table)} != pbd {nspl}", file=sys.stderr)
    flags = FLAG_GAME_SPACE
    data = bytearray(RAIL_MAGIC + struct.pack("<7I", VERSION, nspl, nseg, flags, len(effects), 0, 0))
    for k, sp in enumerate(level.splines):
        f, surf = table[k] if k < len(table) else (0, 13)
        data += struct.pack("<6f", *sp.bbox_min, *sp.bbox_max)
        data += struct.pack("<3i3I", sp.first_segment, sp.segment_count, int(np.int32(np.uint32(surf))), f,
                            enabled[k] if k < len(enabled) else 0, initial[k] if k < len(initial) else 0)
    for sg in level.spline_segments:
        for row in sg.coeffs:
            data += struct.pack("<3f", *row)
        data += struct.pack("<4f", *sg.dist_poly)
        data += struct.pack("<3i", sg.prev, sg.next, sg.spline)
        data += struct.pack("<6f", *sg.bbox_min, *sg.bbox_max)
        data += struct.pack("<2fi", sg.length, sg.start_dist, 0)
    assert len(data) == 32 + nspl * SPLINE_RECORD + nseg * SEGMENT_RECORD
    out = out_dir / f"{stem}.rails.bin"
    out.write_bytes(data)
    print(f"{out.name}: {nspl} splines ({sum(enabled)} enabled, {len(effects)} type-25 records), "
          f"{nseg} segments")

    nrec, recs, idx = ssf_instances(ssf)
    inst = INST_MAGIC + struct.pack("<3I", VERSION, nrec, len(idx)) + recs + struct.pack(f"<{len(idx)}i", *idx)
    out_i = out_dir / f"{stem}.instances.bin"
    out_i.write_bytes(inst)
    if len(idx) != len(level.instances):
        print(f"{stem}: ssf instance count {len(idx)} != pbd {len(level.instances)} "
              "(the game ignores the records then)", file=sys.stderr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tracks", nargs="*", help="track names (default: all non-sky .pbd files)")
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    wanted = {t.lower() for t in args.tracks}
    for level in sorted((ROOT / "unpacked" / "MODELS").rglob("*.pbd")):
        stem = level.stem.lower()
        if stem.endswith("_sky") or (wanted and stem not in wanted):
            continue
        export_track(level, args.out)


if __name__ == "__main__":
    main()
