# Feasibility studies

One page per idea for ReCoF's larger projects. Each page checks the idea against the source and ends with a verdict: possible or not, effort, risks, the recommended path and the first steps. No code was written for them.

| Study | Possible | Effort | Recommended path |
|---|---|---|---|
| [cpu_gpu.md](cpu_gpu.md): use the GPU and more CPU cores | yes | large overall; the cheap engine items are small | Measure first (retail switches, then an `rc_` frame profiler), fix the cheap items (B254, B255, B252, B194, `/O2` for the engine's render and sound units), move models and then the world to the GPU in client.dll with the old path as fallback, add worker threads last; game logic stays on one thread. |
| [hlmovement.md](hlmovement.md): `rc_hlmovement`, real Half-Life 1 movement | yes | medium | One server cvar latched at spawn and restore, sent to client prediction through one physinfo key, shown on the HUD and readable by tools; no save field; steam_legacy Half-Life as the reference. |
| [launcher.md](launcher.md): a separate launcher repository | yes | medium | A separate repository, C# WinForms on .NET Framework 4.8, side-by-side install only in version 1 (the Steam folder is never written), signed manifest with a hash per file; installing over the Steam folder later as an opt-in. |
| [server_sdk.md](server_sdk.md): a server SDK for `.sma` scripts | yes, dedicated servers only after new engine work | large; listen servers alone medium | Metamod plus AMX Mod X with Cry of Fear game data on listen servers first, then a ReHLDS fork adapted to the Cry of Fear engine for dedicated servers; no Pawn machine inside hl.dll. |

## Findings that change other work

- The retail dedicated server does not run Cry of Fear: `hlds.exe` with `swds.dll` stops at `Couldn't find offset for is_donator!!!`, and the Cry of Fear hw.dll crashes when started as a dedicated engine (server_sdk.md).
- MetaRenderer refuses to load with Cry of Fear since 2026-10-10, so it is a reference only (cpu_gpu.md, bugs.md B286).
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
