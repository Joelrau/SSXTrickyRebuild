"""Reference for SSX.Race.IntroScene.StageArea: where the intro / end scene actors stand, computed independently of the
C++ port, and the floor under each of them.
    python tools/make_intro_scene_reference.py
"""
# FUN_0016e518 / FUN_00172100 / FUN_001777D0 / FUN_001741D0
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import csv
import json
import math
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import pbd  # noqa: E402
import pbdmodels  # noqa: E402
from export_prop_sounds import map_instance_names  # noqa: E402

CAM = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "cameras"
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "intro_scene_reference.csv"
COURSE = {"gari": "garibald", "snow": "snowdrea", "elysium": "elysium", "mesa": "mesablan", "merquer": "merqury",
          "aloha": "aloha", "pipe": "pipedrea", "untrack": "untracke", "megaple": "tokyo", "alaska": "alaska"}
SCENES = ["VEN_1", "VEN_2", "VEN_3", "VEN_4", "S_COM1", "S_COM2", "S_COM3", "S_COM4", "PIPE", "UNTR", "WIN_1", "LOS_1",
          "U_Edd", "Edd_U"]
AREA_NAME = {1: "Mdl_StageArea_Start_0", 2: "Mdl_StageArea_Finish_0"}


def mat(m):
    return np.array(m, dtype=float).reshape(4, 4) if m else np.eye(4)


def model_tris(model):
    out = []
    objs = model.objects
    for o in objs:
        M = mat(o.matrix)
        p, guard = o.parent, 0
        while p is not None and 0 <= p < len(objs) and guard < 32:
            M = M @ mat(objs[p].matrix)
            p, guard = objs[p].parent, guard + 1
        for mp in o.meshes:
            v = np.c_[mp.positions, np.ones(len(mp.positions))] @ M
            out.append(v[:, :3][mp.triangles])
    return np.concatenate(out) if out else np.zeros((0, 3, 3))


def floor_z(tris, x, y, zmax):
    """Highest triangle point below zmax on the vertical line through (x, y)."""
    if not len(tris):
        return None
    a, b, c = tris[:, 0], tris[:, 1], tris[:, 2]
    d = (b[:, 1] - c[:, 1]) * (a[:, 0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (a[:, 1] - c[:, 1])
    ok = np.abs(d) > 1e-9
    d = np.where(ok, d, 1.0)
    l1 = ((b[:, 1] - c[:, 1]) * (x - c[:, 0]) + (c[:, 0] - b[:, 0]) * (y - c[:, 1])) / d
    l2 = ((c[:, 1] - a[:, 1]) * (x - c[:, 0]) + (a[:, 0] - c[:, 0]) * (y - c[:, 1])) / d
    l3 = 1.0 - l1 - l2
    z = l1 * a[:, 2] + l2 * b[:, 2] + l3 * c[:, 2]
    hit = ok & (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6) & (z <= zmax)
    return float(z[hit].max()) if hit.any() else None


def wrap(a):
    return a - math.floor(a * (0.5 / math.pi) + 0.5) * 2.0 * math.pi


def scenes_for(track):
    out = {}
    for f in (COURSE[track], "commonob", "scripts"):
        for k, v in json.load(open(CAM / f"{f}.json"))["scenes"].items():
            out.setdefault(k, v)
    return out


def main():
    rows = []
    for track in COURSE:
        maps = list((ROOT / "unpacked" / "MODELS").glob(f"*/data/models/{track}.map"))
        if not maps:
            continue
        mp = maps[0]
        names = map_instance_names(mp)
        by_name = {}
        for i, n in sorted(names.items()):
            by_name.setdefault(n, i)
        lvl = pbd.load(mp.with_suffix(".pbd"))
        models = pbdmodels.load_models(lvl)
        cache = {}
        scenes = scenes_for(track)
        for sc in SCENES:
            if sc not in scenes or (sc == "PIPE" and track != "pipe") or (sc == "UNTR" and track != "untrack"):
                continue
            for k, a in enumerate(scenes[sc]):
                if not a["enabled"] or not a["place"]:
                    continue
                area = a["path"]
                pos, yaw = np.array(a["pos"], dtype=float), a["yaw"]
                if area in AREA_NAME and AREA_NAME[area] in by_name:
                    M = mat(lvl.instances[by_name[AREA_NAME[area]]].matrix)
                    w = np.r_[pos, 1.0] @ M
                    pos = w[:3]
                    ax, ay = np.float32(M[0][0]), np.float32(M[0][1])
                    if ax == 0.0:
                        ayaw = 0.0 if ay == 0.0 else (-math.pi / 2 if ay < 0.0 else math.pi / 2)
                    else:
                        ayaw = math.atan(float(ay) / float(ax))
                        if ax < 0.0:
                            ayaw += math.pi if ay > 0.0 else -math.pi
                    yaw = wrap(yaw + ayaw)
                elif area != 0:
                    yaw = wrap(yaw)
                x, y, z = pos
                best = None
                for j, inst in enumerate(lvl.instances):
                    if not (inst.bbox_min[0] <= x <= inst.bbox_max[0] and inst.bbox_min[1] <= y <= inst.bbox_max[1]
                            and inst.bbox_min[2] <= z + 60 and inst.bbox_max[2] >= z - 300):
                        continue
                    if j not in cache:
                        t = model_tris(models[inst.model_id])
                        if len(t):
                            t = (np.c_[t.reshape(-1, 3), np.ones(t.shape[0] * 3)] @ mat(inst.matrix))[:, :3].reshape(-1, 3, 3)
                        cache[j] = t
                    f = floor_z(cache[j], x, y, z + 60.0)
                    if f is not None and (best is None or f > best[0]):
                        best = (f, j)
                rows.append([track, sc, k, area, f"{x:.4f}", f"{y:.4f}", f"{z:.4f}", f"{yaw:.6f}",
                             "" if best is None else f"{best[0]:.4f}", "" if best is None else names.get(best[1], best[1])])
        print(f"{track}: {sum(1 for r in rows if r[0] == track)} actors")
    with open(OUT, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["track", "scene", "actor", "area", "x", "y", "z", "yaw", "floor_z", "floor_instance"])
        w.writerows(rows)
    print(f"wrote {OUT} ({len(rows)} rows)")


if __name__ == "__main__":
    main()
