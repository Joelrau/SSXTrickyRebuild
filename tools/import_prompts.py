"""Copy the button-prompt images the UI uses from Julio Cacko's "Free Input Prompts" pack (CC0 1.0,
https://juliocacko.itch.io/free-input-prompts) into Unreal/SSXTrickyRebuild/Prompts, cropped to their visible pixels.
Not game data: the result is committed.

Usage: python tools/import_prompts.py <pack>/Source
"""
import libpath  # noqa: F401  (tools/lib on sys.path)
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "Prompts"
BUTTONS = ("cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "start", "select", "up", "down", "left",
           "right", "lstick", "rstick")


def pad(prefix, folder, names):
    return {b: f"{folder}/T_{prefix}_{n}.png" for b, n in zip(BUTTONS, names)}


SETS = {
    "ps5": pad("P5", "P5Gamepad/Default", ("Cross_Color", "Circle_Color", "Square_Color", "Triangle_Color", "L1", "R1",
                                           "L2", "R2", "Options", "Share", "Dpad_UP", "Dpad_Down", "Dpad_Left",
                                           "Dpad_Right", "L", "R")),
    "ps4": pad("P4", "P4Gamepad/Default", ("Cross_Color", "Circle_Color", "Square_Color", "Triangle_Color", "L1", "R1",
                                           "L2", "R2", "Options", "Share", "Dpad_UP", "Dpad_Down", "Dpad_Left",
                                           "Dpad_Right", "L", "R")),
    # T_X_X is the Menu button, T_X_Share the View button.
    "xbox": pad("X", "XGamepad/Default", ("A_Color", "B_Color", "X_Color", "Y_Color", "LB", "RB", "LT", "RT", "X",
                                          "Share", "Dpad_Up", "Dpad_Down", "Dpad_Left", "Dpad_Right", "L", "R")),
    "key": {k.lower(): f"Keyboard_Mouse/Dark/T_{k}_Key_Dark.png" for k in (
        "Space", "Crtl", "BackSpace", "Tab", "Enter", "Esc", "Shift", "Up", "Down", "Left", "Right", "1", "3",
        "Q", "E", "F", "G", "Z", "C", "R", "V", "W", "A", "S", "D", "I", "J", "K", "L")},
}


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    src = Path(sys.argv[1])
    n = 0
    for name, files in SETS.items():
        (OUT / name).mkdir(parents=True, exist_ok=True)
        for key, rel in files.items():
            im = Image.open(src / rel).convert("RGBA")
            box = im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
            if box:
                im = im.crop((max(0, box[0] - 2), max(0, box[1] - 2), min(im.width, box[2] + 2), min(im.height, box[3] + 2)))
            im.save(OUT / name / f"{'ctrl' if key == 'crtl' else key}.png", optimize=True)
            n += 1
    print(f"{n} prompts -> {OUT}")


if __name__ == "__main__":
    main()
