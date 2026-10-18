"""SSX Tricky audio .INF configs: generic INI-style parser plus the SNOW.INF board-sound language."""
from __future__ import annotations

import math
import re
from pathlib import Path

SURFACES = ["PACK", "ICE", "CHUTE", "ROCK", "METAL", "POWDER", "LOOSE", "WOOD", "RAIL", "GLASS"]
MODES = ["GLIDE", "AIGLIDE", "CARVE"]
INPUTS = ["SLIP", "DIG", "LEAN", "BOARD", "BEND"]
OPS_ARG = {"MAP", "ADD", "SUB", "MUL", "DIV"}
OPS_NOARG = {"SQUARE", "SQRT", "PUSH", "POP", "SADD", "SSUB", "SMUL", "SDIV", "ASSIGNVOL", "ASSIGNBEND"}
ALIASES = {"ADDS": "SADD"}


def parse_inf(text: str) -> list[tuple[str, list[tuple[str, str | None]]]]:
    """Return [(section, [(key, value-or-None), ...]), ...] in file order (duplicates kept)."""
    out: list[tuple[str, list]] = []
    cur: list | None = None
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        m = re.fullmatch(r"\[(.+?)\]", line)
        if m:
            cur = []
            out.append((m.group(1).strip(), cur))
            continue
        if cur is None:
            continue
        if "=" in line:
            k, v = line.split("=", 1)
            cur.append((k.strip(), v.strip().strip('"')))
        else:
            cur.append((line, None))
    return out


def compile_snow(text: str) -> dict[str, dict[str, list[tuple]]]:
    """Compile SNOW.INF into {surface: {mode: [(op, args...), ...]}}."""
    programs: dict[str, dict[str, list]] = {}
    group: list[str] = []
    group_has_modes = False
    mode: str | None = None
    cur: list | None = None
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        m = re.fullmatch(r"\[(.+?)\]", line)
        if m:
            name = m.group(1).strip().upper()
            if name in SURFACES:
                if group_has_modes:
                    group, group_has_modes = [], False
                group.append(name)
                mode, cur = None, None
            elif name in MODES:
                if not group:
                    raise ValueError(f"SNOW.INF: [{name}] before any surface")
                mode, cur, group_has_modes = name, [], True
                for srf in group:
                    programs.setdefault(srf, {})[mode] = cur
            else:
                raise ValueError(f"SNOW.INF: unknown section [{name}]")
            continue
        if mode is None:
            raise ValueError(f"SNOW.INF: instruction outside a mode section: {line!r}")
        parts = line.replace(",", " ").split()
        op = ALIASES.get(parts[0].upper(), parts[0].upper())
        args = parts[1:]
        if op == "LOAD":
            a = args[0].upper()
            ins = ("LOAD", a) if a in INPUTS else ("LOADC", float(args[0]))
        elif op == "BOUND":
            ins = ("BOUND", float(args[0]), float(args[1]))
        elif op in OPS_ARG:
            ins = (op, float(args[0]))
        elif op in OPS_NOARG:
            ins = (op,)
        else:
            raise ValueError(f"SNOW.INF: unknown instruction {parts[0]!r}")
        cur.append(ins)
    return programs


def run_snow(prog: list[tuple], inputs: dict[str, float]) -> tuple[float | None, float | None]:
    """Evaluate one program. inputs keys: SLIP DIG LEAN BOARD BEND. Returns (volume, bend)."""
    acc = 0.0
    stack: list[float] = []
    vol = bend = None
    for ins in prog:
        op = ins[0]
        if op == "LOAD":
            acc = float(inputs.get(ins[1], 0.0))
        elif op == "LOADC":
            acc = ins[1]
        elif op == "MAP":
            acc = acc * 127.0 / ins[1]
        elif op == "SQUARE":
            acc = acc * acc
        elif op == "SQRT":
            acc = math.sqrt(max(acc, 0.0))
        elif op == "ADD":
            acc += ins[1]
        elif op == "SUB":
            acc -= ins[1]
        elif op == "MUL":
            acc *= ins[1]
        elif op == "DIV":
            acc = acc / ins[1] if ins[1] else 0.0
        elif op == "BOUND":
            acc = min(max(acc, ins[1]), ins[2])
        elif op == "PUSH":
            stack.append(acc)
        elif op == "POP":
            acc = stack.pop() if stack else 0.0
        elif op in ("SADD", "SSUB", "SMUL", "SDIV"):
            b = stack.pop() if stack else 0.0
            a = stack.pop() if stack else 0.0
            r = {"SADD": a + b, "SSUB": a - b, "SMUL": a * b, "SDIV": (a / b if b else 0.0)}[op]
            stack.append(r)
        elif op == "ASSIGNVOL":
            vol = acc
        elif op == "ASSIGNBEND":
            bend = acc
    return vol, bend


if __name__ == "__main__":
    import sys
    progs = compile_snow(Path(sys.argv[1]).read_text())
    for s in SURFACES:
        print(s, {m: len(p) for m, p in progs.get(s, {}).items()})
    print(run_snow(progs["PACK"]["CARVE"], {"LEAN": 16, "DIG": 0, "SLIP": 200}))
