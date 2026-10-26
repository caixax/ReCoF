# Feasibility: a server SDK for .sma scripts

Study only, nothing is implemented. Written 2026-10-26 against the ReCoF tree (baseline = DeCoF, matching retail 1.6a), the retail files in `versions/1.6a` and a dedicated server test run from a temporary copy.

## Question

Can community servers run AMX Mod X Pawn scripts (`.sma`, compiled to `.amxx`) on Cry of Fear, ideally on a dedicated server? Three paths are compared: (a) Metamod plus AMX Mod X with Cry of Fear game data, (b) ReHLDS adapted to the Cry of Fear engine, (c) a Pawn virtual machine inside the ReCoF hl.dll.

A correction to the starting assessment: `entvars_t` has two extra 4-byte fields, not one. One sits after `light_level` (0x128), the other after `euser4` (0x2A8), so `sizeof(entvars_t)` is 0x2AC and `edict_t` is 0x32C (docs/structs.md:5-28, `src/engine/progdefs.h:114` and `:223`). `usercmd_t` also gained `is_donator`, which moves every `playermove_t` member after `cmd` by 4 bytes (docs/structs.md:83-85), and `clientdata_t` gained `isdonator` (docs/structs.md:87-89).

## What exists today

### Two engines, only one is Cry of Fear's

| | `hw.dll` (game engine) | `swds.dll` (dedicated engine) |
|---|---|---|
| Console banner | `Exe version 1.0.1.4 (cryoffear)`, `Exe build: 22:48:21 Apr 10 2013 (5936)` | `Exe version 1.0.1.4 (valve)`, `Exe build: 13:36:05 Jul 30 2012 (5758)` |
| Origin | Cry of Fear engine (strings `Cry of Fear`, `is_donator`, `isdonator`) | stock Valve build (`C:\buildslave\goldsrc_win32\...` paths, no Cry of Fear string) |
| entvars clear | `push 0x2AC` memset at 4 sites (file 0xA7A23, 0xA7E42, 0xD7C81, 0xE0CF4) | `push 0x2A4` at 1 site (file 0x6BA17): stock layout |
| App id 223710 | present; `cryoffear` maps to 223710 and unknown dirs default to it (`src/engine_hw/sv_main.c:8261-8295`) | absent |
| Default base dir | `cryoffear` (`src/engine_hw/common.c:2561`) | `valve` |
| Dedicated interface | `VENGINE_HLDS_API_VERSION002` (`src/engine_hw/sys_dll2.cpp:762-945`) | same |

`hw.dll` and `sw.dll` are the same file (same MD5). `hlds.exe` (PE date 2008) loads `swds.dll` by name and used `filesystem_stdio.dll` in the test. The launcher starts the game with `-basedir cryoffear -game cryoffear -hw` (`src/coflaunchapp/Form1.cs:207`). Co-op today runs on a listen server hosted from the game. A 2013 Steam guide already reports the "offset for is_donator!" error from the dedicated server tool, and in 2023 players point to a community server browser because the in-game one broke (sources at the end).

### Dedicated server test

Run on 2026-10-26 from `I:\COFPP\tmp\hlds_test` (517 MB): the root exe and the DLLs that `hlds.exe` and `swds.dll` import, `cryoffear/liblist.gam`, `cl_dlls/hl.dll` (the `gamedll` of liblist.gam; Cry of Fear has no `dlls/` folder), `delta.lst`, the cfg files, `models/`, `sprites/`, `events/`, `txtfiles/`, `sound/*.txt`, and the maps `c_start` (single-player), `c_cof_city` and `c_cof_coopend` (co-op). Command: `hlds.exe -console -game cryoffear -insecure +sv_lan 1 +ip 127.0.0.1 +maxplayers 4 -condebug +map <map>`, 12 to 45 seconds per run, crash sites found with cdb. No firewall prompt appeared. No client was connected, so nothing past map load was tested. The folder held only this copy and the server's own output, and was deleted afterwards.

| Run | Result |
|---|---|
| 1. stock `swds.dll` | fatal `W_LoadWadFile: couldn't load gfx.wad`; with `gfx.wad` and `decals.wad` copied into `cryoffear/`, fatal `Couldn't find 'decals.wad' in "DEFAULTGAME" search path`: the stock engine wants a `valve/` base. |
| 2. stock, `-basedir cryoffear` | `Console initialized`, warning `couldn't load fonts.wad`, then fatal **`Couldn't find offset for is_donator!!!`** while parsing `cryoffear/delta.lst:229`. Same on `c_start` and `c_cof_city`: it happens before any map loads. |
| 3. stock, `is_donator` removed from the copy's delta.lst | protocol 48, `Server IP address 127.0.0.1:27015`, hl.dll loads (`SteamAPI_Init() failed; no appID found`, harmless, `src/dlls/game.cpp:487`), then an access violation at EIP 0. The return address is hl.dll 0x10006354, inside `PM_InitTextureTypes` (0x10006200). This fits the shifted `playermove_t`: hl.dll's `pmove->memfgets` reads the engine's `runfuncs`, which is 0 at init (`src/pm_shared/pm_defs.h:205-212`, `pm_shared.c:259`). Same on `c_cof_city`. The entvars mismatch is never reached. |
| 4. Cry of Fear `hw.dll` copied as `swds.dll` | access violation in `Sys_InitGame` (hw.dll 0x01DF47D3): `GL_SetMode( *pmainwindow, ... )` runs in dedicated mode with a NULL window pointer (`src/engine_hw/sys_dll2.cpp:558`; the pointer is set only when not dedicated, `:538-541`). |
| 5. same, that call skipped in the copy | hl.dll loads and `PM_Init` passes, then fatal `Z_Free: freed a pointer without ZONEID`: `SV_SpawnServer` calls `R_ForceCVars` (`sv_main.c:6394`), which sets the renderer cvar `r_dynamic` that a dedicated server never registered (`GL_RMAIN.C:1125-1126`, `GL_RMISC.C:33`). |
| 6. same, that call skipped too | access violation in `GL_LoadTexture2` called from `Mod_LoadTextures` (`GL_MODEL.C:553`, `:647`): the GL model loader uploads the world textures with no GL context. Same on `c_start` and `c_cof_coopend`. Stopped here. |

Result: the shipped `hlds.exe` plus `swds.dll` cannot run Cry of Fear's hl.dll, in single-player maps or co-op, and the Cry of Fear engine cannot run as a dedicated server either. No dedicated server for Cry of Fear exists today. Runs 3, 5 and 6 used patched files in the temporary copy only, to find the next blocker.

### What stock server tools assume

- AMX Mod X core reads entity fields through the stock header: `pPlayer->pEdict->v.health` (amxmodx `amxmodx/amxmodx.cpp:946`); fakemeta builds its `pev` table with `offsetof(entvars_t, ...)` (`modules/fakemeta/pev.cpp:20-36`); edict to index is pointer arithmetic over `edict_t` (`public/HLTypeConversion.h`, `edict_to_id`). On Cry of Fear every field from `sequence` on is 4 bytes off and every index computed that way is wrong (stride 0x32C, not 0x324). Stock binaries cannot work; they must be rebuilt against Cry of Fear headers.
- Ham Sandwich takes vtable indices from per-mod gamedata. Cry of Fear needs its own, generated from the class declarations in `src/dlls` (482 classes keep their 1.55 vtable length, docs/structs.md:81). Some virtuals changed signature: `GiveAmmo` takes four arguments (`ret 0x10`, docs/structs.md:113), so a hook with the stock prototype would unbalance the stack.
- Metamod detects the engine interface version from the table it receives (metamod-hl1 `metamod/meta_eiface.cpp`, `determine_engine_interface_version`, from 138 up to the Steam-era 158 members). hl.dll copies 144 members, the pre-Steam layout (docs/interfaces.md:58), which is an exact prefix of the Steam-era table (compared name by name with `refs/halflife/engine/eiface.h`); the engine's own table in `src/engine_hw/eiface.h` is the Steam-era one. hl.dll has no `GetNewDLLFunctions` (docs/interfaces.md:54), which Metamod treats as optional (unverified on Cry of Fear).
- User message ids are assigned at run time. Metamod records the names as hl.dll registers them (`metamod/engine_api.cpp` `mm_RegUserMsg`, `mutil.cpp` `mutil_GetUserMsgID`), so plugins look ids up by name. What plugins need from ReCoF is the payload format of the 158 messages (`docs/calls/hl_1.6a_usermsgs.csv`), some of which differ from the HLSDK (ScreenFade uses floats, docs/structs.md:30-32).
- The engine accepts `-dll <file>` in place of liblist.gam's `gamedll` (`src/engine_hw/sys_dll.c:1322`), so a server can load Metamod without touching the single-player install. Whether Metamod then finds `cl_dlls/hl.dll` from liblist.gam on its own is unverified.

### Steam

- The Cry of Fear engine reports app id 223710 for `cryoffear` and writes `steam_appid.txt` into the working directory before `SteamGameServer_Init` (`src/engine_hw/sv_steam3.cpp:275-286`). Secure mode is on unless `-insecure` or `sv_lan 1` (`:258`). It also forces `hostname` to "Cry of Fear" (`sv_main.c:6268`). Stock `swds.dll` has no 223710, and ReHLDS returns 70 (Half-Life) for an unknown game dir (ReHLDS `rehlds/engine/sv_main.cpp:8776-8789`).
- Unverified: whether Valve's master servers still list app 223710 servers, whether the 2011 `steam_api_c.dll` game server login still works, and whether a separate dedicated server app id exists. The community browser suggests the in-game listing cannot be relied on.

### Licenses (read in each repository on 2026-10-26)

| Component | License |
|---|---|
| AMX Mod X | GPL v3 or later, with an exception to link with the HL engine and MODs (`public/licenses/LICENSE.txt`); its Pawn 3 machine and compiler are under CompuPhase's zlib-style terms (`ACKNOWLEDGEMENTS.txt`) |
| Metamod (alliedmodders/metamod-hl1, last change 2016) | GPL v2 or later, same linking exception |
| Metamod-R | GPL v3 |
| ReHLDS | MIT since July 2025, GPL v3 before (README, "License Transition") |
| Pawn 4 (compuphase/pawn, archived) | Apache 2.0 plus an exception |

GPL parts stay separate DLLs loaded by the engine or by Metamod, which is the case the exceptions cover. A Pawn machine compiled into hl.dll (option c) would use Pawn 3 or 4 code, not AMX Mod X code.

## Options compared

| | (a) Metamod + AMX Mod X, Cry of Fear build | (b) ReHLDS adapted, (a) on top | (c) Pawn machine in ReCoF hl.dll |
|---|---|---|---|
| Existing .sma | yes, recompiled against Cry of Fear includes; plugins that need CS modules no | yes | no: the AMX Mod X natives would have to be rewritten |
| Server type | listen server only (no dedicated engine exists) | dedicated, Windows; Linux needs hl.dll ported off MSVC, or Wine (unverified) | whatever the engine offers |
| Work | headers (both entvars fields, edict size, usercmd, clientdata); rebuild Metamod and AMX Mod X core, fakemeta, engine, hamsandwich, fun; Ham gamedata; message formats | every Cry of Fear server-side engine change in ReHLDS: `is_donator` in usercmd (struct, delta table `src/engine_hw/delta.c:143`, copied into entvars in `sv_user.c:946` and `:1764`), `isdonator` in clientdata (`delta.c:204`), both entvars fields, 768 model, sound and generic slots (`quakedef.h:52-55`; ReHLDS has 512, `rehlds/common/qlimits.h`), app id, default dir, hostname; then a diff of the remaining `src/engine_hw` server code against ReHLDS | VM, loader, native API, hooks in hl.dll |
| ReCoF binaries touched | none | none (new server binary) | hl.dll |
| Protocol risk | none (Cry of Fear engine) | high: retail clients are build 5936 with Cry of Fear changes, ReHLDS is build 6152/6153 | none |

A fourth way, a dedicated build of ReCoF's own `src/engine_hw`, is protocol-safe by construction, but run 6 shows the GL model loader sits in the server path. It would need a server model loader without GL, as in Valve's swds build, which is what ReHLDS already has. It is the fallback if (b) shows protocol trouble.

## Risks

- **Engine changes not yet found.** Only the changes listed above are known to touch the server. The rest needs a function-by-function diff of the `src/engine_hw` server files against ReHLDS; anything missed is a desync or a crash with retail clients.
- **Protocol.** Whether retail clients accept a ReHLDS-based server is untested.
- **hl.dll assumes a listen server.** `CInterDoorCoop::EndGame` sends `map c_game_menu1` to the first player (`src/dlls/CoF_InterDoorCoop.cpp:527-533`) and `player.cpp:8052-8080` sends `map ...` to clients; on a dedicated server that client would start its own local game. The keypad reads the server machine's keyboard with `GetAsyncKeyState` (B068). The server reads client cvars that do not exist on a dedicated server (docs/interfaces.md:79). `sv_maxupdaterate` defaults to 30 (B244). These are ReCoF fixes, behind `rc_` cvars where runners care.
- **Ham prototypes.** Hooks on changed virtuals corrupt the stack unless the Cry of Fear gamedata gives the right signatures.
- **Runners (docs/speedrun.md).** The SDK must be inert in single-player: load Metamod with `-dll` on servers only, never by editing liblist.gam, so the LiveSplit offsets in hw.dll and client.dll, the `Host_ValidSave` pattern of BunnymodXT and RebuiltSimon's hl.dll patch stay as they are. Dedicated-mode changes go into a separate server binary, not into the client hw.dll whose data the autosplitter reads. Option (c) changes hl.dll and moves RebuiltSimon's offsets (true of any ReCoF hl.dll already). A server with plugins should say so (version string or an `rc_` cvar) so runs and their recordings can be checked.
- **Maintenance.** Three forks (Metamod, AMX Mod X, ReHLDS) to keep in step with upstream.
- **Licenses.** Fine while GPL components stay separate DLLs.

## Verdict

- **Possible:** yes. On listen servers with path (a) and no engine work. On dedicated servers only once a Cry of Fear dedicated engine exists, which is new work: retail `hlds.exe` plus `swds.dll` stops at `Couldn't find offset for is_donator!!!`, and the Cry of Fear engine itself cannot run dedicated.
- **Effort: large.** No dedicated engine exists; ReHLDS must reproduce every Cry of Fear server-side engine change and its wire protocol; Metamod and AMX Mod X must be rebuilt against Cry of Fear headers with new gamedata; hl.dll has listen-server assumptions to fix; Linux also needs hl.dll ported off MSVC. Path (a) alone, listen servers only, is medium.
- **Main risks:** protocol compatibility between a ReHLDS-based server and retail clients; engine changes not yet found; listen-server assumptions in hl.dll; Ham prototypes; keeping single-player and the runners' tools untouched.
- **Recommended path:** (a) first on listen servers, to prove the headers and gamedata; then (b), ReHLDS adapted for dedicated servers, with (a) on top through Metamod or Metamod-R. Not (c): it drops compatibility with existing .sma scripts and makes ReCoF own a scripting API.
- **First three steps:**
  1. Write a Cry of Fear header set for third-party builds (`progdefs.h` with both entvars fields, `usercmd.h`, `entity_state.h`, edict size) and generate the Ham Sandwich gamedata from the `src/dlls` class declarations and the message format table from `docs/calls/hl_1.6a_usermsgs.csv`.
  2. Build Metamod and AMX Mod X (core, fakemeta, engine, hamsandwich) against those headers. On a copy of the game, start a co-op listen server with `-dll` pointing at Metamod, load a test plugin that reads `pev` fields, player indices and a Ham TakeDamage hook, connect a second retail client on LAN, and compare every value with hl.dll.
  3. Fork ReHLDS and add the known Cry of Fear engine changes (`is_donator` in usercmd and its delta, `isdonator` in clientdata, both entvars fields, 768 slots, app id 223710 and default dir), rerun this study's test on `c_start`, `c_cof_city` and `c_cof_coopend`, and connect a retail client. In parallel, diff the `src/engine_hw` server code against ReHLDS for the remaining changes.

Sources: Steam guide of 25 Apr 2013, https://steamcommunity.com/app/223710/discussions/0/828937420112841213; Steam thread of May 2023 (community browser at https://www.moddb.com/games/cry-of-fear/addons/server-browser-for-cry-of-fear), https://steamcommunity.com/app/223710/discussions/0/3826414483642588995; repositories github.com/alliedmodders/amxmodx, github.com/alliedmodders/metamod-hl1, github.com/rehlds/ReHLDS, github.com/rehlds/Metamod-R, github.com/compuphase/pawn.
