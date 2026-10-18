"""EA MicroTalk 10:1 speech decoder (SCHl codec 0x04: every stream of the speech .dat files in SPEECH.BIG).

Written from the game's decoder (frame 0x2E31E8, pulses 0x2E2C50, interpolation 0x2E2E98, reflection -> direct
form 0x2E2F10, synthesis 0x2E3620); its code tables are read from the user's SLUS_203.26. The output matches
vgmstream sample for sample; where vgmstream's float details differ from the EE, vgmstream is followed (marked).

Stream: every SCDl block is u32 samples, u32 offset per channel (from the end of the offset table), then one
payload per channel: u8 reset flag (nonzero only in the stream's first block) and an LSB-first bit stream that
restarts byte-aligned in every block. The first block opens with the header; a block holds ceil(samples / 432)
frames (only the last block is partial). Decoder state carries over from block to block.
Header (15 bits): 1 unused, 4 bits n (a frame whose first reflection index is < 32 - n has multi-pulse excitation,
  the others RELP), 4 bits g, 6 bits m: fixed gains G[0] = 8 (g + 1), G[i] = G[i - 1] * (1.04 + 0.001 m).
Frame (432 samples = 4 subframes of 108):
  12 indices into the 64-entry reflection table RC: 4 x 6 bits, then 8 x 5 bits (+ 16).
  Per subframe 20 bits: 8 lag L, 4 pitch gain (x 1/15), 6 fixed gain index, 1 phase, 1 "no interpolation";
  then 54 pulses on the samples of that phase (0: even samples, 1: odd):
    multi-pulse: CODEBOOK[model][next 8 bits] -> COMMAND {next model, code bits, pulse}; commands 4..28 are a
      pulse -6..+6, 2/3 a run of 7 + (6 bits) zero pulses, 0/1 a pulse of 7 + (number of 1 bits before a 0),
      positive if the following bit is 1. Model 1 follows a pulse with |p| >= 2.
    RELP: bit 0 -> 0, bits 1,0 -> -2, bits 1,1 -> +2.
  The other phase stays zero, or (no-interpolation bit 0) is filled by a 6-tap half-band filter of the pulses and
  the fixed gain is halved.
  Excitation e[n] = pitch gain * e[n - 108 - L] + fixed gain * pulse[n] over a 324-sample history; a source index
  before the history reads its first sample (vgmstream; the EE reads the context fields stored before it).
Synthesis: before samples 0, 12, 24 and 36 of a frame the reflection coefficients move a quarter of the way to the
  frame's targets and are converted to a 12-tap direct form a; y[n] = e[n] + sum_k a[k] y[n - 1 - k], k = 0..11
  in order (vgmstream; the EE's multiply-accumulate rotates the order).
Output: y + 0.5 (y < 0: y - 0.5) in float32, truncated, clamped to int16.
All arithmetic is float32 in the order written.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

import numpy as np

import slus

FRAME, SUB, HIST = 432, 108, 324
F32 = np.float32
LANE_SAMPLES = 1 << 24          # batch size limit: lanes x padded samples


def _f32(bits: int) -> np.float32:
    return np.frombuffer(struct.pack("<I", bits), "<f4")[0]


TAP1, TAP3, TAP5 = _f32(0x3F18EE49), _f32(0xBDEAAEFB), _f32(0x3C93B945)
INV15 = _f32(0x3D888889)
GAIN_STEP, GAIN_BASE = _f32(0x3A83126F), _f32(0x3F851EB8)       # 0.001, 1.04
QUARTER, HALF = F32(0.25), F32(0.5)

_tables = None


def tables():
    """(RC float32[64] at .data 0x35A738, per model 256 x (code bits, kind, pulse, next model)) where kind is
    0 pulse, 1 zero run, 2 large pulse; built from CODEBOOK (2 x 256 bytes, 0x35A838) and COMMAND
    (29 x {i32 next model, i32 code bits, f32 pulse}, 0x35AA38)."""
    global _tables
    if _tables is None:
        rc = np.frombuffer(slus.EXE, "<f4", 64, slus.off(0x35A738)).astype(F32)
        cmds = [struct.unpack_from("<iif", slus.EXE, slus.off(0x35AA38) + 12 * i) for i in range(29)]
        models = []
        for m in range(2):
            row = []
            for b in range(256):
                c = slus.EXE[slus.off(0x35A838) + 256 * m + b]
                nxt, bits, pulse = cmds[c]
                row.append((bits, 0 if c >= 4 else 1 if c >= 2 else 2, int(pulse), nxt))
            models.append(row)
        _tables = rc, models
    return _tables


@dataclass
class Channel:
    """One channel of a stream, parsed: per frame 12 RC indices, per subframe (lag, pitch gain index, fixed gain
    index, phase, no-interpolation) and 54 pulses."""
    samples: int
    frames: int
    gains: np.ndarray
    rc: np.ndarray
    sub: np.ndarray
    pulses: np.ndarray


def _words(payload: bytes) -> list[int]:
    """32-bit little-endian word at every byte offset (bit p is word[p >> 3] >> (p & 7) & 1)."""
    a = np.frombuffer(payload + bytes(8), np.uint8).astype(np.uint32)
    return (a[:-3] | a[1:-2] << 8 | a[2:-1] << 16 | a[3:] << 24).tolist()


def parse_channel(payloads: list[bytes], block_samples: list[int]) -> Channel:
    """Parse one channel's block payloads (reset flag + bit stream each)."""
    _, models = tables()
    nframes = [-(-n // FRAME) for n in block_samples]
    total = sum(nframes)
    rc: list[int] = []
    sub: list[int] = []
    pulses = [0] * (total * 4 * 54)
    i = 0
    thr = gains = None
    for k, (payload, nf) in enumerate(zip(payloads, nframes)):
        if (payload[0] != 0) != (k == 0):
            raise ValueError("MicroTalk decoder reset outside a stream's first block")
        w = _words(payload[1:])
        p = 0
        if k == 0:
            h = w[0]
            thr = 32 - (h >> 1 & 15)
            g = F32(8.0 * ((h >> 5 & 15) + 1))
            step = F32(F32(h >> 9 & 63) * GAIN_STEP) + GAIN_BASE
            gains = np.empty(64, F32)
            gains[0] = g
            for n in range(1, 64):
                gains[n] = gains[n - 1] * step
            p = 15
        for _ in range(nf):
            v = w[p >> 3] >> (p & 7)
            rc += (v & 63, v >> 6 & 63, v >> 12 & 63, v >> 18 & 63)
            multipulse = (v & 63) < thr
            p += 24
            for _half in range(2):
                v = w[p >> 3] >> (p & 7)
                rc += ((v & 31) + 16, (v >> 5 & 31) + 16, (v >> 10 & 31) + 16, (v >> 15 & 31) + 16)
                p += 20
            for _s in range(4):
                v = w[p >> 3] >> (p & 7)
                sub += (v & 255, v >> 8 & 15, v >> 12 & 63, v >> 18 & 1, v >> 19 & 1)
                p += 20
                end = i + 54
                if multipulse:
                    m = 0
                    while i < end:
                        v = w[p >> 3] >> (p & 7)
                        bits, kind, pulse, m = models[m][v & 255]
                        p += bits
                        if kind == 0:
                            pulses[i] = pulse
                            i += 1
                        elif kind == 1:
                            i += (v >> bits & 63) + 7
                            p += 6
                        else:
                            pulse = 7
                            while w[p >> 3] >> (p & 7) & 1:
                                pulse += 1
                                p += 1
                            p += 1
                            pulses[i] = pulse if w[p >> 3] >> (p & 7) & 1 else -pulse
                            p += 1
                            i += 1
                    i = end
                else:
                    while i < end:
                        v = w[p >> 3] >> (p & 7)
                        if v & 1:
                            pulses[i] = 2 if v & 2 else -2
                            p += 2
                        else:
                            p += 1
                        i += 1
    return Channel(sum(block_samples), total, gains, np.array(rc, np.uint8).reshape(total, 12),
                   np.array(sub, np.int16).reshape(total * 4, 5), np.array(pulses, np.int16).reshape(total * 4, 54))


def parse_stream(data: bytes, start: int, end: int, nch: int) -> list[Channel]:
    """Parse the SCHl stream data[start:end] (nch channels, PT tag 0x82) into its channels."""
    payloads: list[list[bytes]] = [[] for _ in range(nch)]
    samples: list[int] = []
    p = start + struct.unpack_from("<I", data, start + 4)[0]
    while p < end:
        size = struct.unpack_from("<I", data, p + 4)[0]
        if data[p:p + 4] == b"SCDl":
            base = p + 12 + 4 * nch
            offs = list(struct.unpack_from(f"<{nch}I", data, p + 12)) + [size - 12 - 4 * nch]
            for c in range(nch):
                payloads[c].append(data[base + offs[c]:base + offs[c + 1]])
            samples.append(struct.unpack_from("<I", data, p + 8)[0])
        p += size
    if any(n % FRAME for n in samples[:-1]):
        raise ValueError("MicroTalk block that is not a whole number of frames before the last block")
    return [parse_channel(payloads[c], samples) for c in range(nch)]


def _direct_form(rc: np.ndarray) -> np.ndarray:
    """Reflection coefficients (12, N) -> direct-form taps (12, N), 0x2E2F10's operation order."""
    t = np.empty_like(rc)
    t[0] = 1.0
    t[1:] = rc[:11]
    u = np.empty_like(rc)
    a = np.empty_like(rc)
    for i in range(12):
        x = -rc[11] * t[11]
        for j in range(10, -1, -1):
            x = x - rc[j] * t[j]
            t[j + 1] = rc[j] * x + t[j]
        t[0] = x
        u[i] = x
        for m in range(i):
            x = x - a[m] * u[i - 1 - m]
        a[i] = x
    return a


def _interpolate(e: np.ndarray, lo: int) -> np.ndarray:
    """Half-band interpolation at e[..., lo::2] (54 samples) from the samples 1, 3, 5 away (vgmstream's sum order)."""
    s = lambda d: e[..., lo + d:lo + d + 107:2]  # noqa: E731
    outer = (s(-5) + s(5)) * TAP5
    return (outer + (s(-3) * TAP3 + s(3) * TAP3)) + (s(-1) * TAP1 + s(1) * TAP1)


def decode_channels(chs: list[Channel]) -> list[np.ndarray]:
    """Decode channels in lockstep (vectorised across channels); returns float32 samples per channel."""
    rc_table, _ = tables()
    lanes = len(chs)
    nf = max(c.frames for c in chs)
    ns = nf * 4
    lane = np.arange(lanes)
    rc_idx = np.zeros((nf, lanes, 12), np.uint8)
    sub = np.zeros((ns, lanes, 5), np.int16)
    pulses = np.zeros((ns, lanes, 54), np.int16)
    gains = np.zeros((lanes, 64), F32)
    targets = np.zeros((nf, lanes, 12), F32)
    for b, c in enumerate(chs):
        rc_idx[:c.frames, b] = c.rc
        targets[:c.frames, b] = rc_table[c.rc]
        sub[:c.frames * 4, b] = c.sub
        pulses[:c.frames * 4, b] = c.pulses
        gains[b] = c.gains
    lag, pitch, fixed, phase, plain = (sub[..., k] for k in range(5))

    # Fixed excitation: pulses on their phase, the other phase zero or interpolated; padded by 5 zeros each side.
    e = np.zeros((ns, lanes, 118), F32)
    odd = (phase == 1)[..., None]
    pv = pulses.astype(F32)
    e[..., 5:113:2] = np.where(odd, 0, pv)
    e[..., 6:114:2] = np.where(odd, pv, 0)
    fill = (plain == 0)[..., None]
    at_odd, at_even = _interpolate(e, 6), _interpolate(e, 5)
    e[..., 6:114:2] = np.where(fill & ~odd, at_odd, e[..., 6:114:2])
    e[..., 5:113:2] = np.where(fill & odd, at_even, e[..., 5:113:2])
    gain = gains[lane, fixed]
    gain = np.where(plain == 0, gain * HALF, gain)
    fixed_part = gain[..., None] * e[..., 5:113]
    del e, pv, at_odd, at_even
    pitch_gain = pitch.astype(F32) * INV15

    # Adaptive codebook: x[HIST + n] is excitation sample n.
    x = np.zeros((HIST + ns * SUB, lanes), F32)
    j = np.arange(SUB)
    for s in range(ns):
        f, q = divmod(s, 4)
        src = FRAME * f + np.maximum((216 + SUB * q - lag[s].astype(np.int64))[:, None] + j, 0)
        x[HIST + SUB * s:HIST + SUB * (s + 1)] = (pitch_gain[s][:, None] * x[src, lane[:, None]] + fixed_part[s]).T
    del fixed_part

    # Reflection coefficients after each quarter step, then direct-form taps as (frame, step, tap, lane).
    rc = np.zeros((lanes, 12), F32)
    steps = np.empty((nf, 4, lanes, 12), F32)
    for f in range(nf):
        delta = (targets[f] - rc) * QUARTER
        for q in range(4):
            rc = rc + delta
            steps[f, q] = rc
    taps = _direct_form(np.ascontiguousarray(steps.reshape(-1, 12).T)).T.reshape(nf, 4, lanes, 12)
    taps = np.ascontiguousarray(taps.transpose(0, 1, 3, 2))

    # Synthesis: y[12 + n] is output sample n; terms rows are e[n], a[0] y[n-1], ..., a[11] y[n-12], summed in
    # that order (add.accumulate is sequential; add.reduce may sum pairwise).
    y = np.zeros((12 + nf * FRAME, lanes), F32)
    terms = np.empty((13, lanes), F32)
    sums = np.empty((13, lanes), F32)
    for f in range(nf):
        for q, (a, b) in enumerate(((0, 12), (12, 24), (24, 36), (36, FRAME))):
            tq = taps[f, q]
            for n in range(FRAME * f + a, FRAME * f + b):
                terms[0] = x[HIST + n]
                np.multiply(tq, y[n:n + 12][::-1], out=terms[1:])
                np.add.accumulate(terms, axis=0, out=sums)
                y[12 + n] = sums[12]
    return [y[12:12 + c.samples, b] for b, c in enumerate(chs)]


def to_pcm16(y: np.ndarray) -> np.ndarray:
    """y +- 0.5 in float32 (away from zero), truncated and clamped to int16."""
    return np.clip(np.trunc(np.where(y < 0, y - HALF, y + HALF)), -32768, 32767).astype(np.int16)


def batches(lengths: list[tuple[int, int]], lane_limit: int = 256) -> list[list[int]]:
    """Group streams (samples, channels) into lockstep batches of similar length; returns lists of stream indices."""
    order = sorted(range(len(lengths)), key=lambda k: lengths[k][0])
    out: list[list[int]] = []
    cur: list[int] = []
    lanes = 0
    for k in order:
        n, ch = lengths[k]
        if cur and (lanes + ch > lane_limit or (lanes + ch) * (n + FRAME) > LANE_SAMPLES):
            out.append(cur)
            cur, lanes = [], 0
        cur.append(k)
        lanes += ch
    if cur:
        out.append(cur)
    return out


def decode_streams(streams: list[list[Channel]]) -> list[np.ndarray]:
    """Decode parsed streams (one lockstep batch); returns int16 PCM (samples, channels) per stream."""
    flat = [c for chs in streams for c in chs]
    with np.errstate(over="ignore", invalid="ignore"):
        ys = decode_channels(flat)
    out, k = [], 0
    for chs in streams:
        out.append(np.stack([to_pcm16(ys[k + c]) for c in range(len(chs))], axis=1))
        k += len(chs)
    return out
