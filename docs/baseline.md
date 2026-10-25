# Baseline

The baseline is the ReCoF build of the unchanged DeCoF source. Every fix is compared with it, so each fix can be shown to change only what it means to change.

## Result

Proven on 2026-10-25 at commit b9ea143, which holds DeCoF master 15ecd44 with only `README.md` changed. All seven binaries were built in a fresh clone at `I:\COFPP\ReCoF` with DeCoF's compilers and compared with DeCoF's own build by `tools/compare_build.py`, which ignores the fields that change on every build.

| Binary | Against DeCoF's build |
|---|---|
| `hl.dll` | identical except build stamps |
| `opengl32.dll` | identical except build stamps |
| `hw.dll`, `GameUI.dll`, `cof.exe` | identical except build stamps and the PDB path at the end of the file, which holds the build folder |
| `client.dll` | 86 bytes differ, all in the RTTI names of 11 classes in anonymous namespaces (see below) |
| `CoFLaunchApp.exe` | the MVID, the debug stamps and the PDB path differ; the path is 9 characters shorter here, which moves the bytes after it |

Both remaining differences come from the build folder, not from the source:

- MSVC names an anonymous namespace `?A0x<hash>` with a hash of the source file's full path, and DeCoF was built in `I:\COFPP\cof-decompiled`. A test file compiled from two folders gets two different names, and the same name again when compiled twice in one folder.
- `tools/coflaunchapp/compare.py` finds all 51 methods, the types, the manifest and every resource of the ReCoF `CoFLaunchApp.exe` identical to retail. Built with `--retail-path`, the only bytes that differ from retail are the per-build fields.

A second build in the same folder is identical to the first: `tools/compare_build.py` prints SAME for all seven.

`make_dist.py` assembles `dist/` from the seven binaries. `sw.dll` stays the retail file there, as in DeCoF; a fix to `hw.dll` that should also reach the software renderer needs `sw.dll` handled too.

The first fresh-clone build found one tool bug: `build.py` wrote a file into `build/jobs` before creating the folder, so `link hw` failed in a new clone. It was fixed in DeCoF (15ecd44) and merged.

## Using the baseline

`build/baseline` is not committed. To make it, build all seven binaries from the baseline commit and copy `build/bin` to `build/baseline`. After a change:

```
python tools/build.py link <dll>
python tools/compare_build.py
```

`compare_build.py` prints SAME for an unchanged binary and the differing byte runs, with the nearest symbol from the link map, for a changed one. Two builds are only comparable when both were made in the same folder, because of the PDB paths and the anonymous namespace names.
