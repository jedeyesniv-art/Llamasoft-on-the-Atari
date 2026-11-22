# Defender 2000 (Atari Jaguar, Llamasoft 1996) - d2k.exe

Moved here from the Tempest collection (C:\Tempest2k) on 2026-11-22 (owner: "this project would then be the home
for D2K"). It isn't an ST game: d2k.exe is its own program on the Virtual Jaguar core, and the ST launcher starts it
as a separate process (a cartridge on the carousel, labelled from the owner's photo of the real cart). The notes below
are Tempest2k's, moved word for word: read their paths with the table here.

## Here (2026-11-22)
- src/d2k/: the program - main.cpp (D2K only now: Tempest 2000's half, run-ahead, remaster, HUD layer left behind),
  d2k*.cpp (renderer, bonus tunnel, stars), host.cpp (libretro host), menu / pixfont / controls / settings / crt /
  crt_menu (Tempest's shared menu and CRT, not the ST app's), volume, runlog, perf, xbr, traps / gpulog / jagsurvey
  (survey tools). Quoted includes resolve inside src/d2k, so the ST app's settings.h / crt.h are never mixed in.
  src/d2k/shaders: its CRT shaders (Tools/d2k/build_shaders.py; the T3K ycc_* ones left behind).
- third_party/virtualjaguar: the patched core, vendored (GPL-3; LLAMASOFT-NOTES.txt: from our branch
  llamasoft-jaguar, commit eb83a9de, trimmed to the build). So d2k.exe is GPL-3; llamatron.exe stays MIT-0 (separate
  programs). Tempest2k keeps its own copy for T2K: a core fix belongs in both.
- Tools/d2k/: the art pack tools (d2k_art_sheet / _route / _pack / _upscale / ridges), jrisc_dis.py,
  build_shaders.py. Real-ESRGAN in third_party/realesrgan (downloaded, not in git).
- Private/d2k/: the cartridge "Defender 2000 (1996).jag" and the high-res art pack d2k_art\ (made from the
  cartridge's pictures: not in git). The build copies d2k_art next to build\d2k.exe; d2k.exe finds the cartridge
  next to itself, in game\, or in Private\ / Private\d2k up the tree.
- logs/d2k/: Tempest2k's D2K test states, dumps and analysis scripts (12 GB, not in git), at the same paths.
- Saves: userdata\defender2000\ (d2k_settings.txt, defender2000.eeprom, states\d2k_slotN.state, logs\d2k.log):
  the launcher passes --userdata=<its userdata>\defender2000; d2k.exe alone defaults to that folder too. The
  owner's Tempest2k build\userdata D2K saves were copied in (the play folder's copy there was older).
- Launcher (src/games.cpp Defender2000, src/external.cpp, main.cpp RunExternal): any .jag / .j64 with "Defender" in
  its name under the search roots, offered only when d2k.exe is next to llamatron.exe; year 1996 (after Llamazap).
  It starts d2k.exe <cart> --from-launcher --userdata=... --master=<volume squared> --even=<volume_normalise>
  --fullscreen=<the launcher's mode, until d2k's own fullscreen setting is saved>, waits underneath (the game's
  window pushed to the front, as Tempest's launcher did) and comes back when it quits ("Return to launcher" in its
  menu). Label: Tools/make_labels.py CARTS (art/1397067-item-big-AJAG-DEFENDER-A-1.jpg, the label's box fitted
  onto the cart's grey); the disk is the cart's grey with the Jaguar red.
- Loudness: volume.cpp levels it to the ST games' -23 LUFS (measured -24.05: x1.128) when --even=1, times the
  launcher's master volume, times its own menu volume.
- Packaging (Tools/package_portable.ps1): d2k.exe + d2k.pdb and licenses\VIRTUAL-JAGUAR-GPL-3.txt everywhere; the
  cartridge and d2k_art only in the portable (play) folder, not the release zip (the leak check refuses .jag /
  .eeprom / .state / d2k_art) - the owner hasn't decided about shipping a commercial cartridge.
- Verified 2026-11-22: the same scripted headless run (3000 frames, into play) in Tempest2k's d2k.exe and ours gives
  byte-identical shots and audio, --d2kcheck exact; the owner's slot 1 state loads; window run native + wide + CRT++
  with the art pack (260 pictures); the launcher starts it, the saves land in userdata\defender2000, it comes back.
- Path changes for the notes below: tools/X.py -> Tools/d2k/X.py; build\d2k_art -> Private\d2k\d2k_art (copied to
  build\d2k_art by the build); src/X -> src/d2k/X; "t2k --..." test options -> d2k.exe (the same options, minus
  T2K's: --rmshot, --stick, --trace, --title, --menuback, --pauseline, --runahead); "Defender 2000.bat" -> the
  launcher or build\d2k.exe; T2K's settings (remaster, stick_mode...) don't apply.
- Ideas: a cartridge shape on the carousel instead of a disk (the owner's photos are in art/).

## Owner's notes (C:\Tempest2k\D2K notes.txt, 2026-11-20)
humanoid sprites in D2K mode flicker and strobe, like they're smeared out. Very jittery.
In Classic and Defender Plus modes, there is a large black square in the middle of the playfield that obscures the ship and many elements. Enemies draw over it, but other elements don't. In addition, the enemy sprites sometimes glitch and move position very briefly, making quick movements.

## Defender 2000 renderer (2026-11-18; PLAN.md "Defender 2000")
- Standalone d2k.exe (2026-11-19, owner: not in the collection for now): the build copies t2k.exe to build\d2k.exe;
  main.cpp g_defender = the exe's name has "d2k" / "defender" (FindRom then looks for "Defender") or the cartridge is
  Defender 2000 (d2k::IsDefender, by file name). Then: userdata\d2k_settings.txt, defender2000.eeprom,
  states\d2k_slotN.state, log d2k, volume key d2k (-24.05 LUFS measured), window title, src/d2k_menu.cpp page (GAME "Restart the game" =
  host::Reset, the core's retro_reset: the console's reset, EEPROM kept; headless --reset=F)
  (volume, save states, controls, picture: renderer / smooth motion / wide / pixels, CRT), ReadPadD2K; the T2K-only
  keys and hooks (F2, camera, pause line, quit to title) are off. "Defender 2000.bat" starts build\d2k.exe.
- Controls (manual + tests): d-pad left / right = the game's turn-and-thrust (push the way you face to thrust, the
  other way turns and thrusts), up / down move; Jaguar B fire, C smart bomb, A lightning laser (Plus only; nothing in
  2000 mode). No hyperspace button in any mode. The game's "Six Buttons" joypad option is Classic's Pro Controller
  arcade layout (A fire, B thrust, C bomb, left shoulder reverse); not used: our arcade buttons work in every mode.
  Arcade buttons (d2k::Arcade): the ship's facing is the word at $190630 (+1 / -1, every mode, set from boot on);
  Thrust holds the d-pad that way, Reverse holds the other way until the word flips (<= 6 frames; 2 always did it,
  the ship keeps most of its speed). Defaults: A fire, B / RB bomb, X laser, RT thrust, LB / LT reverse, Y Option,
  Start pause; setting d2k_stick_lr off = the stick moves up / down only (the d-pad still turns, for the game's
  menus). Bindings apply only in play (2026-11-21, owner's layout: RT fire, A thrust, X reverse, LB laser, B / RB bomb,
  Y option, LT reverse, Start pause): d2k::InPlay = longs $194E10 / $194E14 == $00140000 / $17000000 (a record that
  exists from a game's start to the menus after it, deaths / wave-complete / game over included, every mode + the
  attract demo; 0 on title and menus; RAM search logs/d2k/inplay/search3.py over 2000 / Classic / Plus / idle dumps,
  D2K_RAMDUMPS=F0:F1:step:prefix, D2K_INPLAY_LOG=1). The first marker ($18CD98 == $00760084) was a text position: Plus
  play changes it with its messages, so the owner's buttons fell back to the menu layout after a death. Outside play, fixed: A / Start = Jaguar B (select), B = Jaguar C (back: level
  select -> mode menu), Y = Option, d-pad + stick move. The pause keeps InPlay on. Keyboard: Z fire, C bomb,
  X laser, Space thrust, Shift reverse. Headless: --press=F:thrust / reverse.
- No source exists. Survey: t2k --survey=F0:F1:file (src/jagsurvey.cpp, core hooks vjHostGpuGo / vjHostBlit /
  vjHostOpObject). The picture is all OP bitmaps (CRY, VMODE $06C1, BGEN, per-line BG = the sky gradient set from GPU
  objects); the only RMW object is the scanner bar. Game updates at 30 Hz (every 2nd frame) in play.
- Capture: each bitmap row decoded when the OP reads it (the OP's own written-back DATA / HEIGHT / REMAINDER, live
  CLUT) - the game redraws the stars / scanner-dot buffer during the frame. Draw order = topological sort of the
  per-line walk orders. Line buffer -> core picture offset x 16, row 16 (D2K_CALIBRATE=1 re-measures).
- Oracle: --d2kcheck=F:prefix (software render vs the core's picture; writes images only on a mismatch): 439 / 439 and
  814 / 815 frames exact over a 6000-frame play sweep (the one: a 2x2 near-black speck).
- Window: d2k::Draw = atlas (edge-copy padding) + quads, RMW as add / reverse-subtract deltas, positions blended between
  updates (one update behind). Motion: same image (clipping by DATA / width recognised; same size + moved DATA =
  another animation frame, not motion), else predicted (still / own last motion / a layer's) within 10 px, else a
  layer sibling's. Lead-ins + ghosts fill the left edge while blending. Settings d2k_native (F8), d2k_interpolate (F7),
  d2k_widescreen (F6), d2k_filter sharp (SDL PIXELART) / smooth / nearest (no D2K menu page yet). Tests: D2K_FORCE_A=<0..1> (window), D2K_MOTION_LOG=1,
  D2K_WHO=x,y (with --d2kcheck). Cost ~0.4 ms a present at 1920x1440.
- Playfield layer (2026-11-19): the 8-bit 384x238 bitmap is cleared (blitter) and redrawn every update: sky stars
  (GPU tables, several parallax speeds), scanner blips, lasers (blitter rows built in a scratch row at $18E166..),
  explosion particles. Window: cut into items per update (8-connected pixel groups; a row of dashes = one beam) and
  matched to the update before's: (1) bursts - an explosion's pieces as one group moved by its centroid and scaled
  about it (one scale fits every ring: same start point and time; particles change colour as they age, so grouped by
  outline), its unpaired pieces moved by the model; (2) shape near a prediction (still / own last motion / this
  update's common displacements / the sprite layers'); (3) mutual nearest; (4) beams by row, ends blended. New items
  fade in, vanished ones fade out over the first half of an update. D2K_ITEM_LOG=1 prints per update; D2K_LAYER_DUMP=
  <prefix> writes the layer's bytes per update. Known: a burst leaving the layer's left edge (x < 0) breaks up.
- Matching fixes (2026-11-21, owner: Classic / Plus enemies jumped briefly): SameImage picked the smaller of its two
  readings of a DATA move and THEN rejected it (identical DATA on a 16 px lander moving > 8 px read as "a line down"),
  so landers took a neighbour's place; now invalid readings drop out first. Step 1 is global best-pairs-first, scored
  distance + half the miss from the sprite's own last motion (landers fly in tight pairs at 9 px a frame); a
  same-image match yields to a same-size sprite > 10 px nearer (an animation frame flip); step 2 needs the same
  reflect flag. D2K_MOTION_LOG shows s<step>#<prev index> per sprite. Classic route: matches implying >= 12 px a
  frame 41 -> 10 (rest: world wrap at x ~2040, landers appearing); Plus unmatched 516 -> 179, 2000 505 -> 220.
  Step 4 (same day): a row of strips (same row / height / depth, 32+ px wide) takes the motion most of its same-image
  (step 1) matches agree on, over members matched by steps 2 / 3. 2000's foreground rocks (64x22 at row 232) scroll
  ~+58 an update while thrusting (measured from the pictures: logs/d2k/hum/rowshift.py; the pattern repeats ~64 px, so
  0 / -6 fit nearly as well - don't trust a single shift fit); step 2 had given the left strips another layer's -6.
  Rows split between motions on the routes: 0 (was a few a second while thrusting).
- Scanner (2026-11-21, owner: radar jittery in all modes): the game steps blips a whole scanner pixel every few updates
  (and blinks enemy blips red / yellow <-> green every update - the game's own). 2000 / Plus: the blips are playfield
  layer items; SmoothRadar draws items in line-buffer rows 26..61 at the average of their last 4 positions along the
  match chain, blended between averages (1 px per 4 updates -> 0.25 px each update, ~2 updates late; smooth motion
  only). Classic: NOT done - its radar (and landscape line) is inside the 2-bpp playfield buffer, which is drawn whole
  (IsLayer needs depth 3) and updates at 60 Hz: it steps like the original (owner: looks good, leave it).
  Blip pairing (2026-11-21, owner: 2000's bottom-row blips "move quickly or swap positions"): the humanoid blips (2x2)
  sit in row 54 ~15-40 px apart and a star layer's displacement (a prediction) paired them with a neighbour (205 jumps
  of 16-43 px from the owner's level-2 state). Now blips (3+ px in the scanner rows, BlipOnly) pair only within 4 px
  across / 2 down (BlipMove), stay out of burst grouping, compare by outline (they blink colour), ties go to the
  shorter move; SmoothRadar averages only blip-sized moves (stars crossing the scanner rows are drawn as stars).
  Row-54 jumps 205 -> 3 (4 px steps).
- Plus sky (2026-11-21, owner: aurora cut at 4:3, starfield "swapping pixels"): the aurora is a mirrored pair of
  SCALED strips (type 1, 48 px source drawn 168 wide, row 178) - the wide pads now take type 0 and 1. The sky has
  ~370 items (three star layers at ~1-2 / 3-4 / 6-7 px a frame, ~100 stars each, colour by row and changing): in a
  dense layer (> 150 items of 1-2 px) single pixels only take a motion >= 20 position-only pairs share (h1 in
  MatchItems; colour-blind) and skip the mutual-nearest step; with g_period 1 new items show at once and vanished
  ones go (no half-frame fade flashes). Plus frame 2400 state: new / vanished per frame ~30 -> ~10, odd motions
  (-38..+48) gone. 2000 mode: only big explosions differ (~1 item an update).
- Plus hills past the layer's edges (2026-11-21, owner: "presentation is good"): the hills are one playfield-layer
  item as wide as the 384 px layer, which ends ~20 px short of a 16:9 window's edges. EdgeHistory measures their scroll
  each update by the OUTLINE only (the texture is an animated surface: no shift, palette or 2-D period matches it
  between frames) and keeps a history kEdgeM = 96 px wider each side, drawn past the trailing edge (the leading edge
  has never been seen); columns out of the layer > 3 updates take live colours mirrored about the edge at the same
  depth below the outline. Stars leaving across an edge carry on. The ground blends with the measured scroll (vy 0).
  Rejects fills (2000's full-screen flashes) and ambiguous shifts; a failed measurement (~1%: explosions over the hills,
  a jump every ~100 frames that no shift fits) restarts the history (carrying on with the last scroll was worse).
  D2K_EDGE_LOG=1. Wide only (g_wide_on).
- Plus enemies past 16:9 edges (2026-11-21, owner: they popped in short of the edges): Plus's (arcade-logic) objects
  are NOT the 2000 sprite overlay's: 64 slots x $40 at $190884 (x / y words +2 / +4, flags +6), turned into display
  objects by the GPU overlay copied from $0AF988 ($1AF6xx / $C0xx -> the resident copier $F030B6 -> list $8000). It
  kept x -64..383 (($7FF-masked x + $40) < $1C0) and set flags bit 8 "on screen" (read by other overlays, e.g. the
  smart bomb). d2k.cpp kWideArcade (own patch group, wide only) makes it -128..511: enemies now first appear at
  x -144..-128 / 480..512 (were -16..0 / 368..384); Plus check sweep 169 / 169 exact. Classic draws its enemies another
  way (unchanged). Tools: logs/d2k/pluswide/ (slots.py, dis68k.py 68K via capstone, track*.py), D2K_GPULOG + the
  survey's overlay blits (a1=F031C0, a2 = source) to find which overlay wrote an address.
- Level-end slowdown (2026-11-21, owner's state logs/d2k/hang/owner_l2end.state, end of 2000 level 2): the screen
  fills with ~6500 playfield particles; MatchItems' burst fit / nearest searches are quadratic (BurstFit ~95 scales x
  particles^2): ~2 s an update, the window stopped responding. Over 2000 items (play: a few hundred) an update is not
  matched (drawn as the game puts it). That state: 14 -> 170 fps headless.
- Wide per mode (2026-11-21, owner: Classic and Plus at 4:3, 2000 wide): d2k_widescreen 0 off / 1 all modes / 2
  Defender 2000 only (default; menu PICTURE "Wide screen", F6 cycles 2 -> 1 -> 0). The mode played = byte $18CD4B (0
  Classic, 1 Plus, 2 Defender 2000, 255 on menus; logs/d2k/inplay/modesearch.py); wide is off while InPlay() in
  Classic / Plus (menus and the attract demo stay wide). The owner's settings file was set to 2.
- Wide screens (2026-11-19; setting d2k_widescreen, F6): the object list is built by GPU overlays the 68K
  copies into $F031C0 from RAM - layers $0B2B50 (records -> 128/256 px strips, cull 0..383, left clip by DATA),
  sprites $0A5938 (world x - camera & 4095 - 255, kept while -w < x < 384 + w, left clip). d2k.cpp kWide patches their
  RAM sources (only over the original bytes): no left clips (the OP draws negative XPOS), strips up to 300 left,
  sprites -96..512. Our picture then fills the window around the 4:3 dst (Draw's view); a layer's end strips continue
  in mirrored copies where its record runs short (21:9). HUD stays in the 4:3 place; the playfield layer (stars,
  shots) only covers line-buffer 0..383. The 4:3 core picture is unchanged (sweep 94 / 94 exact with the patches).
  Side effect: the sprite overlay steps animations only for kept objects, now a wider window (no game logic seen).
- Art dump (2026-11-19, for an offline-upscaled replacement pack: the owner's choice): D2K_ART_DUMP=<dir> writes the
  source rectangle of every object drawn (DATA, DWIDTH, first pixel, w x h; stored values + RGBA) once per content to
  <dir>/rects.bin + index.txt; places whose content keeps changing (any shared pixel differs, 6 times in 300 frames)
  are drawing buffers (buffers.txt: Classic playfield, radar, HUD, menu text). tools/d2k_art_sheet.py <dirs> --out X
  joins rectangles overlapping in memory with the same values (backdrop strips -> whole panoramas; animation frames
  stay apart) -> X_pics/<n>.png + contact sheets X_N / X_big_N / X_buffers_N.png + X.txt. Routes:
  tools/d2k_art_route.py <m2000|classic|plus> (16000 frames). First census (attract + 3 modes): 434
  pictures, ~0.95 Mpx (25 big: title pictures, logos, backdrops); 2000 / Plus art is pre-rendered CGI (16-bit),
  Classic is tiny arcade pixel art; 28 seen in several colourings.
- Wide edges (2026-11-19 later; owner: "pop in at the edges", ground short of the border): each backdrop band is a
  window of N strips over a tile map, first strip at x in (-tile, 0], so its right end jumped between 384 and 512 and
  new strips appeared inside a 16:9 view (mirrored pads filled the gap until then). The layer builder's first part
  (per layer descriptor: speed long, count, mask, shift, map mask bytes, map long; record + 24 = strip ids, record
  + 10 = the second part's own count, bit 15 a flag) is patched (kWide 88-byte block at F03218: one tile further
  left; a mask-0 layer stays) and LayerCounts raises both counts by 2 (7 at most). The level's layer list is the long
  at $B811C + 16 * word $B822E (the 68K at $A4EEE puts it in $B8118 just before the builder runs; $B8118 holds the
  sprite list the rest of the frame - never patch through it). Tile table: {DATA, height, id} entries.
  The ground (8-bit 384 x 20 buffer at row 234, redrawn each update: rows of colour ramps, each periodic, 32 + 4 *
  row px) carries on 160 px past both ends, each row repeated at its period (Sprite lead / tail columns). IsLayer
  (the playfield layer) now needs h >= 200. 4:3 checks unchanged with the patches (same results on / off).
- Ridges: tools/d2k_ridges.py (run by d2k_art_pack.py) smooths stair-stepped backdrop outlines in the 4x pack: lines
  through run middles where neighbouring runs differ by <= 3 px (bigger jumps stay vertical: cacti, rocks), ends
  pinned (tiles meet), --ridge-detail 0.5 px rocky roughness, anti-aliased edge (the loader premultiplies real alpha).
  Chosen pictures: >= 128 wide, bottom row solid, >= 12 steps, big jumps <= steps / 4 (desert mountains / dunes; city
  skylines and logos untouched).
- FIXED 2026-11-21 (was the "Plus nebula / radar line gap"; owner: a big black square over Classic's playfield):
  TRANS makes a palettised pixel see-through when its own BITS are 0 (core op.c), not its CLUT entry; Classic / Plus
  2-bpp buffers have a non-zero black at index 0. --d2kcheck sweeps (route, 1800-6000 every 25): Classic, Plus, 2000
  all 169 / 169 exact.
- High-res art (2026-11-19; setting d2k_hires default 1, F4, menu PICTURE "High-res art"): pack build\d2k_art (never
  in git / published: made from the cartridge) = pack.txt (each picture's memory place: depth, DWIDTH, OP pitch, row /
  column = pixel address // and % the line pitch in pixels) + src\<n>.png (1x original) + hi\<n>.png (4x). Built by
  tools/d2k_art_pack.py <census>_pics (Real-ESRGAN realesrgan-x4plus via tools/d2k_upscale.py: colours spread into
  see-through pixels, mask upscaled as grey and cut at 50%, 8 px border; ~15 s for 388 pictures on the GPU; the
  anime model posterises). Real-ESRGAN ncnn-vulkan 20220424 in third_party\realesrgan (owner approved the download).
  Left out: drawing buffers, Classic-only pictures (arcade pixel art, kept blocky) and tiny pictures (both sides <= 24,
  --small: 128 of 388; 2026-11-21, owner: humanoids strobed - the upscaler redraws 1-2 px faces / limbs differently in
  each animation frame). 260 pictures now. d2k.cpp HiFor: a sprite whose
  memory rectangle lies in a picture and whose pixels equal the original there (1% slack) is drawn from the 4x texture
  (linear); scaled objects (the title is one, HSCALE 33) map drawn -> source by 32 / HSCALE, 32 / VSCALE and are
  checked from memory (every 3rd pixel); lead-ins, ghosts and wide-screen mirrored strips use it too; anything else
  (colour changes, unknown art) draws as before. Coverage from play states: 2000 / Plus every art sprite; left: the
  playfield layer, 320x10 message text, a scaled buffer (1CB000 / 1E5400 180x238). Cost ~0.07 ms a present at 1080p.
  Test: D2K_HI_LOG=1 (window: matched / not per update, every 60 frames). Window tests from boot drift from headless
  timing: use a state (logs/d2k/hires/title500.state = the title).
- High scores lost after loading states (2026-11-21, owner: "not saving high scores"; T2K too): the core's save state
  holds the EEPROM chip (eeprom_ram), so loading an older state rolled the chip back and the game's next save (name
  entry, options) wrote the old tables + its change over the file. Now host::LoadState puts the core's save buffer
  (the newest EEPROM, packed on every write = our file) back into the chip after every load (core vjEepromFromSaveBuf,
  libretro.c). D2K's EEPROM: header word, 3 tables x 4 entries x (score long + 6 chars), then $867A + a checksum long
  (not a plain sum: left to the game). The game reads the chip into RAM ($1B9A18 / $1B9A98) when it needs the tables;
  the 10-entry table on screen ($0B5A80, 16 bytes an entry) keeps only its top 4.
- High-res playfield, step 1: sky stars (2026-11-21). Survey (logs/d2k/pf, D2K_GPULOG from the city state while
  firing): the playfield layer ($1CB000 / $1E5400 buffers, shown from row 16) gets 1-px stars from overlay $0ADB98
  (pc $F0334E), 2x2 / 5-px plots and ~7000 small blitter fills from overlay $0A8F40 (blips, particles?), lasers as a
  colour-pattern row built in a scratch row ($18E466 / $18E5E6) and blitted in. src/d2k_stars.cpp ports $0ADB98:
  4 parallax layers x 65 stars, LFSR seed $2D taps $110, x = (lfsr - (camera $183690 >> 9 >> layer)) & $1FF, rows
  first + 2 j (first = word $180FF8 / 2 - $80), colour 2, on while $1AF146 bit 15 / $B6EDE / !$180FFC. Exact
  (D2K_STARS_CHECK=1: every star on a colour-2 pixel). Their pixels are erased from the layer before CutItems and
  Draw shows them as round sub-pixel dots, camera interpolated, the 512 px loop past the 384 px buffer on wide
  screens. Snapshot at the GPU start like the bonus (g_ovl_pending 2). D2K_NO_STARS=1 off. Next: lasers, then
  $0A8F40 (particles / blips).
- Frame pacing (2026-11-21; t2k.exe / d2k.exe window loop, src/main.cpp): the blend time t was read from the clock at
  the draw, which comes ~2 ms later when a frame was emulated first, so at 120 Hz t advanced 0.3 / 0.7 of a frame by
  turns (sd 0.197) instead of 0.5. Now the loop's start (just after the last present returned) snapped to a model of
  the display's refresh grid (period from the display mode, phase pulled 10% a present, k refreshes, resync when far
  off; not with --novsync) is the time for the blend AND for deciding to emulate: 516 of 524 presents step exactly
  0.5 (sd 0.046; the rest real stalls). PACE_LOG=1 logs per present (todo, draw time, t, present return);
  logs/d2k/pace.py summarises. T3K / TxK / T81 have their own loops (not looked at).
- Mode menu's turning Atari logo (2026-11-21, owner: jittery in ours, smooth emulated): one scaled object (type 1,
  DATA $016D08) widened 4 px every 2-3 frames while other things change every frame, so the update blend slid it in one
  frame and held it for the next one or two (and only moved its left edge). Now Motion.dw moves the far edge too, and
  ScaleHist keeps each scaled image's last two placements: drawn moving evenly between them over the frames they took,
  one step behind (D2K_SCALE_TRACE=1 prints the drawn edges). State at the menu: logs/d2k/menu/menu600.state.
- Space-level auroras cut to half the screen (2026-11-21, owner's state logs/d2k/aurora/owner.state, level 6 after the
  planet is lost): OUR bug - LayerCounts capped every count at 7 (later 8), so the six aurora descriptors of levels 5
  and 6 (32 px tiles, count 14 = 448 px, records 40 bytes so room for 16 ids) were cut to 7 = 224 px, in the game's
  own picture too, and the cut value went into save states. Now room = descriptor - record - 24 (8 / 16), a count is
  never lowered, and kFourteen repairs those six descriptors' counts in older states (values read after a clean boot:
  the level data is unpacked at boot; not raw in the cartridge). ExtendLayers now also takes strips clipped at the
  top (DATA whole rows into the tile, every strip alike) and goes a row at a time (records share tiles: the aurora
  rows), each row once; D2K_EXTEND_LOG=1 says why a row isn't extended.
- Bonus tunnel (2000 mode between levels; owner's state logs/d2k/bonus/owner_bonus.state, 2026-11-21): the whole
  playfield layer (384 x 279 8-bit, buffers $1CB000 / $1E5400) is dots from one GPU overlay ($0AB008; disassembly
  logs/d2k/bonus/ovl.dis): LFSR stars (fixed seeds, so every dot keeps its identity), streaks, wave pairs ($ABD2C),
  pods ($AB920, 16-byte entries, with collision logic inside), tunnel rings ($AB720 table, $ABD2A count, wave $1AED6C),
  projected x / z around (192, 139), camera from the sine table ($1AE030) at angle $AB700 (turning; wraps at 512),
  travel $AB704 (+13 a load). Our item matching smeared it (~400 of 500 dots unmatched a update). src/d2k_bonus.cpp
  ports the generator (exact mode == the game's buffer byte for byte on most loads; D2K_BONUS_CHECK=1 compares; the
  rest: the GPU not done by frame end, or a pixel off in near rings after turning - the 68K updates state while the
  GPU draws) and Draw shows its smooth version instead of the layer: round sub-pixel dots, the scalars and pods / ring
  angles interpolated between the shown picture and the one before (by displayed pictures, not frames: the loads come
  1-3 frames apart but each picture shows for one update), stars past the 384 px edges on wide screens. Inputs are
  snapshotted when the 68K starts the GPU ($F03FFC set after the overlay's blit; other blits share A1 $F031C0, only
  cmd $01800001 is an overlay copy). Shown snapshot = the newest drawn into the shown buffer before the update flipped
  to it (the GPU starts the next picture in a buffer still on screen). D2K_NO_BONUS=1 off, D2K_BONUS_TRACE=1 logs.
  CRT++ trails turn the smooth dots into long rays (the owner's CRT look 1 has none). --press now works in window runs.
- Classic auto fire (2026-11-21, owner): Classic's fire is edge-triggered (one laser a press, a new one only when the last
  is done). d2k::AutoFire (main.cpp pad + headless presses): while Fire is held in Classic play (d2k::ModePlayed() 0)
  it presses 4 frames on / 4 off. Setting d2k_autofire (default 1), menu CONTROLS "Auto fire (Classic)".
- Level-end fade on wide screens (2026-11-21, owner): 2000's level end is an explosion of particles then black dots
  in the playfield layer until it is black (the fade between levels), but the layer is only 384 px wide, so a 16:9
  view showed the backdrop at its sides. While the layer's items cover > 12% of it (g_layer_cover, per update; play
  peaks ~3%, the fade rises to 100%; full-screen flashes are 100% too) Draw mirrors them past both layer edges
  (D2K_ITEM_LOG prints the cover).
- City buildings jumping at the wide edges (2026-11-21, owner: level 6). Two causes: (1) LayerCounts capped a layer
  at 7 strips, so the 7-strip city (64 px tiles) got the tile-left shift and no extra strips: its right end fell short
  of even the 4:3 edge (buildings popped in inside 4:3, in the game's own picture). Records are 32 bytes with the
  descriptor right after, so the ids at +24 have room for 8: cap 8 there (core picture wide on == off again in the
  background). (2) A 16:9 view still needs up to ~45 px more on one side or the other: ExtendLayers adds the next real
  tiles from the level's map (builder part 1: pos = camera * speed >> 6, strip i = map[(pos >> shift) + i & mapmask],
  signed id, 0 empty, < 0 reflected; tile table at record +16, 8 bytes {DATA, height, palette word}; tile step record +8)
  as scene strips, after checking the shown strips / gaps fit one map offset and decode identically; pads now only from a
  row's outermost strips. D2K_NO_EXTEND=1 turns it off. Tools: logs/d2k/city/ (layers.py RAM inspector, winscroll.py
  edge-vs-centre shift per present, odd.py motion-log outliers); t2k/d2k --winshots=F0:F1:prefix (every present).
- Black screen / GPU runaway in 2000 mode (2026-11-21, owner's level-6 state logs/d2k/black/owner_black.state, already
  dead): a CORE bug, not ours (crashed with wide / renderer / idle skip off, any timing model). The core's late load
  write-back (issue #811, gpu.c gpu_ldx_slot) also fired for a load in the delay slot of a TAKEN jump: the object task
  $A61B0 ends a handler `jump eq,(r7) / load (r14+56),r0` into `movei #$F03296,r0 / jump ne,(r0)`, so r0 kept the
  loaded data and the GPU ran into main RAM. Now armed only in program order (gpu.c + dsp.c). Repro harness:
  logs/d2k/black/bot.py (random play from the owner's slot-1 state, 5000-frame segments chained by save states,
  CRASH-DETECT lines) + replay.py; before: 4 of 14 seeds crashed, after: 0 of 20 (400k frames). D2K_RAMDUMP(S) also
  write <file>.gpu (GPU RAM + pc + both register banks). GPU task protocol: code copied (blitter) to $F031C0, long
  $F03FFC = go (resident loop at $F03072 polls), the task clears it; the 68K waits ($A423A + 18 inline tst.l).
- Survey tools: D2K_GPULOG=F0:F1:file (GPU loads / stores outside its RAM + blits; core hook vjHostGpuMem),
  D2K_M68KLOG=F0:F1:lo:hi:file (writes into a range by the 68K with its pc, or by another unit as $FF0000xx | who;
  core hook vjHostM68kWrite), D2K_RAMDUMP=F:file (2 MB), D2K_WIDESHOT=F:margin:file (software picture, wider),
  tools/jrisc_dis.py (GPU disassembler: python -I tools/jrisc_dis.py ram.bin <src> <len> <run address>), capstone
  (installed) for 68K.

## Plan history (Tempest2k PLAN.md "Defender 2000")
- No public source (mwenge has T2K and VLM only); no byte-identical code shared with T2K. Work from the ROM.
- Runs in t2k.exe as is (the T2K hooks refuse it). Route: b at 300 / 600 / 900 / 1200 -> D2K mode play by ~1300.
- Survey tool: t2k --survey=F0:F1:file (src/jagsurvey.cpp; core hooks vjHostGpuGo / vjHostBlit / vjHostOpObject).
- First findings: one resident GPU program (10 GPU starts in 3700 frames); the 68K blits code overlays into GPU RAM
  ($F031C0) ~8x a frame. The play picture is OP bitmaps: 16-bit pre-rendered sprites, parallax rock layers (the pillar
  = one 128x229 bitmap), an 8-bit 384x264 layer, the scanner (256x32). OP lists double-buffered at $C000 / $E000.
- The game updates at exactly 30 Hz in play (1199 of 1200 updates last 2 frames); at speed the layers jump 22-29 px
  per update. Objects are already placed past the right edge (x up to ~640).
- 2026-11-18 later: native renderer + interpolation done (CLAUDE.md "Defender 2000 renderer"). Next candidates: what is
  drawn into the 8-bit playfield bitmap (30 Hz still), widescreen (the game culls at the edges: needs the world's
  objects past them), the owner's look at it in the window, then stick controls and the Classic / Plus modes.
- 2026-11-19: shots / particles / stars smooth (the playfield layer cut into matched items) and wide screens (the
  game's two list-building GPU overlays patched in RAM; mirrored layer ends). CLAUDE.md "Defender 2000 renderer".
  Next candidates: the owner's look in the window (motion, wide), a D2K menu page (own settings: native, smooth
  motion, wide, filter), HUD to the corners on wide windows, stars / shots past line-buffer 0..383 on wide windows,
  stick controls, Classic / Plus modes.
- 2026-11-19 later: standalone build\d2k.exe (owner: not in the collection for now) with its own menu page, arcade
  Thrust / Reverse buttons, loudness levelled. Next: the owner's play test (feel of Reverse / Thrust, wide, motion).
- 2026-11-19 later: high-res art - art dump + census (434 pictures, ~0.95 Mpx), Real-ESRGAN 4x pack (build\d2k_art),
  loader in d2k.cpp (setting d2k_hires, F4). Next: the owner's look in play; then maybe the edge fringe on small
  sprites (black baked into their edges), the 28 recoloured pictures (per-colouring replacements), Classic's pixel art
  (a pixel-art scaler?), text / HUD redrawn by us.
- 2026-11-19 later: no pop-in at wide edges (the layer builder covers a tile past each edge), the ground carries on to
  the window's edges, smoothed mountain ridges. Found: Plus mode's nebula clouds + radar landscape line are missing
  from our renderer (GPU-drawn? to investigate); Classic differs a little too.

## Porting recipe (learned on Tempest 2000, 2026-11-09; for Defender 2000 and later)
Finding things
- Survey first: --gpulog over a long run (attract + play + a scripted route), grouped by caller / module / mode / frame
  range; map callers to routines with reference/t2k.sym. A timeline contact sheet (--shot every N frames, a grid) shows
  which screen each range is. --peek / --poke (RAM long / word) and RM_TRACE (published scene per frame) for state.
- What draws a thing is often not what it looks like: the "dash lattice" was zoom text, the HUD is an OP overlay
  (screen3) not a GPU draw, the tunnel is two point routines, the bonus ground grid a starfield. Check the picture
  with the remaster ON (only uncaptured pixels remain) to see what is left.
- Read the GPU routine fully before porting; copy its integer set-up exactly (fmult / sdiv / >> 7 / table bytes) and
  its quirks (psphere steps its phase twice, texter's shrink hack keeps the narrower width, vect3d ends a list on a
  zero vertex BYTE). Cheats / test routes come from the 68K source (pad masks, flags like chenable / warpy).
Verifying
- Same run, GPU vs ours: RM_NOSKIP + RM_OURS + --rmshot, tools/side_by_side.py, and a red / green pixel overlay for
  exact positions. Window-only effects (feedback, ripple, mode7) need --state + --winshot comparisons against
  --set=native=1 (Original mode). Feedback chains need a state from BEFORE they start.
- Always test the path back (play -> quit to title), not just boot -> play; and run-ahead consistency
  (--runahead=2 picture == plain + 2 frames, byte-identical).
Architecture that worked
- Capture at gpurun, skip the GPU for captured modes into the play buffers, draw natively in window space; the core's
  picture over ours with black see-through. Interpolate captured inputs; hidden run-ahead frames rewind ALL host state.
- Screens the game builds by feedback (scar), full-screen warps (ripple, mode7) or the blitter: take over the
  full-overwrite op, keep our own window-sized feedback texture, and have the core's OP skip the stale buffers
  (vjOPSkip ranges); gate other takeovers (text, sprites) on "this buffer's background is ours".
- Overlays (HUD) as our own layer: skip the OP object in the core rather than masking pixels (masking leaves holes);
  move content clusters to the corners, never fixed zones.
- Emulated GPU / blitter work is nearly free: render-paced screens run too fast; pace them by holding the 68K in
  gpuwait (host port), measured against the hardware's look (BigPEmu as the owner's reference).
- Extending to wide screens: the game's world usually exists past 4:3 (widen culls); clipped textures can tile
  mirrored (wrap addressing); far / minified lines need mip levels and a fade.
- Small custom shaders (HLSL -> DXIL, tools/build_shaders.py) for CRY-style dimming (subtract from the brightest
  channel, keep hue) - multiplying or subtracting RGB looks wrong.
Working with the owner
- They test in the window and report with screenshots / save states (copy slotN.state to logs\ first); every tunable
  look gets a menu slider with a sensible default; "Original mode" must turn everything off.
Tooling pitfalls (cost time today)
- Inline python / heredocs mangle backslashes (\n): write edit scripts with Write and run python -I <file>.
- From bash give the exe forward-slash paths; check file times when a fix "shows no change" (stale screenshots).
- Headless runs keep only the LAST --savestate; window runs over ~100 s need the timeout raised.
