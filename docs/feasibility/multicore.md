# Feasibility: more CPU cores in the frame

Study of 2026-10-28 for [B288](../bugs.md) and B286. It extends [cpu_gpu.md](cpu_gpu.md) of 2026-10-26, which covered the GPU side from the code structure and put threads last; this page answers the thread question on its own and against a measured frame. Sources: the `rc_prof 2` dump below taken on the user's machine with ReCoF v0.1.2, the ReCoF tree in `src/` (line numbers as of this date), and the engines named under "What other engines do". Anything marked "unverified" or "estimate" has not been checked in the source or on a running game.

## The question

The game is slow on a modern laptop while seven of its eight cores idle. Can ReCoF spread the frame over more cores, how much is there to win, and can it be done without changing the picture, the game logic or the frame timing runners depend on?

## The measured frame

Machine: Intel Core i5-13420H, 8 cores (4 performance, 4 efficient) and 12 threads, with an NVIDIA GeForce RTX 3050 6GB laptop GPU next to an Intel UHD integrated GPU, Windows 10.

Scene: start of `c_doc_city` (The Doctor's Story), 1920x1080, no night vision, `fps_max 99`, `rc_prof 2`, ReCoF v0.1.2, drawn by the Intel UHD (see below). Frame 10.81 ms (92 fps), worst 14.22 ms, 49 models, 0 dynamic lights.

| Phase | CPU ms | GPU ms | What it is |
|---|---|---|---|
| other | 2.69 | 6.73 | engine, the hl.dll server, sound, the buffer swap and the limiter wait; the GPU figure is the gap, not work |
| world 1st | 3.38 | 1.24 | `RecursiveDrawWorld` and one `glDrawArrays(GL_POLYGON)` per visible surface |
| models | 2.69 | 0.88 | 49 studio models skinned and lit on the CPU |
| world 2nd | 0.69 | 0.77 | second world pass |
| hud | 0.49 | 0.83 | the HUD |
| specular | 0.36 | 0.33 | specular pass |
| transparent | 0.25 | 0.03 | fog, particles, grass, glows |
| sky, decals, water, view, entities, client frame, world, dlights, bump, post fx | 0.26 together | 0.08 together | the rest |

What the numbers say:

- **The frame is CPU bound and the GPU is mostly idle.** The GL scopes add up to 4.16 ms of real GPU work, and the 6.73 ms of "other" GPU time is the gap between the last mark of one frame and the first of the next, which is mostly the GPU waiting. So the GPU finishes its part in under 40% of the frame.
- **The client renderer owns 8.11 ms of the 10.81.** Everything outside it, which is the engine, the local server running hl.dll, sound and the swap, is 2.69 ms. That number has not been split yet; `host_speeds 1` does it.
- **The two big items are world submission (3.38 ms) and models (2.69 ms)**, and both cost several times more on the CPU than on the GPU, which is the signature of per surface and per vertex CPU work rather than of a busy GPU.
- **The limiter does not pollute this measurement.** `rc_prof` measures from `HUD_Redraw` to `HUD_Redraw`, so the wait inside `Host_FilterTime` falls into "other". Here the frame is 10.81 ms against the 10.10 ms the 99 cap allows, so the scene is over the cap and the wait is near zero. Any measurement in a scene that runs under the cap must be taken with `developer 1` and `fps_max 1000`, or "other" is inflated by the wait.

Not measured yet and needed before any of this is planned further:

- **Night vision**, reported at 40 to 50 fps in the same campaign. B198 and B199 brought the light pass from 3.3 ms to 1.6 ms, which does not explain a 20 to 25 ms frame, so something else dominates there. The leading candidate found since, in the renderer audit of the same day, is B291: while any dynamic light is alive the first pass draws every visible surface with the lightmap alone and the second pass draws all of them again for the texture, so the light does not add one pass, it roughly doubles the world. Night vision keeps its radius 900 light alive every frame, so it always turns that path on. The other candidates are the full screen `glCopyTexSubImage2D` grayscale copy and its draw back (B199, still in), the green full screen VGUI panel, and the extra light reaching every model through the per normal loop. A `rc_prof 2` dump with the mask on is the only way to tell, and the answer may well be a GPU item, not a CPU one.
- **Settled on 2026-10-28, and it changes the baseline: the game was running on the Intel UHD, not on the RTX 3050.** `gl_dump` printed `GL Renderer: Intel(R) UHD Graphics`, `GL Version: 4.6.0`, and the client logged the same at video init. Nothing asked for the discrete adapter: cof.exe exports no `NvOptimusEnablement` and there was no per application preference for it (B296). That is why 4.16 ms of GPU work looked high. It also means the CPU numbers above were taken against the Intel GL driver, whose cost per draw call and per state change is higher than NVIDIA's, so `world 1st` and the other submission scopes have to be measured again on the discrete GPU before anything is planned from them.
- **The split inside `models`**, between bone setup, lighting, per vertex skinning and GL submission. The thread plan below stands or falls on that split, and `rc_prof` does not have those sub scopes yet.

### Measured again on the discrete GPU

After B296 was found and the process was moved to the RTX 3050, the same campaign was measured again at 1920x1080, this time uncapped (`developer 1`, `fps_max 1000`). The spot is near the first one but not the same: 32 models instead of 49, and one dynamic light alive instead of none, so this is not a clean A and B against the table above. Frame 7.63 ms (131 fps), worst 11.39 ms.

| Phase | CPU ms | GPU ms |
|---|---|---|
| world 1st | 2.30 | 0.22 |
| dlights (one light) | 1.70 | 1.94 |
| other | 1.61 | n/a |
| models (32) | 0.84 | 0.06 |
| world 2nd | 0.45 | 0.16 |
| hud | 0.25 | 0.16 |
| specular | 0.17 | 0.05 |
| transparent | 0.14 | 0.01 |
| post fx | 0.05 | 0.35 |
| the rest | 0.12 together | 0.09 together |

Read `gpu frame` with care on this driver. It printed 56.22 ms while the game ran at 131 fps, which cannot be GPU busy time. `rc_prof` measures it as the span between the first and the last timestamp of one frame, and the NVIDIA driver queues several frames deep where the Intel one did not, so the span measures submission latency and the leftover lands in "other". The GPU work is the sum of the scopes, 2.98 ms, against 7.63 ms of CPU: still clearly CPU bound. `rc_prof` should print that sum next to the span.

Three things change in the plan:

- **The model path stops being a thread target.** `models` went from 2.69 ms for 49 models to 0.84 ms for 32, which is 0.055 ms per model down to 0.026 ms, halved by changing nothing but the driver. So a large part of that scope was submission cost and not arithmetic. On the discrete GPU the whole scope is 0.84 ms, and spreading its pure math over four cores would save something like 0.3 ms of 7.63, about 4%. That is not worth a job system, per worker scratch buffers and the hazards of [B294](../bugs.md).
- **One dynamic light costs 1.70 ms, 22% of the frame.** That is the full second walk of the BSP, a normal map bind and a per surface tangent matrix (`SetupTexMatrix`, three GL calls) and a draw call for every surface the light reaches. It is B195 exactly, and night vision keeps one of these alive permanently.
- **World drawing is 4.45 ms of the 7.63, 58% of the frame**, counting the first pass, the light and the second pass, and every millisecond of it is GL submission that no worker thread can take.

The conclusion the first measurement already pointed at is now firm: **batching the world is not the first item of the plan, it is almost the whole plan**, and multicore is a small item behind it.
### Night vision, measured

Same spot, same session, 1920x1080, uncapped, 49 models, on the discrete GPU. Three dumps: night vision on, night vision on with `gl_dynlight 0` (which skips the light pass but leaves the two pass flag of B291 set), and night vision off.

| Phase | mask on | mask on, gl_dynlight 0 | mask off |
|---|---|---|---|
| frame | 8.37 ms (119 fps) | 7.00 ms (143 fps) | 7.00 ms (143 fps) |
| dlights | 1.84 | 0.00 | 0.00 |
| world 1st | 2.58 | 2.54 | 2.35 |
| world 2nd | 0.48 | 0.57 | 0.40 |
| models (49) | 1.13 | 1.20 | 1.09 |
| other | 1.50 | 1.78 | 2.24 |
| post fx | 0.04 (0.35 gpu) | 0.05 (0.29 gpu) | 0.00 |

`bump_debug` in the same spot: 4840 world polygons in view, 3747 bumped, 1093 normal, 0 low quality and 468 high quality specular. So the world costs about 9000 draw calls a frame before the light pass is counted.

- **The mask costs 1.37 ms and it is the light pass.** The grayscale copy of B199 is 0.35 ms of GPU time in a frame that is CPU bound, so it does not show. The user's original report of 40 to 50 fps with the mask was the integrated GPU (B296); on the discrete one the mask costs 16% of the frame rate.
- **B291 costs 0.36 ms, near 5%.** Three quarters of the visible world is bumped and already draws twice whatever the flag says, so the doubling only reaches the 1093 normal polygons. Real, cheap to fix, not the headline.
- **The run to run drift of "other" across these three dumps is 0.74 ms**, which is larger than several of the deltas above. Any change worth less than about a millisecond has to be measured as a pair in one session, not across runs.
### What is inside "other", measured

`host_speeds 1` in the same spot, 1068 frames, whole milliseconds (the resolution of that output), with night vision off. The logging itself writes a console line per frame into `qconsole.log`, which costs about 2 ms a frame, so the frame rate here reads 110 instead of 143 and only the shares matter.

| Phase | mean ms | frames at 0 |
|---|---|---|
| gfx (the whole screen update, which contains every client render scope) | 8.41 | 0 |
| host (the limiter and the rest of the loop) | 0.06 | 1006 of 1068 |
| sv (`SV_Frame`, the local server running hl.dll) | 0.01 | 1062 of 1068 |
| cl (the client frame) | 0.00 | 1067 of 1068 |
| snd (the engine sound mixer) | **0.00** | 1068 of 1068 |

That settles the last open question, and it settles it against threads:

- **The sound mixer costs nothing.** It read 0 in every one of the 1068 frames. Moving it to its own thread, which this page listed as the one medium sized engine side item, would buy nothing at all. Drop it.
- **The server costs nothing in this scene.** 514 entities and `sv` at 0 in 1062 frames. B106 and B107 may still matter on the monster heavy maps, but they are not what makes this frame slow.
- **The whole frame is the render path**, so `other` in the `rc_prof` table is not the engine's game work: it is the engine's own render work that sits outside the client's scopes, which is `R_SetupGL` with its matrix readbacks, `R_MarkLeaves`, the engine's entity, sprite and particle drawing, the view model, the VGUI paint and `SwapBuffers`, plus the limiter wait.
## What the numbers mean for threads

Threads move work to another core, they do not remove it. Every part of this frame is one of three kinds, and only one kind can move.

| Kind | What it is | Can a worker thread take it | In this frame |
|---|---|---|---|
| GL submission | driver calls on the thread that owns the context | No. One context belongs to one thread, and making it current elsewhere per frame costs more than it saves | `world 1st` 3.38, `world 2nd` 0.69, `specular` 0.36, `hud` 0.49, part of `models` |
| Pure CPU math | reads model and world data, writes a private buffer, calls nothing | Yes, with fork and join | most of `models`, bone setup, engine sound mixing |
| Game and order bound | server, movement, prediction, packets, entity list, studio events, the random generator | No. Movement, saves and timing depend on this order | `other` in part, `view`, `entities`, `client frame` |

`world 1st` is the largest line in the frame and it is the first kind. Per visible surface it binds a lightmap, sets the texture environments, binds the texture and issues one `glDrawArrays(GL_POLYGON)` (`gl_rsurf.cpp` `DrawNormalPoly`, `DrawPolyFromArray`). The BSP walk itself (`gl_renderer.cpp:492` `RecursiveDrawWorld`) only marks and culls; the time is in the driver calls. No thread can take that work. Fewer and larger draw calls can, which is exactly B195.

So the ceiling for threads is the pure math inside `models` plus whatever part of "other" is sound mixing. On the Intel measurement that looked like 2 ms of 10.81, 15 to 20%; on the discrete GPU it is a few tenths of a millisecond of 7.63, under 5%. **Threading is not the lever here.** What is left for it is the sound mixer and the loading workers, neither of which is in the render path.

## What other engines do

| Engine | Frame on more than one thread | What was actually done | Evidence |
|---|---|---|---|
| GoldSrc retail, Half-Life 25th Anniversary Update (Valve, November 2023) | No | Higher engine limits (edicts, packet entities, sound channels, textures), OpenGL optimizations aimed at the Steam Deck, and a "Lower Input Latency" option that synchronizes the CPU and the GPU, which lowers latency and does not add cores | the update's patch notes |
| Xash3D FWGS, the most active open source GoldSrc compatible engine | No | The only thread creation in the repository is in `engine/common/net_ws.c`; a code search for `CreateThread`, `SDL_CreateThread`, `pthread_create`, `_beginthreadex`, `mutex` and `atomic` across the tree returns nothing else, and the README does not mention threads. A web summary claiming it has a job system, parallel snapshot encoding and `net_graph 4` worker loads was checked against the repository and is false | GitHub code search over FWGS/xash3d-fwgs, 2026-10-28 |
| MetaHookSv MetaRenderer | Partly | Worker threads build GPU resources for studio models and BSP leaves (`r_studio_lazy_load`, `r_leaf_lazy_load`) and the objects stay invisible until their data arrives; the frame itself stays on one thread | cpu_gpu.md, read at commit 7225f589 |
| Source, Valve's next engine | Yes | Two mechanisms. A job thread pool in `public/vstdlib/jobthread.h`: `IThreadPool` with `Start`, `Stop` and a job queue, `CJob` with priorities, and the helpers `ParallelProcess`, `ParallelLoopProcess`, `IterRangeParallel` and `CParallelProcessor`. And a queued material system, `mat_queue_mode`, where drawing is recorded on the game thread and replayed by a render thread a frame later. The queued mode is a known source of bugs outside its default and costs a frame of latency | source-sdk-2013, `src/public/vstdlib/jobthread.h` |
| idTech 4 and later | Yes | A front end that builds the frame's draw data and a back end that submits it, on two threads | the published engine sources |

The useful conclusion: **no GoldSrc engine threads its frame**, not Valve's own 2023 update, not the most active fork. The one close relative that does, Source, does it with a job pool for pure work plus a render thread one frame behind. The first half of that fits ReCoF. The second does not, for the reasons below.

## Why GoldSrc resists threading

Each of these is confirmed in the ReCoF source, and each one constrains the design.

1. **One GL context on one thread.** Everything that touches GL stays where it is. That rules out splitting the drawing itself.
2. **The random generator is shared with the server.** The client calls `gEngfuncs.pfnRandomFloat` from inside the model vertex loop for corona textures (`gl_studiomodel.cpp:2171`, `:2229`) and from particles, rain and grass. That generator lives in the engine and the server uses the same one (`pr_cmds.c`), so a worker calling it would change the sequence the server sees. Any parallel code must not call it, and models that use it must keep their calls on the main thread in the original order.
3. **Engine state inside the model path.** `IEngineStudio.StudioEntityLight` and `StudioSetupLighting` are calls into hw.dll that write engine globals, and `IEngineStudio.StudioClientEvents` plays sounds and spawns effects (`StudioModelRenderer.cpp:1297`, `:1316`, `:1276`). None of that can run on a worker; a parallel model pass has to be split so these stay on the main thread in order.
4. **One set of scratch buffers.** `g_xformverts`, `g_lightvalues` and `g_chrome` are single global arrays of `MAXSTUDIOVERTS` (2048) entries (`gl_studiomodel.cpp:67-84`), the strip buffer `s_RcStrip` is one array of 4096 vertices (`gl_studiomodel.cpp`, B287 step 2), and `m_pbonetransform` points at one engine buffer. Parallel work needs one copy per worker, which is about 210 KB each, so the memory is not the problem; finding every global is.
5. **Determinism for runners.** Movement, saves and the autosplitter depend on the order inside `_Host_Frame` (`host.c`: `Cbuf_Execute`, `CL_Move`, `SV_Frame`, `ClientDLL_Frame`, `CL_ReadPackets`, `CL_EmitEntities`, `Host_UpdateScreen`, `Host_UpdateSounds`). That order cannot change, and nothing threaded may feed back into it.
6. **The model cache.** The engine may reload model data through its cache while drawing. Whether `Mod_Extradata` can evict or reload under a worker has not been checked; it must be, before model data is read off the main thread.

## What can move, ranked

| Work | Mechanism | Hazards to handle | Estimated gain | Effort |
|---|---|---|---|---|
| Studio skinning and lighting | fork and join over the visible models, each worker filling its own vertex buffer, the main thread submitting them afterwards in the original order | per worker scratch buffers; the corona random calls (keep those models serial or draw their values on the main thread first); lighting setup stays on the main thread because it calls the engine | unknown until `models` is split; if 2 of the 2.69 ms is math, 4 workers leave about 0.6, so roughly 1.4 ms or 13% of this frame | small to medium |
| Bone setup | same fork and join, one pass earlier, per entity bone storage instead of the one engine buffer | attachments and `StudioClientEvents` must still run on the main thread in order, after the bones are ready | unknown, likely small here (49 models, mostly static) | small, once the job system exists |
| Engine sound mixing (`S_Update_`, `S_PaintChannels`) | dropped | none left to handle | **none: measured at 0.00 ms in all of 1068 frames** | not worth doing |
| Model, texture and normal map decoding, and buffer building, during loading | workers inside the load window, the main thread joins before loading ends, GL uploads stay on the GL thread | nothing may pop in after loading, or the LiveSplit load remover counts it as game time | shorter loads, no effect on frame rate | medium |
| Particle and decal updates | not recommended | they call the shared random generator | none | n/a |

## Designs that are rejected

- **A render thread one frame behind (the Source `mat_queue_mode` shape).** It adds a frame of input latency, which runners will notice, and the render code here writes state the game side reads back in the same frame: the camera attachment sets the view origin (`view.cpp`), dynamic lights are created while entities are added, and the world root bounding box is rewritten during the view. Recording and replaying all of that correctly is a rewrite of the client renderer, for a latency cost.
- **Threading `SV_Frame` or anything in hl.dll.** It is the game, and the project rule is that game logic stays on one thread.
- **A parallel BSP walk feeding parallel GL submission.** The submission cannot leave the GL thread, so the walk would be parallelised and the 3.38 ms would stay.
- **Moving the whole client frame to a second thread.** Same problem as the render thread, with the addition that the engine calls into the client from several points of `_Host_Frame`.
- **A second GL context sharing objects, for uploads during gameplay.** Worth it only for loading, where it is simpler to decode on workers and upload on the main thread.

## Proposed design: `rc_threads`

A small fork and join job system, no more than that, living in client.dll where the work is and where the current MSVC gives modern atomics.

- **Shape.** N worker threads created once when the client starts and parked on an event. The main thread fills an array of items, posts one batch, then helps with the batch itself and waits for the others. No work crosses a frame boundary, no job spawns another job, no queue survives the join.
- **Size.** `rc_threads`, default the number of performance cores minus one, capped at 4; `rc_threads 0` is the retail behaviour and the fallback. The cap matters on this laptop: a fork and join barrier waits for the slowest worker, and the 4 efficient cores of an i5-13420H finish later than the 4 performance ones, so more workers is not automatically faster. B254 already removed the pin to core 0, so the scheduler is free to place them.
- **What a job may not do.** No GL call, no engine call through `gEngfuncs` or `IEngineStudio`, no random generator, no allocation, no console print, no cvar read other than a value copied before the batch.
- **Determinism.** Each item writes only into its own slot, and the main thread consumes the slots in the original order. The picture must not depend on which worker finished first, and with `rc_threads 0` it must be identical, which is the screenshot test.
- **Scratch.** One block per worker holding the equivalents of `g_xformverts`, `g_lightvalues`, `g_chrome` and `s_RcStrip`, about 210 KB each, allocated once.
- **Measurement.** New `rc_prof` sub scopes inside `models` (bones, lighting, skinning, submission), a count of items per batch, and the time the main thread spends waiting at the join. A join that costs more than the work saved has to show up as a number, not as a feeling.
- **hw.dll.** Nothing of this goes there now. The sound mixer thread is the only engine side item, it is plain Win32 and VC6 can host it, and it has to be checked against the autosplitter offsets with `tools/data_drift.py` like every other hw.dll change.

## How this interacts with the GPU work

The two lines that matter most in the measured frame are ordered against each other like this:

- **B292 is now the best value in the plan**, because the counts say how big it is: 3747 bumped polygons each paying four GL calls to load a texture matrix that never changes, about 15000 wasted calls a frame, plus two of the three matrix calls per surface in every light pass. It is a state cache in one file, no new data and no look risk. **B291, B293 and B295** follow it: a frustum test before the two pass path (worth the measured 0.36 ms), hoisting the per texture work out of the second pass loop, and splitting the per frame setup from the per entity setup.
- **B195, batching the world,** attacks 3.38 ms that threads cannot touch at all. It is the single biggest item in the frame and it also makes every dynamic light pass cheaper, which is where night vision lives. It comes first.
- **B287 step 3, models,** has two possible answers for the same work. GPU skinning removes it; parallel CPU skinning spreads it. They do not add up: whichever lands, the other stops being worth much for the default path. Parallel skinning is far cheaper to build, carries no look risk because the arithmetic is unchanged, works on every GPU, and leaves a fast CPU fallback behind when the GPU path arrives. GPU skinning wins by more but costs GLSL parity work against the Paranoia lighting formula.

The honest recommendation is to let the measurement decide: split `models` in `rc_prof` first, and build the job system only if the per vertex math is 1.5 ms or more. Below that the join overhead eats the win and the effort belongs to B195.

## Runner impact

- `Host_FilterTime` is untouched, no worker sleeps or changes `realtime`, and the frame order stays as it is, so below the cap frames simply get shorter and at the cap nothing changes.
- No worker calls the random generator, so the sequence the server sees is unchanged.
- `rc_threads 0` reproduces the current behaviour exactly and stays in the build.
- The job system lives in client.dll, whose values the autosplitter already finds through the `g_RcAutosplit` tag. The sound mixer thread, when it comes, touches hw.dll and needs the usual offset check.

## Risks

- **The win is small.** On the measured frame the ceiling is around 2 ms, and that assumes the split inside `models` is favourable.
- **A global missed.** The model path has globals in several files; one missed array is a race that shows up as flickering geometry on one machine and never on another.
- **The model cache** reloading data under a worker (point 6 above), which has not been checked.
- **Efficient cores** making the join wait longer than the work saved on this class of laptop.
- **The wrong bottleneck.** The frame measured here was drawn by the Intel integrated GPU (B296), so part of the night vision case may be a GPU problem that none of this touches, and the submission scopes have to be measured again on the discrete GPU.

## Verdict

**Possible: yes. Worth doing for the frame rate: no.** Every measurement taken on 2026-10-28 points the same way. The two candidates this page opened with are gone: the sound mixer measures 0.00 ms in 1068 frames, and the studio model path fell from 2.69 ms to around 1.1 ms by changing nothing but the GPU driver, which shows most of it was submission and not arithmetic, leaving perhaps 0.3 ms for four cores to share. What is left of the frame is GL submission on the one thread that owns the context, and no worker can take a single call of it.

The one thread item still worth building is **workers inside the loading window**, for model, texture and normal map decoding. It shortens loads and changes no frame.

**What the frame actually is.** At 1920x1080 on the discrete GPU, 7.00 ms with no light and 8.37 ms with night vision, against 2.98 ms of real GPU work. About 9000 world draw calls per frame (4840 in the first pass, 3747 in the second, 468 specular), plus a full extra pass per dynamic light. The answer to that is fewer and larger draw calls, not more cores.

**Recommended path, in order:**

1. **B292**, a state cache for the texture matrix and the active unit. 3747 bumped polygons each pay four GL calls a frame to load an identity that never changes, about 15000 calls that do nothing, and every lit surface of every dynamic light pays two more in `SetupTexMatrix`. One file, no new data, no look risk, and it attacks both the first pass and the light pass.
2. **B291** (0.36 ms measured), **B293** and **B295**: a frustum test before the two pass path, the per texture work hoisted out of the second pass loop, and the per frame setup split from the per entity setup.
3. **B195**, the real work: index batches per material so the world is a few dozen draw calls instead of nine thousand, and the light pass reuses the visible surface list instead of walking the BSP again. This is where the remaining several milliseconds are, and it is what makes night vision cheap.
4. **B294** and **B287 step 3** on the model path, for the per vertex work that is left, as a GPU path rather than a threaded one.
5. **B296** shipped, so no laptop player starts on the integrated GPU.
6. Loading workers, when loads are the complaint.

**Not to be built:** a render thread one frame behind, a threaded server, parallel GL submission, a sound mixer thread, and a fork and join job system for studio skinning. The first three are unsafe for this engine, the last two measured too small to pay for themselves.

**Measurement rule learned here:** the `other` scope drifted 0.74 ms between three dumps of one session, so anything worth less than about a millisecond has to be measured as an A and B pair back to back, in the same spot, in one run. And `gpu frame` on the NVIDIA driver measures submission latency, not GPU busy time; the sum of the scopes is the GPU work. `rc_prof` should print that sum.
