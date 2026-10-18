"""EE FPU arithmetic helpers, bit-identical to SSXEE::FpuAdd / FpuSub in Source/SSXTrickyRebuild/Sim/EEFloat.h.

The EE FPU adder keeps one guard bit: the operand with the smaller exponent loses its mantissa bits below half an
ulp of the larger one (masked toward zero), then the sum rounds to nearest; an exponent gap of 25 or more leaves only
the sign of the smaller operand. VU0 sums stay IEEE (plain float32 +). Everything else in the models that mirrors the
single-precision C++ uses numpy float32 scalars.
"""
from __future__ import annotations

import struct

import numpy as np

f32 = np.float32


def _bits(x) -> int:
    return struct.unpack("<I", struct.pack("<f", float(x)))[0]


def _from_bits(u: int) -> np.float32:
    return f32(struct.unpack("<f", struct.pack("<I", u & 0xFFFFFFFF))[0])


def fpu_add(a, b) -> np.float32:
    ua, ub = _bits(f32(a)), _bits(f32(b))
    d = ((ua >> 23) & 0xFF) - ((ub >> 23) & 0xFF)
    if d >= 25:
        ub &= 0x80000000
    elif d > 1:
        ub &= (0xFFFFFFFF << (d - 1)) & 0xFFFFFFFF
    elif d <= -25:
        ua &= 0x80000000
    elif d < -1:
        ua &= (0xFFFFFFFF << (-d - 1)) & 0xFFFFFFFF
    with np.errstate(all="ignore"):
        return f32(_from_bits(ua) + _from_bits(ub))


def fpu_sub(a, b) -> np.float32:
    return fpu_add(a, -f32(b))


def rsqrt_vu(x) -> np.float32:
    """VU0 RSQRT Q = 1 / sqrt(x): the root rounded to float, then the divide."""
    with np.errstate(all="ignore"):
        return f32(f32(1.0) / f32(np.sqrt(np.float64(x))))


def sqrt_vu(x) -> np.float32:
    return f32(np.sqrt(np.float64(max(f32(x), f32(0.0)))))
