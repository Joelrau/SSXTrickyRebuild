"""Export the per-instance prop sound lists (track .adl) the rider prop-hit sound reads at run time.
Usage:
    python tools/export_prop_sounds.py               # every track under unpacked/MODELS
    python tools/export_prop_sounds.py gari mesa     # selected tracks
    python tools/export_prop_sounds.py --check gari  # print the survey only, write nothing
"""
# 0x25EDF0 / 0x22A518 / 0x22A5F0 / 0x2550C8
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
MAGIC = b"SSXP"
VERSION = 2
SHAPE_WORDS = {0: 7, 1: 12, 2: 12, 3: 6}


def name_hash(name: str) -> int:
    """PJW / ELF hash with the 23-bit fold."""
    h = 0
    for c in name.encode("latin-1"):
        h = ((h << 4) + c) & 0xFFFFFFFF
        g = h & 0xF0000000
        if g:
            h ^= g >> 23
            h &= ~g & 0xFFFFFFFF
    return h


def map_instance_names(path: Path) -> dict:
    out, sec = {}, None
    if not path.is_file():
        return out
    for line in path.read_text(errors="replace").splitlines():
        if "BEGIN" in line or "END" in line:
            sec = line.strip("# ").strip()
            continue
        if sec == "INTERNAL INSTANCES BEGIN":
            parts = line.split()
            if len(parts) >= 3 and parts[1].isdigit():
                out[int(parts[1])] = parts[0]
    return out


def read_ltg_grid(path: Path):
    b = path.read_bytes()
    mx, my = struct.unpack_from("<2f", b, 4)
    cell = struct.unpack_from("<f", b, 0x28)[0]
    nx, ny = struct.unpack_from("<2i", b, 0x2C)
    return mx, my, cell, nx, ny


def instance_positions(path: Path):
    sys.path.insert(0, str(ROOT / "tools"))
    import pbd  # noqa: E402
    return [tuple(i.matrix[12:15]) for i in pbd.load(path).instances]


def build(stem: Path) -> dict:
    d = stem.with_suffix(".adl").read_bytes()
    if d[0] != 0 or struct.unpack_from("<f", d, 4)[0] != 1.0:
        raise ValueError(f"{stem.name}.adl: header byte 0 / version 1.0 check failed (0x22A5F0)")
    n = struct.unpack_from("<i", d, 8)[0]
    n_inst = struct.unpack_from("<15i", stem.with_suffix(".pbd").read_bytes(), 4)[2]
    names = map_instance_names(stem.with_suffix(".map"))
    by_hash = {}
    for i, nm in names.items():
        by_hash.setdefault(name_hash(nm), i)
    attached, entries, emitters = 0, {}, []
    pos = instance_positions(stem.with_suffix(".pbd"))
    for k in range(n):
        key, off = struct.unpack_from("<II", d, 12 + 8 * k)
        inst = by_hash.get(key)
        if inst is None or not 0 <= inst < n_inst:
            continue   # 0x2550C8
        attached += 1
        hit, ns = struct.unpack_from("<Ii", d, off)
        ids, p = [], off + 8
        for s in range(ns):
            ty = struct.unpack_from("<i", d, p)[0]
            if ty not in SHAPE_WORDS:
                raise ValueError(f"{stem.name}.adl record {k}: shape type {ty} (0x22AAA0 knows 0..3)")
            if s < 4:
                ids.append(struct.unpack_from("<i", d, p + 4)[0])
            p += 4 * SHAPE_WORDS[ty]
        entries[inst] = (hit, ns, (ids + [0, 0, 0, 0])[:4])
        if ns > 0:   # 0x22B008
            emitters.append((inst, pos[inst], ns, d[off + 8:p]))
    return dict(n_records=n, attached=attached, n_inst=n_inst, entries=entries, emitters=emitters,
                grid=read_ltg_grid(stem.with_suffix(".ltg")))


def write(path: Path, x: dict):
    out = bytearray(MAGIC + struct.pack("<5I", VERSION, x["n_records"], x["attached"], len(x["entries"]), x["n_inst"]))
    out += struct.pack("<3f2iI", *x["grid"], len(x["emitters"]))
    for inst in sorted(x["entries"]):
        hit, ns, ids = x["entries"][inst]
        out += struct.pack("<iIi4i", inst, hit, ns, *ids)
    for inst, p, ns, words in x["emitters"]:
        out += struct.pack("<i3fii", inst, *p, ns, len(words) // 4) + words
    path.write_bytes(bytes(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tracks", nargs="*", help="track names (default: every track with an .adl)")
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    ap.add_argument("--check", action="store_true", help="survey only, write nothing")
    args = ap.parse_args()
    wanted = {t.lower() for t in args.tracks}
    ref = {}
    ref_path = args.out / "prop_sounds_reference.json"
    if ref_path.is_file():
        ref = json.loads(ref_path.read_text()).get("tracks", {})
    if not args.check:
        args.out.mkdir(parents=True, exist_ok=True)
    for adl in sorted((ROOT / "unpacked" / "MODELS").rglob("*.adl")):
        stem = adl.stem.lower()
        if wanted and stem not in wanted:
            continue
        x = build(adl.with_suffix(""))
        hits = {}
        for inst, (hit, _, _) in x["entries"].items():
            hits[hit] = hits.get(hit, 0) + 1
        print(f"{stem}: {x['n_records']} records, {x['attached']} attached, {len(x['entries'])} instances with a sound list "
              f"(of {x['n_inst']}); hit ids {dict(sorted(hits.items(), key=lambda kv: -kv[1])[:6])}")
        seen, samples = set(), []
        for inst in sorted(x["entries"]):
            hit = x["entries"][inst][0]
            if hit not in seen:
                seen.add(hit)
                samples.append([inst, hit])
        types, sids, curves, hit_act, first = {}, {}, {}, 0, None
        for inst, p, ns, words in x["emitters"]:
            w = struct.unpack(f"<{len(words) // 4}i", words)
            q = 0
            for s in range(ns):
                ty, sid = w[q], w[q + 1]
                types[ty] = types.get(ty, 0) + 1
                sids[sid] = sids.get(sid, 0) + 1
                hit_act += sid in (0x10, 0x1C, 0x39)
                fw = struct.unpack(f"<{SHAPE_WORDS[ty]}f", words[4 * q:4 * (q + SHAPE_WORDS[ty])])
                for k in {0: (6,), 1: (11,), 2: (9, 11), 3: ()}[ty]:
                    curves[str(fw[k])] = curves.get(str(fw[k]), 0) + 1
                if first is None and ty == 0:
                    first = dict(instance=inst, pos=list(p), sound=sid, offset=list(fw[2:5]), radius=fw[5], curve=fw[6])
                q += SHAPE_WORDS[ty]
        print(f"  emitters: {len(x['emitters'])} records, shape types {dict(sorted(types.items()))}, hit-activated "
              f"{hit_act}, curves {curves}, sound ids {dict(sorted(sids.items()))}; grid {x['grid']}")
        ref[stem] = dict(records=x["n_records"], attached=x["attached"], entries=len(x["entries"]),
                         hit_id_counts={str(k): v for k, v in sorted(hits.items())}, samples=samples,
                         emitter_records=len(x["emitters"]), shape_types={str(k): v for k, v in sorted(types.items())},
                         hit_activated_shapes=hit_act, grid=list(x["grid"]), first_sphere=first)
        if not args.check:
            write(args.out / f"{stem}.propsnd.bin", x)
    if not args.check:
        ref_path.write_text(json.dumps({"source": "tools/export_prop_sounds.py", "tracks": ref}, indent=1))


if __name__ == "__main__":
    main()
