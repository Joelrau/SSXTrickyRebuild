"""Export the race-mode data the Unreal game reads at run time (race paths, triggers, world floor).
Usage:
    python tools/export_race_data.py                 # every track under unpacked/MODELS with an .aip
    python tools/export_race_data.py gari alaska     # selected tracks
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
import race as rc  # noqa: E402
import track_triggers as tt  # noqa: E402

MODELS = ROOT / "unpacked" / "MODELS"
OUT_DIR = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
MAGIC = b"SSXR"
VERSION = 1
# 0x257DC0
# track ids: 0 Garibaldi, 1 Snowdream, 2 Elysium, 3 Mesablanca, 4 Merqury, 5 Aloha, 6 Pipedream, 7 Untracked,
# 8 Tokyo Megaplex, 9 Trick, 10 Alaska
TRACK_IDS = {"gari": 0, "snow": 1, "elysium": 2, "mesa": 3, "merquer": 4, "aloha": 5, "pipe": 6, "untrack": 7,
             "megaple": 8, "trick": 9, "alaska": 10}
NODE_CLASS_IDS = {v: k for k, v in tt.NODE_CLASSES.items()}


def pack_path(p: rc.Path, is_line: bool) -> bytes:
    out = bytearray()
    out += struct.pack("<ii", p.line_class if not is_line else 50, p.flag if not is_line else 1)
    out += struct.pack("<f", p.dist_to_go_at_start if is_line else 0.0)
    out += struct.pack("<3f", *p.start)
    out += struct.pack("<3f", *p.bbox_min)
    out += struct.pack("<3f", *p.bbox_max)
    out += struct.pack("<II", len(p.segs), len(p.events))
    for s in p.segs:
        out += struct.pack("<4f", *s)
    for e in p.events:
        out += struct.pack("<iiIff", e.type, e.file_type, e.param, e.start, e.end)
    return bytes(out)


def pack_set(track: rc.Track) -> bytes:
    out = bytearray()
    out += struct.pack("<I", len(track.start_paths))
    out += struct.pack("<%dI" % len(track.start_paths), *track.start_paths)
    out += struct.pack("<I", len(track.ai_paths))
    for p in track.ai_paths:
        out += pack_path(p, False)
    out += struct.pack("<I", len(track.line_paths))
    for p in track.line_paths:
        out += pack_path(p, True)
    return bytes(out)


def pack_ssf(ssf: Path) -> bytes:
    if not ssf.exists():
        return struct.pack("<IIII", 0, 0, 0, 0)
    nodes, eff = tt.ssf_records(ssf)
    out = bytearray()
    for t in (17, 18):
        vals = eff.get(t, [])
        out += struct.pack("<I", len(vals)) + struct.pack("<%df" % len(vals), *vals)
    out += struct.pack("<I", len(eff.get(13, [])))
    out += struct.pack("<I", len(nodes))
    for n in nodes:
        cls = n["cls"] if isinstance(n["cls"], int) else NODE_CLASS_IDS.get(n["cls"], -1)
        d = n.get("direction", (0.0, 0.0, 0.0))
        out += struct.pack("<Iii3f3f", n["offset"], cls, n.get("mode", -1), n.get("seconds", 0.0), n.get("gain", 0.0),
                           n.get("target_cms", 0.0), *d)
    return bytes(out)


def export_track(stem: str, models_dir: Path) -> Path:
    aip = models_dir / f"{stem}.aip"
    sop = models_dir / f"{stem}.sop"
    ltg = models_dir / f"{stem}.ltg"
    ssf = models_dir / f"{stem}.ssf"
    sets = [rc.load_track(str(aip))]
    if sop.exists():
        sets.append(rc.load_track(str(sop)))
    flags = (1 if ltg.exists() else 0) | (2 if sop.exists() else 0) | (4 if ssf.exists() else 0)
    floor, bmin, bmax = 0.0, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)
    if ltg.exists():
        h = tt.ltg_header(ltg)
        floor, bmin, bmax = h["world_floor"], h["bbox_min"], h["bbox_max"]
    out = bytearray(MAGIC)
    out += struct.pack("<IiIf3f3fI", VERSION, TRACK_IDS.get(stem, -1), flags, floor, *bmin, *bmax, len(sets))
    for s in sets:
        out += pack_set(s)
    out += pack_ssf(ssf)
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    dst = OUT_DIR / f"{stem}.race.bin"
    dst.write_bytes(bytes(out))
    t = sets[0]
    print(f"{stem}: track id {TRACK_IDS.get(stem, -1)}, {len(t.ai_paths)} AI lines, {len(t.line_paths)} line paths, "
          f"starts {t.start_paths}, race length {t.race_length():.0f} cm, floor {floor:.0f}, "
          f"sets {len(sets)} -> {dst.name} ({len(out)} bytes)")
    return dst


def main():
    wanted = [a.lower() for a in sys.argv[1:]]
    for d in sorted(MODELS.iterdir()):
        models_dir = d / "data" / "models"
        stem = d.name.lower()
        if wanted and stem not in wanted:
            continue
        if not (models_dir / f"{stem}.aip").exists():
            continue
        export_track(stem, models_dir)


if __name__ == "__main__":
    main()
