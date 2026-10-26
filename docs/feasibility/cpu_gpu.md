# Feasibility: GPU rendering and more CPU cores

Study of 2026-10-26 for [B286](../bugs.md) and the rows it names (B287, B288, B195, B194, B254, B255, B252, B198). Sources: the ReCoF tree in `src/` (line numbers as of this date), [speedrun.md](../speedrun.md), [toolchains.md](../toolchains.md), `tools/units.json`, and the MetaRenderer repository (github.com/MetaHookSv/Renderer, read at commit 7225f589 of 2026-10-25). Anything marked "unverified" or "estimate" has not been checked in the source or on a running game.

## The question

Players with weak single core CPUs get low frame rates while the GPU idles (B286). Can ReCoF hand the drawing to the GPU and spread the remaining CPU work over more cores, without changing game logic, frame timing or fps_max behavior for runners? Where should the new code live, given that hl.dll and client.dll move to a current MSVC and hw.dll stays on VC6?

## What draws what today

With the retail defaults (`gl_renderer 1` and `gl_modelrenderer 1`, registered without FCVAR_ARCHIVE at `src/cl_dll/gl_renderer.cpp:1257` and `:1274`, set by no retail .cfg), the Paranoia renderer in client.dll draws the world and every studio model. The engine draws everything else.

| Part | Drawn by | How | Evidence |
|---|---|---|---|
| World, solid brush entities | client.dll, Paranoia `DrawBump` | several passes (first, bump, one per dynamic light, second, specular), one `glDrawArrays(GL_POLYGON)` per surface from a static VBO | `gl_renderer.cpp:657-749`, `gl_rsurf.cpp:197-203`, `:630-633` |
| Studio models | client.dll `CStudioModelRenderer::GL_DrawPoints` | CPU skinning per vertex, CPU lighting per normal, immediate mode | `gl_studiomodel.cpp:1476`, `:1578-1598`, `:1869-1896`; switch at `StudioModelRenderer.cpp:2147-2163` |
| Studio models (fallback) | hw.dll `R_GLStudioDrawPoints` | same structure, only when `gl_renderer 0`, `gl_modelrenderer 0`, or Paranoia turned itself off | `r_studio.c:3973-4221`; self disable at `gl_renderer.cpp:586-612` |
| Sky, world decals | client.dll | `GL_POLYGON` immediate mode | `gl_sky.cpp:298`, `gl_decals.cpp:707` |
| Transparent brush entities, engine water | hw.dll | `R_DrawTEntitiesOnList`, `EmitWaterPolys` | `gl_renderer.cpp:1177-1186`, `r_trans.c:225`, `GL_WARP.C:289` |
| Sprites, beams, temp entities | hw.dll | `R_DrawSpriteModel`, immediate mode | `GL_RMAIN.C:332`, `:734-818` |
| Particles | hw.dll `R_DrawParticles` (physics inside the draw loop) and client particle systems, rain, grass, glows (TriAPI or direct GL) | immediate mode | `R_PART.C:1360-1470`, `tri.cpp:778-821` |
| Cg water | client.dll `CCgWater_guess::Render_guess` | 3 full screen copies and full screen passes every frame with `cg_water 1` | `tri.cpp:817-821`, B194 |
| Post process | client.dll `ApplyPostEffects`, `DrawBlurTest` | returns early unless a grayscale factor is active; blur when damaged | `gl_postprocess.cpp:181`, `:222-227`, `hud_redraw.cpp:308`, `:804-815` |
| HUD and menus | client.dll through hw.dll `SPR_Draw` and the VGUI EngineSurface; GameUI.dll | immediate mode quads, one projection readback per painted panel | `GL_DRAW.C:815`, `vgui_EngineSurfaceHW.cpp:538` |

How the world handoff works: `RendererCalcRefDef` (called from `view.cpp:2132`) runs `PrepareRenderer`, which collapses the bounding box of the world's root node to a point behind the camera (`DisableWorldDrawing`, `gl_renderer.cpp:159-168`). The engine's `R_DrawWorld` then culls everything at the root, while `R_MarkLeaves` (`GL_RMAIN.C:1663`) still marks the PVS that Paranoia reuses. `RendererDrawNormal` restores the box and draws the world at the first studio model draw (`GameStudioModelRenderer.cpp:64`, `:77`) or in `HUD_DrawNormalTriangles` (`tri.cpp:758`). `HUD_AddEntity` returns 0 for solid brush entities so the engine skips them (`gl_renderer.cpp:1177-1186`).

Two corrections for [bugs.md](../bugs.md): B287 locates the CPU skinning in the engine's `R_GLStudioDrawPoints`, which is only the fallback; the default path is the client's `GL_DrawPoints`, with the same per vertex structure. B286 says MetaRenderer replaces this renderer; it refuses to run with Cry of Fear (see below).

## Where the CPU time most likely goes

Ranked from the code structure, not measured:

1. **World light passes.** Each dynamic light walks the BSP again (`RecursiveDrawWorldLight`, `gl_light_dynamic.cpp:762-902`, up to 32 lights) and draws each lit surface with its own texture bind, `glMatrixMode(GL_TEXTURE)` plus `glLoadMatrixf` and draw call (`LightDrawPoly`, `:600-609`; `SetupTexMatrix`, `gl_rsurf.cpp:1046-1069`). Cost grows with surfaces times lights, and Cry of Fear creates many lights per frame (flashlights, renderfx 71, 72, 184 and others in `RendererFilterEntities`, muzzle flashes, B198 night vision with radius 900).
2. **Models on the CPU.** Every vertex is transformed by its bone (`gl_studiomodel.cpp:1580`), every normal is lit with a loop over up to 4 dynamic lights (`:1040-1045`, `MAX_MODEL_DYNLIGHTS` in `StudioModelRenderer.h:20`), then each vertex costs three GL calls (`glTexCoord2f`, `glColor4f`, `glVertex3f`).
3. **Unoptimized engine.** hw.dll is built with VC6 `/Od` (`tools/units.json`, hw cflags `/nologo /c /G5 /W3 /Od`; only bzip2 and vgui_surfacelib units use `/O2`), and that build matches retail byte for byte apart from stamps ([baseline.md](../baseline.md)), so the retail engine code is unoptimized too (the Standard Edition compiler has no optimizer, toolchains.md). Traces used by monster logic, pmove, particles, sprites, TriAPI, VGUI and sound mixing all run as unoptimized code.
4. **Game logic in hl.dll.** B106 (every monster senses the whole map) and B107 put real work into `SV_Frame`. The GPU cannot help here, and changing it changes game logic.
5. **Smaller items.** B194 Cg water, B196 blur, B255 readbacks (`glPushAttrib(GL_ALL_ATTRIB_BITS)` in `GL_MtexStateSaver`, `gl_renderer.cpp:841-858`), the opengl32.dll wrapper (342 pass-through functions built `/Od`), and sound mixing on the main thread, called again three times during rendering (`S_ExtraUpdate`, `GL_RMAIN.C:1673`, `:1690`, `:1747`). B252 costs no frame time below the cap but keeps a core at 100% at the cap.

## The GPU path

### OpenGL version and fallback

The engine creates its context with plain `wglCreateContext` (`GL_VIDNT.C:512`). Current Windows drivers answer that with a compatibility profile of their highest version (unverified per vendor; `gl_dump` prints the version, `GL_RMISC.C:95-102`). In a compatibility context fixed function code and GLSL can be mixed, so ReCoF can convert one pass at a time while everything else keeps working.

- **Require OpenGL 3.3 compatibility profile for the new path:** vertex array objects, GLSL 3.30, uniform buffers, instancing and `ARB_timer_query` are all core there. Use 4.3 or 4.4 features (multi draw indirect, persistent mapped buffers) only if the profiler later shows that submission still dominates.
- **Keep the retail path as the fallback**, picked automatically below 3.3 and by an `rc_` cvar: for old integrated GPUs (Intel HD 3000 class reports 3.1 on Windows, unverified), as the reference for screenshot comparisons, and as an escape hatch for runners. Decide later whether to drop it.
- **No core profile now.** A core context removes immediate mode, which the engine HUD, VGUI, TriAPI, sprites, particles, Cg water and all of Paranoia use. That is the rewrite MetaRenderer had to do.

### World

- The world already sits in a static VBO (`GenerateVertexArray`, `gl_rsurf.cpp:197-203`). The cost is draw calls and per surface state, not vertex transfer.
- Add an index buffer; each frame build index lists per material (texture and lightmap page) from the visible surface list; draw `GL_TRIANGLES` instead of `GL_POLYGON`.
- Replace the texenv combine, NV register combiner and ARB fragment program variants (`gl_rsurf.cpp`, `gl_light_dynamic.cpp`, `gl_postprocess.cpp`, `gl_studiomodel.cpp`) with GLSL that reproduces one reference look.
- Lights, first step: reuse the main visible surface list instead of a new BSP walk, cull surfaces against the light sphere, compute the light coordinates in the vertex shader from per vertex tangent data instead of one texture matrix per surface, and draw one indexed call per material per light. Second step: a forward shader that loops over the lights touching a batch (lights in a uniform buffer), so the world is drawn once instead of once per light.
- Draw calls go from surfaces times passes times lights to materials times lights, then to materials.

### Models

- Upload each studio model once (positions, normals, UVs, bone indices) into a VBO at load time, inside the loading window.
- Per draw, upload the bone matrices (up to 128 3x4 matrices, `MAXSTUDIOBONES`) into a uniform buffer, and do skinning, the Paranoia lighting formula (ambient, shade light, lambert or hemispherical term, dynamic lights, clamp by the largest component, `gl_studiomodel.cpp:1005-1058`) and chrome UVs in the vertex shader.
- Bone setup stays on the CPU: attachments come from those matrices, the camera attachment sets the view origin (`view.cpp:777`) and a light origin (`gl_renderer.cpp:1092`). Server hit boxes are computed separately in hw.dll and are not touched.
- Keep a hidden side effect: the per normal `Lighting` function also runs `StudioCalcAttachments` and `StudioClientEvents` for the dual wield left hand weapon, once per arming (`gl_studiomodel.cpp:1064-1086`, armed at `StudioModelRenderer.cpp:2311`). The GPU path must call that block once at the same point.

### Engine draws, readbacks, wrapper, water

- Sprites, particles, TriAPI and HUD quads can go through one streaming vertex buffer in hw.dll C. Expected share is small; do it only when the profiler says so.
- Readbacks (B255): track GL state in software instead of `glGet*`, `glIsEnabled` and `glPushAttrib`; keep the matrices `R_SetupGL` builds (`GL_RMAIN.C:1309`, `:1321`) and the VGUI projection (`vgui_EngineSurfaceHW.cpp:538`) in memory.
- The wrapper is a marker (`PARANOIA_HACKS_V1`, `OtherOGL.cpp:885`, required by `glmanager.cpp:201` and `gl_renderer.cpp:594`) plus removal of `GL_EXT_paletted_texture` from the extension string. Extension entry points bypass it (`opengl32.cpp:412-429`), so new GL 2+ calls never pass through it; its cost shrinks once per vertex calls are gone. Removing it later means dropping the marker check in the client and shipping no opengl32.dll; the CoF Linux Patcher patches that file, so check what Proton players need first.
- Cg water: port to GLSL with the B194 early out and screen sized targets; this also removes the Cg runtime.

## MetaRenderer

**What it is.** A MetaHookSv plugin (MIT, "Copyright (c) 2021 hzqst"; MetaHookSv itself is MIT) built with Visual Studio 2022 for x86, with GLEW, FreeImage and capstone as dependencies (`CMakeLists.txt`). It hooks engine functions found by byte signatures per engine build (`R_RenderView`, `R_DrawBrushModel`, `R_GLStudioDrawPoints`, `R_DrawSpriteModel`, `GL_BuildLightmaps`, `GL_SetMode*` and more, `src/gl_hooks.cpp`), creates a 4.x core profile context (`src/gl_rmain.cpp:5009-5013`) and emulates the legacy calls the engine still makes (`CoreProfile_glBegin` and others, `src/gl_hooks.cpp:66-76`, `:1390-1402`).

**Techniques worth learning.** World and brush models as VBO plus index buffer with per surface instance data and multi draw indirect (`memory/WorldSurfaceModel.md`). Studio models as a VAO per model with the bone matrices copied into a uniform buffer per draw and skinning in the shader (`memory/DrawStudioModel.md`, sections 4 and 5). A bone cache for multi pass reuse. Persistent mapped ring buffers for TriAPI geometry (`memory/DrawTriAPI.md`). Worker threads that build GPU resources for models and BSP leaves; by default those stay invisible until their data arrives (`r_studio_lazy_load`, `r_leaf_lazy_load`, `docs/en/features.md`). The frame itself stays on one thread as far as the docs show.

**Cry of Fear.** The README lists "GoldSrc_CoF (5936)" and the gamedata has a `cof-5936` identity (`memory/PrivateSymbols.md`), so it can find its hooks in the Cry of Fear hw.dll. But `IPluginsV4::LoadClient` stops with `Sys_Error("Renderer does not support Cry of Fear...")` when the game directory is `cryoffear` (`src/plugins.cpp:102-104`), added on 2026-10-10 (commit c896688b) after issue #12, a crash in client.dll right after the intro videos. So it neither replaces nor bypasses the Paranoia renderer; it refuses to run with it. Even without the crash it would not reach Cry of Fear's look: its takeover points are the engine's world and model functions, which this client bypasses, and Paranoia needs fixed function, NV register combiners, ARB programs and Cg, none of which exist in a core context. The crash cause is not stated in the issue; a plausible one (unverified) is `glGetString(GL_EXTENSIONS)` returning NULL in a core context, used unchecked by the wrapper (`OtherOGL.cpp:879-885`) and the client (`glmanager.cpp:23`).

| Option | What it means | Verdict |
|---|---|---|
| Port into the source | Bring in MetaRenderer and its hook layer, turn Paranoia off (`gl_renderer 0`), rebuild flashlights, bump, specular, Cg water, blur and grayscale in its deferred pipeline, accept its HDR, shadows and lighting model | No: large, changes the look, imports a hooking framework into a source build |
| Learn from it | Reuse its data layouts and ideas (studio VBO and bone UBO, world index batches, ring buffers, load threads), optionally copy small pieces with the MIT notice | Yes |
| Write ReCoF's own | Replace the internals of the Paranoia entry points in client.dll with GLSL, keep cvars, entry points and the look | Yes, this is the main path |

License: copying MetaRenderer code is allowed under MIT if its copyright and permission notice go with it (add the text to `LICENSES/`). Do not pull in FreeImage (GPL or the FreeImage license, unverified) or MetaHook's hooking layer.

## Threads

Rule: the order inside `_Host_Frame` stays as it is (`host.c:1190-1310`: `Cbuf_Execute`, `CL_Move`, `SV_Frame`, `ClientDLL_Frame`, `CL_ReadPackets`, `CL_EmitEntities`, `Host_UpdateScreen`, `Host_UpdateSounds`). Worker threads may only do work whose results are pure and are consumed at a fixed point of the same frame (fork and join), or work that never feeds back into game state.

| Work | Thread | Why and conditions |
|---|---|---|
| Engine sound mixing (`S_Update_`, `S_PaintChannels`, `SND_DMA.C:1539-1766`) | can move | No game state depends on it. Needs a lock or a command queue for `S_StartSound` from the main thread, pinning of sound data in the engine cache, and care with voice and the DSP state. Also removes the three `S_ExtraUpdate` calls during rendering. Medium effort; worth it only if `snd` in host_speeds is large. |
| Model, texture and normal map loading, VBO building | can move inside the load | Decode and build on workers, the main thread waits before loading ends, GL uploads stay on the GL thread. Do not stream after loading (pop in, and the LiveSplit load remover would count it as game time). Paranoia's per map setup already runs in the first rendered frame (`gl_renderer.cpp:625-631`), so its hitch may count today (unverified). |
| Bone setup for drawing | can move with fork and join | Compute bones of all visible studio entities in parallel before the draw loop, then draw in the original order. Needs per entity bone storage (today one engine buffer, `StudioModelRenderer.cpp:70`) and splitting `StudioDrawModel`, which mixes bones, events, lighting and drawing. Medium to large; only after GPU skinning and only if the profiler shows bones matter. |
| Particle and decal updates | stay for now | Engine particles update inside the draw loop (`R_PART.C:1434`); client particles, rain and grass call the engine random generator (`particlesys.cpp:1006`, `rain.cpp:165`, `grass.cpp:202`). That generator is shared with the server (`pr_cmds.c:2503`; client table `cdll_int.c:702-703`, server table `sys_dll.c:1030`), so a worker would race on it and change the sequence the server sees. Batch their drawing on the GPU instead. |
| GL driver work | already moves | Drivers with threaded optimization run submission on another core once the readbacks (B255) are gone. |
| Game logic, movement, prediction, packets, entity list, console commands (fps_max binds), `Host_FilterTime`, studio events, all GL calls on the one context | must stay | Movement, saves and timing depend on this order (speedrun.md); studio events play sounds and spawn effects; one GL context is bound to one thread. |

A render thread one frame behind the game is not recommended: it adds a frame of input latency, and the render code writes state the game side reads (camera attachment, dlights created in `HUD_AddEntity`, the world root bounding box rewritten during the view).

## The hw.dll compiler question

- **Most new GL code belongs in client.dll**, because Paranoia draws the world and models there. hw.dll needs only the profiler hooks, readback removal, the affinity default (B254), the limiter (B252), optional sprite, particle and TriAPI batching, and optionally a debug context through `wglCreateContextAttribsARB`.
- **VC6 C can host that.** OpenGL is a C API; 2.0+ entry points come from `wglGetProcAddress` into function pointers, as the client already does (`glmanager.cpp:444`, `:494-509`); GLSL is plain strings; threads use `CreateThread` and `Interlocked*`. Limits: C89 declarations, no `stdint.h` (Khronos `khrplatform.h` has a non C99 MSVC branch, unverified with VC6, otherwise write the typedefs by hand), x87 floats, an old debugger.
- **Cheaper engine win: the optimizer.** VC6 Professional is installed (toolchains.md) and can build render and sound units with `/O2`: `GL_RSURF.C`, `GL_RMAIN.C`, `GL_DRAW.C`, `GL_WARP.C`, `R_PART.C`, `r_triangle.c`, `r_trans.c`, `SND_DMA.C`, `SND_MIX.C`, the VGUI surface. Units that compute game state keep `/Od` (`pmove.c`, `pmovetst.c`, `sv_*.c`, `world.c`, `cl_pred.c`, `pr_cmds.c`, `host.c`, and `r_studio.c`, which also holds the hit box code): with `/O2` x87 intermediates stay in 80 bit registers where `/Od` rounds every stored value, which can change movement and traces (known mechanism, impact unverified).
- **Moving hw.dll to a current MSVC** means 238 files in `src/engine_hw`, four MASM files, VS 2005 objects, a new float model (SSE2 by default; `/arch:IA32` keeps x87 but the code still differs), every code and data address changing (autosplitter, RebuiltSimon, Linux patcher), and losing the byte match that DeCoF checks rely on. Large, and the GPU work does not need it. If the engine side ever grows (for example replacing the engine HUD and VGUI drawing), a new DLL built with a current MSVC behind a C interface, loaded by hw.dll, is the lighter alternative.

## Measurement plan

### What the user can test on retail today

1. Steam launch options `-noaff -condebug`. CoFLaunchApp appends its own arguments (`src/coflaunchapp/Form1.cs:209-210`), `CEngine::Load` then skips the core 0 pin (`sys_engine.cpp:99-106`), and `-condebug` writes `qconsole.log` (`CONSOLE.C:271`).
2. In a heavy scene, `host_speeds 1` prints per frame `fps host sv cl gfx snd ents` (`host.c:1057-1071`): `sv` is `SV_Frame` (hl.dll), `cl` the client work, `gfx` all drawing including the buffer swap, `snd` the sound update; `host` also contains the limiter wait. Run at `fps_max 100`, then with `developer 1; fps_max 1000` for the uncapped cost (developer 1 changes some behavior, for example B198, so only for measuring).
3. `r_speeds 1` calls `glFinish` before the 3D view and reads the clock after it without one (`GL_RMAIN.C:1723-1758`), so its ms is mostly the CPU time of the 3D view (HUD excluded) and the finish itself adds a stall; its poly counts cover only engine drawn geometry. `bump_debug 1` shows Paranoia's world polygon counts (`gl_rsurf.cpp:1626`).
4. One toggle at a time: `gl_dynlight 0`, `gl_specular 0`, `gl_bump 0`, `cg_water 0` (not on c_lake, where it can crash), `r_drawentities 0` (models), `gl_modelrenderer 0` (engine model path), `gl_renderer 0` (Paranoia off, the look changes), driver threaded optimization on and off, with and without `-noaff`.
5. Per core CPU and GPU load in Task Manager or HWiNFO, and `gl_dump` for the GL version the legacy context gets.

### Profiler to add first

`rc_prof 1` shows per pass averages over one second and `rc_prof_log 1` writes a CSV. CPU scopes use `QueryPerformanceCounter`; GPU scopes use `glQueryCounter(GL_TIMESTAMP)` from `ARB_timer_query`, read four frames later through `GL_QUERY_RESULT_AVAILABLE` so they never stall. Client scopes: `PrepareRenderer`, each `DrawBump` pass, each light, brush entities, decals, sky; per model bones, lighting and submission; particles, rain, grass, glows, Cg water, post process, HUD. Engine scopes: the five host phases already timed by `Host_Speeds`, then `R_SetupGL` and `R_MarkLeaves`, `R_DrawEntitiesOnList`, transparent entities, particles, view model, VGUI paint, swap, sound mixing. The hw.dll part keeps its state on the heap and its cvars in heap allocated `cvar_t`, and every build is checked with `tools/data_drift.py` and the link map against the autosplitter offsets.

### Expected gains (estimates)

| Change | Expected effect |
|---|---|
| `-noaff` | 0 to 10% on average, mainly fewer spikes |
| Readbacks removed, driver threaded optimization working | 0 to 15% in render heavy scenes |
| Render and sound units of hw.dll built `/O2` | Proportional to the engine's share of the frame, perhaps 5 to 15% |
| World batching and single pass lights | The largest item in lamp heavy scenes; world CPU time several times lower |
| GPU skinning and lighting | Model CPU time several times lower; most visible with many monsters and the detailed view models |
| All of the above | Heavy scenes on weak CPUs perhaps 1.5 to 3 times the frame rate. Scenes already at the 100 cap gain no fps but run with lower CPU load. The ceiling is the `sv` share (B106, B107), which this work does not touch. |

## Runner impact

- **Frame timing.** `Host_FilterTime` (`host.c:886-950`) is not touched and new code never sleeps or changes `realtime`. Below the cap, frames get shorter, exactly as on a faster PC; at the cap nothing changes except jitter. `fps_max` down to 4, the 100 cap without developer and `host_framerate` behave as today.
- **Fixed addresses.** client.dll moving to a current MSVC moves every client.dll address the autosplitter reads (cutscene state, in-game time, music, save state), so ReCoF ships its own autosplitter script or exposes the values by name (speedrun.md). For hw.dll, any growth of `.text` past a section boundary moves `.data` and with it the hw.dll offsets; check each build with `tools/data_drift.py`.
- **Loading.** Keep uploads inside the loading window so load removed times stay comparable and nothing pops in.
- **Visuals runners rely on.** Keep `cg_water 1` working on c_lake (B194), and keep the retail render path behind an `rc_` cvar.
- **Random numbers.** The shared generator is seeded from the clock (`pr_cmds.c:2490-2492`), so no run is reproducible today; keeping all generator calls on the main thread in their current order means ReCoF changes nothing here either.

## Risks

- **Look parity.** Paranoia has vendor specific paths (NV register combiners on NVIDIA, fallbacks elsewhere), so ReCoF must pick a reference look and check it with screenshot comparisons against the retail path.
- **Side effects hidden in render code:** the dual wield events in `Lighting`, the camera attachment driving the view, lights created while entities are added, the rewritten world root box. Each must keep its timing.
- **Driver spread:** GLSL and compatibility profile behavior on Intel, AMD and under Proton needs real machines (unverified).
- **Address drift** in hw.dll and client.dll breaking the autosplitter; **float changes** if game logic units of hw.dll get the optimizer.
- **Smaller gains than hoped** where `sv` dominates; measure first.
- **Size:** the 12 client renderer files named here hold about 13,000 lines, plus the engine immediate mode code.

## Verdict

**Possible:** yes. The world and models are drawn by client.dll code that ReCoF owns and will build with a current MSVC, the world is already in a VBO, the GL context is a compatibility context that can mix old and new code, and nothing in the GPU path needs to touch game logic or frame timing.

**Effort: large overall.** The GPU world and model paths in client.dll are medium to large each (GLSL parity for many Paranoia variants, multi pass light logic, hidden side effects). The cheap engine items (pin, readbacks, limiter, `/O2` on render units, water early out) are small. Threads are medium (sound mixer, load workers) to large (bone fork and join). Moving hw.dll to a current MSVC would be large and is not needed.

**Main risks:** look parity with Paranoia; hidden side effects in the model lighting and view code; GPU driver spread; data address drift for the autosplitter; float behavior if engine game logic units get optimized; gains capped by hl.dll game logic.

**Recommended path, in order:**

1. Measure on retail (host_speeds, r_speeds, bump_debug, toggles, `-noaff`).
2. Add the `rc_prof` profiler to client.dll and hw.dll.
3. Cheap engine and client items: no core pin by default with the pin opt in (B254), readbacks removed (B255), limiter sleep before the final spin (B252), water early out (B194), render and sound units of hw.dll at `/O2` with game logic units kept at `/Od`.
4. After client.dll moves to a current MSVC: GPU studio models (VBO, bone uniform buffer, Paranoia lighting in GLSL), behind an `rc_` cvar with the retail path as fallback.
5. GPU world: index batches per material, GLSL passes, batched light passes, then single pass lights; Cg water in GLSL.
6. Engine sprites, particles and TriAPI through a streaming buffer, if the profiler still shows them.
7. Threads: sound mixer, load workers, then bone fork and join if measured worthwhile. Game logic stays on one thread.
8. Later and optional: drop the opengl32 wrapper, core profile, hw.dll compiler move.

**First three concrete steps:**

1. The user runs the retail test: Steam launch options `-noaff -condebug`, a heavy scene (a monster heavy map from B106 and a lamp heavy area), `host_speeds 1` for ten seconds at `fps_max 100` and again uncapped, then the toggles one at a time, with per core CPU and GPU load noted, `gl_dump` output and `qconsole.log` kept.
2. Build `rc_prof` in client.dll (QPC scopes around every Paranoia pass, light, model phase and HUD effect, GL timestamp queries in a four frame ring) and the minimal engine scopes in hw.dll with heap state, then confirm with `tools/data_drift.py` that the autosplitter offsets in hw.dll did not move.
3. Prototype the GPU model path for the view model only behind `rc_gpumodels`, compare screenshots with the retail path, and measure the model scopes before and after.
