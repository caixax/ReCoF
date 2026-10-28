# Feasibility studies

One page per idea for ReCoF's larger projects. Each page checks the idea against the source and ends with a verdict: possible or not, effort, risks, the recommended path and the first steps. No code was written for them.

| Study | Possible | Effort | Recommended path |
|---|---|---|---|
| [cpu_gpu.md](cpu_gpu.md): use the GPU and more CPU cores | yes | large overall; the cheap engine items are small | Measure first (retail switches, then an `rc_` frame profiler), fix the cheap items (B254, B255, B252, B194, `/O2` for the engine's render and sound units), move models and then the world to the GPU in client.dll with the old path as fallback, add worker threads last; game logic stays on one thread. |
| [multicore.md](multicore.md): more CPU cores in the frame | possible, but measured not worth it | n/a for the frame; loading workers medium | Measured on 2026-10-28: the sound mixer is 0.00 ms in 1068 frames, the server 0.01 ms, and the model path lost half its cost to a driver change, so what is left of the frame is GL submission that cannot leave the context thread. Build no job system. Fix the redundant GL state (B292, then B291, B293, B295) and batch the world (B195); keep threads for loading only. |
| [hlmovement.md](hlmovement.md): `rc_hlmovement`, real Half-Life 1 movement | yes | medium | One server cvar latched at spawn and restore, sent to client prediction through one physinfo key, shown on the HUD and readable by tools; no save field; steam_legacy Half-Life as the reference. |
| [launcher.md](launcher.md): a separate launcher repository | yes | medium | A separate repository, C# WinForms on .NET Framework 4.8, side-by-side install only in version 1 (the Steam folder is never written), signed manifest with a hash per file; installing over the Steam folder later as an opt-in. |
| [server_sdk.md](server_sdk.md): a server SDK for `.sma` scripts | yes, dedicated servers only after new engine work | large; listen servers alone medium | Metamod plus AMX Mod X with Cry of Fear game data on listen servers first, then a ReHLDS fork adapted to the Cry of Fear engine for dedicated servers; no Pawn machine inside hl.dll. |

## Findings that change other work

- The retail dedicated server does not run Cry of Fear: `hlds.exe` with `swds.dll` stops at `Couldn't find offset for is_donator!!!`, and the Cry of Fear hw.dll crashes when started as a dedicated engine (server_sdk.md).
- MetaRenderer refuses to load with Cry of Fear since 2026-10-10, so it is a reference only (cpu_gpu.md, bugs.md B286).
- No GoldSrc engine threads its frame: not Valve's Half-Life 25th Anniversary Update of November 2023, and not Xash3D FWGS, whose only thread creation is in `engine/common/net_ws.c`; MetaRenderer threads resource building alone (multicore.md).
- On a hybrid graphics laptop the game starts on the integrated GPU, because cof.exe asks no driver for the discrete one (bugs.md B296). Every measurement has to say which adapter it was taken on.
- Measured in `c_doc_city` on 2026-10-28 at 1920x1080 on a discrete GPU: 7.00 ms a frame against 2.98 ms of GPU work, about 9000 world draw calls, the server and the sound mixer at zero, and one dynamic light costing 1.84 ms. The frame is GL submission, so more cores buy nothing and fewer draw calls buy everything (multicore.md).
- 187 of hw.dll's 197 build units are compiled with `/Od` (only bzip2 and vgui_surfacelib use `/O2`) and still match retail, so the retail engine is unoptimized (cpu_gpu.md).
- Cry of Fear's own model path in client.dll, not the engine's, draws models by default (bugs.md B287).
- A Steam install can already differ from retail: the maintainer's `cof.exe` carries the large address flag (bugs.md B277), so install checks must accept known variants (launcher.md).
- Public releases need their legal status settled first (Valve engine code, GPL code in client.dll), and the rebuilt hw.dll and cof.exe still carry retail icons and version information (launcher.md).

## Suggested order

1. The cheap CPU and GPU items go into the fix sprints, after the profiler.
2. `rc_hlmovement`, medium and self-contained, as the first big project once hl.dll and client.dll are on the current compiler.
3. The GPU model and world paths in the renderer sprint.
4. The launcher, when public releases are possible.
5. The server SDK, largest and last: listen servers first, dedicated servers after.
