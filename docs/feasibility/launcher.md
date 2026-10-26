# Feasibility: a separate launcher with updater and backups

Study only, written 2026-10-26. Every claim names the file or page it was checked against; anything not checked is marked (unverified).

## The question

Should ReCoF ship a separate launcher, in its own repository, that finds the Steam copy of Cry of Fear, checks that it is retail 1.6a, backs up what it will touch, installs and updates ReCoF from GitHub Releases with hash checks, rolls back or uninstalls, and starts either retail or ReCoF? If so: which language, which install model (over the Steam folder or a copy beside it), and what a release may contain.

## What exists today

- **How Steam starts the game.** Steam's cached app info for 223710 (`C:\Program Files (x86)\Steam\appcache\appinfo.vdf`, parsed read-only) has a single launch entry: `CoFLaunchApp.exe`, description "Launch with Console", no arguments, installdir `Cry of Fear`. CoFLaunchApp then starts `cof.exe -basedir cryoffear -game cryoffear -hw -heapsize 1536000 -no3dmenu`, adds the window, resolution and widescreen flags, and appends its own command line, so Steam launch options reach cof.exe (`src/coflaunchapp/Form1.cs` lines 203 to 306). It keeps its settings in `HKCU\Software\Valve\Cry of Fear\Settings` (Form1.cs lines 21 and 64), the key where the engine also stores `EngineDLL` and `CrashInitializingVideoMode` (seen on this machine).
- **Finding the install.** `HKCU\Software\Valve\Steam\SteamPath` gives the Steam folder (stored with forward slashes and lower case here). `steamapps\libraryfolders.vdf` lists each library and its apps (223710 is in `I:\SteamLibrary` on this machine). `appmanifest_223710.acf` gives installdir `Cry of Fear`, buildid 5821301, depot 223711 manifest 3465817078078599269 and SizeOnDisk 4,695,601,334 bytes. The build has not changed since at least 2022-03-31: the Steam Deck record in app info names tested_build_id 5821301 at that date.
- **Retail hashes.** `docs/versions.csv` holds the SHA-256 of every in-scope 1.6a binary. Hashing the Steam install on this machine, seven match, but `cof.exe` does not: it is the retail file with `IMAGE_FILE_LARGE_ADDRESS_AWARE` set and the PE checksum filled in (4 bytes differ), the community 4 GB crash patch described in `docs/bugs.md` B277. A strict "retail or refuse" check would reject the maintainer's own install.
- **What the build produces.** `tools/make_dist.py` copies all of `versions/1.6a` (never hard linked, so the game cannot write through) and replaces up to eight files: `cryoffear/cl_dlls/client.dll`, `hl.dll`, `GameUI.dll`, `opengl32.dll`, `hw.dll`, `sw.dll` (identical to hw.dll in retail), `CoFLaunchApp.exe` and `cof.exe`. The README says to test ReCoF only in a copy, never in the Steam installation: development already works side by side.
- **Retail content inside rebuilt binaries.** The rebuilt `CoFLaunchApp.exe` embeds the images and sounds pulled out of the retail exe at build time (`tools/coflaunchapp/build.py` lines 34 to 36). The rebuilt `hw.dll` and `cof.exe` link the retail icons and version resources through `tools/res_from_pe.py` (`tools/units.json`, the `resources` entries of the hw and launcher links). None of the three can go into a public release as built today.
- **Saves, progress and settings.** Saves go to `SAVE/` under the game directory (`src/engine_hw/host_cmd.c` line 1473), that is `Cry of Fear\cryoffear\SAVE` (the Steam install here holds `saveinfo1.cof` there). Endings and unlockables are in `cryoffear\scriptsettings.dat` and `alt.dat` (`src/cl_dll/CoopStats_guess.cpp` lines 105 to 109). Settings are `cryoffear\config.cfg` plus the registry key above. `scriptsettings.dat` and `config.cfg` are themselves depot files (both are in the untouched steamcmd download in `versions/1.6a`). The store page lists Steam Cloud, but the cached app info has no `ufs` (cloud) section, so nothing shows that saves are synced (unverified).
- **Steam features.** hl.dll and client.dll call `SteamAPI_Init` (`src/dlls/game.cpp` line 487, `src/cl_dll/input.cpp` line 1549) and hl.dll updates one Steam stat (`src/dlls/client.cpp` lines 1489 to 1495); no achievement is set in the source and the store page lists none. The engine maps `cryoffear` to app 223710 (`src/engine_hw/sv_main.c` line 8274) and writes `steam_appid.txt` itself (`src/engine_hw/sv_steam3.cpp` line 278). The Steamworks API overview says that file lets a game run without a Steam launch while the client runs.
- **Signing.** No retail game binary is Authenticode signed (the seven in-scope files checked); only Valve's `steam_api.dll`, `steamclient.dll` and `tier0.dll` are.
- **Runners.** `docs/speedrun.md`: the LiveSplit autosplitter attaches to the process `cof` and reads fixed offsets in hw.dll, client.dll and crashhandler.dll; BunnymodXT and RebuiltSimon patch by pattern and offset. Retail must stay one click away.
- **Permissions.** `BUILTIN\Users` have full control of `C:\Program Files (x86)\Steam\steamapps` on this machine (icacls), so no elevation is needed here; whether Steam sets this everywhere is unverified.

## Options compared

### Own repository or part of ReCoF

A separate repository is the right call. The launcher is new code and can be MIT licensed, whereas ReCoF's LICENSE (part 3) grants no rights over the reconstructed code. That makes the launcher eligible for free open source code signing (SignPath Foundation requires an OSI license for all components and code the team owns, per signpath.org/terms) and publishable before ReCoF is. Its toolchain (a current .NET SDK) shares nothing with VC6, MSVC 14.27 and csc 4.0. It ships and updates itself on its own schedule, and a takedown of the game repository would not take away the user's way to roll back. The cost is a versioned manifest format that both repositories agree on; ReCoF keeps only the script that writes it.

### Should ReCoF's CoFLaunchApp.exe become the launcher?

No. Steam's Play button runs CoFLaunchApp.exe, so with an in-place install every Steam verify or update would put the retail file back and remove the updater. The rebuilt exe carries retail images and sounds. It is a matching decompile target (csc 4.0, .NET 4 Client Profile) and part of the baseline every fix is compared with (`docs/baseline.md`). And network code that replaces DLLs, sitting in the Play path, is exactly what antivirus looks at hardest. The retail CoFLaunchApp.exe stays the game's settings dialog; an improved one can come later from ReCoF itself, with new art.

### Language

| Choice | For | Against |
|---|---|---|
| C# on .NET Framework 4.8, WinForms | Preinstalled on Windows 10 1903 and later and on Windows 11 (Microsoft Learn, ".NET Framework and Windows OS versions"); one small exe, no runtime to install; HttpClient, SHA-256 and ZipArchive built in; same family as CoFLaunchApp | Framework gets fixes, not features; under Proton it needs Wine Mono |
| C# on .NET 10, self-contained single file | Current runtime | Trimming is disabled for WinForms and WPF and Native AOT requires trimming (Microsoft Learn, "Known trimming incompatibilities", "Native AOT"), so the exe carries the whole runtime, tens of MB (unmeasured); bundling native libraries extracts them to `%TEMP%\.net` at start, a pattern antivirus dislikes |
| C# on .NET 10, framework-dependent | Small exe | Users must install the .NET Desktop Runtime first |
| C++ or Rust | Smallest | Much more code for UI, HTTP and zip |

Recommendation: C# targeting `net48` from an SDK-style project built with the current .NET SDK (modern C# syntax, one exe of a few hundred KB, unmeasured). Moving to .NET 10 later is a retarget, not a rewrite.

### Over the Steam folder or beside it

| | In place (swap binaries in the Steam folder) | Side by side (copy the game, as make_dist.py does) |
|---|---|---|
| Disk | About 10 MB plus backups | 4.7 GB per copy (SizeOnDisk above). Hard links for read-only asset folders on the same NTFS volume could cut this to almost nothing, but only if every file the game writes (`config.cfg`, `scriptsettings.dat`, `alt.dat`, `SAVE`, `custom.hpk`, `steam_appid.txt`) is a real copy, and whether a Steam update writes through a link is unverified; a later opt-in |
| Play button, overlay | Unchanged: Steam starts it as before | Steam's entry stays retail; ReCoF starts from the launcher or a shortcut. The overlay is expected not to attach to a cof.exe started outside Steam (unverified). A Steam launch option `"<launcher.exe>" %command%` would let Steam start the launcher, which then starts either build; retail cof.exe is already a child of CoFLaunchApp, which suggests children are covered (unverified for CoF) |
| Stats, achievements | Unchanged | Stats work through `steam_appid.txt` with the client running (Steamworks docs); the game has no achievements to lose; playtime tracking unverified |
| Steam update or verify | Puts retail binaries back, which meets "verify restores the original"; extra files are left alone (unverified); the launcher must notice the silent revert and offer to reinstall | Never touches the copy; the Steam install is never modified, so it cannot be broken |
| Saves | One folder: retail saves load at once, but a save written by a ReCoF build with a versioned save table may not load in retail after switching back | Own `SAVE`: retail saves, `scriptsettings.dat` and `config.cfg` are imported on install, one way; export back only on request, with a warning. The registry key is shared by both |
| Runners | Switching means swapping files; a swap cut off halfway leaves mixed binaries (ReCoF client.dll with retail hw.dll) until a journal or Verify repairs it | Both builds installed at once and the launcher picks; both processes are named `cof`, so tools must tell them apart by the version ReCoF reports (speedrun.md) |
| Fits current rules | Conflicts with the README's "never in your Steam installation" | Matches the README and make_dist.py |

Recommendation: side by side, and only that, in the first version. In place can follow as an opt-in once releases are stable, with the journal and binary backup below.

## Install and update flow

1. **Find.** Read SteamPath, parse `libraryfolders.vdf` (text KeyValues), open `appmanifest_223710.acf`, take installdir and buildid. A buildid other than 5821301 stops with a clear message. A folder picker covers copies outside Steam.
2. **Verify.** Hash the eight in-scope binaries against a table generated from `docs/versions.csv` plus known variants (cof.exe with the large address flag). An unknown hash is reported by file name; for a side-by-side install it does not block, because those files are replaced in the copy, but it is recorded. The 4.7 GB of assets are trusted by buildid, not hashed.
3. **Back up.** Before the first install, zip `cryoffear\SAVE`, `scriptsettings.dat`, `alt.dat`, `config.cfg`, `autoexec.cfg` and `user.scr`, export `HKCU\Software\Valve\Cry of Fear`, write a SHA-256 list, and read the archive back. Location: `%LOCALAPPDATA%\ReCoF\backups\<date>`. Before every later update the ReCoF copy's saves get the same treatment. In-place mode also saves the eight binaries as found, not as retail is assumed to be.
4. **Download.** One call to `GET /repos/{owner}/{repo}/releases/latest`, which skips drafts and prereleases (GitHub REST docs; a test channel uses the list endpoint instead). Unauthenticated calls are limited to 60 an hour (GitHub docs), plenty for one check per start. Fetch the zip, `recof-manifest.json` and its signature. Check the manifest's Ed25519 signature against a public key compiled into the launcher, the zip's SHA-256 against the manifest and against the asset's `digest` field (GitHub REST docs), and every extracted file against the manifest. The digest alone only proves the file is what GitHub holds, not who uploaded it.
5. **Install.** First time: copy the Steam game folder to the chosen target (default a `ReCoF` folder in the same Steam library, user selectable; not Program Files, not Documents). Extract each release into a version store, then apply it to the copy by writing temporary names and renaming, with a journal written before and after. On start, an unfinished journal is rolled forward or back. `sw.dll` is made as a copy of `hw.dll`, as in retail.
6. **Launch.** ReCoF: run the copy's own retail CoFLaunchApp.exe, which starts the copy's cof.exe with the usual dialog, or start cof.exe with the same arguments directly. Retail: `steam://run/223710` (syntax unverified) or the Steam folder's CoFLaunchApp.exe.
7. **Roll back.** Keep the last few versions in the store; rolling back applies an older file set and offers the save backup taken before the update.
8. **Uninstall.** Offer to export saves, delete the copy and the store, keep backups unless the user removes them. In-place mode restores the saved binaries, then suggests Steam's Verify.
9. **Update itself.** Download and verify the new exe, rename the running exe to `.old` (Windows allows renaming a running exe), move the new one in, restart.

**What a release contains.** Only ReCoF-built files: `cof.exe`, `hw.dll`, `opengl32.dll`, `cryoffear/cl_dlls/client.dll`, `hl.dll` and `GameUI.dll`; the signed manifest (version, target buildid 5821301, size and SHA-256 per file, minimum launcher version); the license texts (Half-Life 1 SDK License, GPL-2.0 for the Paranoia parts) and a link to the source. Never `CoFLaunchApp.exe`, Valve's redistributables (`steam_api.dll` and the like) or any asset; `hw.dll` and `cof.exe` first need ReCoF's own icons and version resources. Releases are built by CI from a tag, which is also a SignPath condition.

## Risks

- **Legal.** ReCoF binaries are compiled from reconstructed Team Psykskallar and Valve code, and LICENSE part 3 grants no rights over it; hw.dll and cof.exe are Valve engine and launcher code that the Half-Life 1 SDK License does not cover. Public releases make a takedown a real possibility. client.dll contains Paranoia's GPL-2.0 code, so a binary release carries GPL source obligations, and how GPL-2.0 combines with the SDK license's "only for free" term is an open question. This is not legal advice; asking Team Psykskallar first is advisable. It decides whether the launcher has anything to download.
- **Private repository.** Releases of a private repository are visible only to people with read access (GitHub docs), so updates need a public repository; a token built into a public exe would leak. Until then, testers use "install from a local zip".
- **Antivirus and SmartScreen.** An unsigned exe that downloads DLLs and writes an `opengl32.dll` (a known proxy DLL name) into a game folder will draw heuristic detections. Sign the launcher (SignPath Foundation for the MIT repository; Microsoft's Artifact Signing reportedly offers public trust to individuals only in the US and Canada, unverified), avoid packers and self-extraction, report false positives to Microsoft. The game DLLs stay unsigned, as retail's are.
- **Installs that are not retail.** The large address patch seen here, the CoF Linux Patcher, mods in `cryoffear`. Back up what is there; never assume retail.
- **Saves and progress.** Compatibility is one way; never write automatically into Steam's `SAVE`. Steam's Verify may reset `config.cfg` and `scriptsettings.dat`, since both are depot files (unverified whether Steam exempts them), which the backups cover.
- **Disk.** 4.7 GB per side-by-side copy until hard links are proven safe.
- **Runners.** Moved offsets break the autosplitter; the launcher must show which build runs and keep retail one click away.
- **Linux and Proton.** A net48 launcher under Proton needs Wine Mono; offer the plain zip there.

## Verdict

- **Possible:** yes. Every piece is standard (registry and VDF reading, SHA-256, one GitHub API call, zip, file copy), the side-by-side model is what `tools/make_dist.py` already does, and nothing in the Steam install has to change.
- **Effort:** medium. The happy path is small; the work is in the safety: crash-safe apply and rollback with a journal, handling installs that are not retail (already seen on this machine), save import and backups, signing and antivirus, a test matrix (several libraries, paths with spaces and non-ASCII characters, Steam closed), and the ReCoF side of releases (own resources in hw.dll and cof.exe, manifest, signing key, CI). It becomes large if in-place mode, Linux and the Steam launch option wrapper all go into the first version.
- **Main risks:** the legal status of distributing rebuilt binaries (decides whether public releases exist at all, and updates need public releases); antivirus flags on an unsigned updater; saves that a later ReCoF writes not loading in retail; runners' tools reading moved offsets.
- **Recommended path:** a separate MIT repository, C# WinForms on .NET Framework 4.8, side-by-side install only in version 1, Steam folder read-only, retail CoFLaunchApp.exe kept as the settings dialog in both copies, signed manifest plus GitHub digest for every download, in place as a later opt-in.
- **First three steps:**
  1. In ReCoF: give hw.dll and cof.exe ReCoF's own icons and version resources, drop CoFLaunchApp.exe from the release set, and add a release script that writes the zip, the manifest with SHA-256 per file and its signature from the baseline build. Settle permission, license and repository visibility before any public release.
  2. Create the launcher repository with the read-only half: find the install, hash against a table generated from `docs/versions.csv` plus the large address variant, write and verify the backup, show a report. No writes outside `%LOCALAPPDATA%\ReCoF`.
  3. Add side-by-side install from a local release zip (no network yet), with journal, launch, rollback and uninstall; test against a copy of the `I:\SteamLibrary` install and on a clean Windows machine; add the GitHub update check once the first public release exists.
