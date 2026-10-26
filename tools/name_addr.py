"""Name module offsets with the baseline link maps (retail layout).

Usage:
  python tools/name_addr.py <dll> <offset> [<offset> ...]
  python tools/name_addr.py hw.dll 7f1c9 0x7f336

An offset is relative to the module base, as a debugger prints it (hw+0x7f1c9).
The maps in build/baseline/maps come from the unchanged source, so they name
retail and DeCoF addresses; for a ReCoF build, pass --map build/link/<dll>/<dll>.map.

Crash dumps: retail's crashhandler.dll writes them to Steam's dumps folder
(for example C:\\Program Files (x86)\\Steam\\dumps\\crash_cof.exe_<time>_1.dmp).
Open one with cdb -z <file> -c ".ecxr; kb 20; lm o; q" and name the frames here.
"""
import bisect
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINE = re.compile(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+(?:f\s+)?(?:i\s+)?(\S+)\s*$")


def read_map(path):
    syms, base = [], None
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


def main():
    args = sys.argv[1:]
    path = None
    if "--map" in args:
        i = args.index("--map")
        path = args[i + 1]
        args = args[:i] + args[i + 2:]
    if len(args) < 2:
        print(__doc__)
        return 2
    dll = args[0] if args[0].lower().endswith((".dll", ".exe")) else args[0] + ".dll"
    path = path or os.path.join(ROOT, "build", "baseline", "maps", dll + ".map")
    syms = read_map(path)
    keys = [s[0] for s in syms]
    for a in args[1:]:
        off = int(a, 16)
        i = bisect.bisect_right(keys, off) - 1
        if i < 0:
            print("%s+0x%X: before the first symbol" % (dll, off))
        else:
            print("%s+0x%X = %s+0x%X (%s)" % (dll, off, syms[i][1], off - syms[i][0], syms[i][2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
