"""SSX Tricky game PRNG (reference model).

State: six u32 words s[0..5]. Two global instances exist in the game (stream A = rand(),
stream B = gameplay rand); both start as seed(0). Addresses and code tags are in the comments.

Validation (python models/rng.py): the stream-B state in every RAM snapshot equals
seed(race seed cell) advanced by N calls, where N = s5_ram - s5_seed (s5 is a call counter).
"""
# code: next = FUN_002401e0, seed = FUN_002402c0; stream A state .bss 0x345E58 (rand FUN_0023d4a8,
# srand FUN_0023d498); stream B state .bss 0x345E70 (FUN_0023d4d8 / FUN_0023d4c8); default state
# .data 0x3A4A00 copied into both by FUN_0023d5f8; race seed cell 0x32DE90 = mgr+0x2B00.
M = (1 << 32) - 1
# seed adders 0xF22D0E56 0x96041893 0x3DF3B646 0x40DDE76D 0x97327AE1 0xD1A9FBE7
SEED_ADD = (4063039062, 2516850835, 1039382086, 1088284525, 2536667873, 3517578215)
# .data 0x3A4A00: 0xF22D0E56 0x883126E9 0xC624DD2F 0x0702C49C 0x9E353F7D 0x6FDF3B64
DEFAULT_STATE = (4063039062, 2284922601, 3324304687, 117621916, 2654289789, 1876900708)


class GameRng:
    def __init__(self, seed=None, state=None):
        if state is not None:
            self.s = [x & M for x in state]
        elif seed is not None:
            self.seed(seed)
        else:
            self.s = list(DEFAULT_STATE)

    def seed(self, x):
        acc = x & M
        out = []
        for c in SEED_ADD:          # running 32-bit sum: s[i] = x + c0 + ... + ci
            acc = (acc + c) & M
            out.append(acc)
        self.s = out

    def next(self):
        """One call of next(); returns an unsigned 32-bit value (callers use unsigned % n)."""
        s = self.s
        old5, old4 = s[5], s[4]
        a = (old5 + old4) & M
        c = 1 if (a < old5 or a < old4) else 0
        s[4] = a
        # ripple: s3 += s4 + c, s2 += s3 + c, s1 += s2 + c, s0 += s1 + c (carry = result < addend)
        for i in (3, 2, 1, 0):
            addend = s[i]
            a = (a + addend + c) & M
            c = 1 if a < addend else 0
            s[i] = a
        s[5] = (old5 + 1) & M
        if s[5] == 0:               # counter wrapped: increment s4..s0 with carry
            for i in (4, 3, 2, 1, 0):
                s[i] = (s[i] + 1) & M
                if s[i] != 0:
                    break
            a = s[0]
        return a

    def randint(self, n):
        """rand() % n as the game does it (unsigned remainder)."""
        return self.next() % n

    def uniform(self, lo, hi):
        """Game uniform(lo, hi) (stream B): float in [lo, hi) from the low 23 bits."""  # FUN_0023d4f8
        import struct
        bits = (self.next() & ((1 << 23) - 1)) | (127 << 23)   # mantissa bits, exponent of 1.0
        f = struct.unpack('<f', struct.pack('<I', bits))[0] - 1.0
        return lo + (hi - lo) * f


RACE_SEED_CELL = 3333776   # 0x32DE90 (mgr+0x2B00), the stream-B seed used at race start
STREAM_B_STATE = 3432048   # 0x345E70


def _validate():
    import glob, os, struct
    import sys
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    import traces
    root = str(traces.root() or '.')
    g = GameRng(0)
    assert tuple(g.s) == DEFAULT_STATE, "seed(0) must equal the .data default state"
    print("seed(0) == .data default state: OK")
    dumps = glob.glob(os.path.join(root, 'live', '*', 'spawn_ram.bin')) + \
        glob.glob(os.path.join(root, 'ram', '*.bin'))
    for p in dumps:
        d = open(p, 'rb').read()
        seed = struct.unpack_from('<I', d, RACE_SEED_CELL)[0]
        ram_b = list(struct.unpack_from('<6I', d, STREAM_B_STATE))
        r = GameRng(seed)
        n = (ram_b[5] - r.s[5]) & M
        if n > 5_000_000:
            print(f"{os.path.relpath(p, root)}: N={n} too large, skipped"); continue
        for _ in range(n):
            r.next()
        print(f"{os.path.relpath(p, root)}: stream B = srandB({seed:#x}) + {n} calls: "
              f"{'MATCH' if r.s == ram_b else 'MISMATCH'}")


if __name__ == '__main__':
    _validate()
