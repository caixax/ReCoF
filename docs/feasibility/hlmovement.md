# Feasibility: rc_hlmovement (Half-Life 1 movement)

Written 2026-10-26 against the ReCoF tree (baseline = DeCoF, matching retail 1.6a). Implemented the same day as `rc_hlmovement` (docs/speedrun.md, ReCoF cvars), applied at once from PreThink instead of latched at Spawn and Restore.

## Question

Can ReCoF offer real Half-Life 1 movement as a switch next to the original Cry of Fear movement, decided by the server, agreed by client prediction, visible to runners and their tools, without breaking retail saves? And what does "real HL1 movement" mean for each place where Cry of Fear differs?

The starting assumptions hold. The movement code is one file compiled into both DLLs (`tools/refbuild.py:33` for hl.dll, `:40` for client.dll; the client calls it from `HUD_PlayerMove`, `src/cl_dll/cdll_int.cpp:145`). The engine already carries a server-written key/value string into prediction: the server sets keys with `pfnSetPhysicsKeyValue` (Cry of Fear writes "slj" and "hl" like Half-Life, `src/dlls/player.cpp:7264-7265`), `UpdateClientData` copies the string into clientdata (`src/dlls/client.cpp:3686`), the delta sends it (`cryoffear/delta.lst:41`), and the client copies it into its pmove before predicting (`src/engine_hw/cl_ents.c:803`, `cl_pred.c:148`). Prediction always runs while connected (`cl_pred.c:324`, no off switch).

## Sources and abbreviations

- `pm:N` is `src/pm_shared/pm_shared.c` line N (Cry of Fear).
- `sdk:N` is `pm_shared/pm_shared.c` in `refs/halflife` at commit `c7240b9` (tip of steam_legacy, 2019; the file is identical at `de70fad`). Read it with `git show c7240b9:pm_shared/pm_shared.c`. The checked-out HEAD of `refs/halflife` is post-HL25 (see `docs/refs.md`), so it is not the reference; its extra changes are row 22.
- `player.cpp` and `input.cpp` are `src/dlls/player.cpp` and `src/cl_dll/input.cpp`. `sdk input.cpp` is `c7240b9:cl_dll/input.cpp`.
- Paranoia is `I:\COFPP\DeCoF\refs\Paranoia\code_src\pm_shared\pm_shared.c`, the direct ancestor, used to tell Paranoia changes from Cry of Fear changes.
- The other pm_shared files differ only in typedefs, `#ifdef` guards and texture letters (`pm_defs.h:199`, `pm_math.c`, `pm_materials.h:32`), none of which affect movement.

## Every movement difference

Speeds below assume the shipped settings: `sv_maxspeed 300` (`cryoffear/autoexec.cfg:1`, engine default 320 at `src/engine_hw/sv_user.c:67`), `cl_forwardspeed 300` (`cryoffear/config.cfg:70`, unverified that this is the shipped default; the registered default is 230), side and back 200 (`autoexec.cfg:5-6`), and `skill.cfg:264-287` (primary 75, secondary 45, sprint 125, stair 50).

| # | Topic | Cry of Fear (evidence) | Half-Life (evidence) | In rc_hlmovement 1 |
|---|---|---|---|---|
| 1 | Speed model | `clientmaxspeed` is a percent of wishspeed (Paranoia, "buz"): pm:1503 walk, pm:1798 water, pm:1906 air. The server writes the percent into `pev->maxspeed` every frame in `StaminaThink` (player.cpp:12938-13105), the engine passes it as clientmaxspeed (`sv_user.c:962`, `cl_pred.c:152`). Result: walk 225, sprint 375 clamped to 300. | clientmaxspeed is only a cap: sdk:2846-2849 `maxspeed = min(clientmaxspeed, maxspeed)`. HL game code never writes `pev->maxspeed` (at c7240b9 only `dlls/client.cpp:1701` copies it), so the cap is off and the run speed is sv_maxspeed. | Ignore clientmaxspeed completely (it carries CoF percents). Run speed is sv_maxspeed, set to 320. |
| 2 | Client move speeds | Registered 230/200/210 (input.cpp:1497-1499); autoexec forces side and back to 200 on every launch. | 400/400/400 (sdk input.cpp:988-990), clamped to sv_maxspeed in `PM_CheckParamters` (sdk:2852-2853; Cry of Fear keeps that clamp). | Use 400 for forward, back and side, since autoexec overrides the user's values anyway. |
| 3 | Walk key and client clip | No `+speed`; `+sprint` sets IN_RUN (input.cpp:1370-1372, :1449-1450), disabled on c_nightmare (input.cpp:635-639). No cl_movespeedkey scaling and no `GetClientMaxspeed` clip in `CL_CreateMove` (input.cpp:1132-1175). | `+speed` scales the command by cl_movespeedkey 0.3 and the client clips to clientmaxspeed (sdk input.cpp:698-715, :950). `+speed` does not set IN_RUN in the SDK. | The sprint key walks (x 0.3, so 120), as shift does in HL. Skip the clientmaxspeed clip. |
| 4 | Sprint and stamina | Sprint sets flag bit 18 and sprint speed (player.cpp:13109-13124), drains 2 per 0.1 s (:13150), exhaustion sets bit 19 and the slow 45 speed (:13203); jump costs 15 (player.cpp:3713-3714). pm_shared reads bit 18 at pm:1517, pm:1807, pm:3430 and for sounds. | None. | No sprint (bit 18 never set, bits 18 and 19 cleared on apply), jumping costs nothing. Stamina stays for dodge, melee and breath holding, which are actions, not movement. |
| 5 | Strafe rules | Sprinting zeroes sidemove; forward plus side halves forwardmove (pm:3430-3436). Holding sprint in the air therefore removes air strafing. | None (sdk:2835-2850). | Off. |
| 6 | Low health limp | Key "low" (health below 25, single player only, player.cpp:4144-4188): wishspeed x 0.75, again x 0.75 on `iStepLeft` (pm:1515-1524, B129), longer step times (pm:912-918). B129 calls it a stance; it is the low health limp. | None. | Off. |
| 7 | Minimum wishspeed | Any input gives at least 30 (pm:1526-1527). | None. | Off. |
| 8 | Wading and stairs | Shallow water caps the percent to 30 (player.cpp:13259-13268), floor 30 (:13284); `func_stairs` (src/dlls/triggers.cpp:3983-4000) sets stair speed 50 and sprinting on stairs trips the player (player.cpp:13034-13036). | None. | No effect (percents are ignored, no sprint so no trip). |
| 9 | Air acceleration | Same code (no diff in `PM_AirAccelerate`), but it uses the scaled wishspeed (`accelspeed = accel * wishspeed * frametime`, pm:1750), so strafe gains are smaller (150 side-only vs 320). | sdk `PM_AirAccelerate`. | HL wishspeed, so HL air strafing. |
| 10 | Swimming | Sprint keeps full speed in water (pm:1807-1811). | Always x 0.8 (sdk:1352). | x 0.8. |
| 11 | Jump height | Standing jump uses key "jh" as a percent (pm:3112-3117); Spawn sets 100 (player.cpp:7187), which gives the same 268.3 as HL. Only Paranoia weapon code changes it (src/dlls/paranoia_wpn.cpp:580-715) and no class in src/dlls derives from those weapons (likely dead, unverified at runtime). | Fixed `sqrt(2*800*45)` (sdk:2599). | Fixed HL value. Same result today. |
| 12 | Duck time | `TIME_TO_DUCK 0.6` (pm:72). | 0.4 (sdk:76). | 0.4. |
| 13 | Ducktap | `PM_UnDuck` lifts the origin only when `FL_DUCKING && !bInDuck` (pm:2390), so no ducktap (B132). | Lifts whenever on ground (sdk:1926). | SDK test: native ducktap. |
| 14 | Ladders | `MAX_CLIMB_SPEED 90` (pm:77), used directly (pm:2635-2641). | 200 (sdk:81), clamped to maxspeed and x 0.333 when ducked (sdk:2097-2109). The clamp is Steam era: Paranoia and Spirit (SDK 2.x lineage) lack it. | 200 with the clamp. |
| 15 | +use slowdown | Removed by Cry of Fear (Paranoia still has it at its line 3216; nothing in `PM_PlayerMove`, pm:3557-3720). | Velocity x 0.3 on ground while holding +use (sdk:3025-3028). | Restored. |
| 16 | Bunnyhop cap | Identical (pm:2948-2977). Crop is 0.65 x 1.7 x 300 = 331.5. | sdk:2434-2462. Crop is 0.65 x 1.7 x 320 = 353.6. | Same code; the separate cap cvar from B131 applies to both modes. |
| 17 | Long jump, friction, edge friction, gravity, step, clip, accelerate | Identical code; movevars use engine defaults (`src/engine_hw/sv_phys.c:34-45`, `sv_user.c:66-68`), only sv_maxspeed is changed. | Same. | Nothing to do. |
| 18 | Dodge | `+dodge` (input.cpp:1482, :1162) asks the server to set 600 units/s sideways or forward (player.cpp:13552-13600), costs 15 stamina. | None. | Kept as a Cry of Fear action (runners use dodge jumps); category rules can ban it. |
| 19 | Cry of Fear state locks | fuser3 states (downed, cutscenes, pm:1471-1484, pm:2524-2558, pm:2993), wheelchair "chr" and forced view angles (pm:3473-3498), frozen clears buttons (pm:3468). | None. | Kept in both modes: maps and scripts depend on them. |
| 20 | Spirit and map features | Fog and fly field water types (pm:161-164, pm:1567, pm:1955-1979), moving ladders (pm:2612), disabled ladders `iuser1 != 1` (pm:2714). | None. | Kept in both modes. |
| 21 | Presentation | Step sounds, materials and timing keys "asd/bsd/csd" (pm:374-816, pm:893-1036), bag sounds, spring view punch (pm:3362-3414) and landing punch (pm:3261-3273). | HL steps and `punchangle` (sdk:2733-2738, sdk:2818-2828). | Kept as Cry of Fear in both modes: not physics. |
| 22 | HL25 only (HEAD, not adopted) | Cry of Fear matches steam_legacy here. | HEAD adds: bounce only on the first plane (HEAD:904), +use as maxspeed x 1/3 (HEAD:2855-2861), barnacle ladder flag (HEAD:522-523), duck to get unstuck (HEAD:2990), single player stuck check (HEAD:1660). | Not adopted. A later value could add them if HL runners ask. |

So "real HL1 movement" here means: the steam_legacy pm_shared physics and the HL speed model (rows 1 to 15), with Cry of Fear's map, state and presentation code kept (rows 18 to 21). Sprint is gone because HL already runs at 320; stamina stays but movement no longer spends it. A hybrid (HL physics with Cry of Fear sprint) is possible later as value 2 with the same branches, but it is not recommended now: each value is one more leaderboard category.

## Design

**Switch.** `rc_hlmovement`, registered in hl.dll (`src/dlls/game.cpp`, next to `cof_walksteptime` at :61), flags `FCVAR_SERVER`, default `0`, not archived, not a cheat. Values: `0` Cry of Fear (retail), `1` Half-Life. Only the server changes it: the host in single player and listen co-op, the admin on a dedicated server. A changed FCVAR_SERVER cvar is already logged and broadcast to every client as `"rc_hlmovement" changed to "1"` (`src/engine_hw/cvar.c:263-277`).

**When it takes effect.** Latched: the server applies it in `CBasePlayer::Spawn` and `CBasePlayer::Restore`, next to "slj" and "hl" (player.cpp:7264-7265 and :7539-7547), which covers new game, map change, level transition and save load. Applying means: write physinfo key `hlm` = 0 or 1, set `sv_maxspeed` 300 or 320 (movevars reach prediction through `cl_pred.c:151`), and in HL mode clear flag bits 18 and 19. Applying mid-frame would let a player switch in the middle of a duck or on one ladder; latching avoids that.

**Sync.** Everything that branches reads the key, never the cvar. pm_shared reads it once at the top of `PM_PlayerMove` (`PM_Info_ValueForKey`, as pm:3083 does for "slj") on both sides. Server code outside pm_shared (`StaminaThink`, `Jump`) reads it with `pfnGetPhysicsKeyValue`. The client's input code reads it with `gEngfuncs.PhysInfo_ValueForKey` (`src/engine/cdll_int.h:173`). If the key cannot be written (physinfo is limited to 256 characters, `pm_info.h:22`, and `Info_SetValueForKey` refuses on overflow, `src/engine_hw/info.c:304-310`), every reader sees mode 0 together, so prediction still agrees.

**Visibility.** A HUD tag ("HL movement") drawn whenever the key is 1, and "pending" when the cvar and the key differ. Tools read it by name, not by address: the client API above (RebuiltSimon already holds the client engine table, `I:\COFPP\cof-rs\RebuiltSimon\globals.c:42`), the server API, or the cvar. For memory readers like the LiveSplit script, an exported status block in client.dll with a magic tag and the mode, placed so that no autosplitter offset moves.

**Saves.** No new save field. Retail saves load in either mode because the mode comes from the cvar and Restore rewrites the key. `pev->flags` is saved (`src/dlls/util.cpp:424`), so a sprint bit from a Cry of Fear save is cleared on apply in HL mode. Saves made in HL mode stay valid retail saves.

**Multiplayer and co-op.** The server decides; every client gets the key through clientdata. The limp ("low") is single player only already. A retail client.dll on a ReCoF server would predict Cry of Fear movement against HL physics and stutter, so a ReCoF server in mode 1 should detect ReCoF clients (for example a userinfo key) and warn or refuse the others.

**Demos.** The key travels in the clientdata deltas and the cvar change is a broadcast print, both part of the server stream a demo records, so a demo carries the mode. Whether playback re-runs prediction with it is unverified.

## Risks

- **Runners.** Mode 0 must stay exactly retail: keep the retail code as the else branch, unchanged. The default stays 0. HL mode is a new category, not a replacement. The bhop cap (B131) and ducktap (B132) cvars stay separate from the mode; in HL mode ducktap is native.
- **Runner tools.** RebuiltSimon patches only hl.dll (`Hooks/Server/PM_PreventMegaBunnyJumping.c`, `PM_Unduck.c` scan "hl.dll"), so its bhop cap and ducktap already disagree with client prediction today (stutter unverified). It reads `_pmove` at hl.dll +0x220C50 (`globals.c:30`, matches `build/link/hl/hl.dll.map:21947`, in pm_shared.obj) and CBasePlayer offsets 0x2038, 0x21E8, 0x21F0, 0x24F8. Adding globals to pm_shared.c or members to CBasePlayer can move them: add no members, keep new globals out of pm_shared.obj or check the map. A new HUD element can move client.dll data the autosplitter reads (`docs/speedrun.md`, "Data addresses").
- **Prediction.** Any branch on server-only state (the cvar, `gpGlobals`, CBasePlayer members) inside pm_shared splits the two sides. Verify with `cl_showerror 1` (`cl_pred.c:55` prints errors above 0.5 units).
- **Gameplay.** 320 units/s and no stamina cost change chases and scripted sequences; level scripts were tuned for 225 (unverified which ones break). Weapons never play sprint animations in HL mode. The c_nightmare block on the sprint key also blocks the walk key.
- **Saves.** Low, since nothing new is saved.

## Verdict

- **Possible:** yes. Every difference is located, small and on code both DLLs share, and the server-to-prediction channel already exists and is used the same way for "slj".
- **Effort: medium.** The code is small: about a dozen branches in pm_shared.c, three places in player.cpp (StaminaThink sprint gate, jump cost, Spawn and Restore apply), one cvar, the client walk key and speeds in input.cpp, and one HUD tag. It is not small because parity with HL has to be measured, not assumed, prediction has to be checked in single player and co-op, the tool and autosplitter addresses have to be rechecked in the link maps, and runners should review what HL mode contains (dodge, cap default) before the category exists.
- **Main risks:** prediction splits if any branch reads something other than the physinfo key; moved addresses break RebuiltSimon and the autosplitter; an invisible mode would make categories dishonest.
- **Recommended path:** one cvar, two values, steam_legacy as the reference, latched at Spawn and Restore, one physinfo key read by every branch, HUD tag plus name-based readout, no save field, bhop cap and ducktap as separate cvars.
- **First three steps:**
  1. Plumbing only: register `rc_hlmovement`, write `hlm` in Spawn and Restore, draw the HUD tag from `PhysInfo_ValueForKey("hlm")`. Confirm the key reaches the client in single player and co-op, and that `_pmove` and the CBasePlayer offsets in the link maps did not move.
  2. Branches: in pm_shared.c implement rows 1, 3 (clip), 5 to 7 and 9 to 15 as `if ( hlmove ) <sdk code> else <retail code>`; in player.cpp gate sprint and the jump cost and apply sv_maxspeed; in input.cpp add the walk scaling and the 400 speeds for mode 1.
  3. Measure with a speedometer and `cl_showerror 1`: run 320, walk 120, duck 106.6, ladder 200, bhop crop 353.6, ducktap lift 18 units, zero prediction errors, and the same inputs on Half-Life steam_legacy giving the same numbers. Load a retail save in both modes.

Unverified points to check before implementation: that HL runners use steam_legacy movement as their reference; that the steam_legacy SDK text matches the Steam HL binaries; that `cl_forwardspeed 300` is the shipped default; that the Paranoia jump height path is dead at runtime; demo playback behavior; the worst case physinfo length on maps with custom footstep keys.
