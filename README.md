<p align="center">
  <img src="icon.png" width="180" alt="BlurRecomp icon">
</p>

# BlurRecomp

**A native macOS (Apple Silicon) port of *Blur* (Xbox 360, 2010) via static recompilation.**

By [AbadXDev](https://github.com/AbadXDev).

> 🚧 **Work in progress — not playable yet.** This repository tracks the project's progress. No game assets are included here or distributed with the app, and none ever will be.

## Status (2026-11-19)

- ✅ Original game image verified and extracted (Title ID `41560845`), byte-identical to the source image
- ✅ Full PPC → C++ recompilation: 76,019 functions, native arm64 build
- ✅ The game **boots natively on macOS**: kernel phase reaches Runtime, all boot packages mount (`root`, `root_streaming`, `shaders-speed`, `gamedata`)
- ✅ Boot sequence completes: the main loop runs on its own thread, `Start` runs, and the loader finishes the boot packages (`gamedata_boot.pak`)
- ✅ Intro movies load (Bink), and the master shader files load — 562 files per boot
- ✅ The engine's screen framework dispatches every frame (thousands of dispatches per boot)
- ✅ Stability hardening: a host threading race that froze movie playback is fixed; a stuck GPU frame no longer freezes the game (fence watchdog); a runaway memory-budget report job is neutralized
- ✅ Xenos PM4 command stream decoded (hardware ring + indirect buffers) and executed on the host (CP executor with fence/flip protocol); frame loop presents at **~65–90 fps**
- ✅ The game's UI script system is live: the host now serves the executable's embedded `UIScript` section (137 script entries), the UI manager builds its registry, and its script-stream processor dispatches to the correct handlers
- ✅ Root-caused and fixed a host allocator bug that handed the game's own loaded image out as free physical memory — the streaming system was overwriting the running code; the image now stays intact and the main loop runs deep into the boot (100k+ ticks per session)
- ✅ Hardened the host's XAM notification API: the game polls it with a null handle, which previously read a phantom listener at guest address 0
- ✅ **Shaders recompiled (2026-11-18):** 281 shader containers swept from guest RAM; a real cache of **279 entries, 243 with compiled Metal (AIR) and SPIR-V**. Verified live: 3/3 boots alive at 90 s with the cache linked, no regression.
- ✅ **First rendered frame (2026-11-18):** the PM4→plume translator now executes the game's own draws on the host GPU. The first visible result was a black triangle from the game's fullscreen quad (vertices arrive in pixel coordinates; the pixel→clip conversion fixed the invisible-pixel bug). Post-process quads (A79D family, 7 shader hashes) are translated too.
- ✅ **Ghost gamepad (2026-11-19):** the template only reads physical pads, so the keyboard never reached the game as a pad. A software ghost pad (START/A pulses) changes the game's boot path measurably — a new shader (F336FFD4) appears only with a pad — and its draws now produce pixels in the correct quadrant.
- ✅ **Post-process chain closed (2026-11-19):** the game's real RAM data now reaches the screen through its own post-process path — RAM → texture → emulated EDRAM → resolve copy → destination surface → the game's own quads → Present. It arrives as dense multicolour noise in the upper-left quadrant: real game data, still in the Xbox GPU's tiled format, not a recognisable image yet. The cut was the quad's geometry, not its pixel shader: the pending quad stored a pointer into a rotating 4-buffer vertex cache (`s_vbRectCache176`), and other draws overwrote those 96 bytes before Present, so the quad drew with someone else's geometry. A test with the proven copy-quad geometry painted the noise (run1199); the background fix — snapshotting the 96 bytes into a private buffer at registration (v9.239) — was then verified with the quad's own geometry (run1201). Each earlier suspect was ruled out with a test: the texture link (fixed, 40/40), slot contention (20/20 drains, still black), and the game's pixel shader (run1198: even the simple copy shader painted nothing, so the shader was not the cut at that step). The chain now stands with one test substitution: the synthetic copy pixel shader. Retrying the game's own pixel shader (run1202, v9.240) paints black again — it returns black without its constants, which arrive in SET_CONSTANT packets the translator records but never applies (the constant reads as zero). The synthetic copy shader stays as the surface quad's configuration (v9.241) while that constants front is worked. Next: detile the Xenos-tiled data into a recognisable image — the constants front is parked behind detiling, since the copy shader is faithful for a copy pass. A raw-data dump (v9.242) is in progress so detiling can be tested offline, without depending on game boots.
- 🔄 **In progress — boot lottery and IRQ crash:** healthy boots run ~50–75%; the rest stall early or die young. One killer is caught: the game's graphics-interrupt handler jumps to address **1** when the interrupt fires before its handler pointer is registered (crashes 2026-11-19 at 12:06, 12:27, 13:04 and 13:09). A hardened gate (v9.231: pointer must be inside the game image, aligned and resolvable) is in testing.

## Progress log

- **2026-11-16** — Project start: game image verified and extracted; full recompilation builds and boots to a frame loop.
- **2026-11-17** — PM4 command stream decoded and the frame loop stabilized at ~90 fps. Loader breakthrough: the main loop was running synchronously on the boot thread and starving `Start` — with the loop on its own thread, the boot sequence completes, the boot packages load and the intro movies play. The movie freeze was traced to a lost-wakeup race in the host's synchronization objects and fixed. The engine's screen framework now dispatches every frame. Two intermittent freezes (a GPU fence that never completes, and a memory-budget report job spinning on a collapsed pointer) were root-caused, contained and verified live. Current frontier: the first screen never hands over to the next one.
- **2026-11-18** — The UI layer comes alive, and the deepest bug so far falls. The UI script table was empty because the host's `XexGetModuleSection` was an empty stub — implementing it feeds the manager its 137 embedded scripts. Its script processor also dispatched through a jump table the code generator never registered; a verified switch bridge now routes all 29 cases. But the table kept arriving corrupted in memory while the file was perfect. A write-trap on the code section caught the game's own streaming `memcpy` filling ~3 MB of `.text` with file data — and the trail ended at the host's physical-memory allocator, whose free list covered the *entire* guest window, loaded image included: the game was being handed its own code as scratch memory. Excluding the image and the host function table fixed it — verified across repeated boots: zero writes to `.text`, correct dispatch targets, main loop running 100k+ ticks. Also fixed along the way: a macOS saved-state alert that silently stalled boots, and an unguarded XAM notification call the game makes with a null handle. Later the same day: shaders captured and recompiled (279-entry cache), the PM4→plume translator built, and the **first frame rendered** — the game's own draw, a black triangle on the purple clear colour.
- **2026-11-19** — The ghost pad unblocked the boot path; the resolve chain was built link by link with oracle patterns (green rectangle, stripes) verifying EDRAM write/read and UV derivation; the game's real RAM data reached the emulated EDRAM as noise, pointing at Xbox texture tiling as the next decode. The IRQ jump-to-address-1 crash was caught in a watched boot and a hardened gate is in testing (it held through a full observation on a healthy RAM-flavour boot later the same day). A chain test through the game's own quads came out black, but dumping the copy destination proved the copy works, and the cut was found in the quads' texture link — the link was only made for the synthetic copy shader, never for the game's own shader (40/40 drains confirm it). With the link fixed, dumping the quad's exact surface proved it holds the noise, yet the screen stayed black; a dedicated slot for the surface quads was then ruled out too (the quad drains 20/20 with its texture linked and still paints black). The pixel shader was ruled out as the cut for that step (run1198: even the copy shader painted nothing), and the real cut turned out to be the quad's geometry — a pointer into a rotating vertex-buffer cache that other draws overwrite before Present. With proven geometry the noise painted (run1199, **chain closed**), and the background fix (snapshot the vertices at registration, v9.239) was verified with the quad's own geometry (run1201). Retrying the game's own pixel shader paints black again (run1202): it needs its SET_CONSTANT constants, which the translator records but never applies — that is the front now, alongside detiling the Xenos-tiled data. That constants front was then parked behind the detiling front (the copy shader is faithful for a copy pass), and a raw-data dump (v9.242) is being built so detiling can be tested offline. Honest corrections along the way: an earlier "Activision splash" report was the macOS window title bar, not game content, and an earlier "chain closed" capture had travelled through a debug shortcut, not the game's own path — both retracted in the log before this update.

## What is this?

*Blur* never received a native Mac port. BlurRecomp recompiles the original Xbox 360 executable into native C++ (the same approach as Unleashed Recompiled and other XenonRecomp/ReXGlue projects) and pairs it with a host runtime for graphics, audio and input — so the game can run natively on Apple Silicon without emulation.

## Requirements

- A Mac with Apple Silicon
- **Your own legally obtained copy of *Blur* for Xbox 360** (Games on Demand image). No game assets are included in this repository or distributed with the app, and none ever will be.

## Roadmap

1. ~~Boot to a stable frame loop~~ ✅
2. ~~First rendered frame from the game's own draw stream~~ ✅ (2026-11-18)
3. Title screen
4. Menus and car select
5. In-race gameplay
6. Audio, saves and polish
7. Public release build

## Credits

Built on the Xbox 360 recompilation ecosystem (XenonRecomp / ReXGlue) and informed by the community's work on Marathon/Unleashed Recompiled. *Blur* © Activision / Bizarre Creations. This is a non-commercial, fan-made preservation project.

## License

MIT
