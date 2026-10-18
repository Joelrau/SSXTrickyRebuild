"""Export the world presentation data the renderer needs at run time.
Usage: python tools/export_world_lighting.py [--table <world_track_table.json>]
The per-track presentation table is built by the executable at 0x257DC0 (13 records x 0x94 bytes);
--table copies a JSON file instead.
"""
# 0x2006E8
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import json
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "lib"))
import exe_tables  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
TRACKS = ["Garibaldi", "Snowdream", "Elysium", "Mesablanca", "Merqury", "Aloha", "Pipedream",
          "Untracked", "Tokyo Megaplex", "Trick (tutorial)", "Alaska", "Front end", "(12)"]
# (offset, name, kind)  kind: i = int, f = float, n... = n values
TRACK_FIELDS = [
    (0x00, "flare_on", "i"), (0x04, "flare_rgb1", "3i"), (0x10, "flare_k_a", "f"),
    (0x14, "flare_size", "f"), (0x18, "flare_rgb2", "3i"), (0x24, "flare_k_b", "f"),
    (0x28, "sun_azimuth_deg", "f"), (0x2C, "sun_elevation_deg", "f"), (0x30, "sun_distance", "f"),
    (0x34, "f34", "f"), (0x38, "f38", "f"), (0x3C, "flare_k_c", "3f"),
    (0x48, "colour_rgb", "3i"), (0x54, "snow_chance", "f"), (0x58, "f58", "f"),
    (0x5C, "f5c", "f"), (0x60, "draw_dist_1p", "f"), (0x64, "draw_dist_2p", "f"),
    (0x68, "fog_on", "i"), (0x6C, "fog_dist_1p", "f"), (0x70, "fog_shift_1p", "i"),
    (0x74, "fog_dist_2p", "f"), (0x78, "fog_shift_2p", "i"), (0x7C, "i7c", "i"),
    (0x80, "rider_amb_blend", "f"), (0x84, "rider_amb_target", "3f"), (0x90, "rider_light_scale", "f"),
]
LIGHT_SIZE = 92
SUBCELLS = 16


def light_records(pbd: bytes) -> list:
    counts = struct.unpack_from("<15i", pbd, 4)
    offs = struct.unpack_from("<18i", pbd, 64)
    n, o = counts[6], offs[6]
    return [pbd[o + LIGHT_SIZE * k:o + LIGHT_SIZE * (k + 1)] for k in range(n)]


def grid_lists(ltg: bytes, count_off: int = 0x2E, list_off: int = 0x44):
    min_x, min_y = struct.unpack_from("<2f", ltg, 4)
    cell = struct.unpack_from("<f", ltg, 0x28)[0]
    nx, ny, ncell = struct.unpack_from("<3i", ltg, 0x2C)
    sub = struct.unpack_from("<f", ltg, 0x40)[0]
    _side, sub_n, table = struct.unpack_from("<3i", ltg, 0x44)
    if sub_n != SUBCELLS:
        raise ValueError(f"expected 16 sub-cells per cell, got {sub_n}")
    cells = []
    for co in struct.unpack_from(f"<{ncell}I", ltg, table):
        if not co:
            cells.append(None)
            continue
        sub0 = co + struct.unpack_from("<I", ltg, co + 0x38)[0]
        lists = []
        for k in range(sub_n):
            so = sub0 + 76 * k
            n = struct.unpack_from("<H", ltg, so + count_off)[0]
            p = struct.unpack_from("<I", ltg, so + list_off)[0]
            lists.append(list(struct.unpack_from(f"<{n}i", ltg, co + p)) if (n and p) else [])
        cells.append(lists)
    return (min_x, min_y, cell, sub, nx, ny), cells


def export(models_dir: Path, stem: str, out: Path) -> str:
    pbd = (models_dir / f"{stem}.pbd").read_bytes()
    ltg = (models_dir / f"{stem}.ltg").read_bytes()
    lights = light_records(pbd)
    (min_x, min_y, cell, sub, nx, ny), cells = grid_lists(ltg)
    _, halo_cells = grid_lists(ltg, 0x2C, 0x40)
    b = bytearray(b"SSXL") + struct.pack("<II", 2, len(lights))
    for r in lights:
        b += r
    b += struct.pack("<4f2i", min_x, min_y, cell, sub, nx, ny)
    halos = 0
    for c, h in zip(cells, halo_cells):
        b += struct.pack("<B", c is not None)
        if c is not None:
            for lst in c + h:
                b += struct.pack(f"<I{len(lst)}i", len(lst), *lst)
            halos += sum(len(lst) for lst in h)
    (out / f"{stem}.lights.bin").write_bytes(b)
    return f"{stem}: {len(lights)} lights, {halos} halo entries, grid {nx}x{ny}, {sum(1 for c in cells if c)} cells"


def track_table_records() -> list:
    raw = exe_tables.world_track_table()
    out = []
    for k in range(13):
        rec = {"track": TRACKS[k], "id": k}
        for off, name, kind in TRACK_FIELDS:
            n = int(kind[0]) if kind[0].isdigit() else 1
            t = kind[-1]
            vals = struct.unpack_from("<" + str(n) + t, raw, k * 0x94 + off)
            rec[name] = [round(v, 5) if t == "f" else v for v in vals] if n > 1 else (
                round(vals[0], 5) if t == "f" else vals[0])
        out.append(rec)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--table", type=Path, default=None)
    args = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    for pbd in sorted((ROOT / "unpacked" / "MODELS").rglob("*.pbd")):
        if pbd.stem.endswith("_sky") or not pbd.with_suffix(".ltg").is_file():
            continue
        print(export(pbd.parent, pbd.stem, OUT))
    if args.table:
        json.loads(args.table.read_text())
        shutil.copyfile(args.table, OUT / "world_track_table.json")
    else:
        (OUT / "world_track_table.json").write_text(json.dumps(track_table_records(), indent=1))
    print(f"table -> {OUT / 'world_track_table.json'}")


if __name__ == "__main__":
    main()
