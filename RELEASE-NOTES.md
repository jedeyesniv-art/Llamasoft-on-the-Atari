# Release notes for 3.0.0

Paste the text below the line into the GitHub release, titled "Llamasoft on the Atari 3.0". Before then, set
`VERSION 3.0.0` in `CMakeLists.txt` and build the zip with `Tools\package_portable.ps1 -Release`. (2.0.0's notes
are in this file's history, commit 301857c.)

---

## Llamasoft on the Atari 3.0

**Llamasoft on the ST is now Llamasoft on the Atari: a Falcon game and a Jaguar cartridge have moved in.**

Every game is still the original program running in a small emulated machine, with the remaster layered on
top. 3.0 brings the collection to ten games.

### New: Llamazap (Atari Falcon030, 1993)
Minter's Falcon shooter: 25 stages over five levels, each stage a different style of shoot-'em-up. It was
written for a Falcon with an Atari Jaguar pad plugged in, and its power-ups are bought on that pad's number
keypad, which is what has always made it awkward to play. Here it's complete on any pad:
- **An emulated Falcon030** for it: 68030, 256-colour VIDEL modes, blitter, and the Jaguar pad. Our own mixer
  stands in for its DSP sound program, playing the MOD music and effects **in stereo**.
- **Power-ups from a menu:** hold **LB** and the game stops while every power-up on offer is shown with the
  game's own icons, names and costs. Pick with the right stick, let go to buy.
- **Twin-stick aiming** on its Llamatron stages.
- **50 frames a second on every screen** (the original drew 25), with the **remaster and run-ahead**.
- **START AT STAGE**: jump to any of the 25 stages.
- **Its manual**, typed in from the original's pages. The manual-word protection check is answered for you.
- Rumble, remapping, savestates and its own tuned CRT++ look. **STOCK FALCON** puts it back exactly as it was.

### New: Defender 2000 (Atari Jaguar, 1996)
Llamasoft's Jaguar remake of Defender: Classic, Plus and Defender 2000 itself. On the carousel it's a Jaguar
cartridge, and it runs on a Virtual Jaguar core with our own native renderer:
- **Smooth motion** at your display's rate (the game updates 30 times a second)
- **Wide screen:** the world carries on past the 4:3 edges
- **High-resolution art:** the cartridge's pictures, upscaled four times
- CRT+ / CRT++, savestates, arcade thrust and reverse buttons, and its own settings menu on Back

### For every game
- **Savestates:** four slots per game, kept on disk, from Settings (SAVE STATES) or F2 / F4.
- **Real labels:** the disks wear scans of their real labels where they exist, and hand-drawn art where they
  don't.
- **A tidier settings panel:** savestates and the game's own settings come first. **B now only closes the
  panel**, so an accidental press won't drop you out of a game; X leaves to the disks.

### Respecting the originals
As before: enemies, waves, scoring, difficulty, sound and music are all the originals'. Every deliberate
change can be switched back, and the README lists each one with its switch. **STOCK ST** (or **STOCK
FALCON**) in a game's settings turns every speed-up off at once.

**Download `LlamasoftOnTheAtari-3.0.0-windows.zip`, unpack it anywhere and run `llamatron.exe`.** All the
games are included; `README.txt` has every game's controls.

The collection: Llamatron: 2112 and Super Gridrunner, Trip-A-Tron, Revenge of the Mutant Camels (1 MB STE and
the TT Megamix), Andes Attack, Defender 2, Photon Storm, Hardcore, Llamazap and Defender 2000.

`d2k.exe` (Defender 2000) includes the Virtual Jaguar core and is GPL-3; its source is in this repository.

`LlamasoftOnTheAtari-3.0.0-symbols.zip` has the debug PDBs (only for crash reports).
