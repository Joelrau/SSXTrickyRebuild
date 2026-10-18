"""RAM reference for SSX.Camera.Projection.
usage: python tools/make_camera_projection_reference.py
"""
# 0x7DA700 / 0x75CFC0
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "camera_projection.csv")


def main():
    R = open(os.path.join(TR, "live", "ride_session_1", "spawn_ram.bin"), "rb").read()
    u = lambda a: struct.unpack_from("<I", R, a)[0]
    slot = [u(0x7DA800 + 4 * k) for k in range(3)]
    mat = [u(0x75CFC0 + 4 * k) for k in range(16)]
    with open(OUT, "w", newline="\n") as f:
        f.write("name,hex\n")
        for name, v in zip(("half_fov", "near", "flash"), slot):
            f.write(f"{name},{v:08X}\n")
        for k, v in enumerate(mat):
            f.write(f"m{k // 4}{k % 4},{v:08X}\n")
    print(f"{OUT}: half-FOV {struct.unpack('<f', struct.pack('<I', slot[0]))[0]}, 16 matrix words")


if __name__ == "__main__":
    main()
