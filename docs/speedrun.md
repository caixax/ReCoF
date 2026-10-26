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

- **LiveSplit autosplitter and load remover** (by DrLa, listed for "Cry of Fear" and "Cry of Fear Category Extensions"). It reads fixed offsets in the process `cof`: hw.dll +0x11119E4 pause state, +0x10D279C loading state, +0xF1AA15 map name, +0x820495 menu map, +0x377BD8 co-op start, +0x116BED0 health, +0x9FDC30 can move, +0x1F39F0 alive, +0x1F39C8 demo; client.dll +0x18ED64 cutscene state, +0x1BE920 in-game time, +0x5440C8 music, +0x18A3E0 save state; crashhandler.dll +0x5600C crash state. Source: https://raw.githubusercontent.com/IlyaTsyganov/Livesplit.CryofFear/main/cryoffear.asl
- **BunnymodXT** finds `Host_ValidSave` by the byte pattern `55 8B EC 83 3D ? ? ? ? ? 74 ? 33 C0 E9` to detect the Steam version, and crashes the whole game when the pattern is missing. Source: the comment in RebuiltSimon's Host_ValidSave hook (`I:\COFPP\cof-rs`).
- **RebuiltSimon** writes 19 NOPs at hl.dll +0xEB203 (the noclip reset in `CBasePlayer::PostThink`), stores into `CBasePlayer` at +0x2038, +0x21E8, +0x21F0 and +0x24F8, and uses hw.dll offsets 0x813C8, 0x8123B, 0x812B8, 0x7E0544 and 0x11115AC. Runners use its commands `rs_info`, `rs_hud`, `rs_bhop_cap`, `rs_disable_stamina`, `rs_disable_duck_time`, `rs_autojump`, `rs_ducktap_priority` and `+rs_ducktap`; `+rs_left`, `+rs_right` with `rs_yawspeed` and `rs_cof_save_prevention` are banned. Sources: `I:\COFPP\cof-rs`, https://www.speedrun.com/cof/guides
- **CoF Linux Patcher** replaces cof.exe and patches hw.dll, client.dll and opengl32.dll at fixed offsets for Proton. Source: https://github.com/hinqiwame/cof-linux-patcher

## Rules for ReCoF builds

- **Data addresses.** The autosplitter reads data in hw.dll and client.dll. A fix that adds or resizes a global, a cvar or a string in those binaries can move every later variable. Before each test build, run `python tools/check_runner.py`: it checks that the variable at each autosplitter offset is still the same one as in retail, using the link maps. If a change has to move them, ship an updated autosplitter script with ReCoF, or give the values a stable place that a script can find by name.
- **Room in hw.dll and client.dll.** Both DLLs keep their uninitialized globals (where most autosplitter values live) at the end of `.data`, so any new initialized global moves them, and code or strings that outgrow the room before the next section move everything after it. At the baseline hw.dll has 2266 bytes of room in `.text` and 2308 in `.rdata`, client.dll 3636 and 1182; `check_runner.py` prints what is left. New variables in these two DLLs go into a ReCoF source file linked after all retail objects, uninitialized (cvars filled in at registration), and never as new members of a struct the autosplitter reads into (`CHud`, `client_state_t`, `server_t`).
- **`Host_ValidSave`.** Keep it byte-identical. New save logic goes into a separate function that it, or its callers, call.
- **Code patched by tools.** RebuiltSimon's NOP patch and member offsets only fit retail. ReCoF should offer its runner features natively (bhop cap, ducktap, infinite stamina, noclip and god with sv_cheats, raw input) and report its own version, so tools can tell ReCoF apart from retail and refuse to patch it.
- **Linux.** The Linux patcher will not apply to rebuilt binaries. The opengl32.dll fix in bugs.md removes the main reason Proton needs it.

## ReCoF cvars

- **`rc_retail_gameplay`** (server, default 0). ReCoF fixes balance bugs by default: damage, health, AI and collision behave as the developers wrote them to. Set it to 1 to get the retail behavior of every such fix back at once. It covers: B047 (saw crazy hit timer), B052 (crazy bitch weapon cases), B062 (co-op door moves the monsters near its exit back to their spawn), B096 (ladder climb invulnerability), B025 (Twitcher 4 uses its own skill values), B026 (the Faceless use their own skill values).
- **`rc_bhopcap`** (server, default 1 as in retail). 0 removes the bunny hop speed cap (B131). The server passes it to client prediction as the physinfo key `nbh`, so both sides agree; a separate category if used.
- **`rc_ducktap`** (server, default 0 as in retail). 1 brings back the Half-Life ducktap that Cry of Fear removed (B132), as RebuiltSimon's ducktap option does. Passed to client prediction as the physinfo key `dkt`.
- **`rc_infinite_stamina`** (server, default 0 as in retail). 1 gives infinite stamina (B094), what RebuiltSimon's `rs_disable_stamina` does by writing memory. The stamina bar still shows.
- **`god`, `noclip`, `notarget`, `fly`** (engine commands, need `sv_cheats 1`). Registered again, and PostThink keeps noclip and god mode while `sv_cheats` is 1 (B095), what RebuiltSimon gets by patching hl.dll. With `sv_cheats 0` they do nothing, as in retail. They do nothing in co-op (multiplayer runs with deathmatch set).
