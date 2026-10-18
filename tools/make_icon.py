"""Windows icon of the packaged game (SSXTricky.exe) from the game's own loading-screen logo.

    python tools/make_icon.py      # -> Unreal/SSXTrickyRebuild/Build/Windows/Application.ico (git-ignored)

The "SSX Tricky" logo is the top region of converted/frontend/loading/LOADING/001_loa2.png
(bbox x 3..255, y 2..154). It is cropped, padded to a square with an 8% margin on an orange
(244,118,40) background, resized to 256 px (LANCZOS) and written as a multi-size .ico (16..256).
UnrealBuildTool embeds <Project>/Build/Windows/Application.ico into the game target's exe.
Game-derived data: the .ico is never committed.
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "converted" / "frontend" / "loading" / "LOADING" / "001_loa2.png"
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "Build" / "Windows" / "Application.ico"

LOGO_BOX = (3, 2, 256, 155)          # PIL crop box: x 3..255, y 2..154 inclusive
BACKGROUND = (244, 118, 40, 255)
MARGIN = 0.08
SIZE = 256
ICO_SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]


def main():
    if not SRC.is_file():
        sys.exit(f"{SRC} not found: convert the disc first (build_data.bat)")
    logo = Image.open(SRC).convert("RGBA").crop(LOGO_BOX)
    w, h = logo.size
    side = int(round(max(w, h) / (1.0 - 2.0 * MARGIN)))
    square = Image.new("RGBA", (side, side), BACKGROUND)
    square.alpha_composite(logo, ((side - w) // 2, (side - h) // 2))
    icon = square.resize((SIZE, SIZE), Image.LANCZOS)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    icon.save(OUT, format="ICO", sizes=ICO_SIZES)
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
