"""Default record (high score) tables from the executable -> converted/frontend/progression_tables.json.
    python tools/export_progression_data.py [out.json]
"""
# FUN_00161B40 / 0x161D8C / 0x1631C0 / 0x3352F0
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import struct
import sys

import capstone

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, '..'))
EXE = open(os.path.join(repo, 'extracted', 'SLUS_203.26'), 'rb').read()
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'converted', 'frontend', 'progression_tables.json')


def off(va):
    return va - 0xFF000


def u32(va):
    return struct.unpack_from('<I', EXE, off(va))[0]


def i32(va):
    return struct.unpack_from('<i', EXE, off(va))[0]


START, STOP = 0x161D8C, 0x1631C0      # FUN_00161B40
MGR = 0x10000000
SP = 0x20000000
M64 = (1 << 64) - 1


class Cpu:
    def __init__(self):
        self.r = {'zero': 0, 'fp': MGR, 'sp': SP, 's3': 0x380000, 's4': 0x380000, 's5': 0x380000,
                  's6': 0x380000, 's7': MGR + 0x3110}
        self.mem = {}

    def get(self, n):
        if n not in self.r:
            raise SystemExit('register $%s read before it is set (range changed?)' % n)
        return self.r[n]

    def set(self, n, v):
        if n != 'zero':
            self.r[n] = v & M64

    def rb(self, a):
        if a in self.mem:
            return self.mem[a]
        if 0x100000 <= a < 0x100000 + len(EXE):
            return EXE[off(a)]
        if a >= SP:
            return 0
        raise SystemExit('read of unmapped address %#x' % a)

    def load(self, a, n):
        return int.from_bytes(bytes(self.rb(a + i) for i in range(n)), 'little')

    def store(self, a, n, v):
        for i in range(n):
            self.mem[a + i] = (v >> (8 * i)) & 0xFF


def sx(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def run():
    md = capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS64 + capstone.CS_MODE_LITTLE_ENDIAN)
    md.detail = True
    c = Cpu()
    code = EXE[off(START):off(STOP)]
    for ins in md.disasm(code, START):
        ops = ins.operands
        regs = [ins.reg_name(o.reg) for o in ops if o.type == capstone.mips.MIPS_OP_REG]
        m = ins.mnemonic

        def mem():
            o = [x for x in ops if x.type == capstone.mips.MIPS_OP_MEM][0]
            return (c.get(ins.reg_name(o.mem.base)) + o.mem.disp) & 0xFFFFFFFF

        imm = [o.imm for o in ops if o.type == capstone.mips.MIPS_OP_IMM]
        if m == 'nop':
            continue
        if m == 'lui':
            c.set(regs[0], sx(imm[0] << 16, 32))
        elif m == 'ori':
            c.set(regs[0], c.get(regs[1]) | (imm[0] & 0xFFFF))
        elif m == 'addiu':
            c.set(regs[0], sx(c.get(regs[1]) + imm[0], 32))
        elif m == 'addu':
            c.set(regs[0], sx(c.get(regs[1]) + c.get(regs[2]), 32))
        elif m == 'move':
            c.set(regs[0], c.get(regs[1]))
        elif m in ('lw', 'lh', 'lb'):
            n = {'lw': 4, 'lh': 2, 'lb': 1}[m]
            c.set(regs[0], sx(c.load(mem(), n), 8 * n))
        elif m in ('sw', 'sh', 'sb', 'sd'):
            n = {'sw': 4, 'sh': 2, 'sb': 1, 'sd': 8}[m]
            c.store(mem(), n, c.get(regs[0]))
        elif m in ('ldl', 'ldr'):
            a = mem()
            al, b = a & ~7, a & 7
            d = c.load(al, 8)
            old = c.r.get(regs[0], 0)
            if m == 'ldl':   # little endian: bytes al..a go to the top of the register
                sh = 56 - 8 * b
                v = ((d << sh) & M64) | (old & ((1 << sh) - 1))
            else:            # bytes a..al+7 go to the bottom
                sh = 8 * b
                v = (d >> sh) | (old & ~(M64 >> sh) & M64)
            c.set(regs[0], v)
        elif m in ('sdl', 'sdr'):
            a = mem()
            al, b = a & ~7, a & 7
            v = c.get(regs[0])
            if m == 'sdl':   # top b+1 bytes of the register -> al..a
                for i in range(b + 1):
                    c.store(al + i, 1, v >> (8 * (7 - b + i)))
            else:            # low 8-b bytes -> a..al+7
                for i in range(8 - b):
                    c.store(a + i, 1, v >> (8 * i))
        else:
            raise SystemExit('unsupported instruction %#x %s %s' % (ins.address, m, ins.op_str))
    return c


def tables(c):
    def b(o):
        return c.mem.get(MGR + o, 0)

    res = {}
    for t in range(13):
        tab = {}
        for k, kind in enumerate(('race', 'showoff', 'tc')):
            base = 0x16C4 + t * 0x150 + k * 0x70
            rows = []
            for e in range(5 if k < 2 else 1):
                eb = base + e * 0x14
                name = bytes(b(eb + i) for i in range(12)).split(b'\0')[0].decode('latin-1')
                value = struct.unpack('<i', bytes(b(eb + 0x10 + i) for i in range(4)))[0]
                rows.append([name, value])
            tab[kind] = rows
        res[str(t)] = tab
    return res


def check_constants():
    circuit = (0, 1, 2, 3, 4, 8, 5, 11)
    so = {0: (55000, 40000, 25000), 1: (95000, 65000, 35000), 2: (225000, 150000, 75000),
          3: (225000, 150000, 75000), 4: (275000, 175000, 125000), 8: (350000, 225000, 100000),
          5: (175000, 115000, 75000), 11: (500000, 300000, 150000)}
    race = [tuple(i32(0x3352F0 + 16 * k + 4 * j) for j in range(4)) for k in range(8)]
    show = [tuple(i32(0x335370 + 16 * k + 4 * j) for j in range(4)) for k in range(8)]
    bad = []
    if tuple(r[0] for r in race) != circuit or tuple(r[1:] for r in race) != ((1, 2, 3),) * 8:
        bad.append('race circuit list')
    if tuple(r[0] for r in show) != circuit or any(so[r[0]] != r[1:] for r in show):
        bad.append('showoff circuit list')
    if tuple(i32(0x37EE40 + 4 * k) for k in range(4)) != (15, 10, 5, 0):
        bad.append('medal rank points')
    if tuple(i32(0x3352C0 + 4 * k) for k in range(12)) != (3, 4, 7, 0, 10, 5, 6, 1, 11, 8, 9, 2):
        bad.append('rider unlock order')
    costs = (0, 0, 5, 20, 35, 60, 90, 120, 160, 200, 240, 999)
    if any(i32(0x332154 + (c * 12 + b) * 20) != costs[b] for c in range(12) for b in range(12)):
        bad.append('board costs')
    crc = []
    for i in range(256):
        v = i
        for _ in range(8):
            v = (v >> 1) ^ 0xA001 if v & 1 else v >> 1
        crc.append(v)
    if any(EXE[off(0x34F0F8) + i] | EXE[off(0x34F1F8) + i] << 8 != crc[i] for i in range(256)):
        bad.append('crc table')
    if bad:
        raise SystemExit('constants differ from the executable: ' + ', '.join(bad))


if __name__ == '__main__':
    check_constants()
    cpu = run()
    venue_mask = None
    data = dict(source='SLUS_203.26 FUN_00161B40 record stores 0x161D8C..0x1631C0',
                records=tables(cpu))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=1)
    n = sum(1 for t in data['records'].values() for k in t.values() for r in k if r[0])
    print('constants OK; wrote 13 x 3 record tables (%d named rows) to %s' % (n, out))
