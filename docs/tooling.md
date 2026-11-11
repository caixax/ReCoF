# Tooling

What a ReCoF build needs, and the commands. The decompilation's tools (the Ghidra project, objdiff, the matching workflow and its notes) stay in [DeCoF](https://github.com/caixax/DeCoF).

## Tools

They live outside the repository. The paths are the defaults the scripts use.

| Tool | Location | Builds |
|---|---|---|
| Visual Studio 2022 Build Tools, MSVC 14.44, Windows SDK 10.0.26100.0 | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` | hl.dll, client.dll (`tools/msvc1444.py`) |
| cl.exe and link.exe 14.27.29112 | `I:\Tools\msvc-14.27.29112` | import libraries and library members (`tools/implib.py`, `tools/libmember.py`) |
| Visual C++ 6.0, and 6.0 SP6 with the Processor Pack | `I:\Tools\vc6\VC98`, `I:\Tools\vc6pp\VC98` | hw.dll, opengl32.dll, GameUI.dll, cof.exe (`tools/msvc6.py`) |
| csc.exe 4.0.30319.1 (.NET Framework 4.0) | `I:\Tools\csc40` | CoFLaunchApp.exe |
| Python 3 with pefile and capstone | system install | the scripts |

The retail game goes in `versions/1.6a` (read only, never committed): the links compare against it, and hw.dll takes the third-party code that has no source from it.

## Commands

```
python tools/build.py base [unit ...]       compile src/ units into build/base/
python tools/build.py link <dll>            link a DLL into build/bin/ (hl, client, hw, opengl32, gameui, launcher)
python tools/coflaunchapp/build.py          build CoFLaunchApp.exe into build/coflaunchapp/
python tools/check_runner.py                LiveSplit and BunnymodXT addresses kept, hw.dll room left
python tools/compare_build.py               compare build/bin with the baseline (docs/baseline.md)
python tools/name_addr.py hw.dll <offset>   name an address from a crash report
python tools/make_dist.py --keep --bin build/bin    a playable copy in dist/
python tools/make_release.py --version X.Y.Z [--channel stable]    the release files for GitHub and the launcher
```

## Notes

- `tools/units.json` lists every unit, its flags and each DLL's link: the order, the libraries and the import libraries made from retail third-party DLLs.
- hw.dll links third-party code that has no source and no official library (the Aureal A3D library, the IGA client, the Miles helper, the DirectX manager of the OpenGL wrapper) from the retail bytes: `tools/delink_retail.py` writes those objects at link time from `versions/`, named from `tools/symbols/hw_*.csv`. Nothing retail-derived is committed.
- VC6 numbers anonymous namespaces at random; `tools/vc6_anon.py` gives them the retail names after each compile, so the objects are reproducible.
- hw.dll must keep the addresses the speedrun tools read: no new globals or statics, new strings as `static const char` arrays, and `tools/check_runner.py` after every hw.dll link (docs/speedrun.md).
