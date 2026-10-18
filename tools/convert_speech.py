"""Speech sample converter: every MicroTalk stream of SPEECH.BIG's speech .dat files to WAV (tools/lib/microtalk.py).
Usage:  python tools/convert_speech.py [--section mc ...] [--jobs N] [--force] [--vgmstream PATH]
--vgmstream PATH also decodes every stream with that vgmstream-cli and reports streams whose PCM or format differ.
"""
from __future__ import annotations

import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import time
import wave
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audio_ea as ea          # noqa: E402
import eabig                   # noqa: E402
import microtalk               # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "extracted" / "DATA"
SPEECH_SRC = ROOT / "unpacked" / "AUDIO" / "SPEECH" / "data" / "speech"
OUT = ROOT / "converted" / "audio" / "speech"
SECTIONS = ["fe", "mc", "char", "narr", "anim"]
BLOCK = 256   # block = 256 * (shift + 1), shift 0 in every group
FRAME, FRAMES_PER_BLOCK = 432, 6


def walk_streams(data: bytes):
    """Yield (offset, end) of every SCHl..SCEl stream in a .dat."""
    p = 0
    n = len(data)
    while p + 8 <= n:
        if data[p:p + 4] != b"SCHl":
            p = (p // BLOCK + 1) * BLOCK
            continue
        q = p
        while q + 8 <= n:
            tag = data[q:q + 4]
            size = struct.unpack_from("<I", data, q + 4)[0]
            if size < 8:
                raise ValueError(f"bad block size at 0x{q:x}")
            q += size
            if tag == b"SCEl":
                break
        yield p, q
        p = (q + BLOCK - 1) // BLOCK * BLOCK


def check_stream(data: bytes, start: int, end: int) -> dict:
    """Header tags and SCDl block sample counts."""
    tags, _ = ea.parse_pt(data, start + 8)
    plat = struct.unpack_from("<H", data, start + 10)[0]
    info = {"platform": plat, "version": tags.get(0x80), "codec2": tags.get(0xA0), "rate": tags.get(0x84),
            "samples": tags.get(0x85), "channels": tags.get(0x82, 1)}
    p = start + struct.unpack_from("<I", data, start + 4)[0]
    blocks = []
    while p < end:
        tag = data[p:p + 4]
        size = struct.unpack_from("<I", data, p + 4)[0]
        if tag == b"SCDl":
            blocks.append(struct.unpack_from("<I", data, p + 8)[0])
        p += size
    info["blocks"] = len(blocks)
    full = FRAME * FRAMES_PER_BLOCK
    info["block_ok"] = bool(blocks) and all(b % FRAME == 0 and 0 < b <= full for b in blocks[:-1]) \
        and 0 < blocks[-1] <= full and sum(blocks) == info["samples"]
    return info


def read_wav(path: Path):
    with wave.open(str(path), "rb") as w:
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        pcm = np.frombuffer(w.readframes(n), dtype="<i2").reshape(-1, ch)
    return pcm, rate


def analyse(pcm: np.ndarray, rate: int) -> dict:
    """Peak, longest run of full-scale samples, and the fraction of energy in 100..4000 Hz (speech band)."""
    x = pcm.astype(np.float32)
    peak = int(np.abs(x).max()) if x.size else 0
    clip = np.abs(x) >= 32767
    run = 0
    if clip.any():
        c = clip.any(axis=1).astype(np.int8)
        d = np.diff(np.concatenate(([0], c, [0])))
        run = int((np.where(d == -1)[0] - np.where(d == 1)[0]).max())
    mono = x.mean(axis=1)
    band = 1.0
    if mono.size >= 1024 and np.any(mono):
        spec = np.abs(np.fft.rfft(mono * np.hanning(mono.size))) ** 2
        f = np.fft.rfftfreq(mono.size, 1.0 / rate)
        band = float(spec[(f >= 100) & (f <= 4000)].sum() / max(spec.sum(), 1e-9))
    dx = np.abs(np.diff(mono))
    edge = 1.0
    if dx.size > 2 * FRAME and dx.mean() > 0:
        edge = float(dx[FRAME - 1::FRAME].mean() / dx.mean())
    return {"peak": peak, "clip_run": run, "speech_band": round(band, 4), "frame_edge": round(edge, 3)}


def write_wav(path: Path, pcm: np.ndarray, rate: int):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(pcm.shape[1])
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.astype("<i2").tobytes())


def vgmstream_differs(cli: str, stream: bytes, pcm: np.ndarray, rate: int, tmp: Path) -> bool:
    """Cross-check: decode the stream with vgmstream-cli and compare format and samples."""
    asf, wav = tmp.with_suffix(".check.asf"), tmp.with_suffix(".check.wav")
    asf.write_bytes(stream)
    try:
        r = subprocess.run([cli, "-o", str(wav), str(asf)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode != 0 or not wav.is_file():
            return True
        ref, ref_rate = read_wav(wav)
        return ref_rate != rate or ref.shape != pcm.shape or bool((ref != pcm).any())
    finally:
        asf.unlink(missing_ok=True)
        wav.unlink(missing_ok=True)


def decode_batch(jobs: list[tuple[bytes, int, str, int]], cli: str | None) -> list[tuple[dict, bool | None]]:
    """Worker: decode (stream, channels, wav path, rate) jobs in one lockstep batch and write the WAVs; returns per
    job the analysis fields and the vgmstream cross-check result (None without --vgmstream)."""
    pcms = microtalk.decode_streams([microtalk.parse_stream(stream, 0, len(stream), ch) for stream, ch, _, _ in jobs])
    out = []
    for (stream, _, wav, rate), pcm in zip(jobs, pcms):
        write_wav(Path(wav), pcm, rate)
        rec = {"decoded": int(pcm.shape[0])}
        rec.update(analyse(pcm, rate))
        out.append((rec, vgmstream_differs(cli, stream, pcm, rate, Path(wav)) if cli else None))
    return out


def write_tables():
    """speech_tables.json: the cFE3DSound entry-2 request table (.data 0x343710, 81 ids x {category mask, item
    mask}) from the user's extracted/SLUS_203.26 (game data: git-ignored output)."""
    exe = (ROOT / "extracted" / "SLUS_203.26").read_bytes()
    base = 0x343710 - 0x100000 + 0x1000
    rows = [list(struct.unpack_from("<II", exe, base + 8 * i)) for i in range(0x51)]
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "speech_tables.json").write_text(json.dumps({"source": "SLUS_203.26 .data 0x343710", "fe_requests": rows}))
    print(f"  speech_tables.json: {len(rows)} FE request ids")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--section", action="append", choices=SECTIONS)
    ap.add_argument("--vgmstream", help="vgmstream-cli to cross-check every decoded stream against")
    ap.add_argument("--jobs", type=int, default=max(2, (os.cpu_count() or 4) - 1))
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--keep-streams", action="store_true")
    ap.add_argument("--limit", type=int, default=0, help="streams per .dat (testing)")
    args = ap.parse_args()
    if not SPEECH_SRC.exists():
        eabig.unpack(DATA / "AUDIO" / "SPEECH.BIG", ROOT / "unpacked" / "AUDIO" / "SPEECH")
    write_tables()
    index_path = OUT / "speech_index.json"
    index = json.loads(index_path.read_text()) if index_path.exists() else {}
    stats = {"streams": 0, "mono": 0, "stereo": 0, "header_bad": 0, "block_bad": 0, "decoded": 0,
             "length_bad": 0, "channels_bad": 0, "clip_runs": 0, "low_band": 0}
    if args.vgmstream:
        stats["vgmstream_differs"] = 0
    jobs = []        # (stream bytes, wav path, rate, rec) to decode
    existing = []    # (wav path, rec) converted before
    t0 = time.time()
    for section in args.section or SECTIONS:
        (OUT / section).mkdir(parents=True, exist_ok=True)
        shutil.copy2(SPEECH_SRC / section / "headers.big", OUT / section / "headers.big")
        for dat in sorted((SPEECH_SRC / section).rglob("*.dat")):
            data = dat.read_bytes()
            key = f"{section}/{dat.stem}"
            entry = index.setdefault(key, {})
            odir = OUT / section / dat.stem
            odir.mkdir(parents=True, exist_ok=True)
            for k, (s, e) in enumerate(walk_streams(data)):
                if args.limit and k >= args.limit:
                    break
                info = check_stream(data, s, e)
                stats["streams"] += 1
                stats["stereo" if info["channels"] == 2 else "mono"] += 1
                if (info["platform"], info["version"], info["codec2"], info["rate"]) != (5, 2, 4, 36000):
                    stats["header_bad"] += 1
                if not info["block_ok"]:
                    stats["block_bad"] += 1
                name = f"{s:08x}"
                wav = odir / f"{name}.wav"
                rec = {"samples": info["samples"], "channels": info["channels"], "bytes": e - s, "blocks": info["blocks"]}
                entry[name] = rec
                if args.keep_streams:
                    (odir / f"{name}.asf").write_bytes(data[s:e])
                if args.force or not wav.is_file():
                    jobs.append((data[s:e], wav, info["rate"], rec))
                else:
                    existing.append((wav, rec))
            print(f"  {key}: {len(entry)} streams")

    results = []     # (rec, pcm samples, rate, pcm channels)
    for wav, rec in existing:
        pcm, rate = read_wav(wav)
        rec["decoded"] = int(pcm.shape[0])
        rec.update(analyse(pcm, rate))
        results.append((rec, pcm.shape[0], rate, pcm.shape[1]))
    groups = microtalk.batches([(rec["samples"], rec["channels"]) for _, _, _, rec in jobs])
    print(f"decoding {len(jobs)} streams in {len(groups)} batches ({args.jobs} jobs)")
    with ProcessPoolExecutor(args.jobs) as pool:
        futures = []
        for g in groups:
            batch = [(jobs[k][0], jobs[k][3]["channels"], str(jobs[k][1]), jobs[k][2]) for k in g]
            futures.append((g, pool.submit(decode_batch, batch, args.vgmstream)))
        for n, (g, fut) in enumerate(futures):
            for k, (fields, differs) in zip(g, fut.result()):
                rec = jobs[k][3]
                rec.update(fields)
                results.append((rec, rec["decoded"], jobs[k][2], rec["channels"]))
                if differs:
                    stats["vgmstream_differs"] += 1
                    print(f"  vgmstream differs: {jobs[k][1]}")
            if n % 10 == 9 or n == len(futures) - 1:
                print(f"  {n + 1}/{len(futures)} batches ({time.time() - t0:.0f}s)")
    edges = []
    for rec, n, rate, channels in results:
        if n != rec["samples"] or rate != 36000:
            stats["length_bad"] += 1
        elif channels != rec["channels"]:
            stats["channels_bad"] += 1
        else:
            stats["decoded"] += 1
            if rec.get("clip_run", 0) > 2:
                stats["clip_runs"] += 1
            if rec.get("speech_band", 1) < 0.5:
                stats["low_band"] += 1
            edges.append(rec.get("frame_edge", 1.0))
    index_path.parent.mkdir(parents=True, exist_ok=True)
    index_path.write_text(json.dumps(index, separators=(",", ":")))
    if edges:
        e = np.array(edges)
        stats["frame_edge_mean"] = round(float(e.mean()), 3)
        stats["frame_edge_p99"] = round(float(np.percentile(e, 99)), 3)
    print(json.dumps(stats, indent=1))
    print(f"done in {time.time() - t0:.0f}s -> {index_path}")
    bad = stats["header_bad"] + stats["block_bad"] + stats["length_bad"] + stats["channels_bad"] \
        + stats.get("vgmstream_differs", 0)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
