"""Validate models/particles.py against the disc data and RAM.

usage: python scripts/particles_validate.py
1. PARTICLE.SSH image table (38 images, the tags the code paths use).
2. Every .ssf emitter record on all tracks decodes; texture/blend/copies ranges; the families.
3. RAM (traces/ram/paused_race_1.bin, Garibaldi racing, 6 riders): every live board-spray puff
   record is reproduced field by field from the model's rules, the surface table row and the
   rider's light total; every live carve cloud's alpha matches the fade law.
4. Snapshots: live emitter nodes (none at these moments).
"""
import math
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "models"))
import effects as fx  # noqa: E402
import particles as pt  # noqa: E402

REB = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
import traces  # noqa: E402
SSH = REB / "extracted" / "DATA" / "TEXTURES" / "PARTICLE.SSH"
MODELS = REB / "unpacked" / "MODELS"
RAM = (traces.root() or Path(".")) / "ram" / "paused_race_1.bin"
RIDERS = [0x885170, 0xBDAF30, 0xC84E70, 0xD2EDB0, 0xDD8CC0, 0xE8AC70]
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
    return cond


def ssh_table():
    d = SSH.read_bytes()
    n = struct.unpack_from("<I", d, 8)[0]
    out = []
    for i in range(n):
        tag = d[16 + 8 * i:20 + 8 * i].decode("latin-1")
        off = struct.unpack_from("<I", d, 20 + 8 * i)[0]
        w, h = struct.unpack_from("<2H", d, off + 4)
        out.append((tag, w, h))
    return out


def part1():
    t = ssh_table()
    check(len(t) == 38, "PARTICLE.SSH has %d images" % len(t))
    used = {0: "brk1", 3: "ndl1", 13: "str1", 16: "part", 19: "snfl", 20: "clod", 23: "ex07",
            28: "spr2", 29: "tral", 33: "beam", 37: "exlm"}
    for k, tag in used.items():
        check(t[k][0] == tag, "image %d is %s, expected %s" % (k, t[k][0], tag))
    print("1. PARTICLE.SSH: %d images; used tags %s" % (len(t),
          ", ".join("%d %s %dx%d" % (k, t[k][0], t[k][1], t[k][2]) for k in sorted(used))))


def part2():
    n = 0
    tex, blend, copies = {}, {}, {}
    for p in sorted(MODELS.glob("*/data/models/*.ssf")):
        s = fx.parse_ssf(p)
        for recs in s.effects:
            for r in recs:
                if r.type == 2 and r.i(0) in (0, 2):
                    c = pt.EmitterConfig.from_record(r)
                    n += 1
                    tex[c.texture] = tex.get(c.texture, 0) + 1
                    blend[c.blend] = blend.get(c.blend, 0) + 1
                    copies[c.copies] = copies.get(c.copies, 0) + 1
                    check(0 <= c.texture < 38, "%s texture %d" % (p.stem, c.texture))
                    check(0 <= c.blend < len(pt.EMITTER_BLEND), "%s blend %d" % (p.stem, c.blend))
                    check(c.count > 0 and c.time_scale > 0, "%s count/ts" % p.stem)
                    check(c.life - c.life_range / 2 >= -1e-6, "%s negative life lo" % p.stem)
                    ps = pt.ParticleSystem.from_emitter(c)
                    check(abs(ps.interval * c.count - (c.max_life if c.continuous else c.emit_time)
                              * c.time_scale) < 1e-3, "%s interval" % p.stem)
    check(n == 979, "emitter records %d" % n)
    print("2. emitters: %d records (12 track files); textures %s; blend words %s -> modes %s; "
          "copies %s" % (n, dict(sorted(tex.items())), dict(sorted(blend.items())),
                         {b: pt.EMITTER_BLEND[b] for b in blend}, dict(sorted(copies.items()))))
    # fireworks family (Garibaldi effect 120: the cylinder fountain)
    g = fx.parse_ssf(next(MODELS.glob("*/data/models/gari.ssf")))
    fw = [r for r in g.effects[120] if r.type == 2][0]
    c = pt.EmitterConfig.from_record(fw)
    check((c.count, c.copies, c.texture, pt.emitter_blend_mode(c.blend)) == (200, 8, 0, 5),
          "fireworks cylinder config")
    ps = pt.ParticleSystem.from_emitter(c, rng=lambda: 0.37)
    peak = 0.0
    for f in range(150):
        ps.update()
        sp = ps.sprites()
        if sp:
            peak = max(peak, max(s["pos"][2] for s in sp))
    print("   fireworks (gari effect 120): N 200, 8 copies, burst 0.3 s, additive, image 0; model "
          "fountain peak %.0f cm" % peak)


class Ram:
    def __init__(self, path):
        self.d = Path(path).read_bytes()

    def u(self, a):
        return struct.unpack_from("<I", self.d, a & 0x1FFFFFF)[0]

    def i(self, a):
        return struct.unpack_from("<i", self.d, a & 0x1FFFFFF)[0]

    def f(self, a):
        return struct.unpack_from("<f", self.d, a & 0x1FFFFFF)[0]

    def v(self, a, n):
        return tuple(self.f(a + 4 * k) for k in range(n))


def close(a, b, tol=1e-3):
    return abs(a - b) <= tol * max(1.0, abs(b))


def part3():
    m = Ram(RAM)
    game = m.u(0x337C58)
    rm = m.u(game + 0x730)
    surf_tab = m.u(rm + 0x24)
    puffs = fields = clouds = 0
    for r in RIDERS:
        h = m.u(r + 0x5AE0)
        fxb = m.u(h + 0x3E0)
        s_id = m.u(r + 0x290)
        row = m.d[(surf_tab + 100 * s_id) & 0x1FFFFFF:][:100]
        surf = pt.SurfaceSpray.from_row(row)
        light = m.v(h + 0x470, 4)
        # spray ring (FX +0x4B60)
        sp = fxb + 0x4B60
        n, start = m.i(sp), m.i(sp + 4)
        rgb = pt.spray_colour(light, surf.desat, surf.brighten)
        for k in range(n):
            ps = sp + 0x40 + ((start + k) % pt.SPRAY_RING) * 0x150
            N = m.i(ps)
            exp = {
                "copies": (m.i(ps + 4), 1),
                "interval": (m.f(ps + 0x0C), 0.025 * 5.0 / N),
                "size_span": (m.f(ps + 0x10), surf.size * 0.4),
                "life_span": (m.f(ps + 0x14), (surf.life_hi - surf.life_lo) * 5.0),
                "size_base": (m.f(ps + 0x18), surf.size * 0.8 - surf.size * 0.4),
                "life_base": (m.f(ps + 0x1C), surf.life_lo * 5.0 - (surf.life_hi - surf.life_lo) * 5.0),
                "copy_dt": (m.f(ps + 0x24), 0.005 * 5.0),
                "ts": (m.f(ps + 0x3C), 5.0),
                "drift_z": (m.f(ps + 0x58), -100.0 / 25.0),
                "texture": (m.i(ps + 0x140), surf.texture),
                "blend": (m.i(ps + 0x144), surf.blend),
                "c0_r": (m.f(ps + 0xE0), rgb[0] * 128), "c0_g": (m.f(ps + 0xE4), rgb[1] * 128),
                "c0_b": (m.f(ps + 0xE8), rgb[2] * 128), "c0_a": (m.f(ps + 0xEC), surf.alpha * 128),
                "slope_a": (m.f(ps + 0x11C), -surf.alpha * 128 / (surf.life_hi * 5.0)),
                "slope_r": (m.f(ps + 0x110), 0.0),
                "t_frames": (m.f(ps + 8) * 12.0, round(m.f(ps + 8) * 12.0)),
            }
            for key, (got, want) in exp.items():
                fields += 1
                check(close(got, want), "rider %#x puff %d %s %r != %r" % (r, k, key, got, want))
            check(1 <= N <= 3, "rider %#x puff %d N %d" % (r, k, N))
            va, vb = m.v(ps + 0x70, 3), m.v(ps + 0x80, 3)
            # ground spray: across-axis jitter spread >= 300 cm/s, no up-axis jitter
            check(math.sqrt(sum(x * x for x in va)) * 5.0 >= 300.0 - 0.5, "puff Va %r" % (va,))
            check(vb == (0.0, 0.0, 0.0), "puff Vb %r" % (vb,))
            fields += 2
            puffs += 1
        # carve clouds (FX +0x0000)
        cn, cs = m.i(fxb + 0x1C10), m.i(fxb + 0x1C14)
        prev = None
        for k in range(cn):
            e = fxb + 0x10 + ((cs + k) % pt.CARVE_CLOUD_RING) * 0x70
            age, inten = m.f(e), m.f(e + 0x44)
            c = dict(age=age, life=m.f(e + 0x5C), fade_in=m.f(e + 0x58), intensity=inten,
                     size_min=m.f(e + 0x60), size_end=m.f(e + 0x64), pos=m.v(e + 0x20, 3),
                     up=m.v(e + 0x30, 3), variant=m.i(e + 0x40), rgb=m.v(e + 0x4C, 3))
            consts = (c["life"], c["fade_in"], c["size_min"], c["size_end"])
            check(all(close(a, b) for a, b in zip(consts, (2.0, 0.2, 80.0, 400.0))),
                  "cloud consts %r" % (consts,))
            check(0 <= c["variant"] <= 3, "cloud variant")
            check(inten >= 0.2 * 0.2 - 1e-6, "cloud intensity %f" % inten)  # |steer| > 0.2 (surface 1)
            dr = pt.carve_cloud_draw(c)
            check(dr is not None and close(m.f(e + 0x48), dr["alpha_unit"], 2e-3),
                  "cloud alpha %r vs %r" % (m.f(e + 0x48), dr and dr["alpha_unit"]))
            if prev is not None:
                check(math.dist(prev, c["pos"]) > pt.CARVE_CLOUD_SPACING, "cloud spacing")
            prev = c["pos"]
            clouds += 1
    print("3. RAM paused_race_1 (Garibaldi, racing, 6 riders, surface 1): %d spray puffs, %d "
          "fields reproduced; %d carve clouds (alpha law, spacing > 70 cm, intensity >= 0.04, "
          "variant 0..3)" % (puffs, fields, clouds))


def part4():
    vt = {"cEmitterNode": 0x36C7F8, "cParticleNode": 0x36C718, "cCollideEmitterNode": 0x36C650,
          "cParticleMan": 0x392D30}
    files = [RAM] + sorted(((traces.root() or Path(".")) / "live").glob("*/spawn_ram.bin"))
    for p in files:
        d = p.read_bytes()
        cnt = {k: sum(1 for mm in re.finditer(re.escape(struct.pack("<I", v)), d) if mm.start() % 4 == 0)
               for k, v in vt.items()}
        print("4. %s: vtable hits %s" % (p.parent.name + "/" + p.name, cnt))
    m = Ram(RAM)
    pm = m.u(m.u(m.u(0x337C58) + 0x730) + 0x80)
    check(m.u(pm + 8) == 0x392D30, "cParticleMan at racemgr+0x80")
    check(m.i(pm + 0xB54) == 250 and m.i(pm + 0xC) == 0, "PowerFX pool 250 free, 0 slots")


def main():
    part1()
    part2()
    if RAM.exists():
        part3()
        part4()
    else:
        print("3/4. RAM snapshot missing (git-ignored)")
    print("FAIL %d" % len(fails) if fails else "ALL OK")
    for f in fails[:20]:
        print("  ", f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
