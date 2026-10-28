# Speedrunning and ReCoF

ReCoF must not take away what runners use, and must not break the tools they run. This page lists both. A fix that touches anything here keeps the retail behavior available behind an `rc_` cvar, and its row in [bugs.md](bugs.md) says so in the Runners column.

The sources are the speedrun.com guides, forums, rules and resources for Cry of Fear, the LiveSplit autosplitter, the RebuiltSimon source and the Steam discussions, collected on 2026-10-26.

## Techniques to keep

- **fps_max binds.** The sewer wall skip after the mace boss binds fps_max 4 before touching the event trigger and fps_max 100 after crossing the wall, and low frame rates push the player past some barriers. Keep fps_max down to at least 4, the halving of commands longer than 50 ms in `SV_RunCmd`, and per-frame brush and trigger physics. Sources: https://www.speedrun.com/cof/forums/1nc48, https://www.speedrun.com/posts/2e4i5
- **The 100 fps cap.** Runs are played at fps_max 100 at most, and `developer 1` lifts the cap without counting as cheating. Keep 100 as the default cap; a higher cap is opt-in. Sources: https://www.speedrun.com/cof/guides/frecp, https://steamcommunity.com/app/223710/discussions/0/2243300286291995391/
- **Console commands in the speedrun guide:** `clockwindow 0`, `cl_fastcredits 1`, `cl_fovmultiplier` from 0 to 2 (1.3 to 1.5 in use), `cl_showfps 1`, `developer 1`, `host_framerate` within the rules, binds to `inventoryuse 1` to `6`, `inventorydrop 1` to `6` and `padlockrotate 1` to `4`, `map c_nightmare;difficulty 1` and `map c_doc_city`. Keep every one with its meaning. Source: https://www.speedrun.com/cof/guides/frecp
- **Bunny hopping under the Half-Life cap.** A jump above 1.7 times maxspeed sets the speed to 0.65 of that, about 331 units. Dodge jumps, crouch jumps and air strafing are used, the maze skip is pure movement, and a "Bhopcap" category extension exists. Keep `PM_PreventMegaBunnyJumping`, the dodge and its timing. Sources: https://www.speedrun.com/cof/forums/rfwpm, https://docs.google.com/document/d/1kfr3qTK1xn6Zt5w7Rn3adUwcBadBsfs1kjdxodtQtto
- **Using objects through walls.** `PlayerUse` searches a sphere of 80 units with no line of sight test. It is allowed even in All Key Items + NMG. Source: https://www.speedrun.com/api/v1/categories/zdn0poe2
- **Using NPCs to unlock or trigger entities.** Banned only in All Key Items + NMG, so it is used elsewhere. Source: same rules page.
- **Water Glitch and Lake Skip.** The door and pipe setups of the Water Glitch, and the Lake Skip from c_lake straight to c_forestday, are used in New Game and New Game+ and banned in All Key Items + NMG. Keep door, pipe and water collision as they are. Sources: https://www.speedrun.com/cof/guides/va9ss, https://www.speedrun.com/cof/resources, https://github.com/IlyaTsyganov/Livesplit.CryofFear
- **Phone skip in chapter 5.** Climbing the sides of the study room skips two of the three phone numbers. Source: https://www.speedrun.com/ru-RU/users/A2ombie/threads
- **Typing the chapter 1 computer login while paused** is allowed, so the computer panel must keep accepting input while paused.
- **Early doors and the camera.** Entering doors as early as possible, early door use and the camera freezing enemies are part of routes. Source: m3Lith's notes above.
- **Practice saves.** Practice saves are copied into `SAVE` as `.sav` files plus `saveinfo*.cof`. Keep the save format and the folder. Sources: https://www.speedrun.com/cof/resources, https://steamcommunity.com/app/223710/discussions/0/622954302090716968

## Tools that read or patch the game

These tools work on the retail binaries by address or by byte pattern. Any ReCoF build that moves what they look for breaks them, sometimes by crashing the game instead of failing cleanly.

- **LiveSplit autosplitter and load remover** (by DrLa, listed for "Cry of Fear" and "Cry of Fear Category Extensions"). It reads fixed offsets in the process `cof`: hw.dll +0x11119E4 pause state, +0x10D279C loading state, +0xF1AA15 map name, +0x820495 menu map, +0x72F11B ambient, +0x377BD8 co-op start, +0x116BED0 health, +0x9FDC30 can move, +0x1F39F0 alive, +0x1F39C8 demo; client.dll +0x18ED64 cutscene state, +0x1BE920 in-game time, +0x5440C8 music, +0x18A3E0 save state; crashhandler.dll +0x5600C crash state. Source: https://raw.githubusercontent.com/IlyaTsyganov/Livesplit.CryofFear/main/cryoffear.asl
- **BunnymodXT** finds `Host_ValidSave` by the byte pattern `55 8B EC 83 3D ? ? ? ? ? 74 ? 33 C0 E9` to detect the Steam version, and crashes the whole game when the pattern is missing. Source: the comment in RebuiltSimon's Host_ValidSave hook (`I:\COFPP\cof-rs`).
- **RebuiltSimon** writes 19 NOPs at hl.dll +0xEB203 (the noclip reset in `CBasePlayer::PostThink`), stores into `CBasePlayer` at +0x2038, +0x21E8, +0x21F0 and +0x24F8, and uses hw.dll offsets 0x813C8, 0x8123B, 0x812B8, 0x7E0544 and 0x11115AC. Runners use its commands `rs_info`, `rs_hud`, `rs_bhop_cap`, `rs_disable_stamina`, `rs_disable_duck_time`, `rs_autojump`, `rs_ducktap_priority` and `+rs_ducktap`; `+rs_left`, `+rs_right` with `rs_yawspeed` and `rs_cof_save_prevention` are banned. Sources: `I:\COFPP\cof-rs`, https://www.speedrun.com/cof/guides
- **CoF Linux Patcher** replaces cof.exe and patches hw.dll, client.dll and opengl32.dll at fixed offsets for Proton. Source: https://github.com/hinqiwame/cof-linux-patcher

## Rules for ReCoF builds

- **Data addresses.** The autosplitter reads data in hw.dll and client.dll. In hw.dll a fix that adds or resizes a global, a cvar or a string can move every later variable. Before each test build, run `python tools/check_runner.py`: it checks that the variable at each hw.dll autosplitter offset is still the same one as in retail, using the link maps. client.dll is built with a newer compiler (docs/toolchains.md), so its four values sit elsewhere and move from build to build; scripts find them through the block below, and check_runner reports them as BLOCK while the block is in the map.
- **Room in hw.dll.** hw.dll keeps its uninitialized globals (where most autosplitter values live) at the end of `.data`, so any new initialized global moves them, and code or strings that outgrow the room before the next section move everything after it. At the baseline hw.dll has 2266 bytes of room in `.text` and 2308 in `.rdata`; `check_runner.py` prints what is left. New hw.dll variables go into a ReCoF source file linked after all retail objects, uninitialized (cvars filled in at registration), and never as new members of a struct the autosplitter reads into (`client_state_t`, `server_t`). hw.dll is built without string pooling (VC6), so even a new string literal lands in `.data` and moves them; put new strings in `static const char` arrays, which go to `.rdata`. client.dll has no such limit any more, but the members the block points at (`gHUD.m_BlackBar_guess.m_iOn_guess`, `gHUD.m_flSaveHackTime_guess`, `decaltextures`, the `READ_STRING` buffer) must keep their meaning.
- **`Host_ValidSave`.** Keep it byte-identical. New save logic goes into a separate function that it, or its callers, call.
- **Code patched by tools.** RebuiltSimon's NOP patch and member offsets only fit retail; in ReCoF's hl.dll, built with a newer compiler, the patch site at +0xEB203 holds other code, so RebuiltSimon must not be loaded with ReCoF. ReCoF should offer its runner features natively (bhop cap, ducktap, infinite stamina, noclip and god with sv_cheats, raw input) and report its own version, so tools can tell ReCoF apart from retail and refuse to patch it.
- **Linux.** The Linux patcher will not apply to rebuilt binaries. The opengl32.dll fix in bugs.md removes the main reason Proton needs it.

## ReCoF autosplitter block

ReCoF builds client.dll with a newer compiler than retail (docs/toolchains.md), so the four client.dll values the LiveSplit script reads are at other addresses and can move from one ReCoF build to the next. client.dll therefore holds a block found by its tag, not by its address (`g_RcAutosplit` in `src/cl_dll/rc_client.cpp`, filled when the client starts). hw.dll keeps all its retail addresses, so the script's hw.dll lines stay as they are.

| Offset | Field | What it points to |
|---|---|---|
| 0 | tag | the 16 bytes `ReCoF_AUTOSPLIT` and a zero byte |
| 16 | version | 1 (an int) |
| 20 | cutscene | 4 bytes, retail client.dll +0x18ED64 (`gHUD.m_BlackBar_guess.m_iOn_guess`, nonzero while the cutscene bars show) |
| 24 | igt | 4 bytes, retail client.dll +0x1BE920 (`decaltextures[0]` + 8, nonzero once a map's decals are loaded) |
| 28 | music | 40 bytes, retail client.dll +0x5440C8 (`READ_STRING`'s buffer + 0x58) |
| 32 | savestate | 4 bytes, retail client.dll +0x18A3E0 (`gHUD.m_flSaveHackTime_guess`, nonzero while a tape recorder save is pending) |

The fields at 20 to 32 are 32-bit pointers to the bytes, which stay at the same place for the life of the process. The script below adds ReCoF support to the existing `cryoffear.asl` without changing its retail behavior: it looks for the tag in client.dll a few times after the game starts, and when it finds one it reads the four values through the block instead of the fixed addresses. The original script has no license, so ReCoF ships only this addition.

```csharp
// in init { }, before the existing lines
vars.recofBlock = IntPtr.Zero;
vars.recofTries = 0;
vars.recofNextScan = 0;

// a new update block (or the start of an existing one)
update
{
    if (vars.recofBlock == IntPtr.Zero && vars.recofTries < 20 && Environment.TickCount > vars.recofNextScan)
    {
        vars.recofTries++;
        vars.recofNextScan = Environment.TickCount + 1000;
        var client = modules.FirstOrDefault(m => m.ModuleName.ToLower() == "client.dll");
        if (client != null)
        {
            var scanner = new SignatureScanner(game, client.BaseAddress, client.ModuleMemorySize);
            vars.recofBlock = scanner.Scan(new SigScanTarget(0, "52 65 43 6F 46 5F 41 55 54 4F 53 50 4C 49 54 00 01 00 00 00"));
        }
    }
    if (vars.recofBlock != IntPtr.Zero)
    {
        IntPtr b = vars.recofBlock;
        current.cutscenestate = game.ReadValue<float>((IntPtr)game.ReadValue<int>(b + 20));
        current.igt = game.ReadValue<float>((IntPtr)game.ReadValue<int>(b + 24));
        current.music = game.ReadString((IntPtr)game.ReadValue<int>(b + 28), 40);
        current.savestate = game.ReadValue<float>((IntPtr)game.ReadValue<int>(b + 32));
    }
}
```

## ReCoF cvars

- **`rc_retail_gameplay`** (server, default 0). ReCoF fixes balance bugs by default: damage, health, AI and collision behave as the developers wrote them to. Set it to 1 to get the retail behavior of every such fix back at once. It covers: B047 (saw crazy hit timer), B052 (crazy bitch weapon cases), B062 (co-op door moves the monsters near its exit back to their spawn), B096 (ladder climb invulnerability), B025 (Twitcher 4 uses its own skill values), B026 (the Faceless use their own skill values), B051 (suicider head explosion centred on the head), B085 (monster eye height), B117 (monster turn speed fixed at the 100 fps rate), B118 (point sized monsters cannot step through thin walls and closed doors), B119 (scripted sequence ends do not move monsters through walls).
- **`rc_bhopcap`** (server, default 1 as in retail). 0 removes the bunny hop speed cap (B131). The server passes it to client prediction as the physinfo key `nbh`, so both sides agree; a separate category if used.
- **`rc_ducktap`** (server, default 0 as in retail). 1 brings back the Half-Life ducktap that Cry of Fear removed (B132), as RebuiltSimon's ducktap option does. Passed to client prediction as the physinfo key `dkt`.
- **`rc_infinite_stamina`** (server, default 0 as in retail). 1 gives infinite stamina (B094), what RebuiltSimon's `rs_disable_stamina` does by writing memory. The stamina bar still shows.
- **`god`, `noclip`, `notarget`, `fly`** (engine commands, need `sv_cheats 1`). Registered again, and PostThink keeps noclip and god mode while `sv_cheats` is 1 (B095), what RebuiltSimon gets by patching hl.dll. With `sv_cheats 0` they do nothing, as in retail. They do nothing in co-op (multiplayer runs with deathmatch set).
- **`rc_hlmovement`** (server, default 0 as in retail). 1 gives Half-Life 1 movement as the WON version played it: the steam_legacy pm_shared physics (docs/feasibility/hlmovement.md), run 320 (`sv_maxspeed` is set to 320, and back to 300 when it is turned off), no sprint, jumping costs no stamina, no speed percent, limp or minimum speed, no diagonal halving, swim speed x 0.8, duck time 0.4, ladders at 200, native ducktap, the `+use` slowdown and move speeds of 400. The sprint key does nothing (it is not sent, and the server strips it), there is no dodge, the Cry of Fear movement camera punches are off (jump, landing, step and ladder bob; weapon recoil and scripted punches stay), and a hard landing only takes the fall damage, without the Cry of Fear fall over. Turning it on also sets `rc_bhopcap 0` (WON had no bunny hop cap), `rc_ducktap 1` and `rc_autojump 1`, and turning it off sets them back to 1, 0 and 0; each can be changed by hand afterwards. A ground prestrafe reaches about 470 to 500, as in every Half-Life version, and the engine keeps the 100 fps limit WON had unless developer is on. The Cry of Fear state locks and footsteps stay. It applies at once and reaches client prediction through the physinfo key `hlm`; the HUD shows "HL movement" in the top left corner while it is on. A separate category.
- **`+rc_ducktap`** (client command), **`rc_autojump`** (client, default 0, not saved in config.cfg), **`rc_ducktap_priority`** (client, default 1), **`rc_jump_priority`** (client, default 0). The input aids of RebuiltSimon (`+rs_ducktap`, `rs_autojump`, `rs_ducktap_priority`), built in. While `+rc_ducktap` is held on the ground, duck is pressed every other frame; it only lifts the player where ducktap exists (`rc_ducktap 1` or `rc_hlmovement 1`). `rc_autojump 1` releases jump in the air, so holding jump jumps again on landing. With `rc_ducktap_priority 1`, holding `+rc_ducktap` pauses autojump; with `rc_jump_priority 1`, holding jump pauses `+rc_ducktap`.
- **`rc_speedometer`** (client, default 0), **`rc_speedometer_jump`** (1), **`rc_speedometer_3d`** (0), **`rc_speedometer_pos`** (".5 .9"), **`rc_speedometer_color`** ("255 255 255"). The speedometer of RebuiltSimon's HUD, built in: the horizontal speed (3D with `rc_speedometer_3d 1`) from client prediction, and below it the speed at the last takeoff, at a position given as fractions of the screen.
- **`rc_rawinput`** (client, default 1). Raw mouse input (B184): the mouse counts from `WM_INPUT` replace the cursor movement, so Windows acceleration no longer applies, and the game stops forcing "enhance pointer precision" while it has focus. `m_filter` and the sensitivity work as before. 0 gives the retail mouse (cursor movement with forced acceleration). RebuiltSimon's `rs_rawinput` does the same from outside.
- **`rc_prof`** (client, default 0) and **`rc_prof_dump`**. Frame profiler (B286): 1 shows top left how many milliseconds of CPU time each phase of the frame takes (view, entities, world passes, dynamic lights, sky, models, transparent effects, water, post effects, HUD, and "other" for the engine, the server game logic, sound and the buffer swap), averaged over half a second, with the frame time, the worst frame and the number of models and dynamic lights drawn. 2 adds the GPU time of the same phases (needs GL_ARB_timer_query). `rc_prof_dump` prints the numbers to the console. It changes nothing in the game.
- **`-rc_aff`** (launch option). Pins the engine's main thread to the first core as retail did (B254). Without it ReCoF lets Windows place the thread; game logic and frame timing do not depend on the core.
- **`-rc_nosleep`** (launch option). Keeps the retail frame limiter, which spins a whole core until the next frame is due (B252). Without it ReCoF sleeps in 1 ms steps while more than 3 ms remain and spins only the last part, so frames start at the same moment and host_frametime and the msec of each command do not change.
- **`rc_modelarrays`** (client, default 1). Studio models send each triangle strip to the driver as one vertex array draw instead of three calls per vertex (B287). The picture is the same; 0 gives the retail immediate mode for comparison.
