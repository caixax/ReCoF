"""Build the files of a ReCoF release in the format the ReCoF Launcher installs.

Usage:
  python tools/make_release.py --version 0.1.0 [--channel beta] [--launcher ../ReCoF-Launcher]
                               [--min-launcher 0.1.0] [--buildid 5821301] [--notes-url URL]

1. Stages the rebuilt binaries in build/release/v<version>/files, laid out as
   they go into the game folder (hw.dll at the top, cryoffear/cl_dlls/... under
   it), plus sw.dll as a copy of hw.dll as in retail, CoFLaunchApp.exe (the
   rebuilt settings dialog, from build/coflaunchapp), the files of
   tools/dist_extra (Play ReCoF.bat, recof-cvars.json) and README-ReCoF-TEST.txt.
2. Runs `cargo xtask pack-recof` of the ReCoF-Launcher repository on that
   folder. It writes build/release/v<version>/upload/recof-<version>.zip,
   recof-manifest.json and recof-manifest.json.sig, signed with the launcher's
   key (keys/<id>.secret.key there, or RECOF_LAUNCHER_SIGNING_KEY), and checks
   the manifest against the rules the launcher applies. The format is
   described in that repository's docs/releases.md.
3. Writes build/release/v<version>/upload/ReCoF-v<version>.zip, the same files
   in one zip for installing by hand (sw.dll included, which the launcher set
   lists as a copy of hw.dll).

The four files then go to the GitHub release v<version> of caixax/ReCoF
(command printed at the end). The release body is the changelog the launcher
shows before it updates. A beta (the default channel here) is a GitHub
prerelease: launchers on the stable channel do not see it.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "bin")
EXTRA = os.path.join(ROOT, "tools", "dist_extra")
COFLAUNCHAPP = os.path.join(ROOT, "build", "coflaunchapp", "CoFLaunchApp.exe")
RC_VERSION = os.path.join(ROOT, "src", "cl_dll", "rc_version.h")

# (path in the game folder, file in build/bin, what it is)
FILES = [
    ("cof.exe", "launcher.exe", "engine launcher"),
    ("hw.dll", "hw.dll", "GoldSrc engine"),
    ("sw.dll", "hw.dll", "the same file as hw.dll, as in retail"),
    ("opengl32.dll", "opengl32.dll", "Paranoia OpenGL wrapper"),
    ("cryoffear/cl_dlls/hl.dll", "hl.dll", "game logic (server DLL)"),
    ("cryoffear/cl_dlls/client.dll", "client.dll", "client DLL and renderer"),
    ("cryoffear/cl_dlls/GameUI.dll", "GameUI.dll", "menus"),
]

README = """ReCoF {version} (test build)

WARNING: for testing only. These files have not been confirmed to work in game
yet. Use them in a copy of the game, never in your Steam installation. If you
use them anyway, you do so at your own risk.

ReCoF is Cry of Fear 1.6a rebuilt from the DeCoF source with bug fixes and
improvements. Speedrun techniques are kept; balance fixes can be switched back
to retail with the console command rc_retail_gameplay 1.

HOW TO TEST
With the ReCoF Launcher: install or update ReCoF from it (this build is on the
beta channel), or use Maintenance, Install from a file, with recof-{version}.zip,
recof-manifest.json and recof-manifest.json.sig in one folder.

By hand:
1. Copy your Cry of Fear folder (Steam: steamapps\\common\\Cry of Fear) to
   another place, for example "Cry of Fear ReCoF test".
2. Extract ReCoF-v{version}.zip into that copy and let it replace the files.
3. Start Steam, then double-click "Play ReCoF.bat" in the copy. It starts the
   game with the launcher's settings plus -condebug, which writes the console
   to qconsole.log next to cof.exe. Open the .bat in Notepad to set the
   resolution, windowed mode or extra options (-novid skips the intro videos).
4. Report problems with the map name and what you did; qconsole.log helps.

NOTES
- Do not load RebuiltSimon with this build: its hl.dll patch lands on other
  code. ReCoF has its options built in (see below).
- Retail saves load.
- LiveSplit: the hw.dll values are where the script expects them; the client.dll
  values moved. The script addition is in docs/speedrun.md of the ReCoF
  repository ("ReCoF autosplitter block").
- The Linux patcher does not apply to these files.
- Sprays: Options, Spray makes your spray from a PNG, JPG, BMP, GIF or TIFF
  picture in cryoffear\\logos; Options, Keyboard has the Spray logo key
  (impulse 201). Single player has no wait between sprays.

NEW CONSOLE OPTIONS (default in brackets)
  rc_retail_gameplay [0]   1 = retail behavior of every balance fix
  rc_hlmovement [0]        1 = Half-Life 1 movement as in WON (no stamina or sprint,
                           Half-Life ladders); turning it on sets rc_bhopcap 0,
                           rc_ducktap 1 and rc_autojump 1, turning it off sets them back
  rc_bhopcap [1]           0 = no bunny hop speed cap
  rc_ducktap [0]           1 = Half-Life ducktap
  rc_infinite_stamina [0]  1 = stamina never runs out
  +rc_ducktap, rc_autojump [0], rc_ducktap_priority [1], rc_jump_priority [0]
  rc_speedometer [0] (and rc_speedometer_jump, _3d, _pos, _color)
  rc_rawinput [2]          raw mouse input, no Windows acceleration: 2 = read on a
                           thread of its own as RInput does, 1 = read on the game
                           window, 0 = retail
  rc_prof [0]              1 = frame time per renderer phase, 2 = plus GPU
  rc_prof_dump             prints the rc_prof numbers to the console
  rc_modelarrays [1]       0 = retail model drawing, for comparison
  rc_dlightcull [1]        0 = retail, draw lights that cannot reach the screen
  rc_vsync [0]             1 = vertical sync on; Options, Video sets it too
  rc_spray_size [0]        size of your spray on the wall in units (0 = a unit a
                           picture pixel); Options, Spray sets it too
  rc_crosshair [1]         0 = no red crosshair (the ammo display stays)
  rc_cross [0]             1 = custom crosshair as BunnymodXT's bxt_cross, set by
                           rc_cross_color, _alpha, _thickness, _size, _gap,
                           _outline, _circle_radius, _dot_color, _dot_size,
                           _top_line, _bottom_line, _left_line, _right_line
  rc_viewmodel_fov [0]     field of view of the weapon model (0 = the view's)
  rc_viewmodel_ofs_forward, rc_viewmodel_ofs_right, rc_viewmodel_ofs_up [0]
                           move the weapon model, in units
  god, noclip, notarget, fly work with sv_cheats 1
  Launch options: -novid (skip the intro videos), -rc_aff (pin to the first core
  as retail), -rc_nosleep (retail frame limiter that spins a whole core)

HOW TO GO BACK
Delete the test copy. If you replaced files in your Steam installation, Steam
can restore them: right-click Cry of Fear, Properties, Installed Files, Verify
integrity of game files.

Requires Cry of Fear 1.6a (the current Steam version), Steam build {buildid}.
Built from the ReCoF source at commit {commit}.

FILES (SHA-256)
"""


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--channel", default="beta", choices=["beta", "stable"])
    ap.add_argument("--launcher", default=os.path.join(os.path.dirname(ROOT), "ReCoF-Launcher"))
    ap.add_argument("--min-launcher", default="0.1.0")
    ap.add_argument("--buildid", default="5821301")
    ap.add_argument("--notes-url")
    args = ap.parse_args()

    version = args.version.lstrip("v")
    notes = args.notes_url or "https://github.com/caixax/ReCoF/releases/tag/v%s" % version
    base = os.path.join(ROOT, "build", "release", "v" + version)
    stage = os.path.join(base, "files")
    upload = os.path.join(base, "upload")
    for d in (stage, upload):
        if os.path.exists(d):
            shutil.rmtree(d)
        os.makedirs(d)

    missing = [src for _rel, src, _what in FILES if not os.path.isfile(os.path.join(BIN, src))]
    if missing:
        sys.exit("not built yet: %s (run the build.py links first)" % ", ".join(missing))
    if not os.path.isfile(COFLAUNCHAPP):
        sys.exit("not built yet: %s (run tools/coflaunchapp/build.py)" % COFLAUNCHAPP)

    # the main menu shows the version client.dll was built with
    with open(RC_VERSION, encoding="ascii") as f:
        source_version = f.read().split('RECOF_VERSION\t"', 1)[1].split('"', 1)[0]
    if source_version != version:
        sys.exit("src/cl_dll/rc_version.h says %s: set it to %s, then relink client.dll" % (source_version, version))
    with open(os.path.join(BIN, "client.dll"), "rb") as f:
        if ("ReCoF-v%s\0" % version).encode("ascii") not in f.read():
            sys.exit("client.dll does not show ReCoF-v%s in the menu yet: relink it (build.py link client)" % version)

    staged = []
    for rel, src, what in FILES:
        dest = os.path.join(stage, *rel.split("/"))
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copyfile(os.path.join(BIN, src), dest)
        staged.append((rel, what))
    shutil.copyfile(COFLAUNCHAPP, os.path.join(stage, "CoFLaunchApp.exe"))
    staged.append(("CoFLaunchApp.exe", "settings launcher"))
    for name in sorted(os.listdir(EXTRA)):
        shutil.copyfile(os.path.join(EXTRA, name), os.path.join(stage, name))
        staged.append((name, "test launch with the console log on" if name.endswith(".bat") else "ReCoF options for the launcher"))

    # the commit of the last source change, which is what the binaries were built from
    commit = subprocess.run(["git", "-C", ROOT, "log", "-1", "--format=%h", "--", "src"],
                            stdout=subprocess.PIPE, text=True).stdout.strip()
    text = README.format(version=version, buildid=args.buildid, commit=commit)
    for rel, what in staged:
        text += "  %s  %s (%s)\n" % (sha256(os.path.join(stage, *rel.split("/"))), rel.replace("/", "\\"), what)
    with open(os.path.join(stage, "README-ReCoF-TEST.txt"), "w", encoding="ascii", newline="\r\n") as f:
        f.write(text)

    if not os.path.isdir(args.launcher):
        sys.exit("ReCoF-Launcher not found at %s (pass --launcher)" % args.launcher)
    cmd = ["cargo", "xtask", "pack-recof", "--dir", stage, "--version", version, "--channel", args.channel,
           "--min-launcher", args.min_launcher, "--buildid", args.buildid, "--notes-url", notes, "--out", upload]
    print(" ".join(cmd))
    if subprocess.call(cmd, cwd=args.launcher) != 0:
        sys.exit("pack-recof failed")

    # the same set in one zip for installing by hand
    full = os.path.join(upload, "ReCoF-v%s.zip" % version)
    with zipfile.ZipFile(full, "w", zipfile.ZIP_DEFLATED) as z:
        for dirpath, _dirs, names in os.walk(stage):
            for name in sorted(names):
                p = os.path.join(dirpath, name)
                z.write(p, os.path.relpath(p, stage).replace("\\", "/"))

    print("\nupload these to the GitHub release v%s of caixax/ReCoF:" % version)
    for name in sorted(os.listdir(upload)):
        p = os.path.join(upload, name)
        if os.path.isfile(p):
            print("  %-26s %9d  %s" % (name, os.path.getsize(p), sha256(p)[:16]))
    print('\n  gh release upload v%s -R caixax/ReCoF --clobber "%s" "%s" "%s" "%s"' % (
        version, os.path.join(upload, "recof-%s.zip" % version), os.path.join(upload, "recof-manifest.json"),
        os.path.join(upload, "recof-manifest.json.sig"), full))


if __name__ == "__main__":
    main()
