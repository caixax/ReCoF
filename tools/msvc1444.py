"""Toolchain environment for ReCoF's hl.dll and client.dll: cl 19.44 / link 14.44.

ReCoF moved the two game DLLs from the retail toolset (14.27, tools/msvc.py,
which DeCoF keeps) to the MSVC 14.44.35207 toolset of the installed Visual
Studio Build Tools 2022 with the Windows SDK 10.0.26100.0. The code no longer
matches retail byte for byte, so build.py check against retail is only a
guide for these two DLLs (docs/toolchains.md).

Usage as a module: env(), cl_path(), link_path().
Usage as a script:  python tools/msvc1444.py cl <args...>
"""
import os
import subprocess
import sys

VC_DIR = (r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
          r"\VC\Tools\MSVC\14.44.35207")
TOOLS_DIR = os.path.join(VC_DIR, "bin", "Hostx86", "x86")
SDK_DIR = r"C:\Program Files (x86)\Windows Kits\10"
SDK_VER = "10.0.26100.0"

# Flags added to every C and C++ compile of this toolchain (build.py). Since
# VS 2022, x86 code converts an out-of-range float or double to unsigned int
# by wrapping (__ftol3) where 14.27 returned 0xFFFFFFFF (__ftoui3); /fpcvt:IA
# gives the 14.27 results again (checked for negative, huge and 2^31..2^32
# values against the same test built with 14.27).
CFLAGS = ["/fpcvt:IA"]


def cl_path():
    return os.path.join(TOOLS_DIR, "cl.exe")


def link_path():
    return os.path.join(TOOLS_DIR, "link.exe")


def env():
    e = dict(os.environ)
    e["INCLUDE"] = ";".join([
        os.path.join(VC_DIR, "include"),
        os.path.join(SDK_DIR, "Include", SDK_VER, "ucrt"),
        os.path.join(SDK_DIR, "Include", SDK_VER, "um"),
        os.path.join(SDK_DIR, "Include", SDK_VER, "shared"),
    ])
    e["LIB"] = ";".join([
        os.path.join(VC_DIR, "lib", "x86"),
        os.path.join(SDK_DIR, "Lib", SDK_VER, "ucrt", "x86"),
        os.path.join(SDK_DIR, "Lib", SDK_VER, "um", "x86"),
    ])
    # rc.exe and mt.exe (the linker embeds the manifest with them)
    sdk_bin = os.path.join(SDK_DIR, "bin", SDK_VER, "x86")
    e["PATH"] = os.pathsep.join([TOOLS_DIR, sdk_bin, e.get("PATH", "")])
    # Keep the compiler from picking up stray settings
    for k in ("CL", "_CL_", "LINK", "_LINK_"):
        e.pop(k, None)
    return e


def check():
    for p in (cl_path(), link_path(), os.path.join(VC_DIR, "include"),
              os.path.join(SDK_DIR, "Include", SDK_VER),
              os.path.join(SDK_DIR, "Lib", SDK_VER, "ucrt", "x86", "libucrt.lib")):
        if not os.path.exists(p):
            sys.exit("missing toolchain component: %s" % p)


if __name__ == "__main__":
    check()
    if len(sys.argv) < 2 or sys.argv[1] not in ("cl", "link"):
        sys.exit("usage: msvc1444.py cl|link <args...>")
    exe = cl_path() if sys.argv[1] == "cl" else link_path()
    sys.exit(subprocess.call([exe] + sys.argv[2:], env=env()))
