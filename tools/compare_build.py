"""Compare two builds of the same binaries byte by byte, ignoring only the
fields that change on every build.

Usage:
  python tools/compare_build.py                       build/bin against build/baseline
  python tools/compare_build.py <new dir> <old dir>   any two folders
  python tools/compare_build.py <new file> <old file>

Ignored: COFF timestamp, optional header checksum, export, resource, load
config and debug directory timestamps, CodeView GUID or signature and age,
the PDB path a VC6 link writes at the end of the file (it holds the build
folder) with the debug size that follows its length, and the .NET MVID.
Everything else must be identical. A difference is printed as byte runs,
each with the nearest symbol from the link map when there is one.

Two builds are only comparable when both were made in the same folder:
the PDB paths and the names MSVC gives anonymous namespaces (?A0x...,
derived from the source file's full path) change with it.
"""
import bisect
import glob
import os
import re
import struct
import sys

import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = ("hl.dll", "client.dll", "hw.dll", "opengl32.dll", "GameUI.dll", "launcher.exe", "CoFLaunchApp.exe")


def masks(path):
    """The file's bytes (cut before a trailing PDB path) and the ranges that
    may differ between two builds, as (offset, length, field)."""
    data = open(path, "rb").read()
    pe = pefile.PE(data=data)
    m = [(pe.FILE_HEADER.get_file_offset() + 4, 4, "COFF timestamp"),
         (pe.OPTIONAL_HEADER.get_file_offset() + 64, 4, "checksum")]
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        m.append((pe.DIRECTORY_ENTRY_EXPORT.struct.get_file_offset() + 4, 4, "export timestamp"))

    def resdir(d):
        m.append((d.struct.get_file_offset() + 4, 4, "resource timestamp"))
        for e in d.entries:
            if hasattr(e, "directory"):
                resdir(e.directory)
    if hasattr(pe, "DIRECTORY_ENTRY_RESOURCE"):
        resdir(pe.DIRECTORY_ENTRY_RESOURCE)
    if hasattr(pe, "DIRECTORY_ENTRY_LOAD_CONFIG"):
        m.append((pe.DIRECTORY_ENTRY_LOAD_CONFIG.struct.get_file_offset() + 4, 4, "load config timestamp"))
    tail = None
    for dbg in getattr(pe, "DIRECTORY_ENTRY_DEBUG", []):
        m.append((dbg.struct.get_file_offset() + 4, 4, "debug timestamp"))
        p = dbg.struct.PointerToRawData
        sig = data[p:p + 4]
        if sig == b"RSDS":
            m.append((p + 4, 20, "CodeView GUID and age"))
        elif sig == b"NB10":
            m.append((p + 8, 8, "CodeView signature and age"))
            if p + dbg.struct.SizeOfData >= len(data) - 4:
                # VC6: the record, PDB path included, closes the file
                m.append((dbg.struct.get_file_offset() + 16, 4, "debug size"))
                tail = p + 16
    clr = pe.OPTIONAL_HEADER.DATA_DIRECTORY[14]
    if clr.VirtualAddress:
        co = pe.get_offset_from_rva(clr.VirtualAddress)
        mdrva = struct.unpack_from("<I", data, co + 8)[0]
        mo = pe.get_offset_from_rva(mdrva)
        q = mo + 16 + struct.unpack_from("<I", data, mo + 12)[0]
        count = struct.unpack_from("<H", data, q + 2)[0]
        q += 4
        for _ in range(count):
            off, size = struct.unpack_from("<II", data, q)
            name = data[q + 8:data.index(b"\0", q + 8)]
            q += 8 + ((len(name) + 4) & ~3)
            if name == b"#GUID":
                m.append((mo + off, size, "MVID"))
    if tail is not None:
        data = data[:tail]
    return data, m, pe


def symbols(path, pe):
    """(rva, name) from the link map of a binary in build/bin, if any."""
    out = []
    maps = glob.glob(os.path.join(ROOT, "build", "link", "*", os.path.basename(path) + ".map"))
    if not maps:
        return out
    base = pe.OPTIONAL_HEADER.ImageBase
    for line in open(maps[0], errors="replace"):
        x = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s", line)
        if x:
            out.append((int(x.group(2), 16) - base, x.group(1)))
    out.sort()
    return out


def compare(new, old):
    a, ma, pe = masks(new)
    b, mb, _ = masks(old)
    if len(a) != len(b):
        return "DIFF size %d against %d" % (len(a), len(b))
    skip = bytearray(len(a))
    for off, n, _f in ma + mb:
        skip[off:off + n] = b"\1" * n
    diffs = [i for i in range(len(a)) if a[i] != b[i] and not skip[i]]
    if not diffs:
        return "SAME"
    runs, start = [], diffs[0]
    for x, y in zip(diffs, diffs[1:] + [None]):
        if y != x + 1:
            runs.append((start, x))
            start = y
    syms = symbols(new, pe)
    keys = [s[0] for s in syms]
    lines = ["DIFF %d bytes in %d runs" % (len(diffs), len(runs))]
    for s, e in runs[:12]:
        rva = pe.get_rva_from_offset(s)
        k = bisect.bisect_right(keys, rva) - 1 if rva is not None else -1
        lines.append("  0x%x-0x%x  %s" % (s, e, syms[k][1] if k >= 0 else ""))
    if len(runs) > 12:
        lines.append("  ... %d more runs" % (len(runs) - 12))
    return "\n".join(lines)


def main():
    args = sys.argv[1:]
    if not args:
        args = [os.path.join(ROOT, "build", "bin"), os.path.join(ROOT, "build", "baseline")]
    if len(args) != 2:
        sys.exit(__doc__)
    new, old = args
    pairs = [(new, old)] if os.path.isfile(new) else \
        [(os.path.join(new, f), os.path.join(old, f)) for f in FILES]
    same = True
    for n, o in pairs:
        if not (os.path.exists(n) and os.path.exists(o)):
            print("%-17s missing" % os.path.basename(n))
            same = False
            continue
        r = compare(n, o)
        same &= r == "SAME"
        print("%-17s %s" % (os.path.basename(n), r))
    sys.exit(0 if same else 1)


if __name__ == "__main__":
    main()
