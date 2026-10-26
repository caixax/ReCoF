# ReCoF

Cry of Fear improved version.

ReCoF fixes the bugs of **Cry of Fear** (Steam release, version string `1.6a`) and improves the game, starting from the source of [DeCoF](https://github.com/caixax/DeCoF), the complete matching decompilation of every retail binary: the game DLLs, the engine, the Paranoia renderer, the menus and both launchers.

> [!IMPORTANT]
> ReCoF is work in progress. Test it only in a copy of the game, never in your Steam installation.

## Principles

- **DeCoF stays faithful.** DeCoF reproduces retail exactly, bugs included. Fixes and improvements are made here. Corrections to the decompilation itself, such as a wrong name or a real mismatch, go to DeCoF and are merged in.
- **Speedrun techniques stay.** Movement tech, skips and exploits that runners use are not removed unless they crash the game or corrupt saves. When a fix changes something runners rely on, the original behavior stays available behind a cvar.
- **New cvars and commands use the prefix `rc_`.**
- **Retail saves keep loading.** A fix that has to change a save table versions it and still reads the old format.
- **Every change is measured against the baseline.** Before any fix, a ReCoF build is identical to DeCoF's except for build stamps and the name of the build folder, so each fix can be checked to change only the functions it means to change. hl.dll and client.dll are built with a current compiler since Sprint 5, so for them `python tools/build.py --retail-cc check <unit>` does that check with the retail compiler (docs/toolchains.md).

## Building

The tools and steps are the same as DeCoF's: see "Building it yourself" in the [DeCoF README](https://github.com/caixax/DeCoF#building-it-yourself) and [docs/tooling.md](docs/tooling.md).

1. Copy your Cry of Fear game folder, the one that holds `cof.exe`, to `versions/1.6a`. The scripts only read it.
2. Install the compilers listed in the DeCoF README, and the Visual Studio 2022 Build Tools (C++ workload, MSVC 14.44, Windows 11 SDK 10.0.26100) for hl.dll and client.dll. The default locations are under `I:\Tools` and the Visual Studio default; the paths are set at the top of `tools/msvc.py`, `tools/msvc1444.py`, `tools/msvc6.py` and `tools/coflaunchapp/build.py`.
3. Put the third-party headers into `refs/`: `halflife`, `a3d-sdk`, `bzip2-1.0.2-dl`, `dxheaders`, `irrklang-1.6.0` and `id3lib-3.8.3`, from the sources in the DeCoF README.
4. Build:

```
python tools/build.py link hl
python tools/build.py link client
python tools/build.py link hw
python tools/build.py link opengl32
python tools/build.py link gameui
python tools/build.py link launcher
python tools/coflaunchapp/build.py
python tools/make_dist.py --bin build/bin
```

The binaries land in `build/bin`, and `dist/` becomes a copy of the game that runs them.

## Merging DeCoF corrections

ReCoF keeps DeCoF's history, so later corrections to the decompilation merge in directly:

```
git remote add decof https://github.com/caixax/DeCoF.git
git fetch decof
git merge decof/master
```

## Documentation

| File | What it covers |
|---|---|
| [docs/tooling.md](docs/tooling.md) | Tool versions and sources, the build and link commands per binary |
| [docs/toolchains.md](docs/toolchains.md) | How each compiler was identified |
| [docs/howto_cof_source.md](docs/howto_cof_source.md) | The workflow for a source file and the compiler patterns that decide a match |
| [docs/structs.md](docs/structs.md) | Struct and class layouts that differ from the Half-Life SDK |
| [docs/original_bugs.md](docs/original_bugs.md) | Bugs of the retail game found during the decompilation, the starting point for ReCoF's fixes |
| [docs/refs.md](docs/refs.md) | Reference repositories, what each provides and its license |
| [docs/bugs.md](docs/bugs.md) | The bug inventory: every known problem, its evidence, severity, runner impact and fix plan |
| [docs/speedrun.md](docs/speedrun.md) | Speedrun techniques ReCoF keeps, and the runner tools that depend on the binaries |
| [docs/baseline.md](docs/baseline.md) | The baseline build every fix is compared with, and how to compare (`tools/compare_build.py`) |
| [docs/feasibility/](docs/feasibility/README.md) | Feasibility studies for the larger projects: GPU and multicore, `rc_hlmovement`, a launcher repository, a server SDK |

## License

See [LICENSE](LICENSE), inherited from DeCoF. The code reconstructed from the Cry of Fear binaries belongs to its original authors, and no rights over it are granted. Code derived from the Half-Life SDK stays under the [Half-Life 1 SDK License](LICENSES/Half-Life-1-SDK.txt), and code derived from Paranoia under Paranoia's terms.

This project is not affiliated with or endorsed by Team Psykskallar or Valve. The repository contains no game assets or retail binaries; you need your own copy of Cry of Fear.
