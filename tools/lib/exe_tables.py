"""Run-time tables that SLUS_203.26 builds from code constants, produced by running the game's own
initialisers in ee_exec (nothing here is stored data: the values come from the user's executable).

    surface_table()        0x256188  surface physics, 20 records x 25 words
    world_track_table()    0x257DC0  per-track presentation, 13 records x 0x94 bytes
    aggression_table()     0x167208  relationship table [me][other] = 4 words
    anim_boot_image()      0x15AEC0  animation id lookup tables (BSS) + property-table rewrites
    fe_sprite_sheet()      0x1F1580 + 0x1F8C98  front-end icon records and their pages
    hud_sprite_sheet()     0x1F4440 + 0x1F9080  in-race icon records and their pages
    hud_tables()           0x19E888  HUD layouts 0x33CA40, boost records 0x338008, letters 0x338198
"""
from __future__ import annotations

import os
import struct
from functools import lru_cache

from ee_exec import EE

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE_PATH = os.path.join(REPO, "extracted", "SLUS_203.26")

SHEET_RECORD = 0x1C
_STUB_VCALL = 0x01FFFF00
_TEX_LOOKUP = 0x2C2528        # (ssh, image name) -> handle; stubbed to return the name pointer
_MEMSET = 0x2FB468
_HUD_RESET = 0x1A7D78


@lru_cache(maxsize=1)
def exe_bytes() -> bytes:
    with open(EXE_PATH, "rb") as f:
        return f.read()


def _cpu() -> EE:
    return EE(exe_bytes())


def surface_table() -> list:
    """20 rows of 25 values; words 17 and 21 are integers, the rest floats."""
    cpu = _cpu()
    t = cpu.alloc(0x1000)
    cpu.call(0x256188, a0=t)
    rows = []
    for s in range(20):
        o = t + s * 100
        rows.append([struct.unpack_from("<i" if i in (17, 21) else "<f", cpu.mem, o + 4 * i)[0] for i in range(25)])
    return rows


def world_track_table() -> bytes:
    cpu = _cpu()
    t = cpu.alloc(0x1000)
    cpu.call(0x257DC0, a0=t)
    return cpu.read(t, 13 * 0x94)


def aggression_table() -> list:
    cpu = _cpu()
    t = cpu.alloc(0x900)
    cpu.call(0x167208, a0=t)
    return [[list(struct.unpack_from("<4i", cpu.mem, t + me * 0xC0 + o * 0x10)) for o in range(12)] for me in range(12)]


@lru_cache(maxsize=1)
def anim_boot_image() -> bytes:
    """EE memory image after the boot-time animation table initialiser (addresses = EE addresses)."""
    cpu = _cpu()
    cpu.call(0x15AEC0)
    return bytes(cpu.mem)


def _sheet_cpu():
    cpu = _cpu()
    sheet = cpu.alloc(0x2000)
    game, obj, vt = cpu.alloc(0x1000), cpu.alloc(0x100), cpu.alloc(0x200)
    cpu.store(0x337C58, 4, game)
    cpu.store(game + 0x724, 4, obj)
    cpu.store(obj + 0x10, 4, vt)
    cpu.store(vt + 0x154, 4, _STUB_VCALL)
    stubs = {_TEX_LOOKUP: lambda c: c.__setitem__("v0", c["a1"]),
             _STUB_VCALL: lambda c: c.__setitem__("v0", c["a2"])}
    return cpu, sheet, stubs


def _sheet_records(cpu, sheet, count):
    """{id: (page name or None, w, h, f3, f4, f5, f6)} for the records the initialiser filled."""
    out = {}
    for i in range(count):
        e = sheet + 8 + i * SHEET_RECORD
        p = cpu.u32(e)
        page = cpu.cstr(p) if 0x100000 <= p < 0x2000000 else None
        vals = struct.unpack_from("<6f", cpu.mem, e + 4)
        if any(vals):
            out[i] = (page,) + vals
    return out


def fe_sprite_sheet(count: int = 140) -> dict:
    cpu, sheet, stubs = _sheet_cpu()
    cpu.call(0x1F1580, a0=sheet)
    cpu.call(0x1F8C98, stubs=stubs, a0=sheet, a1=1)
    return _sheet_records(cpu, sheet, count)


def hud_sprite_sheet(count: int = 0x8A) -> dict:
    cpu, sheet, stubs = _sheet_cpu()
    cpu.call(0x1F4440, a0=sheet)
    cpu.call(0x1F9080, stubs=stubs, a0=sheet, a1=1)
    return _sheet_records(cpu, sheet, count)


def hud_tables() -> EE:
    """CPU state after the HUD overlay table setup; read 0x33CA40 / 0x338008 / 0x338198 from it."""
    cpu = _cpu()
    this = cpu.alloc(0x1000)

    def memset(c):
        c.write(c["a0"], bytes([c["a1"] & 0xFF]) * c["a2"])
        c["v0"] = c["a0"]

    cpu.call(0x19E888, stubs={_HUD_RESET: lambda c: c.__setitem__("v0", 0), _MEMSET: memset}, a0=this)
    return cpu
