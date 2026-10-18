"""Export the track effect / event tables the run-time effect interpreter reads (Effects/TrackEffects.h).
Usage:
    python tools/export_effects.py              # every .ssf under unpacked/MODELS (12 files incl. the front end)
    python tools/export_effects.py gari mesa    # selected tracks
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
import effects as fx  # noqa: E402
import pbd  # noqa: E402

OUT_DIR = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
MAGIC = b"SSXE"
VERSION = 1
TRACK_IDS = {"gari": 0, "snow": 1, "elysium": 2, "mesa": 3, "merquer": 4, "aloha": 5, "pipe": 6, "untrack": 7,
             "megaple": 8, "trick": 9, "alaska": 10, "ssxfe": 11}


def check(s: fx.Ssf, raw: bytes) -> list:
    """scripts/effects_validate.py check_parse, per file."""
    h, errs = s.header, []
    if h["magic"] != 0x1E0200 or struct.unpack_from("<h", raw, 4)[0] != 0x1500:
        errs.append("magic")
    if h["n_inst"] and h["n_inst"] != len(s.inst_index):
        errs.append("inst count")
    spans = sorted({(lst[0].offset, lst[-1].offset + lst[-1].size)
                    for lst in [*s.effects, *(f for _n, f in s.functions)] if lst})
    pos = s.data_base
    for a, b in spans:
        if a != pos:
            errs.append(f"gap/overlap at {a:#x}")
            break
        pos = b
    if pos != s.data_end:
        errs.append("tail")
    for r in [r for e in s.effects for r in e] + [r for _n, f in s.functions for r in f]:
        exp = fx.expected_size(r)
        if exp is None or exp != r.size or r.type not in fx.OP_NAMES:
            errs.append(f"size type {r.type}")
        if r.type == 7 and not (0 <= r.i(0) < h["n_inst"] and 0 <= r.i(1) < h["n_effect"]):
            errs.append("op7 ref")
        if r.type in (21, 26) and not (0 <= r.i(0) < h["n_func"]):
            errs.append("op21 ref")
        if r.type == 24 and not (0 <= r.i(0) < h["n_inst"]):
            errs.append("op24 ref")
        if r.type == 25 and not (0 <= r.i(0) < h["n_spline"] and r.i(1) in (0, 1)):
            errs.append("op25 ref")
    if any(not (-1 <= e < h["n_effect"]) for ev in s.events for e in ev):
        errs.append("event slot ref")
    if any(not (-1 <= rec[6] < h["n_event"]) for rec in s.inst_records):
        errs.append("inst event ref")
    return sorted(set(errs))


def export(ssf_path: Path, out_dir: Path) -> bool:
    stem = ssf_path.stem.lower()
    raw = ssf_path.read_bytes()
    s = fx.parse_ssf(ssf_path)
    errs = check(s, raw)
    if errs:
        print(f"{stem}: FAILED {errs[:4]}, not written", file=sys.stderr)
        return False
    h = s.header
    n_inst = len(s.inst_index)
    mats = [[0.0] * 16 for _ in range(n_inst)]
    pbd_path = ssf_path.with_suffix(".pbd")
    if pbd_path.is_file():
        level = pbd.load(pbd_path)
        if len(level.instances) == n_inst:
            mats = [list(i.matrix) for i in level.instances]
        else:
            print(f"{stem}: pbd instance count {len(level.instances)} != ssf {n_inst}", file=sys.stderr)

    lists = [*s.effects, *(f for _n, f in s.functions)]
    records, first = [], []
    for lst in lists:
        first.append(len(records))
        records.extend(lst)
    blob = raw[s.data_base:s.data_end]

    nx = ny = 0
    mn, cell = (0.0, 0.0), 0.0
    cells, refs = [], []
    ltg = ssf_path.with_suffix(".ltg")
    if ltg.is_file():
        gn = fx.grid_from_ltg(ltg.read_bytes(), showoff=False)
        ga = fx.grid_from_ltg(ltg.read_bytes(), showoff=True)
        nx, ny, mn, cell = gn.nx, gn.ny, (gn.min_x, gn.min_y), gn.cell
        for c in range(nx * ny):
            if c not in gn.cell_instances:
                cells.append((-1, 0, -1, 0))
                continue
            a, b = gn.cell_instances[c], ga.cell_instances[c]
            cells.append((len(a), len(refs), len(b), len(refs) + len(a)))
            refs += a + b

    out = bytearray(MAGIC)
    out += struct.pack("<I i 7I 2i I 3f", VERSION, TRACK_IDS.get(stem, -1), n_inst, h["n_event"], h["n_effect"],
                       h["n_func"], len(records), h["n_spline"], len(blob), nx, ny, len(refs), mn[0], mn[1], cell)
    assert len(out) == 64
    for i in range(n_inst):
        out += struct.pack("<iI16f", s.event_of(i), s.initial_flags(i), *mats[i])
    for ev in s.events:
        out += struct.pack("<7i", *ev)
    for k, lst in enumerate(s.effects):
        out += struct.pack("<2I", first[k], len(lst))
    for k, (name, lst) in enumerate(s.functions):
        out += struct.pack("<2I16s", first[len(s.effects) + k], len(lst), name.encode("latin-1"))
    for r in records:
        out += struct.pack("<3I", r.type, r.size, r.offset - s.data_base)
    for fl, _surf in s.splines:
        out += struct.pack("<I", fl)
    for c in cells:
        out += struct.pack("<4i", *c)
    out += struct.pack(f"<{len(refs)}i", *refs)
    out += blob
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / f"{stem}.effects.bin").write_bytes(out)
    print(f"{stem}.effects.bin: {n_inst} instances, {h['n_event']} events, {h['n_effect']} effects, "
          f"{h['n_func']} functions, {len(records)} records, {h['n_spline']} splines, grid {nx}x{ny}")
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tracks", nargs="*")
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    args = ap.parse_args()
    wanted = {t.lower() for t in args.tracks}
    ok = True
    for p in sorted((ROOT / "unpacked" / "MODELS").glob("*/data/models/*.ssf")):
        if wanted and p.stem.lower() not in wanted:
            continue
        ok &= export(p, args.out)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
