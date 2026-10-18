"""Reader for the user's extracted SLUS_203.26 (one PT_LOAD segment: file offset = va - 0xFF000)."""
import os
import struct

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
EXE_PATH = os.path.join(REPO, "extracted", "SLUS_203.26")
with open(EXE_PATH, "rb") as _f:
    EXE = _f.read()


def off(va):
    return va - 0xFF000


def u32(va):
    return struct.unpack_from("<I", EXE, off(va))[0]


def i32(va):
    return struct.unpack_from("<i", EXE, off(va))[0]


def u16(va):
    return struct.unpack_from("<H", EXE, off(va))[0]


def s16(va):
    return struct.unpack_from("<h", EXE, off(va))[0]


def u8(va):
    return EXE[off(va)]


def f32(va):
    return struct.unpack_from("<f", EXE, off(va))[0]


def cstr(va):
    o = off(va)
    return EXE[o:EXE.index(b"\0", o)].decode("latin-1")


def board_table():
    """Board table at .data 0x332148: 12 characters x 12 boards x 20 bytes
    {char* name, u32 index, u32 type, u32 unlock cost, u8 stat[4]}."""
    out = []
    for c in range(12):
        for b in range(12):
            a = 0x332148 + (c * 12 + b) * 20
            out.append(dict(char=c, slot=b, addr=a, name=cstr(u32(a)), index=u32(a + 4), type=u32(a + 8),
                            cost=u32(a + 12), stats=list(EXE[off(a + 16):off(a + 20)])))
    return out
