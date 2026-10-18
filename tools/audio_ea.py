"""EA PS2 audio containers and codecs as used by SSX Tricky (decoders written from the bytes).
Containers
  * SCHl stream: chunks 'SCHl' (header, "PT" tag list), 'SCCl' (block count), 'SCDl' (data
    blocks), 'SCEl' (end). Every chunk is tag(4) + u32 LE size (size includes the 8 bytes).
    A .mus / speech .dat is many SCHl streams back to back, each starting on a 0x80 boundary.
  * BNKl bank (version 5): "BNKl", u16 version, u16 slot count, u32 sample-data offset,
    u32 0, u32 sample-data size, then one u32 per slot: offset of the slot's PT header relative
    to the slot's own table entry (0 = empty slot). Sample data is addressed by PT tag 0x88
    (channel 0) / 0x89 (channel 1) as absolute file offsets.
PT tag list ("PT" + u16 LE platform, 5 = PS2): byte tag, byte length, big-endian value;
0xFC/0xFD/0xFE are section markers without payload, 0xFF ends the list.
  0x80 version (2)          0x82 channels (default 1)    0x84 sample rate (default 22050)
  0x85 sample count         0x86 loop start              0x87 loop end (inclusive)
  0x88 data offset ch0      0x89 data offset ch1         0xA0 codec (default PS-ADPCM)
  0x06 priority, 0x07/0x0A/0x0E/0x11/0x1D-0x23 playback parameters (volume, pan, envelope...)
Codecs (tag 0xA0)
  * absent  -> Sony PS-ADPCM ("VAG"): 16-byte frames, byte0 = predictor<<4 | shift,
               byte1 = flags, 14 data bytes = 28 samples, low nibble first.
  * 0x0A    -> EA-XA v2: 15-byte frames, byte0 = coef index<<4 | shift, 14 data bytes =
               28 samples, high nibble first. In SCDl blocks every channel starts with 4 bytes
               of initial history (LE int16 hist1, hist2), so blocks decode independently;
               bank sounds have no history prefix (start from 0).
               A 0xEE header would mark a raw 16-bit frame; none occur in this game's data.
  * 0x09    -> signed 8-bit PCM, preceded by a 16-byte header
               (u16 BE codec 9, u16 0, u32 LE loop start, u32 LE loop end, u32 LE samples).
  * 0x04    -> EA MicroTalk 10 (speech): decoded by tools/lib/microtalk.py.
"""
from __future__ import annotations

import struct
import wave
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

CODEC_VAG = -1
CODEC_MT10 = 0x04
CODEC_S8 = 0x09
CODEC_EAXA = 0x0A
CODEC_NAMES = {CODEC_VAG: "ps-adpcm", CODEC_MT10: "microtalk10", CODEC_S8: "pcm-s8", CODEC_EAXA: "ea-xa-v2"}

DEFAULT_RATE = 22050


def parse_pt(data: bytes, pos: int) -> tuple[dict[int, int], int]:
    """Parse a PT tag list at `pos` ("PT" + platform). Returns (tags, end position)."""
    if data[pos:pos + 2] != b"PT":
        raise ValueError(f"no PT header at 0x{pos:x}")
    p = pos + 4
    tags: dict[int, int] = {}
    while p < len(data):
        t = data[p]
        p += 1
        if t == 0xFF:
            break
        if t in (0xFC, 0xFD, 0xFE):
            continue
        n = data[p]
        p += 1
        tags[t] = int.from_bytes(data[p:p + n], "big")
        p += n
    return tags, p


@dataclass
class SoundInfo:
    codec: int
    channels: int
    rate: int
    samples: int
    loop_start: int | None = None
    loop_end: int | None = None
    tags: dict = field(default_factory=dict)

    @staticmethod
    def from_tags(tags: dict[int, int]) -> "SoundInfo":
        loop_s, loop_e = tags.get(0x86), tags.get(0x87)
        return SoundInfo(
            codec=tags.get(0xA0, CODEC_VAG),
            channels=tags.get(0x82, 1),
            rate=tags.get(0x84, DEFAULT_RATE),
            samples=tags.get(0x85, 0),
            loop_start=loop_s,
            loop_end=loop_e,
            tags=tags,
        )


EAXA_TABLE = np.array([0, 240, 460, 392, 0, 0, -208, -220, 0, 1, 3, 4, 7, 8, 10, 11, 0, -1, -3, -4],
                      dtype=np.int64)
VAG_COEFS = np.array([[0, 0], [60, 0], [115, -52], [98, -55], [122, -60],
                      [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0]],
                     dtype=np.int64)


def _pad_units(units: list[bytes], frame_size: int) -> tuple[np.ndarray, np.ndarray]:
    """Stack per-unit frame data into a (U, F, frame_size) uint8 array (zero padded)."""
    nframes = np.array([len(u) // frame_size for u in units], dtype=np.int64)
    F = int(nframes.max()) if len(units) else 0
    arr = np.zeros((len(units), F, frame_size), dtype=np.uint8)
    for i, u in enumerate(units):
        n = int(nframes[i])
        if n:
            arr[i, :n] = np.frombuffer(u[:n * frame_size], dtype=np.uint8).reshape(n, frame_size)
    return arr, nframes


def decode_eaxa_units(units: list[bytes], hist: np.ndarray | None = None) -> np.ndarray:
    """Decode EA-XA v2 frame runs, vectorised over independent units (blocks x channels)."""
    arr, _ = _pad_units(units, 15)
    U, F, _ = arr.shape
    out = np.zeros((U, F * 28), dtype=np.int16)
    if U == 0 or F == 0:
        return out
    if np.any(arr[:, :, 0] == 0xEE):
        raise NotImplementedError("EA-XA raw PCM (0xEE) frame encountered")
    h1 = np.zeros(U, dtype=np.int64) if hist is None else hist[:, 0].astype(np.int64)
    h2 = np.zeros(U, dtype=np.int64) if hist is None else hist[:, 1].astype(np.int64)
    for f in range(F):
        hdr = arr[:, f, 0].astype(np.int64)
        c1 = EAXA_TABLE[(hdr >> 4)]
        c2 = EAXA_TABLE[(hdr >> 4) + 4]
        shift = (hdr & 0x0F) + 8
        body = arr[:, f, 1:].astype(np.int64)
        nib = np.empty((U, 28), dtype=np.int64)
        nib[:, 0::2] = body >> 4
        nib[:, 1::2] = body & 0x0F
        s = ((nib << 28).astype(np.uint32).view(np.int32).astype(np.int64)) >> shift[:, None]
        base = f * 28
        for k in range(28):
            y = (s[:, k] + c1 * h1 + c2 * h2 + 128) >> 8
            np.clip(y, -32768, 32767, out=y)
            out[:, base + k] = y
            h2 = h1
            h1 = y
    return out


def decode_eaxa_scalar(data: bytes, pos: int, nsamples: int, hist=(0, 0)) -> np.ndarray:
    """Sequential EA-XA v2 decoder that also handles 0xEE raw-PCM frames (61 bytes:
    0xEE, BE hist1, BE hist2, 28 BE samples). Used only for the few units that contain them."""
    out = np.zeros(nsamples, dtype=np.int16)
    h1, h2 = int(hist[0]), int(hist[1])
    n = 0
    tbl = [int(v) for v in EAXA_TABLE]
    while n < nsamples:
        hdr = data[pos]
        if hdr == 0xEE:
            h1, h2 = struct.unpack_from(">hh", data, pos + 1)
            smp = struct.unpack_from(">28h", data, pos + 5)
            k = min(28, nsamples - n)
            out[n:n + k] = smp[:k]
            if k == 28:
                h1, h2 = smp[27], smp[26]
            n += k
            pos += 61
            continue
        c1, c2, shift = tbl[hdr >> 4], tbl[(hdr >> 4) + 4], (hdr & 0x0F) + 8
        for i in range(28):
            if n >= nsamples:
                break
            b = data[pos + 1 + i // 2]
            nib = (b >> 4) if (i & 1) == 0 else (b & 0x0F)
            s = (((nib << 28) & 0xFFFFFFFF) ^ 0x80000000) - 0x80000000
            y = ((s >> shift) + c1 * h1 + c2 * h2 + 128) >> 8
            y = -32768 if y < -32768 else (32767 if y > 32767 else y)
            out[n] = y
            h2, h1 = h1, y
            n += 1
        pos += 15
    return out


def has_aligned_ee(frames: bytes) -> bool:
    return any(frames[i] == 0xEE for i in range(0, len(frames), 15))


def decode_vag_units(units: list[bytes]) -> np.ndarray:
    """Decode PS-ADPCM frame runs, vectorised over units (each unit continuous from hist 0)."""
    arr, _ = _pad_units(units, 16)
    U, F, _ = arr.shape
    out = np.zeros((U, F * 28), dtype=np.int16)
    if U == 0 or F == 0:
        return out
    h1 = np.zeros(U, dtype=np.int64)
    h2 = np.zeros(U, dtype=np.int64)
    for f in range(F):
        hdr = arr[:, f, 0].astype(np.int64)
        pred = (hdr >> 4) & 0x0F
        shift = hdr & 0x0F
        c1 = VAG_COEFS[pred, 0]
        c2 = VAG_COEFS[pred, 1]
        body = arr[:, f, 2:].astype(np.int64)
        nib = np.empty((U, 28), dtype=np.int64)
        nib[:, 0::2] = body & 0x0F
        nib[:, 1::2] = body >> 4
        s = ((nib << 12).astype(np.uint16).view(np.int16).astype(np.int64)) >> shift[:, None]
        base = f * 28
        for k in range(28):
            y = s[:, k] + ((c1 * h1 + c2 * h2 + 32) >> 6)
            np.clip(y, -32768, 32767, out=y)
            out[:, base + k] = y
            h2 = h1
            h1 = y
    return out


@dataclass
class Stream:
    info: SoundInfo
    pcm: np.ndarray   # (samples, channels) int16
    offset: int   # file offset of the SCHl chunk
    size: int                  # bytes up to the end of SCEl
    blocks: int
    block_samples_total: int


def _chunks(data: bytes, pos: int):
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        if size < 8:
            return
        yield tag, pos, size
        pos += size
        if tag == b"SCEl":
            return


def find_schl(data: bytes, align: int = 0x80) -> list[int]:
    """Offsets of every SCHl stream (they start on `align` boundaries)."""
    out = []
    pos = 0
    while True:
        pos = data.find(b"SCHl", pos)
        if pos < 0:
            return out
        if pos % align == 0:
            out.append(pos)
        pos += 4


def read_streams(data: bytes, offsets: list[int], decode: bool = True) -> list[Stream]:
    """Parse (and decode) several SCHl streams; EA-XA blocks of all of them are decoded in one
    vectorised pass (every block restarts from its stored history, see verify_block_history)."""
    metas = []
    units: list[bytes] = []
    hist: list[tuple[int, int]] = []
    for off in offsets:
        tags, _ = parse_pt(data, off + 8)
        info = SoundInfo.from_tags(tags)
        blocks = []
        end = off
        for tag, pos, size in _chunks(data, off):
            end = pos + size
            if tag != b"SCDl":
                continue
            ns = struct.unpack_from("<I", data, pos + 8)[0]
            chofs = struct.unpack_from(f"<{info.channels}I", data, pos + 12)
            base = pos + 12 + 4 * info.channels
            blk = []
            for c in range(info.channels):
                start = base + chofs[c]
                stop = base + chofs[c + 1] if c + 1 < info.channels else pos + size
                h = struct.unpack_from("<hh", data, start)
                nfr = (ns + 27) // 28
                frames = data[start + 4:start + 4 + nfr * 15]
                if len(frames) < nfr * 15 or start + 4 + nfr * 15 > stop:
                    raise ValueError(f"SCDl at 0x{pos:x}: channel {c} overruns its block")
                blk.append(len(units))
                units.append(frames)
                hist.append(h)
            blocks.append((ns, blk))
        metas.append((off, end - off, info, blocks))
    if decode and units:
        codecs = {m[2].codec for m in metas}
        if codecs != {CODEC_EAXA}:
            raise NotImplementedError(f"SCHl decode only implemented for EA-XA, got {codecs}")
        dec = decode_eaxa_units(units, np.array(hist, dtype=np.int64))
    out = []
    for off, size, info, blocks in metas:
        total = sum(ns for ns, _ in blocks)
        pcm = np.zeros((0, info.channels), dtype=np.int16)
        if decode and blocks:
            pcm = np.empty((total, info.channels), dtype=np.int16)
            p = 0
            for ns, blk in blocks:
                for c, u in enumerate(blk):
                    pcm[p:p + ns, c] = dec[u, :ns]
                p += ns
        out.append(Stream(info, pcm, off, size, len(blocks), total))
    return out


def verify_block_history(data: bytes, offset: int, max_blocks: int = 64) -> dict:
    """Decode the first blocks of an EA-XA SCHl stream continuously (carrying state across
    blocks) and compare the carried state with the history stored at each block start."""
    tags, _ = parse_pt(data, offset + 8)
    info = SoundInfo.from_tags(tags)
    h = [[0, 0] for _ in range(info.channels)]
    checked = match = 0
    for tag, pos, size in _chunks(data, offset):
        if tag != b"SCDl":
            continue
        ns = struct.unpack_from("<I", data, pos + 8)[0]
        chofs = struct.unpack_from(f"<{info.channels}I", data, pos + 12)
        base = pos + 12 + 4 * info.channels
        for c in range(info.channels):
            start = base + chofs[c]
            stored = struct.unpack_from("<hh", data, start)
            if checked >= 1:
                match += int(abs(stored[0] - h[c][0]) <= 256 and abs(stored[1] - h[c][1]) <= 256)
            nfr = (ns + 27) // 28
            dec = decode_eaxa_units([data[start + 4:start + 4 + nfr * 15]],
                                    np.array([h[c]], dtype=np.int64))[0, :ns]
            h[c] = [int(dec[-1]), int(dec[-2])]
        checked += 1
        if checked >= max_blocks:
            break
    return {"blocks_checked": checked, "channel_blocks_matching_carried_state": match,
            "of": (checked - 1) * info.channels}


@dataclass
class BankSound:
    slot: int
    info: SoundInfo
    pcm: np.ndarray | None


def read_bank(data: bytes) -> list[BankSound]:
    if data[:4] != b"BNKl":
        raise ValueError("not a BNKl bank")
    _ver, n = struct.unpack_from("<HH", data, 4)
    entries = []
    for i in range(n):
        rel = struct.unpack_from("<I", data, 0x14 + 4 * i)[0]
        if not rel:
            continue
        tags, _ = parse_pt(data, 0x14 + 4 * i + rel)
        entries.append((i, SoundInfo.from_tags(tags)))

    vag_units, vag_idx, xa_units, xa_hist, xa_idx = [], [], [], [], []
    results: dict[tuple[int, int], np.ndarray] = {}
    for i, info in entries:
        offs = [info.tags.get(0x88), info.tags.get(0x89)][:info.channels]
        for c, o in enumerate(offs):
            if info.codec == CODEC_VAG:
                nfr = (info.samples + 27) // 28
                vag_units.append(data[o:o + nfr * 16])
                vag_idx.append((i, c))
            elif info.codec == CODEC_EAXA:
                nfr = (info.samples + 27) // 28
                h = (0, 0)
                frames = data[o:o + nfr * 15]
                if has_aligned_ee(frames):
                    results[(i, c)] = decode_eaxa_scalar(data, o, info.samples, h)
                    continue
                xa_hist.append(h)
                xa_units.append(frames)
                xa_idx.append((i, c))
            elif info.codec == CODEC_S8:
                hdr_codec = struct.unpack_from(">H", data, o)[0]
                _ls, _le, ns = struct.unpack_from("<III", data, o + 4)
                if hdr_codec != 9 or ns != info.samples:
                    raise ValueError(f"slot {i}: S8 sub-header mismatch")
                pcm = np.frombuffer(data, dtype=np.int8, count=ns, offset=o + 16).astype(np.int16) << 8
                results[(i, c)] = pcm
    if vag_units:
        dec = decode_vag_units(vag_units)
        for u, key in enumerate(vag_idx):
            results[key] = dec[u]
    if xa_units:
        dec = decode_eaxa_units(xa_units, np.array(xa_hist, dtype=np.int64))
        for u, key in enumerate(xa_idx):
            results[key] = dec[u]
    sounds = []
    for i, info in entries:
        chans = [results.get((i, c)) for c in range(info.channels)]
        if any(ch is None for ch in chans):
            sounds.append(BankSound(i, info, None))
            continue
        pcm = np.stack([ch[:info.samples] for ch in chans], axis=1)
        sounds.append(BankSound(i, info, pcm))
    return sounds


def write_wav(path: Path, pcm: np.ndarray, rate: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pcm = np.ascontiguousarray(pcm.astype("<i2"))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(pcm.shape[1] if pcm.ndim == 2 else 1)
        w.setsampwidth(2)
        w.setframerate(int(rate))
        w.writeframes(pcm.tobytes())


def pcm_stats(pcm: np.ndarray) -> dict:
    """Numerical sanity checks written into the manifest."""
    x = pcm.astype(np.int32)
    n = int(x.shape[0])
    st = {
        "samples": n,
        "peak": int(np.abs(x).max()) if n else 0,
        "rms_dbfs": round(float(20 * np.log10(max(np.sqrt(np.mean(x.astype(np.float64) ** 2)), 1e-9) / 32768)), 2) if n else None,
        "full_scale_samples": int(np.count_nonzero((x >= 32767) | (x <= -32768))),
    }
    st["full_scale_fraction"] = round(st["full_scale_samples"] / max(n * x.shape[1], 1), 7)
    if x.ndim == 2 and x.shape[1] == 2 and n > 1:
        a, b = x[:, 0].astype(np.float64), x[:, 1].astype(np.float64)
        sa, sb = a.std(), b.std()
        st["lr_correlation"] = round(float(np.mean((a - a.mean()) * (b - b.mean())) / (sa * sb)), 4) if sa > 0 and sb > 0 else None
    if n > 1:
        d = np.abs(np.diff(x[:, 0].astype(np.float64)))
        r = np.sqrt(np.mean(x[:, 0].astype(np.float64) ** 2))
        st["diff_to_rms"] = round(float(d.mean() / r), 3) if r > 0 else None
    return st
