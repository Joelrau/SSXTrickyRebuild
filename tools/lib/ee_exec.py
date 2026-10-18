"""Small MIPS R5900 (PS2 EE) interpreter for the table initialisers of SLUS_203.26.

Several run-time tables (surface physics, HUD layouts, animation lookup tables, sprite sheets, per-track presentation
records, the relationship table) are built by the executable at boot or on load from immediate constants. The
exporters run those initialisers here, on a memory image of the user's own SLUS_203.26, and read the tables back.
Only what the initialisers use is implemented (integer ALU, loads/stores incl. unaligned and 128-bit, branches with
delay slots, calls, a scalar COP1 subset); an unknown opcode stops with an error naming its address.

    cpu = EE(exe_bytes)
    cpu.call(0x256188, a0=obj, stubs={0x2C2528: lambda cpu: ...})
    cpu.read(addr, n)
"""
from __future__ import annotations

import struct

import numpy as np

M64 = (1 << 64) - 1
M32 = 0xFFFFFFFF
RAM_SIZE = 32 << 20
RETURN_SENTINEL = 0x01FFFFF0          # call() returns when the program counter reaches this address
HEAP_BASE = 0x01800000                # alloc() hands out zeroed blocks from here (top 8 MB of EE RAM)
STACK_TOP = 0x01FF0000

REG_NAMES = ("zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 "
             "s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra").split()
REG = {n: i for i, n in enumerate(REG_NAMES)}


def _sx(v: int, bits: int) -> int:
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def _f(bits: int) -> np.float32:
    return np.frombuffer(struct.pack("<I", bits & M32), "<f4")[0]


def _b(x) -> int:
    return struct.unpack("<I", struct.pack("<f", np.float32(x)))[0]


def _fpu_add(a: int, b: int) -> int:
    """EE FPU add: the smaller operand keeps one guard bit below the larger's ulp, then round to nearest."""
    d = ((a >> 23) & 0xFF) - ((b >> 23) & 0xFF)
    if d >= 25:
        b &= 0x80000000
    elif d > 1:
        b &= (M32 << (d - 1)) & M32
    elif d <= -25:
        a &= 0x80000000
    elif d < -1:
        a &= (M32 << (-d - 1)) & M32
    with np.errstate(all="ignore"):
        return _b(_f(a) + _f(b))


class EEError(RuntimeError):
    pass


class EE:
    def __init__(self, exe: bytes):
        phoff = struct.unpack_from("<I", exe, 0x1C)[0]
        _, foff, vaddr, _, filesz, _memsz, _, _ = struct.unpack_from("<8I", exe, phoff)
        self.mem = bytearray(RAM_SIZE)
        self.mem[vaddr:vaddr + filesz] = exe[foff:foff + filesz]
        self.r = [0] * 32
        self.hi = self.lo = 0
        self.f = [0] * 32
        self.fcc = False
        self.heap = HEAP_BASE
        self.steps = 0

    def _a(self, a: int) -> int:
        a &= 0x1FFFFFFF                                   # kseg / uncached mirrors
        if a >= RAM_SIZE:
            raise EEError(f"access outside EE RAM: {a:#x} at pc {self.pc:#x}")
        return a

    def load(self, a: int, n: int) -> int:
        a = self._a(a)
        return int.from_bytes(self.mem[a:a + n], "little")

    def store(self, a: int, n: int, v: int):
        a = self._a(a)
        self.mem[a:a + n] = (v & ((1 << (8 * n)) - 1)).to_bytes(n, "little")

    def read(self, a: int, n: int) -> bytes:
        a = self._a(a)
        return bytes(self.mem[a:a + n])

    def write(self, a: int, data: bytes):
        a = self._a(a)
        self.mem[a:a + len(data)] = data

    def u32(self, a: int) -> int:
        return self.load(a, 4)

    def cstr(self, a: int) -> str:
        a = self._a(a)
        return bytes(self.mem[a:self.mem.index(0, a)]).decode("latin1")

    def alloc(self, n: int, align: int = 16) -> int:
        self.heap = (self.heap + align - 1) & ~(align - 1)
        a = self.heap
        self.heap += n
        return a

    def __getitem__(self, name: str) -> int:
        return self.r[REG[name]]

    def __setitem__(self, name: str, v: int):
        if name != "zero":
            self.r[REG[name]] = v & M64

    def _set(self, i: int, v: int):
        if i:
            self.r[i] = v & M64

    def _set32(self, i: int, v: int):
        if i:
            self.r[i] = _sx(v, 32) & M64

    def call(self, func: int, stubs: dict | None = None, max_steps: int = 5_000_000, **args) -> int:
        """Run func(args) until it returns; stubs maps a callee address to a Python function(cpu) that sets v0."""
        self.stubs = stubs or {}
        for k, v in args.items():
            self[k] = v
        if not self["sp"]:
            self["sp"] = STACK_TOP
        self["ra"] = RETURN_SENTINEL
        self.pc, self.npc = func, func + 4
        limit = self.steps + max_steps
        while self.pc != RETURN_SENTINEL:
            if self.pc in self.stubs and self.pc != func:
                self.stubs[self.pc](self)
                self.pc, self.npc = self.r[31] & M32, (self.r[31] & M32) + 4
                continue
            self.step()
            if self.steps > limit:
                raise EEError(f"{func:#x}: no return after {max_steps} instructions")
        return self["v0"]

    def _branch(self, cond: bool, target: int, likely: bool = False):
        if cond:
            self.npc = target
        elif likely:                                     # branch-likely not taken: the delay slot is nullified
            self.pc, self.npc = self.npc + 4, self.npc + 8
            return True
        return False

    def step(self):
        pc = self.pc
        w = self.load(pc, 4)
        self.steps += 1
        self.pc, self.npc = self.npc, self.npc + 4
        op, rs, rt, rd, sa, fn = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
        imm = w & 0xFFFF
        simm = _sx(imm, 16)
        R = self.r
        btarget = (pc + 4 + (simm << 2)) & M32
        if op == 0:
            if fn == 0x00: self._set32(rd, (R[rt] << sa) & M32)
            elif fn == 0x02: self._set32(rd, (R[rt] & M32) >> sa)
            elif fn == 0x03: self._set32(rd, _sx(R[rt], 32) >> sa)
            elif fn == 0x04: self._set32(rd, (R[rt] << (R[rs] & 31)) & M32)
            elif fn == 0x06: self._set32(rd, (R[rt] & M32) >> (R[rs] & 31))
            elif fn == 0x07: self._set32(rd, _sx(R[rt], 32) >> (R[rs] & 31))
            elif fn == 0x08: self.npc = R[rs] & M32
            elif fn == 0x09: self._set(rd, pc + 8); self.npc = R[rs] & M32
            elif fn == 0x0A:
                if R[rt] == 0: self._set(rd, R[rs])
            elif fn == 0x0B:
                if R[rt] != 0: self._set(rd, R[rs])
            elif fn == 0x0F: pass
            elif fn == 0x10: self._set(rd, self.hi)
            elif fn == 0x11: self.hi = R[rs]
            elif fn == 0x12: self._set(rd, self.lo)
            elif fn == 0x13: self.lo = R[rs]
            elif fn == 0x14: self._set(rd, R[rt] << (R[rs] & 63))
            elif fn == 0x16: self._set(rd, R[rt] >> (R[rs] & 63))
            elif fn == 0x17: self._set(rd, _sx(R[rt], 64) >> (R[rs] & 63))
            elif fn in (0x18, 0x19):
                if fn == 0x18: p = _sx(R[rs], 32) * _sx(R[rt], 32)
                else: p = (R[rs] & M32) * (R[rt] & M32)
                self.lo, self.hi = _sx(p, 32) & M64, _sx(p >> 32, 32) & M64
                self._set(rd, self.lo)
            elif fn in (0x1A, 0x1B):
                a, b = (_sx(R[rs], 32), _sx(R[rt], 32)) if fn == 0x1A else (R[rs] & M32, R[rt] & M32)
                if b:
                    q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
                    self.lo, self.hi = _sx(q, 32) & M64, _sx(a - q * b, 32) & M64
            elif fn in (0x20, 0x21): self._set32(rd, R[rs] + R[rt])
            elif fn in (0x22, 0x23): self._set32(rd, R[rs] - R[rt])
            elif fn == 0x24: self._set(rd, R[rs] & R[rt])
            elif fn == 0x25: self._set(rd, R[rs] | R[rt])
            elif fn == 0x26: self._set(rd, R[rs] ^ R[rt])
            elif fn == 0x27: self._set(rd, ~(R[rs] | R[rt]))
            elif fn == 0x2A: self._set(rd, int(_sx(R[rs], 64) < _sx(R[rt], 64)))
            elif fn == 0x2B: self._set(rd, int(R[rs] < R[rt]))
            elif fn in (0x2C, 0x2D): self._set(rd, R[rs] + R[rt])
            elif fn in (0x2E, 0x2F): self._set(rd, R[rs] - R[rt])
            elif fn == 0x38: self._set(rd, R[rt] << sa)
            elif fn == 0x3A: self._set(rd, R[rt] >> sa)
            elif fn == 0x3B: self._set(rd, _sx(R[rt], 64) >> sa)
            elif fn == 0x3C: self._set(rd, R[rt] << (sa + 32))
            elif fn == 0x3E: self._set(rd, R[rt] >> (sa + 32))
            elif fn == 0x3F: self._set(rd, _sx(R[rt], 64) >> (sa + 32))
            else: raise EEError(f"unhandled SPECIAL {fn:#x} at {pc:#x}")
        elif op == 1:
            v = _sx(R[rs], 64)
            if rt in (0, 2): self._branch(v < 0, btarget, rt == 2)
            elif rt in (1, 3): self._branch(v >= 0, btarget, rt == 3)
            else: raise EEError(f"unhandled REGIMM {rt:#x} at {pc:#x}")
        elif op == 2: self.npc = (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
        elif op == 3: self.r[31] = pc + 8; self.npc = (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
        elif op in (4, 0x14): self._branch(R[rs] == R[rt], btarget, op == 0x14)
        elif op in (5, 0x15): self._branch(R[rs] != R[rt], btarget, op == 0x15)
        elif op in (6, 0x16): self._branch(_sx(R[rs], 64) <= 0, btarget, op == 0x16)
        elif op in (7, 0x17): self._branch(_sx(R[rs], 64) > 0, btarget, op == 0x17)
        elif op in (8, 9): self._set32(rt, R[rs] + simm)
        elif op == 0x0A: self._set(rt, int(_sx(R[rs], 64) < simm))
        elif op == 0x0B: self._set(rt, int(R[rs] < (simm & M64)))
        elif op == 0x0C: self._set(rt, R[rs] & imm)
        elif op == 0x0D: self._set(rt, R[rs] | imm)
        elif op == 0x0E: self._set(rt, R[rs] ^ imm)
        elif op == 0x0F: self._set32(rt, imm << 16)
        elif op == 0x11: self._cop1(w, pc, rs, rt, rd, sa, fn, btarget)
        elif op in (0x18, 0x19): self._set(rt, R[rs] + simm)
        elif op == 0x1A or op == 0x1B:
            a = (R[rs] + simm) & M32
            base, k = a & ~7, a & 7
            dw = self.load(base, 8)
            if op == 0x1A:
                sh = 8 * (7 - k)
                self._set(rt, (R[rt] & ((1 << sh) - 1)) | ((dw << sh) & M64))
            else:
                sh = 8 * k
                keep = (M64 << (64 - sh)) & M64 if sh else 0
                self._set(rt, (R[rt] & keep) | (dw >> sh))
        elif op == 0x1E: self._set(rt, 0); self.q = getattr(self, "q", {}); self.q[rt] = self.load((R[rs] + simm) & ~15 & M32, 16)
        elif op == 0x1F:
            q = getattr(self, "q", {}).get(rt)
            v = q if q is not None and (q & M64) == R[rt] else R[rt]
            self.store((R[rs] + simm) & ~15 & M32, 16, v)
        elif op == 0x20: self._set(rt, _sx(self.load(R[rs] + simm, 1), 8))
        elif op == 0x21: self._set(rt, _sx(self.load(R[rs] + simm, 2), 16))
        elif op == 0x23: self._set(rt, _sx(self.load(R[rs] + simm, 4), 32))
        elif op == 0x24: self._set(rt, self.load(R[rs] + simm, 1))
        elif op == 0x25: self._set(rt, self.load(R[rs] + simm, 2))
        elif op == 0x27: self._set(rt, self.load(R[rs] + simm, 4))
        elif op == 0x28: self.store(R[rs] + simm, 1, R[rt])
        elif op == 0x29: self.store(R[rs] + simm, 2, R[rt])
        elif op == 0x2B: self.store(R[rs] + simm, 4, R[rt])
        elif op == 0x2C or op == 0x2D:
            a = (R[rs] + simm) & M32
            base, k = a & ~7, a & 7
            dw = self.load(base, 8)
            if op == 0x2C:
                n = k + 1                                 # bytes base..a get the top n bytes of rt
                mask = (1 << (8 * n)) - 1
                dw = (dw & ~mask & M64) | (R[rt] >> (8 * (8 - n)))
            else:
                sh = 8 * k
                dw = (dw & ((1 << sh) - 1)) | ((R[rt] << sh) & M64)
            self.store(base, 8, dw)
        elif op == 0x2F: pass
        elif op == 0x31: self.f[rt] = self.load(R[rs] + simm, 4)
        elif op == 0x33: pass
        elif op == 0x37: self._set(rt, self.load(R[rs] + simm, 8))
        elif op == 0x39: self.store(R[rs] + simm, 4, self.f[rt])
        elif op == 0x3F: self.store(R[rs] + simm, 8, R[rt])
        elif op == 0x1C and fn == 0x09 and sa == 0x12 and R[rt] == 0:   # por rd, rs, $zero (128-bit move)
            self._set(rd, R[rs])
            q = getattr(self, "q", {})
            if rs in q: q[rd] = q[rs]
        else:
            raise EEError(f"unhandled opcode {op:#x} (word {w:#010x}) at {pc:#x}")

    def _cop1(self, w, pc, fmt, ft, fs, fd, fn, btarget):
        F = self.f
        if fmt == 0x00: self._set32(ft, F[fs])                                      # mfc1
        elif fmt == 0x04: F[fs] = self.r[ft] & M32                                  # mtc1
        elif fmt in (0x02, 0x06): pass                                              # cfc1 / ctc1
        elif fmt == 0x08:                                                           # bc1f / bc1t (+likely)
            tf, likely = ft & 1, bool(ft & 2)
            self._branch(self.fcc == bool(tf), btarget, likely)
        elif fmt == 0x10:
            a, b = F[fs], F[ft]
            with np.errstate(all="ignore"):
                if fn == 0x00: F[fd] = _fpu_add(a, b)
                elif fn == 0x01: F[fd] = _fpu_add(a, b ^ 0x80000000)
                elif fn == 0x02: F[fd] = _b(_f(a) * _f(b))
                elif fn == 0x03: F[fd] = _b(_f(a) / _f(b)) if b & 0x7FFFFFFF else (a ^ b) & 0x80000000 | 0x7F7FFFFF
                elif fn == 0x04: F[fd] = _b(np.sqrt(abs(_f(b))))
                elif fn == 0x05: F[fd] = a & 0x7FFFFFFF
                elif fn == 0x06: F[fd] = a
                elif fn == 0x07: F[fd] = a ^ 0x80000000
                elif fn == 0x24: F[fd] = int(np.trunc(_f(a))) & M32                    # cvt.w.s
                elif fn == 0x28: F[fd] = _b(max(_f(a), _f(b)))
                elif fn == 0x29: F[fd] = _b(min(_f(a), _f(b)))
                elif fn == 0x32: self.fcc = _f(a) == _f(b)
                elif fn == 0x34: self.fcc = _f(a) < _f(b)
                elif fn == 0x36: self.fcc = _f(a) <= _f(b)
                elif fn == 0x30: self.fcc = False
                else: raise EEError(f"unhandled COP1.S {fn:#x} at {pc:#x}")
        elif fmt == 0x14 and fn == 0x20:                                            # cvt.s.w
            F[fd] = _b(np.float32(_sx(F[fs], 32)))
        else:
            raise EEError(f"unhandled COP1 fmt {fmt:#x} fn {fn:#x} at {pc:#x}")
