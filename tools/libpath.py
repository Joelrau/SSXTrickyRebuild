"""Puts tools/lib, tools/lib/models and tools/lib/scripts on sys.path.
Every tool imports it first:  import libpath  # noqa: F401"""
import os
import sys

LIB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib")
for _d in (os.path.join(LIB, "scripts"), os.path.join(LIB, "models"), LIB):
    if _d not in sys.path:
        sys.path.insert(0, _d)

