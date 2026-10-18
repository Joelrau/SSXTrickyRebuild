"""Optional PCSX2 recordings of the original game, used only for test reference data.

Set SSX_TRACES to a directory holding the recordings (live/<session>/..., ram/*.bin, gs/*.gs.zst). Nothing the
game itself needs comes from here; scripts that need a recording print "skipped (no traces)" without it.
"""
import os
import sys
from pathlib import Path


class TracesMissing(FileNotFoundError):
    pass


def root():
    """The SSX_TRACES directory, or None."""
    p = os.environ.get("SSX_TRACES", "").strip()
    return Path(p) if p and Path(p).is_dir() else None


def path(*parts) -> Path:
    """SSX_TRACES/<parts>; raises TracesMissing when SSX_TRACES is unset or the file is not there."""
    r = root()
    if r is None:
        raise TracesMissing("SSX_TRACES is not set")
    p = r.joinpath(*parts)
    if not p.exists():
        raise TracesMissing(f"{p} not found")
    return p


def available(*parts) -> bool:
    try:
        path(*parts)
        return True
    except TracesMissing:
        return False


def require(*parts, name=None):
    """Exit 0 with 'skipped (no traces)' unless SSX_TRACES is set and every listed recording exists (each part is a
    path tuple or str)."""
    if root() is None:
        print(f"{name or os.path.basename(sys.argv[0])}: skipped (no traces)")
        sys.exit(0)
    for p in parts:
        if not available(*([p] if isinstance(p, str) else p)):
            print(f"{name or os.path.basename(sys.argv[0])}: skipped (no traces)")
            sys.exit(0)
