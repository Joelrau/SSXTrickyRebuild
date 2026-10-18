"""Export the rider profile tables the rider simulation reads at run time (riders.bin).
Usage:
    python tools/export_rider_profiles.py                 # -> Unreal/SSXTrickyRebuild/SimData/riders.bin
    python tools/export_rider_profiles.py --out FILE
    python tools/export_rider_profiles.py --dump          # also print what was exported
"""
# 0x331EA8 / 0x335260 / 0x332148
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import json
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "riders.bin")
CHARS = os.path.join(REPO, "converted", "characters")

N_RIDERS, N_BOARDS, N_BONES = 12, 12, 19
MODEL_IDS = ["eddie", "kaori", "luther", "mac", "moby", "zoe", "jp", "elise", "psymon", "seeiah", "brodi", "marisol"]
MMM_ID = "zz_mmm"
BONE_NAMES = ["hips", "lower_back", "upper_back", "neck", "head", "l_clav", "l_bicep", "l_forearm", "l_hand",
              "r_clav", "r_bicep", "r_forearm", "r_hand", "l_thigh", "l_calf", "l_foot", "r_thigh", "r_calf", "r_foot"]
GLTF_TO_GAME = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], dtype=np.float64).T   # inverse of convert_characters.BASIS


def quat_to_mat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def bind_bones(model_id):
    """Bind-pose world positions of bones 0..18 in game model space (cm), or None."""
    path = os.path.join(CHARS, model_id, model_id + ".gltf")
    if not os.path.exists(path):
        return None
    doc = json.load(open(path))
    nodes = doc["nodes"]
    joints = doc["skins"][0]["joints"]
    parent = {c: i for i, n in enumerate(nodes) for c in n.get("children", [])}

    def world(i):
        n = nodes[i]
        m = np.eye(4)
        m[:3, :3] = quat_to_mat(n.get("rotation", [0, 0, 0, 1]))
        m[:3, 3] = n.get("translation", [0, 0, 0])
        return world(parent[i]) @ m if i in parent else m

    names = [nodes[j].get("name", "") for j in joints]
    if names[1:1 + N_BONES] != BONE_NAMES:   # joint 0 is the converter's added root
        print(f"  {model_id}: unexpected bone names {names[1:1 + N_BONES]}; no bones exported")
        return None
    return [GLTF_TO_GAME @ world(j)[:3, 3] * 100.0 for j in joints[1:1 + N_BONES]]


class RiderTables:
    """Rider records (.data 0x331EA8, 0x38 bytes each), board records (0x332148, 12 x 12 x 0x14) and the
    attribute groups (0x335260, 4 x {count, index[3]}) of the executable."""

    def __init__(self, exe_path):
        with open(exe_path, "rb") as f:
            self.exe = f.read()
        groups = [struct.unpack_from("<4i", self.exe, self._off(0x335260 + 16 * i)) for i in range(4)]
        self.ATTR = [list(g[1:1 + g[0]]) for g in groups]

    @staticmethod
    def _off(va):
        return va - 0x100000 + 0x1000

    def _cstr(self, va):
        o = self._off(va)
        return self.exe[o:self.exe.index(0, o)].decode("latin1")

    def rider(self, c):
        b = self.exe[self._off(0x331EA8 + c * 0x38):][:0x38]
        i32 = lambda o: struct.unpack_from("<i", b, o)[0]
        return dict(name=self._cstr(struct.unpack_from("<I", b, 4)[0]), stats=list(b[0xC:0x1C]), alt=list(b[0x1C:0x20]),
                    b20=b[0x20], scale=struct.unpack_from("<f", b, 0x24)[0], stance=i32(0x28), trickset=i32(0x2C),
                    w30=i32(0x30), b34=b[0x34])

    def board(self, c, k):
        o = self._off(0x332148 + c * 0xF0 + k * 0x14)
        p, _idx, typ, cost = struct.unpack_from("<4I", self.exe, o)
        return dict(name=self._cstr(p), type=typ, cost=cost, stats=list(self.exe[o + 16:o + 20]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--dump", action="store_true")
    args = ap.parse_args()

    try:
        rp = RiderTables(os.path.join(REPO, "extracted", "SLUS_203.26"))
    except OSError as e:
        sys.exit(f"cannot read the game executable for the rider tables: {e}")

    out = bytearray()
    out += b"SSXR" + struct.pack("<4I", 1, N_RIDERS, N_BOARDS, N_BONES)
    for g in rp.ATTR:
        idx = list(g) + [0] * (3 - len(g))
        out += struct.pack("<4I", len(g), *idx)

    for c in range(N_RIDERS):
        r = rp.rider(c)
        bones = bind_bones(MODEL_IDS[c])
        out += r["name"].encode("latin1")[:15].ljust(16, b"\0")
        out += bytes(r["stats"]) + bytes(r["alt"]) + bytes([r["b20"], r["b34"], 0, 0])
        out += struct.pack("<f3i", r["scale"], r["stance"], r["trickset"], r["w30"])
        out += struct.pack("<I", 1 if bones else 0)
        for b in (bones or [np.zeros(3)] * N_BONES):
            out += struct.pack("<3f", *b)
        if args.dump:
            print(f"{c:2d} {r['name']:8s} stats {r['stats']} alt {r['alt']} mass {r['b20']} rec34 {r['b34']} "
                  f"scale {r['scale']:.2f} stance {r['stance']} trickset {r['trickset']} bones {'yes' if bones else 'no'}")
    for c in range(N_RIDERS):
        for k in range(N_BOARDS):
            b = rp.board(c, k)
            out += b["name"].encode("latin1")[:23].ljust(24, b"\0")
            out += struct.pack("<2i", b["type"], b["cost"]) + bytes(b["stats"])
    mmm = bind_bones(MMM_ID)
    out += struct.pack("<I", 1 if mmm else 0)
    for b in (mmm or [np.zeros(3)] * N_BONES):
        out += struct.pack("<3f", *b)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)
    print(f"wrote {args.out} ({len(out)} bytes)")


if __name__ == "__main__":
    main()
