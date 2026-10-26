"""Check that a build keeps what speedrun tools read or patch at fixed places
(docs/speedrun.md).

Usage:
  python tools/check_runner.py      build/bin and build/link/*/*.map against
                                    build/baseline and build/baseline/maps

Checks:
  - every address the LiveSplit autosplitter and RebuiltSimon read still holds
    the same variable or function: the symbol that contains it in the baseline
    map must sit at the same address in the new map;
  - the bytes RebuiltSimon overwrites at hl.dll +0xEB203 are unchanged;
  - BunnymodXT's Host_ValidSave pattern still matches, first in hw.dll's code
    and at Host_ValidSave itself.

The baseline maps are the link maps of the unchanged source (Sprint 0 build).
LiveSplit and BunnymodXT results decide the exit code (1 when anything moved
or changed). RebuiltSimon results are warnings: its code addresses and patch
site move with almost any code fix, and ReCoF offers its features natively
instead. Only the address of each variable is checked, not the layout inside
it: never add or reorder members of a struct the autosplitter reads into
(CHud gHUD, client_state_t cl, server_t sv and the others listed above).
"""
import bisect
import os
import re
import sys

import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NEW_BIN = os.path.join(ROOT, "build", "bin")
OLD_BIN = os.path.join(ROOT, "build", "baseline")
OLD_MAPS = os.path.join(ROOT, "build", "baseline", "maps")
NEW_MAPS = {"hl.dll": "hl", "client.dll": "client", "hw.dll": "hw"}

# (binary, module offset, tool, what it is)
ADDRESSES = [
    ("hw.dll", 0x11119E4, "LiveSplit", "pause state"),
    ("hw.dll", 0x10D279C, "LiveSplit", "loading state"),
    ("hw.dll", 0xF1AA15, "LiveSplit", "map name"),
    ("hw.dll", 0x820495, "LiveSplit", "menu map"),
    ("hw.dll", 0x377BD8, "LiveSplit", "co-op start"),
    ("hw.dll", 0x116BED0, "LiveSplit", "health"),
    ("hw.dll", 0x9FDC30, "LiveSplit", "can move"),
    ("hw.dll", 0x1F39F0, "LiveSplit", "alive"),
    ("hw.dll", 0x1F39C8, "LiveSplit", "demo"),
    ("client.dll", 0x18ED64, "LiveSplit", "cutscene state"),
    ("client.dll", 0x1BE920, "LiveSplit", "in-game time"),
    ("client.dll", 0x5440C8, "LiveSplit", "music"),
    ("client.dll", 0x18A3E0, "LiveSplit", "save state"),
    ("hw.dll", 0x812B8, "RebuiltSimon", "notarget command"),
    ("hw.dll", 0x813C8, "RebuiltSimon", "noclip command"),
    ("hw.dll", 0x8123B, "RebuiltSimon", "god command"),
    ("hw.dll", 0x7E0544, "RebuiltSimon", "player pointer"),
    ("hw.dll", 0x11115AC, "RebuiltSimon", "health"),
    ("hl.dll", 0xEB203, "RebuiltSimon", "noclip reset in CBasePlayer::PostThink"),
]
# (binary, module offset, length, tool, what it is)
PATCHED = [("hl.dll", 0xEB203, 19, "RebuiltSimon", "19 NOPs over the noclip reset")]
BXT = "55 8B EC 83 3D ? ? ? ? ? 74 ? 33 C0 E9"

LINE = re.compile(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+(?:f\s+)?(?:i\s+)?(\S+)\s*$")


def read_map(path):
    """Sorted [(rva, name, obj)] of a link map, public and static symbols."""
    base = None
    syms = []
    for l in open(path, encoding="latin-1"):
        if base is None:
            m = re.search(r"Preferred load address is ([0-9a-fA-F]+)", l)
            if m:
                base = int(m.group(1), 16)
            continue
        m = LINE.match(l)
        if m and int(m.group(2), 16) >= base:
            syms.append((int(m.group(2), 16) - base, m.group(1), m.group(3)))
    syms.sort()
    return syms


def bss_start(path):
    """Offset of the .bss group inside its section, from the map's group list."""
    for l in open(path, encoding="latin-1"):
        m = re.match(r"^\s*[0-9a-fA-F]{4}:([0-9a-fA-F]{8})\s+[0-9a-fA-F]{8}H\s+\.bss\s", l)
        if m:
            return int(m.group(1), 16)
    return 0


def containing(syms, rva):
    i = bisect.bisect_right([s[0] for s in syms], rva) - 1
    return syms[i] if i >= 0 else None


def find(syms, name, obj):
    hits = [s for s in syms if s[1] == name and s[2] == obj]
    return hits[0] if len(hits) == 1 else None


def rva_bytes(pe, rva, n):
    return pe.get_data(rva, n)


def pattern_hits(pe, pattern):
    text = next(s for s in pe.sections if s.Name.rstrip(b"\0") == b".text")
    data = text.get_data()
    rx = re.compile(b"".join(b"." if p == "?" else re.escape(bytes([int(p, 16)])) for p in pattern.split()), re.S)
    return [text.VirtualAddress + m.start() for m in rx.finditer(data)]


def main():
    bad = 0
    maps = {}
    for dll, folder in NEW_MAPS.items():
        old = os.path.join(OLD_MAPS, dll + ".map")
        new = os.path.join(ROOT, "build", "link", folder, dll + ".map")
        if not os.path.exists(old) or not os.path.exists(new):
            print("missing map for %s (%s or %s)" % (dll, old, new))
            return 2
        maps[dll] = (read_map(old), read_map(new))
    for dll, off, tool, what in ADDRESSES:
        old, new = maps[dll]
        s = containing(old, off)
        n = find(new, s[1], s[2]) if s else None
        if n is None:
            print("MISSING %-10s +0x%-8X %-12s %s: %s not found in the new map" % (dll, off, tool, what, s[1] if s else "?"))
            bad += tool != "RebuiltSimon"
            continue
        moved = n[0] + (off - s[0])
        state = "OK     " if moved == off else ("WARN   " if tool == "RebuiltSimon" else "MOVED  ")
        bad += moved != off and tool != "RebuiltSimon"
        print("%s %-10s +0x%-8X %-12s %-40s %s+0x%X%s" % (state, dll, off, tool, what, s[1], off - s[0], "" if moved == off else " now at +0x%X" % moved))
    pes = {}
    for dll in NEW_MAPS:
        pes[dll] = (pefile.PE(os.path.join(OLD_BIN, dll), fast_load=True), pefile.PE(os.path.join(NEW_BIN, dll), fast_load=True))
    for dll, off, n, tool, what in PATCHED:
        same = rva_bytes(pes[dll][0], off, n) == rva_bytes(pes[dll][1], off, n)
        bad += not same and tool != "RebuiltSimon"
        print("%s %-10s +0x%-8X %-12s %s" % ("OK     " if same else ("WARN   " if tool == "RebuiltSimon" else "CHANGED"), dll, off, tool, what + ("" if same else ": the tool would overwrite other code")))
    old, new = maps["hw.dll"]
    hv = [s for s in new if s[1] == "_Host_ValidSave"]
    hits = pattern_hits(pes["hw.dll"][1], BXT)
    ok = len(hv) == 1 and hits and hits[0] == hv[0][0]
    old_fn = [s for s in old if s[1] == "_Host_ValidSave"]
    same = ok and old_fn and rva_bytes(pes["hw.dll"][0], old_fn[0][0], 15) == rva_bytes(pes["hw.dll"][1], hv[0][0], 15)
    bad += not ok
    print("%s %-10s %-11s %-12s Host_ValidSave pattern%s" % ("OK     " if ok else "MISSING", "hw.dll", "", "BunnymodXT",
          "" if ok else ": first match %s, Host_ValidSave %s" % (hex(hits[0]) if hits else "none", hex(hv[0][0]) if hv else "?")))
    if ok and not same:
        print("        hw.dll Host_ValidSave start differs from retail, check the whole function")
    for dll in ("hw.dll", "client.dll"):
        old_pe, new_pe = pes[dll]
        room = []
        for i, s in enumerate(new_pe.sections[:-1]):
            name = s.Name.rstrip(b"\0").decode()
            if name in (".text", ".rdata"):
                room.append("%s %d bytes" % (name, new_pe.sections[i + 1].VirtualAddress - s.VirtualAddress - s.Misc_VirtualSize))
        bss = [bss_start(os.path.join(OLD_MAPS, dll + ".map")), bss_start(os.path.join(ROOT, "build", "link", NEW_MAPS[dll], dll + ".map"))]
        print("room   %-10s %s before the next section moves; .bss starts %+d bytes from retail (anything but 0 moves the variables in it)" % (
            dll, ", ".join(room), bss[1] - bss[0]))
    print("LiveSplit and BunnymodXT addresses kept" if not bad else "%d runner check(s) failed" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
