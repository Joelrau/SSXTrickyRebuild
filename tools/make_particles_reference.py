"""Reference data for the SSX.Particles.* tests (git-ignored SimData output).
Usage: python tools/make_particles_reference.py
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
import effects as fx  # noqa: E402
import particles as pt  # noqa: E402
import particles_validate as pv  # noqa: E402

OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "particles_reference.json"
MODELS = ROOT / "unpacked" / "MODELS"
FRAMES = (1, 4, 12, 30, 60, 100)
STRIDE_TARGET = 48


def rng_seq():
    i = [0]

    def r():
        i[0] += 1
        return ((i[0] * 7919) % 997) / 997.0
    return r


def matrix(k):
    if k % 2 == 0:
        return ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))
    a, b = math.radians(30 + 17 * k), math.radians(10)
    ca, sa, cb, sb = math.cos(a), math.sin(a), math.cos(b), math.sin(b)
    rz = ((ca, sa, 0), (-sa, ca, 0), (0, 0, 1))
    rx = ((1, 0, 0), (0, cb, sb), (0, -sb, cb))
    m = [[sum(rz[i][t] * rx[t][j] for t in range(3)) for j in range(3)] for i in range(3)]
    return (tuple(m[0]) + (0,), tuple(m[1]) + (0,), tuple(m[2]) + (0,), (1000.0 * k, -500.0, 250.0, 1))


def sample(words, k):
    c = pt.EmitterConfig.from_words(words)
    m = matrix(k)
    ps = pt.ParticleSystem.from_emitter(c, inst_matrix=m, rng=rng_seq())
    frames = []
    done = 0
    for f in FRAMES:
        while done < f:
            ps.update()
            done += 1
        sp = ps.sprites()
        step = max(1, len(sp) // STRIDE_TARGET)
        frames.append(dict(frame=f, t=ps.t, count=len(sp), step=step,
                           sprites=[[*s["pos"], s["half_size"], *s["rgba"], s["particle"], s["copy"]]
                                    for s in sp[::step]]))
    return dict(words=[w & 0xFFFFFFFF for w in words], matrix=[list(r) for r in m],
                frames=frames, blend_mode=ps.blend_mode, texture=ps.texture, continuous=ps.continuous)


def emitters():
    counts, samples = {}, []
    k = 0
    for p in sorted(MODELS.glob("*/data/models/*.ssf")):
        s = fx.parse_ssf(p)
        n = 0
        tex, blend, copies = {}, {}, {}
        for ei, recs in enumerate(s.effects):
            for r in recs:
                if r.type == 2 and r.i(0) in (0, 2):
                    words = [r.i(j + 1) & 0xFFFFFFFF for j in range(51)]
                    c = pt.EmitterConfig.from_words(words)
                    tex[c.texture] = tex.get(c.texture, 0) + 1
                    blend[c.blend] = blend.get(c.blend, 0) + 1
                    copies[c.copies] = copies.get(c.copies, 0) + 1
                    fw = p.stem.lower() == "gari" and ei == 120
                    if n % 13 == 0 or fw:
                        d = sample(words, k)
                        d.update(track=p.stem.lower(), effect=ei, index=n)
                        samples.append(d)
                        k += 1
                    n += 1
        counts[p.stem.lower()] = dict(n=n, textures={str(a): b for a, b in sorted(tex.items())},
                                      blend={str(a): b for a, b in sorted(blend.items())},
                                      copies={str(a): b for a, b in sorted(copies.items())})
    return counts, samples


def ram_cases():
    m = pv.Ram(pv.RAM)
    game = m.u(0x337C58)
    rm = m.u(game + 0x730)
    surf_tab = m.u(rm + 0x24)
    spray, clouds = [], []
    for r in pv.RIDERS:
        h = m.u(r + 0x5AE0)
        fxb = m.u(h + 0x3E0)
        s_id = m.u(r + 0x290)
        row = m.d[(surf_tab + 100 * s_id) & 0x1FFFFFF:][:100]
        light = m.v(h + 0x470, 4)
        sp = fxb + 0x4B60
        n, start = m.i(sp), m.i(sp + 4)
        for k in range(n):
            ps = sp + 0x40 + ((start + k) % pt.SPRAY_RING) * 0x150
            spray.append(dict(
                rider=r, surface=s_id, row=[struct.unpack_from("<f", row, o)[0] for o in range(0x34, 0x58, 4)],
                row_int=[struct.unpack_from("<i", row, 0x44)[0], struct.unpack_from("<i", row, 0x54)[0]],
                light=list(light), N=m.i(ps), copies=m.i(ps + 4), t=m.f(ps + 8), interval=m.f(ps + 0x0C),
                size_span=m.f(ps + 0x10), life_span=m.f(ps + 0x14), size_base=m.f(ps + 0x18),
                life_base=m.f(ps + 0x1C), copy_dt=m.f(ps + 0x24), ts=m.f(ps + 0x3C), drift_z=m.f(ps + 0x58),
                texture=m.i(ps + 0x140), blend=m.i(ps + 0x144), c0=list(m.v(ps + 0xE0, 4)),
                slope=list(m.v(ps + 0x110, 4)), va=list(m.v(ps + 0x70, 3)), vb=list(m.v(ps + 0x80, 3))))
        cn, cs = m.i(fxb + 0x1C10), m.i(fxb + 0x1C14)
        for k in range(cn):
            e = fxb + 0x10 + ((cs + k) % pt.CARVE_CLOUD_RING) * 0x70
            clouds.append(dict(rider=r, age=m.f(e), life=m.f(e + 0x5C), fade_in=m.f(e + 0x58),
                               intensity=m.f(e + 0x44), size_min=m.f(e + 0x60), size_end=m.f(e + 0x64),
                               pos=list(m.v(e + 0x20, 3)), up=list(m.v(e + 0x30, 3)), variant=m.i(e + 0x40),
                               rgb=list(m.v(e + 0x4C, 3)), alpha=m.f(e + 0x48)))
    return spray, clouds


def small_laws():
    out = {}
    out["flakes"] = [[a, b, u, pt.flake_half_size(a, b, u)] for a, b, u in
                     ((5.0, 7.0, 0.0), (5.5, 6.5, 0.5), (5.9, 6.1, 0.99))]
    out["ghosts"] = [[s, b, list(pt.ghost_tint(s, b) or ())] for s, b in
                     ((0.0, 0.0), (0.25, 0.0), (0.6, 0.0), (1.0, 0.0), (0.25, 0.5), (0.334, 0.0), (0.666, 0.0))]
    out["landing"] = [[f, pt.landing_puff_count(f), pt.landing_intensity_add(f)] for f in
                      (-100.0, -2500.0, -5000.0, -12000.0)]
    out["shadow_k"] = [[mo, h, pt.shadow_silhouette_level(mo, h)] for mo, h in
                       ((2, 0.0), (2, -2.5), (2, -10.0), (1, -10.0), (2, 3.0))]
    slot = pt.PowerFXSlot()
    seq = []
    for f in range(90):
        p = (10.0 * f, 2.0 * f * f / 60.0, 100.0 + 5.0 * math.sin(f / 7.0))
        made = slot.frame(p, (8.0, 0.0, 0.0))
        if f % 15 == 14:
            seq.append(dict(frame=f, made=made, n=len(slot.parts),
                            first=[*slot.parts[0]["pos"], slot.parts[0]["size"], *slot.parts[0]["rgba"]],
                            last=[*slot.parts[-1]["pos"], slot.parts[-1]["size"], *slot.parts[-1]["rgba"]]))
    out["powerfx"] = seq
    c = dict(life=2.0, fade_in=0.2, intensity=0.1, size_min=80.0, size_end=400.0, pos=(0, 0, 0), up=(0, 0, 1),
             variant=2, rgb=(1, 1, 1))
    out["cloud_law"] = []
    for age in (0.0, 0.1, 0.2, 0.5, 1.0, 1.9, 2.0):
        c["age"] = age
        d = pt.carve_cloud_draw(c)
        out["cloud_law"].append([age, d["size"], d["alpha_unit"], d["pos"][2]])
    out["spray_colour"] = [[list(l), d, b, list(pt.spray_colour(l, d, b))] for l, d, b in
                           (((1, 0.8, 0.9, 1.1), 0.537, 0.137), ((1, 0.2, 0.5, 1.4), 0.7, 0.2), ((1, 2, 2, 2), 0.5, 0.0))]
    return out


def main():
    counts, samples = emitters()
    total = sum(c["n"] for c in counts.values())
    data = dict(total=total, counts=counts, samples=samples, laws=small_laws())
    if pv.RAM.exists():
        data["spray"], data["clouds"] = ram_cases()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(data))
    print("%s: %d emitter records, %d samples, %d spray bursts, %d clouds" % (
        OUT, total, len(samples), len(data.get("spray", [])), len(data.get("clouds", []))))


if __name__ == "__main__":
    main()
