"""Extract every file from an ISO9660 image, stripping ';1' version suffixes."""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import sys
from pathlib import Path
import pycdlib

iso_path, out_dir = sys.argv[1], Path(sys.argv[2])
iso = pycdlib.PyCdlib()
iso.open(iso_path)
count = 0
for root, _dirs, files in iso.walk(iso_path="/"):
    base = root.rstrip("/")
    for name in files:
        dest = out_dir / base.lstrip("/") / name.split(";")[0]
        dest.parent.mkdir(parents=True, exist_ok=True)
        iso.get_file_from_iso(str(dest), iso_path=f"{base}/{name}")
        count += 1
iso.close()
print(f"extracted {count} files to {out_dir}")
