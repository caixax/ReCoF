#!/usr/bin/env python3
"""Builds the server game DLL for Linux, hl.so, from the same unit list as hl.dll (tools/units.json).

Runs on Linux (or WSL) with GCC and its 32-bit multilib (gcc-multilib g++-multilib). Only the dedicated server
loads hl.so; the game itself stays on Windows.

usage: python3 tools/build_linux.py [--out build/linux] [--jobs N] [--only <unit name>] [--keep-going]

- Every unit compiles with -m32 -O2 and SSE2 maths (-msse2 -mfpmath=sse), as MSVC's x86 default, so movement and
  physics give the same numbers as the Windows build.
- src/dlls/rc_linux.h is included first in every unit: the Windows names the code uses (stricmp, _snprintf, ...).
- libstdc++ and libgcc are linked statically, so hl.so needs only the C library of the server's system; build on
  the oldest system it must run on (CI uses Debian 11).
- The link refuses undefined symbols: the engine reaches hl.so only through the function tables it passes in.
"""
import argparse
import concurrent.futures
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CFLAGS = ["-m32", "-O2", "-fPIC", "-fno-strict-aliasing", "-march=i686", "-msse2", "-mfpmath=sse",
          "-fpermissive", "-w", "-fno-omit-frame-pointer"]
DEFINES = ["NDEBUG", "LINUX", "_LINUX", "POSIX", "QUIVER", "VOXEL", "QUAKE2", "VALVE_DLL"]
COMPAT = "src/dlls/rc_linux.h"


def compile_unit(unit, includes, out_dir):
    src = os.path.join(ROOT, unit["source"])
    obj = os.path.join(out_dir, "obj", unit["name"].replace("/", "_") + ".o")
    if src.endswith(".c"):
        cmd = ["gcc", "-std=gnu99"]
    else:
        cmd = ["g++", "-std=gnu++14"]
    cmd += CFLAGS + ["-D" + d for d in DEFINES] + ["-I" + os.path.join(ROOT, i) for i in includes]
    cmd += ["-include", os.path.join(ROOT, COMPAT), "-c", src, "-o", obj]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return unit["name"], obj, r.returncode, r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "linux"))
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--only")
    ap.add_argument("--keep-going", action="store_true")
    args = ap.parse_args()

    hl = json.load(open(os.path.join(ROOT, "tools", "units.json")))["dlls"]["hl"]
    # rc_steam_linux.cpp: the Steam functions the game DLL imports on Windows, answering "no Steam"
    units = hl["units"] + [{"name": "dlls/rc_steam_linux", "source": "src/dlls/rc_steam_linux.cpp"}]
    if args.only:
        units = [u for u in units if u["name"] == args.only]
    os.makedirs(os.path.join(args.out, "obj"), exist_ok=True)

    objs, failed = [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for name, obj, rc, err in pool.map(lambda u: compile_unit(u, hl["includes"], args.out), units):
            if rc != 0:
                failed.append(name)
                print(f"FAIL {name}\n{err.strip()}\n", file=sys.stderr)
            else:
                objs.append(obj)
    print(f"compiled {len(objs)} of {len(units)} units")
    if failed:
        print("failed: " + " ".join(failed))
        if not args.keep_going:
            return 1
    if args.only or failed:
        return 0 if not failed else 1

    out = os.path.join(args.out, "hl.so")
    cmd = ["g++", "-m32", "-shared", "-o", out] + objs
    cmd += ["-static-libstdc++", "-static-libgcc", "-Wl,--no-undefined", "-ldl", "-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stderr.strip(), file=sys.stderr)
        return 1
    print("linked " + os.path.relpath(out, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
