# ReCoF

Cry of Fear improved version.

ReCoF fixes the bugs of **Cry of Fear** (Steam release, version string `1.6a`) and improves the game, starting from the source of [DeCoF](https://github.com/caixax/DeCoF), the complete matching decompilation of every retail binary: the game DLLs, the engine, the Paranoia renderer, the menus and both launchers.

> [!IMPORTANT]
> ReCoF is work in progress. Test it only in a copy of the game, never in your Steam installation.

> [!TIP]
> The recommended way to install and play ReCoF is the [ReCoF Launcher](https://github.com/caixax/ReCoF-Launcher/releases/latest). It makes the copy of the game for you, checks every file it downloads, keeps ReCoF and itself up to date, and backs up your saves before each update. Installing a release zip by hand still works.

> [!WARNING]
> **Windows 11 and Smart App Control.** ReCoF's files are not digitally signed yet. On a PC where Smart App Control is on, Windows refuses to load them and the game stops with a "Bad Image" error that names `hw.dll` (error status `0xc0e90002`). To play ReCoF, turn it off: open **Windows Security**, go to **App & browser control**, click **Smart App Control settings** and choose **Off**. A shortcut: press Win+R, type `windowsdefender://smartapp/` and press Enter. On many versions of Windows 11 it cannot be turned back on afterwards without resetting Windows; Microsoft Defender keeps protecting the PC either way.
>
> Windows 10 is not affected, nor is Windows 11 when Smart App Control is off or still in evaluation mode. The ReCoF Launcher checks this and warns you before it starts the game.

## Principles

- **DeCoF stays faithful.** DeCoF reproduces retail exactly, bugs included. Fixes and improvements are made here. Corrections to the decompilation itself, such as a wrong name or a real mismatch, go to DeCoF and are merged in.
- **Speedrun techniques stay.** Movement tech, skips and exploits that runners use are not removed unless they crash the game or corrupt saves. When a fix changes something runners rely on, the original behavior stays available behind a cvar.
- **New cvars and commands use the prefix `rc_`.**
- **Retail saves keep loading.** A fix that has to change a save table versions it and still reads the old format.
- **Every change is measured against the baseline.** Before any fix, a ReCoF build is identical to DeCoF's except for build stamps and the name of the build folder, so each fix can be checked to change only the functions it means to change (docs/baseline.md). hl.dll and client.dll are built with a current compiler since Sprint 5, so their code no longer matches retail byte for byte; DeCoF's tools compare a unit with the retail compiler.

## Building

The tools and steps are the same as DeCoF's: see "Building it yourself" in the [DeCoF README](https://github.com/caixax/DeCoF#building-it-yourself) and [docs/tooling.md](docs/tooling.md).

1. Copy your Cry of Fear game folder, the one that holds `cof.exe`, to `versions/1.6a`. The scripts only read it.
2. Install the compilers listed in the DeCoF README, and the Visual Studio 2022 Build Tools (C++ workload, MSVC 14.44, Windows 11 SDK 10.0.26100) for hl.dll and client.dll. The default locations are under `I:\Tools` and the Visual Studio default; the paths are set at the top of `tools/msvc.py`, `tools/msvc1444.py`, `tools/msvc6.py` and `tools/coflaunchapp/build.py`.
3. Put the third-party headers into `refs/`: `halflife`, `a3d-sdk`, `bzip2-1.0.2-dl`, `dxheaders`, `irrklang-1.6.0` and `id3lib-3.8.3`, from the sources in the DeCoF README.
4. Fetch the submodules, discord-rpc and RapidJSON for the Discord status: `git submodule update --init`.
5. Build:

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

## Making a release

Releases are installed and updated by the [ReCoF Launcher](https://github.com/caixax/ReCoF-Launcher), which only accepts the format described in its [docs/releases.md](https://github.com/caixax/ReCoF-Launcher/blob/master/docs/releases.md): `recof-<version>.zip`, `recof-manifest.json` and its Ed25519 signature `recof-manifest.json.sig`. After building every binary as above:

```
python tools/make_release.py --version 0.2.0 --channel beta
```

It stages the binaries in game folder layout with `Play ReCoF.bat` and `tools/dist_extra/recof-cvars.json` (the options the launcher shows; add a new `rc_` cvar there when it should appear in the launcher), and runs `cargo xtask pack-recof` from a ReCoF-Launcher checkout next to this one, which signs the manifest with the launcher key. Both sets carry the rebuilt `CoFLaunchApp.exe`; the script also writes `ReCoF-v<version>.zip`, the same files in one zip for installing by hand. Upload the four files from `build/release/v<version>/upload` to the GitHub release `v<version>`; its body is the changelog the launcher shows. A beta is a GitHub prerelease, which launchers on the stable channel do not see.

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
| [docs/tooling.md](docs/tooling.md) | The tools a build needs and the commands |
| [docs/original_bugs.md](docs/original_bugs.md) | Bugs of the retail game found during the decompilation, the starting point for ReCoF's fixes |
| [docs/bugs.md](docs/bugs.md) | The bug inventory: every known problem, its evidence, severity, runner impact and fix plan |
| [docs/speedrun.md](docs/speedrun.md) | Speedrun techniques ReCoF keeps, and the runner tools that depend on the binaries |
| [docs/baseline.md](docs/baseline.md) | The baseline build every fix is compared with, and how to compare (`tools/compare_build.py`) |
| [docs/feasibility/](docs/feasibility/README.md) | Feasibility studies for the larger projects: GPU and multicore, `rc_hlmovement`, a launcher repository, a server SDK |

The decompilation's own notes (how each compiler was identified, the source workflow, struct layouts, reference repositories and their licenses) and its tools stay in [DeCoF](https://github.com/caixax/DeCoF).

## Credits

- **Team Psykskallar** made Cry of Fear. ReCoF fixes and extends their game and ships none of its assets.
- **Valve** made the GoldSrc engine and the Half-Life SDK that Cry of Fear is built on.
- **BUzer** wrote the Paranoia renderer that Cry of Fear's client and OpenGL wrapper come from.
- **cd_n0** made RebuiltSimon. ReCoF's information panel, HUD numbers, ducktap and autojump aids and other speedrun options are rebuilt from what it shows and does; none of its code is used.
- **YaLTeR** made BunnymodXT, whose practice commands, crosshair, timer and ideas ReCoF follows, and the fork of discord-rpc that ReCoF's Discord status uses.
- **IlyaTsyganov** wrote the Cry of Fear LiveSplit script, whose timing rules ReCoF's run timer follows.
- **Discord** made discord-rpc, and **Tencent and Milo Yip** made RapidJSON; both are MIT and come in as submodules.

## License

See [LICENSE](LICENSE), inherited from DeCoF. The code reconstructed from the Cry of Fear binaries belongs to its original authors, and no rights over it are granted. Code derived from the Half-Life SDK stays under the [Half-Life 1 SDK License](LICENSES/Half-Life-1-SDK.txt), and code derived from Paranoia under Paranoia's terms.

This project is not affiliated with or endorsed by Team Psykskallar or Valve. The repository contains no game assets or retail binaries; you need your own copy of Cry of Fear.
