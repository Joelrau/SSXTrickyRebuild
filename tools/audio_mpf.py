"""EA Pathfinder interactive-music maps (.mpf "PFDx", version 3.1) paired with .mus segment files.
Layout recovered from the SSX Tricky files (all integers little-endian):
  0x00  "xDFP" (PFDx), u8 3, u8 1 (version 3.1), 0xB0 ...
  0x0E  u8 cols        columns of the event table (all columns of a row hold the same value
                       in this game's files)
  0x0F  u8 events      number of game events (11 for race songs, 6 for menu songs)
  0x10  u8 actions     number of action records
  0x11  u8 controls    number of control (router variable) lists
  0x12  u16 nodes      node count
  0x24  u16[nodes]     node offsets, in 4-byte units from the file start
  node: u16 segment    1-based index into the segment table (0 = section-start marker without
                       audio, 0xFFFF = control/silence node)
        u8  0
        u8  section    low 7 bits = section number (0x7F = control nodes); bit 7 = section start
        u8  1, u8 beats-per-bar?, u8 4 (4/4), u8 0
        u8  0, u8 loop (0xFF on some control nodes: the engine's loop counter start, FUN_002BF108 /
                       FUN_002BF038 - the counting node routes by the counter & 0x7F, counting down),
        u8  marker     (non-zero on a few nodes, ids 1..5): 1-based control list whose (from, to) pairs remap
                       the node's successor (FUN_002BF290; the long endings loop back into the song body)
        u8  branch count
        branch[count]: u8 lo, u8 hi, u16 next node  -- take this branch when the control value
                       (0..127) lies in [lo, hi]. One branch = unconditional successor; 0 (or no
                       range holding the value) = end.
  After the last node: u8 table[events][cols] (event -> action index), padded to 4 bytes;
  action[actions]: u8 0xFF, u8 flags, u16 node   (flags & 3 != 0 (1 or 2) = go to node; flags & 3 == 0
                       = no action, the song plays on (FUN_002BF490 / FUN_002BF508); bit 7 =
                       immediately instead of at the next segment boundary; node 0xFFFF = none);
  then (controls + 1) u32 pointers (4-byte units): pointer i starts control sub-list i (u32 entries
  from << 16 | to, i.e. u16 pairs (to, from): the remap list of marker i + 1); the last pointer points
  at a u32 that points at the segment table: u32 offset/4 into the .mus (start of that segment's SCHl stream) and u32
  duration in ms, one per SCHl stream in the .mus.
"""
from __future__ import annotations

import struct
from pathlib import Path


def parse_mpf(data: bytes) -> dict:
    if data[:4] != b"xDFP":
        raise ValueError("not a PFDx .mpf")
    cols, nev, nact, nctl = data[0x0E], data[0x0F], data[0x10], data[0x11]
    nn = struct.unpack_from("<H", data, 0x12)[0]
    offs = [struct.unpack_from("<H", data, 0x24 + 2 * i)[0] * 4 for i in range(nn)]
    nodes = []
    for o in offs:
        seg = struct.unpack_from("<H", data, o)[0]
        nb = data[o + 11]
        branches = [[data[o + 12 + 4 * i], data[o + 13 + 4 * i], struct.unpack_from("<H", data, o + 14 + 4 * i)[0]]
                    for i in range(nb)]
        nodes.append({
            "segment": -1 if seg == 0xFFFF else seg - 1,   # 0-based SCHl index, -1 = none
            "section": data[o + 3] & 0x7F,
            "section_start": bool(data[o + 3] & 0x80),
            "beats": data[o + 4],
            "loop": data[o + 9],
            "marker": data[o + 10],
            "branches": branches,
        })
    o = offs[-1]
    end = o + 12 + 4 * data[o + 11]
    table = data[end:end + cols * nev]
    events = [table[i * cols] for i in range(nev)]
    rows_uniform = all(len(set(table[i * cols:(i + 1) * cols])) == 1 for i in range(nev))
    p = (end + cols * nev + 3) & ~3
    actions = []
    for i in range(nact):
        _ff, flags, node = struct.unpack_from("<BBH", data, p + 4 * i)
        actions.append({"flags": flags, "immediate": bool(flags & 0x80), "op": flags & 0x7F,
                        "node": -1 if node == 0xFFFF else node})
    p += 4 * nact
    ptrs = [struct.unpack_from("<I", data, p + 4 * i)[0] * 4 for i in range(nctl + 1)]
    control_lists = [list(struct.unpack_from(f"<{(ptrs[i + 1] - ptrs[i]) // 2}H", data, ptrs[i]))
                     for i in range(nctl)]
    p = struct.unpack_from("<I", data, ptrs[nctl])[0] * 4
    segs = [list(struct.unpack_from("<II", data, p + 8 * i)) for i in range((len(data) - p) // 8)]
    return {
        "version": [data[4], data[5]],
        "columns": cols,
        "event_rows_uniform": rows_uniform,
        "controls": nctl,
        "control_lists": control_lists,
        "nodes": nodes,
        "events": [{"event": e, "action": a, **actions[a]} for e, a in enumerate(events)],
        "actions": actions,
        "segments": [{"mus_offset": s[0] * 4, "duration_ms": s[1]} for s in segs],
    }


def default_path(mpf: dict, start: int, control: int = 64, limit: int = 100000) -> tuple[list[int], str]:
    """Follow the graph from `start` with a fixed control value; returns (nodes, 'end'|'loop:N')."""
    seen: list[int] = []
    idx: dict[int, int] = {}
    n = start
    while n not in idx and len(seen) < limit:
        idx[n] = len(seen)
        seen.append(n)
        br = mpf["nodes"][n]["branches"]
        if not br:
            return seen, "end"
        nxt = br[-1][2]
        for lo, hi, node in br:
            if lo <= control <= hi:
                nxt = node
                break
        n = nxt
    return seen, f"loop:{n}"


if __name__ == "__main__":
    import json
    import sys
    m = parse_mpf(Path(sys.argv[1]).read_bytes())
    print(json.dumps({k: v for k, v in m.items() if k != "nodes"}, indent=1)[:4000])
