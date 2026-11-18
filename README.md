<p align="center">
  <img src="icon.png" width="180" alt="BlurRecomp icon">
</p>

# BlurRecomp

**A native macOS (Apple Silicon) port of *Blur* (Xbox 360, 2010) via static recompilation.**

By [AbadXDev](https://github.com/AbadXDev).

> 🚧 **Work in progress — not playable yet.** This repository tracks the project's progress. The source code will be published here when the game renders its first frames.

## Status (2026-11-18)

- ✅ Original game image verified and extracted (Title ID `41560845`), byte-identical to the source image
- ✅ Full PPC → C++ recompilation: 76,019 functions, native arm64 build
- ✅ The game **boots natively on macOS**: kernel phase reaches Runtime, all boot packages mount (`root`, `root_streaming`, `shaders-speed`, `gamedata`)
- ✅ Boot sequence completes: the main loop runs on its own thread, `Start` runs, and the loader finishes the boot packages (`gamedata_boot.pak`)
- ✅ Intro movies load and play through (Bink), and the master shader files load — 562 files per boot
- ✅ The engine's screen framework dispatches every frame (thousands of dispatches per boot)
- ✅ Stability hardening: a host threading race that froze movie playback is fixed; a stuck GPU frame no longer freezes the game (fence watchdog); a runaway memory-budget report job is neutralized
- ✅ Xenos PM4 command stream decoded (hardware ring + indirect buffers) and executed on the host (CP executor with fence/flip protocol); frame loop presents at **~65–90 fps**
- ✅ The game's UI script system is live: the host now serves the executable's embedded `UIScript` section (137 script entries), the UI manager builds its registry, and its script-stream processor dispatches to the correct handlers
- ✅ Root-caused and fixed a host allocator bug that handed the game's own loaded image out as free physical memory — the streaming system was overwriting the running code; the image now stays intact and the main loop runs deep into the boot (100k+ ticks per session)
- ✅ Hardened the host's XAM notification API: the game polls it with a null handle, which previously read a phantom listener at guest address 0
- 🔄 In progress: a small set of remaining sporadic crashes is localized (a host video-thread race, two guest string-table walks, an early-boot null critical section) and being closed out one by one
- 🔄 In progress: re-pointing the host's D3D layer at Blur's own device functions (the inherited template's 105 graphics hooks target other games) so the game's draw stream reaches the GPU
- ⬜ Draw execution on the host GPU (DRAW_INDX / IM_LOAD)
- ⬜ Xenos shader microcode translation
- ⬜ Input, audio, save data

## Progress log

- **2026-11-16** — Project start: game image verified and extracted; full recompilation builds and boots to a frame loop.
- **2026-11-17** — PM4 command stream decoded and the frame loop stabilized at ~90 fps. Loader breakthrough: the main loop was running synchronously on the boot thread and starving `Start` — with the loop on its own thread, the boot sequence completes, the boot packages load and the intro movies play. The movie freeze was traced to a lost-wakeup race in the host's synchronization objects and fixed. The engine's screen framework now dispatches every frame. Two intermittent freezes (a GPU fence that never completes, and a memory-budget report job spinning on a collapsed pointer) were root-caused, contained and verified live. Current frontier: the first screen never hands over to the next one.
- **2026-11-18** — The UI layer comes alive, and the deepest bug so far falls. The UI script table was empty because the host's `XexGetModuleSection` was an empty stub — implementing it feeds the manager its 137 embedded scripts. Its script processor also dispatched through a jump table the code generator never registered; a verified switch bridge now routes all 29 cases. But the table kept arriving corrupted in memory while the file was perfect. A write-trap on the code section caught the game's own streaming `memcpy` filling ~3 MB of `.text` with file data — and the trail ended at the host's physical-memory allocator, whose free list covered the *entire* guest window, loaded image included: the game was being handed its own code as scratch memory. Excluding the image and the host function table fixed it — verified across repeated boots: zero writes to `.text`, correct dispatch targets, main loop running 100k+ ticks. Also fixed along the way: a macOS saved-state alert that silently stalled boots, and an unguarded XAM notification call the game makes with a null handle.

## What is this?

*Blur* never received a native Mac port. BlurRecomp recompiles the original Xbox 360 executable into native C++ (the same approach as Unleashed Recompiled and other XenonRecomp/ReXGlue projects) and pairs it with a host runtime for graphics, audio and input — so the game can run natively on Apple Silicon without emulation.

## Requirements

- A Mac with Apple Silicon
- **Your own legally obtained copy of *Blur* for Xbox 360** (Games on Demand image). No game assets are included in this repository or distributed with the app, and none ever will be.

## Roadmap

1. ~~Boot to a stable frame loop~~ ✅
2. First rendered frame from the game's own draw stream
3. Title screen
4. Menus and car select
5. In-race gameplay
6. Audio, saves and polish
7. Public release build

## Credits

Built on the Xbox 360 recompilation ecosystem (XenonRecomp / ReXGlue) and informed by the community's work on Marathon/Unleashed Recompiled. *Blur* © Activision / Bizarre Creations. This is a non-commercial, fan-made preservation project.

## License

MIT
