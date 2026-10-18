"""Decode SSX Tricky's audio into WAV files plus a manifest (game data: output is git-ignored).
Usage:  python tools/convert_audio.py [--only music|jukebox|sfx|intro|speech|crowd] [--force]
"""
from __future__ import annotations

import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import json
import re
import shutil
import struct
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audio_ea as ea          # noqa: E402
import audio_inf               # noqa: E402
import audio_mpf               # noqa: E402
import eabig                   # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "extracted" / "DATA"
UNPACKED = ROOT / "unpacked" / "AUDIO"
OUT = ROOT / "converted" / "audio"

CONFIGS = ["AUDIO.INF", "BANKS.INF", "MUSIC.INF", "MUSICMAP.INF", "JUKEBOX.INF", "INTROMUS.INF",
           "SNOW.INF", "CROWD.INF", "CHANT.INF", "SPEECH.INF", "NASCRIPT.INF"]
INTRO_BIGS = ["ALASKA", "ALOHA", "ELYSIUM", "GARI", "MEGAPLEX", "MERQURY", "MESA", "PIPEDRM",
              "SNOWDRM", "UNTRACK"]
MENU_SONGS = {"ssxmenu", "femenuquad"}


def ensure_unpacked(name: str) -> Path:
    dest = UNPACKED / name
    if not dest.exists():
        n = eabig.unpack(DATA / "AUDIO" / f"{name}.BIG", dest)
        print(f"  unpacked {name}.BIG ({n} files)")
    return dest


class Manifest:
    def __init__(self):
        self.entries: list[dict] = []
        self.skipped: list[dict] = []
        self.pathfinder: dict[str, dict] = {}

    def add(self, *, id, category, source, path: Path, info: ea.SoundInfo, pcm: np.ndarray,
            header_samples: int, imported: bool, loop=None, exported_range=None, extra=None):
        e = {
            "id": id,
            "name": path.stem,
            "category": category,
            "source": source,
            "file": path.relative_to(OUT).as_posix(),
            "codec": ea.CODEC_NAMES.get(info.codec, hex(info.codec)),
            "rate": info.rate,
            "channels": int(pcm.shape[1]),
            "samples": int(pcm.shape[0]),
            "duration": round(pcm.shape[0] / info.rate, 4),
            "header_samples": header_samples,
            "import": imported,
            "stats": ea.pcm_stats(pcm),
        }
        if loop is not None:
            e["loop_start"], e["loop_end"] = loop
            e["loop_in_range"] = bool(0 <= loop[0] <= loop[1] < header_samples)
        if exported_range is not None:
            e["exported_range"] = exported_range
        if extra:
            e.update(extra)
        self.entries.append(e)
        return e

    def write(self):
        OUT.mkdir(parents=True, exist_ok=True)
        cats: dict[str, dict] = {}
        for e in self.entries:
            c = cats.setdefault(e["category"], {"count": 0, "seconds": 0.0, "imported": 0})
            c["count"] += 1
            c["seconds"] = round(c["seconds"] + e["duration"], 2)
            c["imported"] += int(e["import"])
        checks = {
            "entries": len(self.entries),
            "sample_count_mismatches": [e["id"] for e in self.entries
                                        if e["header_samples"] != e.get("decoded_samples", e["samples"])],
            "loop_points_out_of_range": [e["id"] for e in self.entries if e.get("loop_in_range") is False],
            "clipping_storms": [e["id"] for e in self.entries if e["stats"]["full_scale_fraction"] > 1e-2],
            "entries_with_any_full_scale": sum(1 for e in self.entries if e["stats"]["full_scale_samples"]),
            "max_full_scale_fraction": max((e["stats"]["full_scale_fraction"] for e in self.entries), default=0),
            "total_full_scale_samples": sum(e["stats"]["full_scale_samples"] for e in self.entries),
        }
        doc = {"generated": time.strftime("%Y-%m-%d %H:%M:%S"), "root": "converted/audio",
               "summary": cats, "checks": checks, "pathfinder": self.pathfinder,
               "entries": self.entries, "skipped": self.skipped}
        (OUT / "manifest.json").write_text(json.dumps(doc, indent=1))
        return checks, cats


def bank_sounds_to_wavs(m: Manifest, bank_path: Path, category: str, subdir: str, source: str,
                        imported: bool = True):
    data = bank_path.read_bytes()
    stem = bank_path.stem.replace(" ", "")
    for snd in ea.read_bank(data):
        sid = f"{category}/{stem}/{snd.slot:02d}"
        if snd.pcm is None:
            m.skipped.append({"id": sid, "reason": f"codec {snd.info.codec} not decoded"})
            continue
        info = snd.info
        pcm = snd.pcm
        loop = None
        rng = None
        if info.loop_start is not None and info.loop_end is not None:
            loop = (int(info.loop_start), min(int(info.loop_end), info.samples - 1))   # inclusive loop end (EA-XA bank sounds store the sample count)
            if loop[0] <= loop[1]:
                rng = [loop[0], loop[1] + 1]
                pcm = pcm[loop[0]:loop[1] + 1]
        path = OUT / subdir / stem / f"{stem}_{snd.slot:02d}.wav"
        ea.write_wav(path, pcm, info.rate)
        m.add(id=sid, category=category, source=f"{source}:{bank_path.name}#{snd.slot}", path=path,
              info=info, pcm=pcm, header_samples=info.samples, imported=imported, loop=loop,
              exported_range=rng, extra={"decoded_samples": int(snd.pcm.shape[0]), "slot": snd.slot,
                                         "priority": info.tags.get(0x06)})


def do_jukebox(m: Manifest):
    src = ensure_unpacked("JUKEBOX") / "data" / "audio"
    for f in sorted(src.iterdir()):
        data = f.read_bytes()
        st = ea.read_streams(data, [0])[0]
        path = OUT / "music" / "jukebox" / f"{f.name}.wav"
        ea.write_wav(path, st.pcm, st.info.rate)
        e = m.add(id=f"music/jukebox/{f.name}", category="music", source=f"JUKEBOX.BIG:{f.name}", path=path,
                  info=st.info, pcm=st.pcm, header_samples=st.info.samples, imported=True,
                  extra={"decoded_samples": st.block_samples_total, "blocks": st.blocks, "kind": "jukebox"})
        print(f"  jukebox {f.name:16s} {e['duration']:7.1f}s {st.info.rate} Hz")


def do_pathfinder(m: Manifest):
    src = ensure_unpacked("MUSIC") / "data" / "audio"
    for mpf_path in sorted(src.glob("*.mpf")):
        song = mpf_path.stem
        graph = audio_mpf.parse_mpf(mpf_path.read_bytes())
        mus = (src / f"{song}.mus").read_bytes()
        offs = [s["mus_offset"] for s in graph["segments"]]
        found = ea.find_schl(mus)
        if offs != found:
            raise ValueError(f"{song}: segment table does not match SCHl positions")
        streams = ea.read_streams(mus, offs)
        rates = {s.info.rate for s in streams}
        chans = {s.info.channels for s in streams}
        if len(rates) != 1 or len(chans) != 1:
            raise ValueError(f"{song}: mixed rates/channels {rates} {chans}")
        rate = rates.pop()
        pos = 0
        worst_ms = 0.0
        hdr_mismatch = 0
        for seg, st in zip(graph["segments"], streams):
            seg["start_sample"] = pos
            seg["samples"] = int(st.pcm.shape[0])
            ms = st.pcm.shape[0] * 1000.0 / rate
            seg["decoded_ms"] = round(ms, 2)
            worst_ms = max(worst_ms, abs(ms - seg["duration_ms"]))
            hdr_mismatch += int(st.info.samples != st.block_samples_total)
            pos += st.pcm.shape[0]
        pcm = np.concatenate([s.pcm for s in streams], axis=0)
        path = OUT / "music" / "pathfinder" / song / f"{song}_segments.wav"
        ea.write_wav(path, pcm, rate)
        info = ea.SoundInfo(ea.CODEC_EAXA, pcm.shape[1], rate, pcm.shape[0])
        m.add(id=f"music/pathfinder/{song}/segments", category="music", source=f"MUSIC.BIG:{song}.mus",
              path=path, info=info, pcm=pcm, header_samples=sum(s.info.samples for s in streams),
              imported=False, extra={"decoded_samples": sum(s.block_samples_total for s in streams),
                                     "kind": "pathfinder_segments", "segments": len(streams),
                                     "worst_segment_duration_error_ms": round(worst_ms, 2),
                                     "segment_header_mismatches": hdr_mismatch})
        graph.update({"song": song, "rate": rate, "channels": pcm.shape[1],
                      "segments_wav": path.relative_to(OUT).as_posix()})
        jpath = OUT / "music" / "pathfinder" / song / f"{song}.json"
        jpath.write_text(json.dumps(graph))
        m.pathfinder[song] = {"json": jpath.relative_to(OUT).as_posix(), "segments": len(streams),
                              "nodes": len(graph["nodes"]), "events": len(graph["events"]),
                              "rate": rate, "worst_segment_duration_error_ms": round(worst_ms, 2)}
        print(f"  pathfinder {song:12s} {len(streams):4d} segs {len(graph['nodes']):4d} nodes "
              f"{pcm.shape[0] / rate:7.1f}s  max seg dur err {worst_ms:.2f} ms")
        if song in MENU_SONGS:
            nodes, how = audio_mpf.default_path(graph, graph["events"][0]["node"])
            parts, loop_at = [], None
            loop_node = int(how.split(":")[1]) if how.startswith("loop:") else None
            acc = 0
            for n in nodes:
                if n == loop_node:
                    loop_at = acc
                s = graph["nodes"][n]["segment"]
                if s >= 0:
                    sg = graph["segments"][s]
                    parts.append(pcm[sg["start_sample"]:sg["start_sample"] + sg["samples"]])
                    acc += sg["samples"]
            body = np.concatenate(parts[0:], axis=0)
            if loop_at:   # export only the looping part so the SoundWave loops seamlessly
                body = body[loop_at:]
            lpath = OUT / "music" / "pathfinder" / song / f"{song}_loop.wav"
            ea.write_wav(lpath, body, rate)
            linfo = ea.SoundInfo(ea.CODEC_EAXA, body.shape[1], rate, body.shape[0])
            m.add(id=f"music/menu/{song}", category="music", source=f"MUSIC.BIG:{song}.mpf event 0",
                  path=lpath, info=linfo, pcm=body, header_samples=body.shape[0], imported=True,
                  loop=(0, body.shape[0] - 1), extra={"kind": "menu_render"})
    for bnk in sorted(src.glob("*.bnk")):
        bank_sounds_to_wavs(m, bnk, "music", "music/loops", "MUSIC.BIG")


def do_intro(m: Manifest):
    for big in INTRO_BIGS:
        src = ensure_unpacked(big)
        for f in sorted(src.iterdir()):
            data = f.read_bytes()
            st = ea.read_streams(data, [0])[0]
            path = OUT / "music" / "intro" / big / f"{f.name}.wav"
            ea.write_wav(path, st.pcm, st.info.rate)
            m.add(id=f"music/intro/{big}/{f.name}", category="music", source=f"{big}.BIG:{f.name}", path=path,
                  info=st.info, pcm=st.pcm, header_samples=st.info.samples, imported=True,
                  extra={"decoded_samples": st.block_samples_total, "kind": "intro"})
        print(f"  intro {big}")


def do_sfx(m: Manifest):
    src = ensure_unpacked("AUDIO") / "data" / "audio"
    banks_inf = audio_inf.parse_inf((DATA / "CONFIG" / "BANKS.INF").read_text())
    swap = {v.lower().replace(" ", "") for _s, kv in banks_inf for k, v in kv if k.upper() == "SWAP" and v}
    for bnk in sorted(src.glob("*.bnk")):
        name = bnk.name.lower().replace(" ", "")
        if name in swap:
            bank_sounds_to_wavs(m, bnk, "ambience", "ambience", "AUDIO.BIG")
        elif re.match(r"c_", name):
            bank_sounds_to_wavs(m, bnk, "speech", "speech/chant", "AUDIO.BIG")
        else:
            bank_sounds_to_wavs(m, bnk, "sfx", "sfx", "AUDIO.BIG")
    for eam in sorted(src.glob("*.eam")):
        m.skipped.append({"id": f"sfx/crowd_midi/{eam.stem}", "reason": "MIDx crowd sequence (CROWD.INF): copied by --only crowd"})
    print(f"  sfx/ambience banks: {len(list(src.glob('*.bnk')))}")


# 0x2D5410 / 0x2D59D8
# 0x01 / 0x02 velocity range, 0x03 / 0x04 key range, 0x06 priority, 0x07 root key, 0x0A bend range (semitones), 0x0C pan,
# 0x0D pan random, 0x0E volume, 0x0F volume random, 0x10 fine tune (semitones), 0x11 pitch random (cents)
PATCH_TAGS = (0x01, 0x02, 0x03, 0x04, 0x06, 0x07, 0x0A, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11)


def do_crowd(m: Manifest):
    """CROWD.INF's MIDx sequences (copied verbatim) plus the Crowd.bnk patch tags for the port's sequencer."""
    src = ensure_unpacked("AUDIO") / "data" / "audio"
    dst = OUT / "crowd"
    dst.mkdir(parents=True, exist_ok=True)
    n = 0
    for eam in sorted(src.glob("*.eam")):
        shutil.copy2(eam, dst / eam.name)
        n += 1
    data = (src / "Crowd.bnk").read_bytes()
    count = struct.unpack_from("<H", data, 6)[0]
    patches = {}
    for i in range(count):
        rel = struct.unpack_from("<I", data, 0x14 + 4 * i)[0]
        if not rel:
            continue
        p = 0x14 + 4 * i + rel
        if b"\xfe" in data[p + 4:data.index(b"\xff", p + 4)]:
            raise ValueError(f"Crowd.bnk #{i}: more than one key zone")
        tags, _ = ea.parse_pt(data, p)
        patches[str(i)] = {f"{t:02X}": tags[t] for t in PATCH_TAGS if t in tags}
    (dst / "crowd_patches.json").write_text(json.dumps({"bank": "Crowd", "patches": patches}, indent=1))
    print(f"  crowd: {n} .eam sequences, {len(patches)} Crowd.bnk patches -> {dst}")


def do_speech(m: Manifest):
    src = ensure_unpacked("SPEECH") / "data" / "speech"
    for bnk in sorted(src.rglob("*.bnk")):
        bank_sounds_to_wavs(m, bnk, "speech", "speech/feselect", "SPEECH.BIG")
    n = 0
    for dat in sorted(src.rglob("*.dat")):
        with open(dat, "rb") as fh:
            head = fh.read(1 << 16)
        pos = head.find(b"SCHl")
        codec = None
        if pos >= 0:
            tags, _ = ea.parse_pt(head, pos + 8)
            codec = tags.get(0xA0)
        m.skipped.append({"id": f"speech/{dat.relative_to(src).as_posix()}",
                          "reason": "EA MicroTalk 10 speech (codec 4): per-sample streams by tools/convert_speech.py" if codec == 4
                          else f"no SCHl stream / codec {codec}"})
        n += 1
    print(f"  speech: {n} MicroTalk .dat banks left to tools/convert_speech.py, feselect banks decoded")


def copy_configs():
    dst = OUT / "config"
    dst.mkdir(parents=True, exist_ok=True)
    for c in CONFIGS:
        shutil.copy2(DATA / "CONFIG" / c, dst / c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", action="append", choices=["jukebox", "music", "intro", "sfx", "speech", "crowd"])
    args = ap.parse_args()
    jobs = args.only or ["jukebox", "music", "intro", "sfx", "speech", "crowd"]
    m = Manifest()
    old = OUT / "manifest.json"
    if args.only and old.exists():
        prev = json.loads(old.read_text())
        kinds = {"jukebox": lambda e: e.get("kind") == "jukebox",
                 "music": lambda e: e["category"] == "music" and e.get("kind") not in ("jukebox", "intro"),
                 "intro": lambda e: e.get("kind") == "intro",
                 "sfx": lambda e: e["category"] in ("sfx", "ambience") or e["file"].startswith("speech/chant"),
                 "speech": lambda e: e["file"].startswith("speech/feselect"),
                 "crowd": lambda e: False}
        m.entries = [e for e in prev["entries"] if not any(kinds[j](e) for j in jobs)]
        m.pathfinder = {} if "music" in jobs else prev.get("pathfinder", {})
        m.skipped = [s for s in prev.get("skipped", []) if not (("speech" in jobs and s["id"].startswith("speech/"))
                                                                  or ("sfx" in jobs and s["id"].startswith(("sfx/", "ambience/"))))]
    t0 = time.time()
    copy_configs()
    for j in jobs:
        print(f"[{j}]")
        {"jukebox": do_jukebox, "music": do_pathfinder, "intro": do_intro, "sfx": do_sfx, "speech": do_speech,
         "crowd": do_crowd}[j](m)
    checks, cats = m.write()
    print(json.dumps({"summary": cats, "checks": {k: (v if not isinstance(v, list) else v[:10]) for k, v in checks.items()}}, indent=1))
    print(f"done in {time.time() - t0:.0f}s -> {OUT / 'manifest.json'}")


if __name__ == "__main__":
    main()
