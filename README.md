<p align="center">
  <img src="icon.png" width="180" alt="BlurRecomp icon">
</p>

# BlurRecomp

**A native macOS (Apple Silicon) port of *Blur* (Xbox 360, 2010) via static recompilation.**

By [AbadXDev](https://github.com/AbadXDev).

> 🚧 **Work in progress — not playable yet.** This repository tracks the project's progress. The source code will be published here when the game renders its first frames.

## Status (2026-11-17)

- ✅ Original game image verified and extracted (Title ID `41560845`), byte-identical to the source image
- ✅ Full PPC → C++ recompilation: 76,019 functions, native arm64 build
- ✅ The game **boots natively on macOS**: kernel phase reaches Runtime, all boot packages mount (`root`, `root_streaming`, `shaders-speed`, `gamedata`)
- ✅ Boot sequence completes: the main loop runs on its own thread, `Start` runs, and the loader finishes the boot packages (`gamedata_boot.pak`)
- ✅ Intro movies load and play through (Bink), and the master shader files load — 562 files per boot
- ✅ The engine's screen framework dispatches every frame (thousands of dispatches per boot)
- ✅ Stability hardening: a host threading race that froze movie playback is fixed; a stuck GPU frame no longer freezes the game (fence watchdog); a runaway memory-budget report job is neutralized
- ✅ Xenos PM4 command stream decoded (hardware ring + indirect buffers) and executed on the host (CP executor with fence/flip protocol); frame loop presents at **~65–90 fps**
- 🔄 In progress: the game holds on its first (boot/loading) screen — only one screen is registered and nothing registers the next one yet; tracing what that screen is waiting for
- ⬜ Draw execution on the host GPU (DRAW_INDX / IM_LOAD)
- ⬜ Xenos shader microcode translation
- ⬜ Input, audio, save data

## Progress log

- **2026-11-16** — Project start: game image verified and extracted; full recompilation builds and boots to a frame loop.
- **2026-11-17** — PM4 command stream decoded and the frame loop stabilized at ~90 fps. Loader breakthrough: the main loop was running synchronously on the boot thread and starving `Start` — with the loop on its own thread, the boot sequence completes, the boot packages load and the intro movies play. The movie freeze was traced to a lost-wakeup race in the host's synchronization objects and fixed. The engine's screen framework now dispatches every frame. Two intermittent freezes (a GPU fence that never completes, and a memory-budget report job spinning on a collapsed pointer) were root-caused, contained and verified live. Current frontier: the first screen never hands over to the next one.

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
