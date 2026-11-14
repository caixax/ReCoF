"""Test maps with many monsters, for measuring the frame with crowds (B337).

  python tools/rc_stressmap.py [--counts 0 16 32 64] [--install]

Writes build/stressmaps/rc_stress_<n>.map, one per monster count (with --glass
rc_stressview_<n>.map, where a wall the player sees through keeps them idle), and compiles each
with SDHLT (sdHLCSG, sdHLBSP, sdHLVIS, sdHLRAD; set SDHLT to the folder of the
Win64 tools, I:/Tools/sdhlt/sdhlt-v1.3.1/tools/Win64 by default). --install copies
the .bsp files to dist/cryoffear/maps.

Every map is the same arena, 3072 units square and 384 high, with a grid of pillars
the monsters have to walk around, a grid of info_node for their routes and lights
under the ceiling. The player starts at the south wall facing north; the monsters
stand in the north half facing south, the Cry of Fear monsters that chase in the
heavy maps (c_the_stairway3, c_doc_swift, c_ending5c) in turn. The textures come
from cof3.wad of the game.
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "stressmaps")
SDHLT = os.environ.get("SDHLT", r"I:\Tools\sdhlt\sdhlt-v1.3.1\tools\Win64")
WAD = os.path.join(ROOT, "versions", "1.6a", "cryoffear", "cof3.wad")

HALF = 1536		# the arena's inside runs from -HALF to HALF on x and y
HEIGHT = 384
WALL = 32
PILLAR = 32		# half the width of a pillar
PILLARS = (-1024, -512, 0, 512, 1024)
NODE_STEP = 192

# (classname, extra keys): the chasers of the heavy maps
MONSTERS = [
    ("monster_slower", {}),
    ("monster_faster", {}),
    ("monster_twitcher2", {}),
    ("monster_rcrazy", {}),
    ("monster_faceless", {"model": "models/psycho.mdl"}),
    ("monster_slower3", {}),
    ("monster_twitcher3", {}),
    ("monster_child", {}),
    ("monster_krypande", {}),
    ("monster_crazybitch", {}),
]


def box(x1, y1, z1, x2, y2, z2, tex):
    faces = [
        ((x1, 0, 0), (x1, 1, 0), (x1, 0, 1)),
        ((x2, 0, 0), (x2, 0, 1), (x2, 1, 0)),
        ((0, y1, 0), (0, y1, 1), (1, y1, 0)),
        ((0, y2, 0), (1, y2, 0), (0, y2, 1)),
        ((0, 0, z1), (1, 0, z1), (0, 1, z1)),
        ((0, 0, z2), (0, 1, z2), (1, 0, z2)),
    ]
    lines = ["{"]
    for p in faces:
        pts = " ".join("( %d %d %d )" % q for q in p)
        lines.append("%s %s 0 0 0 1 1" % (pts, tex))
    lines.append("}")
    return "\n".join(lines)


def entity(keys, brushes=()):
    lines = ["{"]
    for k, v in keys.items():
        lines.append('"%s" "%s"' % (k, v))
    lines.extend(brushes)
    lines.append("}")
    return "\n".join(lines)


def near_pillar(x, y, margin):
    return any(abs(x - px) < PILLAR + margin and abs(y - py) < PILLAR + margin for px in PILLARS for py in PILLARS)


def monster_spots(count):
    """count spots in the north half, at least 96 units apart and clear of the pillars"""
    spots = []
    y = 1344
    while len(spots) < count and y > 128:
        x = -1344
        while len(spots) < count and x <= 1344:
            if not near_pillar(x, y, 56):
                spots.append((x, y))
            x += 96
        y -= 96
    if len(spots) < count:
        sys.exit("no room for %d monsters" % count)
    return spots


def make_map(count, glass=False):
    h, t = HALF, HEIGHT
    world = [
        box(-h - WALL, -h - WALL, -WALL, h + WALL, h + WALL, 0, "c3_crete1"),			# floor
        box(-h - WALL, -h - WALL, t, h + WALL, h + WALL, t + WALL, "c3_wall6"),		# ceiling
        box(-h - WALL, -h - WALL, 0, -h, h + WALL, t, "c3_wall1"),					# west
        box(h, -h - WALL, 0, h + WALL, h + WALL, t, "c3_wall1"),						# east
        box(-h, -h - WALL, 0, h, -h, t, "c3_wall1"),									# south
        box(-h, h, 0, h, h + WALL, t, "c3_wall1"),									# north
    ]
    for px in PILLARS:
        for py in PILLARS:
            world.append(box(px - PILLAR, py - PILLAR, 0, px + PILLAR, py + PILLAR, t, "c3_metal1"))

    ents = [entity({
        "classname": "worldspawn",
        "wad": WAD.replace("\\", "/"),
        "MaxRange": "8192",
        "message": "ReCoF stress test, %d monsters" % count,
    }, world)]
    ents.append(entity({"classname": "info_player_start", "origin": "0 -1400 36", "angles": "0 90 0"}))
    # weapons and ammunition under the player at the start, picked up on spawn, for the shooting runs
    for i, cls in enumerate(["weapon_m16", "weapon_shotgun", "ammo_m16", "ammo_m16", "ammo_m16", "ammo_shells",
                             "ammo_shells", "ammo_shells"]):
        ents.append(entity({"classname": cls, "origin": "%d -1400 8" % (i * 4 - 14)}))

    for x in range(-h + 64, h - 63, 768):
        for y in range(-h + 64, h - 63, 768):
            ents.append(entity({"classname": "light", "origin": "%d %d %d" % (x + 320, y + 320, t - 24),
                                "_light": "255 240 220 450"}))

    c = -h + NODE_STEP // 2
    for x in range(c, h, NODE_STEP):
        for y in range(c, h, NODE_STEP):
            if not near_pillar(x, y, 40):
                ents.append(entity({"classname": "info_node", "origin": "%d %d 16" % (x, y)}))

    if glass:
        # a solid wall the player sees through across the middle: the monsters never see the player or reach
        # the player, so they stay where they are, idle, and the picture stays the same from run to run
        ents.append(entity({"classname": "func_wall", "rendermode": "2", "renderamt": "70"},
                           [box(-h, -8, 0, h, 8, t, "c3_wall1")]))

    for i, (x, y) in enumerate(monster_spots(count)):
        cls, extra = MONSTERS[i % len(MONSTERS)]
        keys = {"classname": cls, "origin": "%d %d 36" % (x, y), "angles": "0 270 0"}
        keys.update(extra)
        ents.append(entity(keys))

    return "\n".join(ents) + "\n"


def run(exe, args, cwd):
    p = subprocess.run([os.path.join(SDHLT, exe)] + args, cwd=cwd, capture_output=True, text=True,
                       errors="replace")
    if p.returncode != 0:
        print(p.stdout[-3000:])
        print(p.stderr[-3000:])
        sys.exit("%s failed (%d)" % (exe, p.returncode))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--counts", type=int, nargs="+", default=[0, 16, 32, 64])
    ap.add_argument("--install", action="store_true")
    ap.add_argument("--glass", action="store_true",
                    help="rc_stressview_<n>: a wall the player sees through keeps the monsters idle")
    a = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    for n in a.counts:
        name = ("rc_stressview_%d" if a.glass else "rc_stress_%d") % n
        open(os.path.join(OUT, name + ".map"), "w", newline="\n").write(make_map(n, a.glass))
        run("sdHLCSG_x64.exe", ["-nowadtextures", name], OUT)
        run("sdHLBSP_x64.exe", [name], OUT)
        run("sdHLVIS_x64.exe", ["-fast", name], OUT)
        run("sdHLRAD_x64.exe", ["-fast", "-bounce", "1", name], OUT)
        print("built", os.path.join(OUT, name + ".bsp"))
        if a.install:
            dst = os.path.join(ROOT, "dist", "cryoffear", "maps")
            shutil.copy2(os.path.join(OUT, name + ".bsp"), dst)
            print("installed", os.path.join(dst, name + ".bsp"))


if __name__ == "__main__":
    main()
