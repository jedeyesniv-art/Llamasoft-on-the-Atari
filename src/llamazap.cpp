#include "llamazap.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cpu.h"
#include "games.h"
#include "glyphs.h"
#include "log.h"
#include "machine.h"
#include "pad.h"
#include "remap.h"
#include "remaster.h"
#include "remaster_falcon.h"
#include "settings.h"

namespace llamazap {
namespace {

// The manual check (copy protection: "enter from page p paragraph q line l word no. w", picked at random from the
// manual's text, which the program keeps as one 7-bit code a word at $473EE: $FC pages, $FD paragraphs, $FE lines,
// $FF words). The word typed on the alphabet grid is hashed to the same kind of code: d3 at $1AB9C, compared with
// the wanted code ($5138A); a wrong one is also kept ($526C0) and checked again in play ($13B44: the game turns
// on the player later). We have no manual: the hook makes the typed word's code the wanted one, as if the player
// had looked it up - any word passes.
constexpr uint32_t kWanted = 0x5138A;
constexpr uint32_t kCheck = 0x1AB9C, kCheckPass = 0x1ABA8;  // cmp.w $5138A,d3 ; bne (fail) ; (pass)

// 50 frames a second (llamazap_50fps, default 1): the main loop ($14006..$1404E) draws into the next screen, then
// sets the countdown $2F9A2 to the divisor $49DDC (2: 25 drawn frames a second) and waits for the VBL to count it
// down ($1316A, which also shows the new screen). All the game's logic is in the VBL: with 1 the scroll is identical
// frame for frame, and the Falcon's own 16 MHz draws all 50 (~70% of the instructions still in the wait loop). The
// game itself has it as a hidden key: 1-9 = the divisor ($10AFA). In play = the VBL routine *$2F98C is the level's
// own, *$5269A (set by each level type's set-up: $11FE4 Beasties / Air-Sea, $11CE0 Space, $11CF2 Furred, $11D20
// test-fly, ...: all 10 types of the 25 stages, table $2E666; at first only $11FE4 was known: levels 3-5 stayed
// at 25, owner 2026-11-20). Checked on every stage type: the same events as the original (some stage intros a few
// frames shorter: timed by drawn frames), every frame drawn at the 68030's own speed. Outside play too (2026-11-20,
// owner: "everything always"): the title, menus, attract screens ($1AC34 $26722 $2676A $260CC), stage intros
// ($1A9CA), briefings ($25F0E) and the fades ($274EC $275B6) keep their lengths frame for frame at 1 (their logic is
// in the VBL as well) and show every frame (were every other); all 10 stage types, deaths and game over checked.
constexpr uint32_t kDivisor = 0x49DDC, kDivisorRead = 0x1403C, kVblRoutine = 0x2F98C, kLevelRoutine = 0x5269A,
                   kNoRoutine = 0x13DA0;
bool g_50fps = true;
// Start at stage (owner 2026-11-20, llamazap_stage 0 = the game's own, 1..25): the 25 stages are a table of
// descriptors at $2E666 (5 levels x 5; null-ended); starting level n begins at entry 5 (n - 1). The start writes
// that place to $2E6EE ($10278: move.l a0,$2E6EE); the hook writes stage s's instead, play carries on in order.
constexpr uint32_t kStages = 0x2E666, kStageAt = 0x2E6EE, kStageStart = 0x10278, kStageNext = 0x1027E;
int g_stage = 0;
// Twin stick on the Llamatron stages (owner 2026-11-20; llamazap_twin_stick, default 1): Llamatronic (VBL $12C90)
// and Son of Llamatron ($12C6C) fire on their own in the direction $525F2 (0..7 clockwise from up-left: UL U UR R
// DR D DL L); each VBL ($1C3E4..) the pad's A / C step it round a press at a time, then ($1C460) unless B is held
// (the lock) it follows the way the ship moves (table $1C5FE). The hook there: with the right stick pushed it is
// the stick's angle instead and the follow is skipped; let go, the game's own again. No keypad on those stages,
// so the right stick is free.
constexpr uint32_t kFireDir = 0x525F2, kFireFollow = 0x1C460, kFireFollowDo = 0x1C46E, kFireFollowSkip = 0x1C480;
constexpr uint32_t kPadB = 0x4E8B9;
bool g_twin_stick = true;
bool g_stereo = true;  // llamazap_stereo: the voices panned as the game sends them
float g_fx_volume = 1;  // llamazap_fx_volume: the effects voice (the game sends it ~10 dB over the music)
int g_aim = -1;  // the right stick's direction in the game's numbering, -1 = not pushed


uint16_t Get16(const st::Machine& m, uint32_t a) { return uint16_t(m.Ram()[a] << 8 | m.Ram()[a + 1]); }
uint32_t Get32(const st::Machine& m, uint32_t a) { return uint32_t(Get16(m, a)) << 16 | Get16(m, a + 2); }
void Put16(st::Machine* m, uint32_t a, uint16_t v) { m->Ram()[a] = uint8_t(v >> 8), m->Ram()[a + 1] = uint8_t(v); }
uint16_t Get16From(const std::vector<uint8_t>& v, uint32_t a) { return a + 1 < v.size() ? uint16_t(v[a] << 8 | v[a + 1]) : 0; }

bool InLevel(const st::Machine& m) {
  uint32_t r = Get32(m, kVblRoutine);
  return r == Get32(m, kLevelRoutine) && r != kNoRoutine && r >= 0x10000;
}
// The level types whose VBL routine runs the keypad handler $1301C (power-ups, ship select); not Night Mares
// ($12C6C) and the others set at $1A404.. (no keypad there in the original).
bool LlamatronStage(const st::Machine& m) {
  if (!InLevel(m)) return false;
  uint32_t r = Get32(m, kVblRoutine);
  return r == 0x12C90 || r == 0x12C6C;
}
bool KeypadLevel(const st::Machine& m) {
  if (!InLevel(m)) return false;
  switch (Get32(m, kVblRoutine)) {
    case 0x11FE4: case 0x11CE0: case 0x11CF2: case 0x11D20: case 0x120AA: case 0x120E2: return true;
    default: return false;
  }
}

// The screen itself is skipped (owner 2026-11-20: a block to play): $1AAC6 calls the entry screen ($26A28: the
// alphabet grid, then d4 = -1 + the sum of the letters' codes, 7 bits) only while $4B628 is set (once a session:
// play clears it after its own check at $13B44). The hook answers d4 = the wanted code without showing it; the
// check at $1AB9C then passes as typed (its hook stays as a safety). $5138A is set by the screen's question
// ($2635E): never asked, it stays as it is - the same value both sides of both checks. A trap in it: the screen
// writes the rts at $24158 a byte at a time ($26446 $4E, $164E2 $75), the end of a routine play calls - left
// out, play stops on an illegal instruction. The hook writes it.
constexpr uint32_t kEntryCall = 0x1AAC6, kEntryNext = 0x1AACC;  // bsr.l $26A28
constexpr uint32_t kPlantedRts = 0x24158;

void OnEntryScreen(st::Machine& m) {
  cpu::Set(cpu::Reg(cpu::D0 + 4), (cpu::Get(cpu::Reg(cpu::D0 + 4)) & 0xFFFF0000u) | Get16(m, kWanted));
  Put16(&m, kPlantedRts, 0x4E75);
  if (!m.sandbox) LOG("llamazap: the manual check skipped");
  cpu::Set(cpu::PC, kEntryNext);
}

void Put32(st::Machine* m, uint32_t a, uint32_t v) { Put16(m, a, uint16_t(v >> 16)), Put16(m, a + 2, uint16_t(v)); }

void OnFireFollow(st::Machine& m) {  // was: btst #1,$4E8B9 ; beq $1C480 (B pressed = 0: the lock)
  if (g_aim >= 0) {
    Put16(&m, kFireDir, uint16_t(g_aim));
    cpu::Set(cpu::PC, kFireFollowSkip);
    return;
  }
  cpu::Set(cpu::PC, (m.Ram()[kPadB] & 2) ? kFireFollowDo : kFireFollowSkip);
}

void OnStageStart(st::Machine& m) {
  uint32_t at = cpu::Get(cpu::A0);
  if (g_stage > 0) {
    at = kStages + 4 * uint32_t(g_stage - 1);
    if (!m.sandbox) LOG("llamazap: starting at stage %d", g_stage);
  }
  Put32(&m, kStageAt, at);
  cpu::Set(cpu::PC, kStageNext);
}

void OnManualCheck(st::Machine& m) {
  uint32_t want = Get16(m, kWanted);
  if (!m.sandbox) LOG("llamazap: typed word's code $%02X, the manual's $%02X", unsigned(cpu::Get(cpu::Reg(cpu::D0 + 3)) & 0xFFFF), want);
  for (int r : {3, 4}) cpu::Set(cpu::Reg(cpu::D0 + r), (cpu::Get(cpu::Reg(cpu::D0 + r)) & 0xFFFF0000u) | want);
  if (!m.sandbox) LOG("llamazap: the manual check answered");
  cpu::Set(cpu::PC, kCheckPass);
}

// The keypad (owner 2026-11-20, second go): R3 = key 1 (power-up 0: a new ship); every other power-up from the
// LB menu (the right stick's keypad flicks are gone: the menu does it better). In play the keys buy power-ups (1-9:
// power-ups 0-8, table $11CC8). Buying 1 shows SELECT SHIP: $2F594 >= 0 counts down ~300 frames, and a key's column
// on the Jaguar keypad picks the ship ($13046: 1 4 7 * Phoenix, 2 5 8 0 Falcon, 3 6 9 # Condor; first all keys must
// be up: $49DB8). While it shows, X / A / B pick (keys 4 / 5 / 6: Phoenix, Falcon, Condor - left, middle, right
// as the buttons sit) and do nothing else; the d-pad moves as always.
constexpr uint32_t kShipSelect = 0x2F594;
// Keyboard: cursor keys, Z / Space = A, X = B, C = C, O = Option, P = Pause, digits = the keypad (keypad * and /
// = * and #). Every key also goes to the Falcon (name entry types there).
uint32_t FromKeyboard() {
  int n = 0;
  const bool* k = SDL_GetKeyboardState(&n);
  if (!k) return 0;
  auto down = [&](SDL_Scancode c) { return int(c) < n && k[c]; };
  uint32_t bits = 0;
  if (down(SDL_SCANCODE_UP)) bits |= st::kJagUp;
  if (down(SDL_SCANCODE_DOWN)) bits |= st::kJagDown;
  if (down(SDL_SCANCODE_LEFT)) bits |= st::kJagLeft;
  if (down(SDL_SCANCODE_RIGHT)) bits |= st::kJagRight;
  if (down(SDL_SCANCODE_Z) || down(SDL_SCANCODE_SPACE)) bits |= st::kJagA;
  if (down(SDL_SCANCODE_X)) bits |= st::kJagB;
  if (down(SDL_SCANCODE_C)) bits |= st::kJagC;
  if (down(SDL_SCANCODE_O)) bits |= st::kJagOption;
  if (down(SDL_SCANCODE_P)) bits |= st::kJagPause;
  static const SDL_Scancode kTop[10] = {SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
                                        SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9};
  static const SDL_Scancode kPad[10] = {SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3,
                                        SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7,
                                        SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9};
  for (int i = 0; i < 10; ++i)
    if (down(kTop[i]) || down(kPad[i])) bits |= st::kJagKey0 << i;
  if (down(SDL_SCANCODE_KP_MULTIPLY)) bits |= st::kJagStar;
  if (down(SDL_SCANCODE_KP_DIVIDE)) bits |= st::kJagHash;
  return bits;
}


// The power-up menu (owner 2026-11-20): LB held in play stops the game (no frames run) and shows the nine keypad
// power-ups in the right stick's layout (R3 = 1 in the middle), named from the game's own descriptions; the stick
// (or R3) picks one, B clears the pick, letting go of LB resumes and presses that key for the game to buy it.
// The purchase ($11A34): key n = power-up n - 1, refused if n - 1 > the tokens word $4B544 (so it holds one
// less than the player has), else that word goes down by n. The ship's slots: *$49BA8 + $24 + 4 x (n - 1), byte
// 0 negative = not on offer now (speed down at the slowest, orb fire before an orb...), byte 1 = the type: its
// description at *($2C1F2 + 4 x type) + 8 ("2speed up: 3increase\speed of ship/": digits = colours, \ a new
// line, / the end). Not while the attract demo plays ($2CAD6 = 1) or a ship is being chosen.
constexpr uint32_t kTokens = 0x4B544, kShip = 0x49BA8, kTexts = 0x2C1F2, kDemo = 0x2CAD6;

struct Cell {
  bool offered = false, affordable = false;
  int type = -1;  // the power-up's type (its name and icon)
  std::string name;
  std::vector<std::string> about;
};
// The power-ups' icons (the test-fly panel's, $25334..: table $4B4FE + 4 x type, 17 of them): 15 x 10 pixels, the
// image from +16, a 16-pixel group a row in 4 planes (8 bytes); the panel draws them with d2 = $A00F = those
// planes onto the screen's 0-3 and planes 5 and 7 set where drawn: colour = value | $A0, 0 not drawn. Made (in
// the palette now) as the menu opens.
constexpr uint32_t kIcons = 0x4B4FE;
constexpr int kIconTypes = 17, kIconW = 15, kIconH = 10;
std::vector<uint32_t> g_icon_px[kIconTypes];  // ARGB, 0 = clear
SDL_Texture* g_icon_tex[kIconTypes] = {};
bool g_icons_new = false;
void MakeIcons(const st::Machine& m) {
  for (int t = 0; t < kIconTypes; ++t) {
    const uint32_t a = Get32(m, kIcons + 4 * uint32_t(t)) + 16;
    auto& px = g_icon_px[t];
    px.assign(kIconW * kIconH, 0);
    if (a < 0x10000 || a + 80 >= st::kFalconRam) continue;
    for (int y = 0; y < kIconH; ++y)
      for (int x = 0; x < kIconW; ++x) {
        int v = 0;
        for (int k = 0; k < 4; ++k) v |= ((Get16(m, a + uint32_t(y * 8 + k * 2)) >> (15 - x)) & 1) << k;
        if (v) px[size_t(y * kIconW + x)] = m.FalconColour(v | 0xA0);
      }
  }
  g_icons_new = true;
}
struct Menu {
  bool open = false;
  int pick = 0;  // 1..9, 0 none
  int tokens = 0;
  Cell cells[10];
};
Menu g_menu;
bool g_lb_was = false;
bool g_stick_blocked = false;  // after the menu: R3 is no key until let go
int g_inject_key = 0, g_inject_frames = 0;

std::string Upper(std::string t) {
  for (char& c : t)
    if (c >= 'a' && c <= 'z') c = char(c - 32);
  return t;
}

void ReadCells(const st::Machine& m) {
  g_menu.tokens = int16_t(Get16(m, kTokens)) + 1;
  uint32_t ship = Get32(m, kShip);
  for (int n = 1; n <= 9; ++n) {
    Cell& c = g_menu.cells[n];
    c = Cell{};
    if (ship < 0x10000 || ship + 0x24 + 4 * 9 >= st::kFalconRam) continue;
    uint32_t slot = ship + 0x24 + 4 * uint32_t(n - 1);
    c.offered = !(m.Ram()[slot] & 0x80);
    c.affordable = n - 1 <= int16_t(Get16(m, kTokens));
    c.type = m.Ram()[slot + 1];
    uint32_t text = Get32(m, kTexts + 4 * uint32_t(m.Ram()[slot + 1]));
    if (text < 0x10000 || text + 300 >= st::kFalconRam) continue;
    std::string line;
    bool named = false;
    for (uint32_t a = text + 8; a < text + 300; ++a) {
      char ch = char(m.Ram()[a]);
      if (ch == '/' || ch == 0) break;
      if (ch >= '0' && ch <= '9') continue;  // colours
      if (!named && ch == ':') {
        c.name = Upper(line), line.clear(), named = true;
        continue;
      }
      if (ch == '\\') {
        if (named) c.about.push_back(Upper(line));
        line.clear();
        continue;
      }
      if (ch >= 32 && ch < 127 && !(ch == ' ' && line.empty())) line += ch;
    }
    if (named && !line.empty()) c.about.push_back(Upper(line));
    if (!named) c.name = Upper(line);
  }
}

// The stick's cell (the keypad's layout on it), 0 = in the middle.
int StickKey(const pad::State& p) {
  float x = p.rx, y = p.ry;
  if (x * x + y * y < 0.6f * 0.6f) return 0;
  int col = x < -0.38f ? 0 : x > 0.38f ? 2 : 1, row = y < -0.38f ? 0 : y > 0.38f ? 2 : 1;
  static const int kKey[3][3] = {{2, 3, 4}, {5, 0, 6}, {7, 8, 9}};
  return kKey[row][col];
}

// Runs the menu for this frame; true = it is open (the game gets no input and no frames).
bool MenuFrame(st::Machine* m, const pad::State& p, bool ship_select) {
  const bool lb = p.Held(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
  const bool lb_down = lb && !g_lb_was;
  g_lb_was = lb;
  const bool play = KeypadLevel(*m) && Get16(*m, kDemo) != 1;
  if (!g_menu.open) {
    if (!lb_down || !play || ship_select) return false;
    g_menu.open = true, g_menu.pick = 0;
    MakeIcons(*m);
    if (!m->sandbox) LOG("llamazap: power-up menu");
  }
  ReadCells(*m);
  if (lb) {
    if (int k = StickKey(p)) g_menu.pick = k;
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) g_menu.pick = 1;
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_EAST)) g_menu.pick = 0;
    return true;
  }
  g_menu.open = false;
  g_stick_blocked = true;
  const Cell& c = g_menu.cells[g_menu.pick];
  if (g_menu.pick && c.offered && c.affordable) {
    g_inject_key = g_menu.pick, g_inject_frames = 4;  // a frame with no key (the game waits for keys up), then 3
    if (!m->sandbox) LOG("llamazap: bought %d (%s)", g_menu.pick, c.name.c_str());
  }
  return false;
}

// Survey (tests; env LLAMAZAP_SURVEY=<from>:<to>:<file>, frames counted by Update): who draws into the three
// 640 x 300 screen buffers ($5C570, $8B370, $BA170: *$5291C..; 192000 bytes each) - CPU writes and blitter jobs,
// by the code's 16-byte block - and how much of the shown buffer is written a frame.
constexpr uint32_t kBuffers = 0x5291C, kBufferBytes = 192000;
struct SurveyRow {
  uint64_t cpu[3] = {}, blit_jobs[3] = {}, blit_words[3] = {};
};
struct Survey {
  int from = -1, to = -1, frame = 0, frames = 0;
  std::string file;
  uint32_t base[3] = {};
  std::map<uint32_t, SurveyRow> rows;
  std::map<uint32_t, int> routines;  // VBL routine -> frames
  st::Machine* m = nullptr;
} g_survey;
int BufferOf(uint32_t a) {
  for (int i = 0; i < 3; ++i)
    if (g_survey.base[i] && a >= g_survey.base[i] && a < g_survey.base[i] + kBufferBytes) return i;
  return -1;
}
void SurveyWrite(uint32_t a, uint32_t pc) {
  int b = BufferOf(a);
  if (b >= 0) ++g_survey.rows[pc & ~15u].cpu[b];
}
std::map<uint32_t, int> g_survey_logged;
void SurveyBlit(uint32_t dst, uint32_t src, int xcount, int ycount, uint32_t pc) {
  int b = BufferOf(dst);
  if (b < 0) return;
  if (SDL_getenv("LLAMAZAP_SURVEY_CALLERS")) {  // tests: jobs into a rectangle of the buffer, with the stack
    static int n = 0;
    uint32_t off = dst - g_survey.base[b];
    int line = int(off / 640), byte = int(off % 640);
    if (line >= 180 && line < 230 && byte >= 560 && n++ < 40) {
      uint32_t sp = cpu::Get(cpu::A7);
      st::Machine* mm = g_survey.m;
      if (!mm) return;
      LOG("panel blit pc %06X line %d byte %d src %06X %d x %d; stack %06X %06X %06X %06X %06X %06X", pc, line, byte, src,
          xcount, ycount, Get32(*mm, sp), Get32(*mm, sp + 4), Get32(*mm, sp + 8), Get32(*mm, sp + 12), Get32(*mm, sp + 16), Get32(*mm, sp + 20));
    }
  }
  if (g_survey_logged[pc & ~15u]++ < 12) {
    int sb = BufferOf(src);
    uint32_t off = dst - g_survey.base[b];
    LOG("survey blit pc %06X: %d x %d words, dst buf%d +%u (line %u, byte %u), src %06X%s%d", pc, xcount, ycount, b, off,
        off / 640, off % 640, src, sb >= 0 ? " = buf" : "", sb);
  }
  SurveyRow& r = g_survey.rows[pc & ~15u];
  ++r.blit_jobs[b], r.blit_words[b] += uint64_t(xcount) * uint64_t(ycount);
}
void SurveyFrame(st::Machine* m) {
  Survey& s = g_survey;
  if (s.from < 0 || m->sandbox) return;
  s.m = m;
  ++s.frame;
  if (s.frame == s.from) {
    for (int i = 0; i < 3; ++i) s.base[i] = Get32(*m, kBuffers + 4 * uint32_t(i));
    cpu::g_write_watch = SurveyWrite, st::g_blit_watch = SurveyBlit;
  }
  if (s.frame >= s.from && s.frame < s.to) ++s.frames, ++s.routines[Get32(*m, kVblRoutine)];
  if (s.frame != s.to) return;
  cpu::g_write_watch = nullptr, st::g_blit_watch = nullptr;
  FILE* f = std::fopen(s.file.c_str(), "w");
  if (!f) return;
  std::fprintf(f, "frames %d, buffers %06X %06X %06X\nVBL routines:", s.frames, s.base[0], s.base[1], s.base[2]);
  for (auto& [r, n] : s.routines) std::fprintf(f, " %06X x%d", r, n);
  std::fprintf(f, "\n%-8s %12s %12s %12s | %8s %10s | %8s %10s | %8s %10s  (per frame)\n", "pc", "cpu buf0", "cpu buf1",
               "cpu buf2", "jobs0", "words0", "jobs1", "words1", "jobs2", "words2");
  const double n = std::max(1, s.frames);
  for (auto& [pc, r] : s.rows)
    std::fprintf(f, "%06X %12.1f %12.1f %12.1f | %8.1f %10.1f | %8.1f %10.1f | %8.1f %10.1f\n", pc, r.cpu[0] / n,
                 r.cpu[1] / n, r.cpu[2] / n, r.blit_jobs[0] / n, r.blit_words[0] / n, r.blit_jobs[1] / n,
                 r.blit_words[1] / n, r.blit_jobs[2] / n, r.blit_words[2] / n);
  std::fclose(f);
  LOG("llamazap: survey written to %s", s.file.c_str());
}

// The remaster (pilot 2026-11-20: the Beasties / Air-Sea stages, VBL $11FE4; remaster_falcon.cpp draws). Every
// sprite of the game comes to $14848 (or its twin $146CC) with d0 = its left x on the screen, d4 its right, d1 its
// middle line in the buffer, a0 the image (+2 width, +4 height), d2 the colour planes, $51346 the variant; there
// it is clipped to the 384 pixels, the buffer's scroll ($49B9C / $49B9E) added and drawn by the blitter, a plane at
// a time. The hook keeps it instead (screen x = d0, y = top - $49B9E) and returns as drawn (d0 = 1). Its image:
// the same call run once in a copy of the machine onto a filled buffer, twice (fill 0 and $FF: what differs was
// not drawn). Dots ($250E0: d0 / d1 the buffer's x / y, d2 the colour; 2 x 2 pixels) likewise. The main loop's
// screen swap ($14012) ends a frame's list: the buffer drawn (*(*(*$2F9D6))) is shown from the next VBL.
// The background: the game's own clean copy, buffer 0 (Machine::SetCleanSource: the shown buffers' lines read
// from it), which the VIDEL's start in the shown buffer slides.
constexpr uint32_t kSpriteA = 0x14848, kSpriteANext = 0x1484E, kSpriteB = 0x146CC, kSpriteBNext = 0x146D2;
// A third way in ($1457C / $1458A, from game objects: the laser beams, the formation ships): d0 is already the
// buffer's x (0..639, wrapped), d1 the middle line; at $145B2 (the same clr.l) it goes on unclipped but for lines.
constexpr uint32_t kSpriteC = 0x145B2, kSpriteCNext = 0x145B8;
constexpr uint32_t kClip = 0x51338, kVariant = 0x51346, kScrollX = 0x49B9C, kScrollY = 0x49B9E, kWide = 0x2C704;
constexpr uint32_t kDotPlot = 0x250E0, kDotPlotNext = 0x250E2, kSwap = 0x14012, kSwapSame = 0x14022, kSwapOther = 0x14032;
constexpr uint32_t kDrawDesc = 0x2F9D6, kDescA = 0x2F9A6;
constexpr int kPlayLines = 200;  // the playfield; the HUD below (timer B split at line 200)

struct Queued {
  uint64_t id;
  uint32_t entry;
  uint32_t regs[16];
  uint16_t variant;
  bool buffer_x;  // d0 is the buffer's x (kSpriteC), not the screen's
};
std::vector<Queued> g_queue;
// Tests (env LLAMAZAP_REMASTER_VERIFY): capture and still let the game draw - the items composed over its picture
// must then be the picture itself (a wrong place or image shows).
bool g_verify = false;

uint32_t DrawBuffer(const st::Machine& m) { return Get32(m, Get32(m, Get32(m, kDrawDesc))); }

uint64_t Mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }

void OnSprite(st::Machine& m, uint32_t next) {
  auto pass = [&] {
    Put32(&m, kClip, 0);
    cpu::Set(cpu::PC, next);
  };
  if (m.sandbox && !m.ahead) return pass();  // decoding an image: the original's work
  if (!remaster_falcon::Capturing()) return pass();
  const bool buffer_x = next == kSpriteCNext;
  const uint32_t a0 = cpu::Get(cpu::A0);
  const int16_t sx = int16_t(Get16(m, kScrollX)) + (Get16(m, kWide) ? 32 : 0);
  const int16_t w = int16_t(Get16(m, a0 + 2));
  int16_t left = int16_t(cpu::Get(cpu::D0)), right = buffer_x ? int16_t(left + w) : int16_t(cpu::Get(cpu::Reg(cpu::D0 + 4)));
  const int16_t mid = int16_t(cpu::Get(cpu::Reg(cpu::D0 + 1)));
  const int16_t h = int16_t(Get16(m, a0 + 4));
  int16_t top = int16_t(mid - (h >> 1));
  const int16_t sy = int16_t(Get16(m, kScrollY));
  if (top >= sy + kPlayLines) return pass();  // below the playfield (the HUD's): the game's own
  auto clipped = [&] {
    cpu::Set(cpu::D0, cpu::Get(cpu::D0) & 0xFFFF0000u);
    remaster::Return(m);
  };
  int16_t screen_x = left, screen_top = int16_t(top - sy);
  if (buffer_x) {
    // The display runs on through memory: a buffer x left of where it starts shows a line higher, on the right.
    int off = left - sx;
    if (off < 0) off += 640, --screen_top;
    screen_x = int16_t(off), right = int16_t(off + w);
    if (screen_x >= 0x180 && right - 640 > 0) screen_x = int16_t(screen_x - 640), right = int16_t(right - 640), ++screen_top;
    if (top + h <= 0 || top > 0x12B) return clipped();
  } else if (right < 0 || left >= 0x180 || top + h <= sy || top > 0x12B) {
    return clipped();
  }
  if (screen_x >= 0x180 || right <= 0) return clipped();
  const uint16_t variant = Get16(m, kVariant);
  uint64_t id = Mix(Mix(Mix(Mix(1469598103934665603ull, next), a0), cpu::Get(cpu::Reg(cpu::D0 + 2))), variant);
  if (!remaster_falcon::HaveImage(id)) {
    bool queued = false;
    for (const auto& q : g_queue) queued |= q.id == id;
    if (!queued) {
      Queued q{id, next == kSpriteANext ? kSpriteA : next == kSpriteBNext ? kSpriteB : kSpriteC, {}, variant, buffer_x};
      for (int r = 0; r < 16; ++r) q.regs[r] = cpu::Get(cpu::Reg(r));
      g_queue.push_back(q);
    }
  }
  remaster_falcon::Item it;
  const uint32_t ret = Get32(m, cpu::Get(cpu::A7));
  it.key = uint64_t(ret) << 32 | (next & 0xFFFF);
  it.x = float(screen_x - (buffer_x ? 0 : (Get16(m, kWide) ? 32 : 0))), it.y = float(screen_top);
  it.image = id;
  remaster_falcon::Add(it);
  if (g_verify) return pass();
  cpu::Set(cpu::D0, (cpu::Get(cpu::D0) & 0xFFFF0000u) | 1);
  remaster::Return(m);
}
void OnSpriteA(st::Machine& m) { OnSprite(m, kSpriteANext); }
void OnSpriteB(st::Machine& m) { OnSprite(m, kSpriteBNext); }
void OnSpriteC(st::Machine& m) { OnSprite(m, kSpriteCNext); }

void OnDot(st::Machine& m) {  // was: tst.w d1
  const int16_t y = int16_t(cpu::Get(cpu::Reg(cpu::D0 + 1)));
  auto original = [&] {
    uint32_t sr = cpu::Get(cpu::SR) & ~0xFu;
    if (y < 0) sr |= 8;
    if (y == 0) sr |= 4;
    cpu::Set(cpu::SR, sr);
    cpu::Set(cpu::PC, kDotPlotNext);
  };
  if ((m.sandbox && !m.ahead) || !remaster_falcon::Capturing() || y < 0 || y >= 0x12B) {
    uint32_t sr = cpu::Get(cpu::SR) & ~0xFu;
    if (y < 0) sr |= 8;
    if (y == 0) sr |= 4;
    cpu::Set(cpu::SR, sr);
    cpu::Set(cpu::PC, kDotPlotNext);
    return;
  }
  const int16_t sy = int16_t(Get16(m, kScrollY)), sx = int16_t(Get16(m, kScrollX));
  if (y < sy + kPlayLines) {
    remaster_falcon::Item it;
    it.dot = true;
    it.key = uint64_t(Get32(m, cpu::Get(cpu::A7))) << 32 | 0xD07;
    // As the sprites: left of where the display starts = a line higher, on the right (it runs on through memory).
    int off = int(cpu::Get(cpu::D0) & 0x7FFE) - sx - (Get16(m, kWide) ? 32 : 0), line = y - sy;
    if (off < 0) off += 640, --line;
    if (off >= 640) off -= 640, ++line;
    it.x = float(off), it.y = float(line);
    it.colour = uint8_t(cpu::Get(cpu::Reg(cpu::D0 + 2)));
    remaster_falcon::Add(it);
  }
  if (g_verify) return original();
  remaster::Return(m);  // (the erase list doesn't get this one: nothing drawn to erase)
}

void OnSwap(st::Machine& m) {  // was: cmpi.l #$2F9A6,$2F9D6 ; bne $14032
  if (!m.sandbox || m.ahead) remaster_falcon::EndFrame(DrawBuffer(m));
  cpu::Set(cpu::PC, Get32(m, kDrawDesc) == kDescA ? kSwapSame : kSwapOther);
}

// The queued images: each call run in a copy of the machine (between frames: Musashi isn't re-entrant).
// One call in `fork` (a copy of the machine made once for a batch: a call changes nothing it needs but the
// buffers, which are filled again first).
bool RunCopy(st::Machine* m, st::Machine* fork, const Queued& q, uint8_t fill, int x, int top, std::vector<uint8_t>* out) {
  uint8_t* ram = fork->Ram();
  // Both shown buffers filled: it draws into the one its own state says (read back from whichever changed).
  const uint32_t bufs[2] = {Get32(*m, kBuffers + 4), Get32(*m, kBuffers + 8)};
  for (uint32_t b : bufs) std::memset(ram + b, fill, kBufferBytes);
  const uint32_t magic = Get32(*m, kBuffers) + kBufferBytes - 16, sp = (q.regs[15] & 0xFFFFFE) - 512;
  ram[magic] = 0x4A, ram[magic + 1] = 0xFC;
  static bool done = false;
  done = false;
  fork->AddHook(magic, [magic](st::Machine&) {
    done = true;
    cpu::Set(cpu::PC, magic);
    cpu::EndSlice();
  });
  for (int k = 0; k < 4; ++k) ram[sp + k] = uint8_t(magic >> (24 - 8 * k));
  Put16(fork, kVariant, q.variant);
  std::vector<uint8_t> context = cpu::SaveContext();
  cpu::SetMachine(fork);
  cpu::Set(cpu::SR, 0x2700);
  for (int r = 0; r < 15; ++r) cpu::Set(cpu::Reg(r), q.regs[r]);
  const int16_t d0 = int16_t(q.regs[0]), d4 = int16_t(q.regs[4]);
  const int16_t h = int16_t(Get16(*fork, q.regs[8] + 4));
  const int16_t bx = int16_t(q.buffer_x ? x + int16_t(Get16(*fork, kScrollX)) + (Get16(*fork, kWide) ? 32 : 0) : x);
  cpu::Set(cpu::D0, (q.regs[0] & 0xFFFF0000u) | uint16_t(bx));
  cpu::Set(cpu::Reg(cpu::D0 + 4), (q.regs[4] & 0xFFFF0000u) | uint16_t(x + (d4 - d0)));
  cpu::Set(cpu::Reg(cpu::D0 + 1), (q.regs[1] & 0xFFFF0000u) | uint16_t(top + (h >> 1)));
  cpu::Set(cpu::A7, sp);
  cpu::Set(cpu::PC, q.entry);
  for (int i = 0; i < 20 && !done; ++i) cpu::Execute(100000);
  cpu::SetMachine(m);
  cpu::RestoreContext(context);
  if (!done) return false;
  uint32_t drawn = bufs[0];
  for (uint32_t b : bufs) {
    const uint8_t* p = ram + b + uint32_t(std::max(0, top)) * 640;
    for (uint32_t i = 0; i < 640u * 32u; ++i)
      if (p[i] != fill) {
        drawn = b;
        break;
      }
  }
  out->assign(ram + drawn, ram + drawn + kBufferBytes);
  return true;
}

void DecodeQueued(st::Machine* m) {
  if (g_queue.empty()) return;
  const uint32_t buffer = DrawBuffer(*m);
  if (buffer < 0x10000 || buffer + kBufferBytes > st::kFalconRam) return g_queue.clear();
  static int total = 0;
  uint64_t t0 = SDL_GetTicksNS();
  std::unique_ptr<st::Machine> fork;
  for (const Queued& q : g_queue) {
    const int16_t d0 = int16_t(q.regs[0]), d4 = int16_t(q.regs[4]);
    const int16_t w = q.buffer_x ? int16_t(Get16(*m, q.regs[8] + 2)) : int16_t(d4 - d0), h = int16_t(Get16(*m, q.regs[8] + 4));
    const int sx = int16_t(Get16(*m, kScrollX)) + (q.buffer_x && Get16(*m, kWide) ? 32 : 0), sy = int16_t(Get16(*m, kScrollY));
    const int x = 96 + (q.buffer_x ? ((d0 - sx) & 15) : (d0 & 15)), top = sy + 40;
    std::vector<uint8_t> a, b;
    remaster_falcon::Image im;
    if (!fork) {
      fork = std::make_unique<st::Machine>(*m);
      fork->sandbox = true;
      fork->ahead = false;
    }
    if (w > 0 && h > 0 && h < 250 && RunCopy(m, fork.get(), q, 0, x, top, &a) && RunCopy(m, fork.get(), q, 0xFF, x, top, &b)) {
      // The rectangle it may have drawn into, in the buffer: 16 pixels more each side.
      const int bx0 = x + sx - 16, bx1 = x + sx + w + 16;
      int x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
      std::vector<uint16_t> all(size_t(bx1 - bx0) * size_t(h), remaster_falcon::kClear);
      auto pixel = [&](const std::vector<uint8_t>& s, int bx, int by) {
        uint32_t at = uint32_t(by) * 640 + uint32_t(bx / 16) * 16;
        int v = 0;
        for (int k = 0; k < 8; ++k) v |= ((Get16From(s, at + uint32_t(k * 2)) >> (15 - bx % 16)) & 1) << k;
        return v;
      };
      for (int j = 0; j < h; ++j)
        for (int bx = bx0; bx < bx1; ++bx) {
          int va = pixel(a, bx, top + j), vb = pixel(b, bx, top + j);
          if (va != vb) continue;
          all[size_t(j) * size_t(bx1 - bx0) + size_t(bx - bx0)] = uint16_t(va);
          x0 = std::min(x0, bx), x1 = std::max(x1, bx), y0 = std::min(y0, j), y1 = std::max(y1, j);
        }
      if (x1 >= 0) {
        im.w = x1 - x0 + 1, im.h = y1 - y0 + 1, im.ox = x0 - (x + sx), im.oy = y0;
        im.px.resize(size_t(im.w) * size_t(im.h));
        for (int j = 0; j < im.h; ++j)
          for (int i = 0; i < im.w; ++i)
            im.px[size_t(j) * im.w + i] = all[size_t(j + y0) * size_t(bx1 - bx0) + size_t(x0 + i - bx0)];
      }
    }
    if (SDL_getenv("LLAMAZAP_IMAGES"))
      LOG("image %016llX: entry %06X variant %d, %d x %d -> %d x %d", (unsigned long long)q.id, q.entry, q.variant, w, h, im.w, im.h);
    remaster_falcon::SetImage(q.id, std::move(im));
  }
  total += int(g_queue.size());
  if (total < 3 || total % 50 < int(g_queue.size()))
    LOG("llamazap remaster: %d sprite images so far (%.2f ms for the last %zu)", total, (SDL_GetTicksNS() - t0) / 1e6,
        g_queue.size());
  g_queue.clear();
}

}  // namespace

void Reset() {
  LoadSettings();
  remaster_falcon::Start();
  g_queue.clear();
  g_verify = SDL_getenv("LLAMAZAP_REMASTER_VERIFY") != nullptr;
  if (const char* e = SDL_getenv("LLAMAZAP_SURVEY")) {
    std::string v = e;
    size_t c1 = v.find(':'), c2 = v.find(':', c1 + 1);
    if (c1 != std::string::npos && c2 != std::string::npos)
      g_survey.from = std::atoi(v.c_str()), g_survey.to = std::atoi(v.c_str() + c1 + 1), g_survey.file = v.substr(c2 + 1);
  }
  g_menu = Menu{};
  g_lb_was = g_stick_blocked = false;
  g_inject_frames = 0;
}

bool MenuOpen() { return g_menu.open; }

bool InPlay(const st::Machine& m) { return InLevel(m); }

void PreviewFrame(st::Machine* m, int f) {
  if (Get16(*m, kCheck) == 0xB679 && Get32(*m, kCheck + 2) == kWanted) {
    Put16(m, kCheck, 0x4AFC);
    m->AddHook(kCheck, OnManualCheck);
  }
  if (Get16(*m, kEntryCall) == 0x61FF && Get32(*m, kEntryCall + 2) == 0xBF60) {
    Put16(m, kEntryCall, 0x4AFC);
    m->AddHook(kEntryCall, OnEntryScreen);
  }
  if (Get16(*m, kDivisorRead) == 0x3039 && Get32(*m, kDivisorRead + 2) == kDivisor) Put16(m, kDivisor, g_50fps ? 1 : 2);
  uint32_t bits = 0;
  if (!InLevel(*m)) {
    if (f % 300 >= 297) bits = st::kJagA;  // (a tap every 6 s: past the intro, the menus, a game over)
  } else if (int16_t(Get16(*m, kShipSelect)) >= 0 && KeypadLevel(*m)) {
    if (f % 60 >= 57) bits = st::kJagKey0 << 6;  // SELECT SHIP: the right-hand column's (the beginner's ship)
  } else {
    // Six seconds one way, six the other (fire let go at the turn so the ship faces the way it goes), drifting
    // up and down.
    const int phase = f % 600;
    bits |= phase < 300 ? st::kJagRight : st::kJagLeft;
    if (phase % 300 >= 20) bits |= st::kJagA;
    const double v = std::sin(f * 0.013) + 0.5 * std::sin(f * 0.041);
    if (v > 0.5) bits |= st::kJagUp;
    if (v < -0.5) bits |= st::kJagDown;
  }
  m->SetJagpad(bits);
}

void LoadSettings() {
  g_50fps = settings::Get("llamazap_50fps", 1) != 0;
  g_stage = std::clamp(int(settings::Get("llamazap_stage", 0)), 0, 25);
  g_twin_stick = settings::Get("llamazap_twin_stick", 1) != 0;
  g_fx_volume = float(std::clamp(settings::Get("llamazap_fx_volume", 1.0), 0.0, 1.0));
  g_stereo = settings::Get("llamazap_stereo", 1) != 0;
}

// The pad as the Jaguar pad: left stick / d-pad the directions, A = A (fire), B = B (jump / lock the fire angle),
// X = C (fire), RT = A too, Y = Option, Start = Pause, R3 = keypad 1 (a new ship), LB the power-up menu; on
// SELECT SHIP X / A / B the ships; on the Llamatron stages the right stick aims.
void Update(st::Machine* m) {
  const pad::State& p = pad::Now();
  m->SetDspFxGain(g_fx_volume * g_fx_volume);  // (squared, as the master volume: 0.5 = -12 dB)
  m->SetDspStereo(g_stereo);
  SurveyFrame(m);
  uint32_t bits = 0;
  // (Only in play: $2F594 also holds 300 through the title and menus.)
  const bool ship_select = int16_t(Get16(*m, kShipSelect)) >= 0 && KeypadLevel(*m);
  if (MenuFrame(m, p, ship_select)) {
    m->SetJagpad(0);
    return;
  }
  // SELECT SHIP: X / A / B are the ships (left, middle, right), nothing else then.
  const bool west = p.Held(SDL_GAMEPAD_BUTTON_WEST), south = p.Held(SDL_GAMEPAD_BUTTON_SOUTH),
             east = p.Held(SDL_GAMEPAD_BUTTON_EAST);
  if (ship_select) {
    if (west) bits |= st::kJagKey0 << 4;
    if (south) bits |= st::kJagKey0 << 5;
    if (east) bits |= st::kJagKey0 << 6;
  }
  uint8_t joy = pad::JoyBits(p);  // left stick or d-pad
  if (joy & st::kJoyUp) bits |= st::kJagUp;
  if (joy & st::kJoyDown) bits |= st::kJagDown;
  if (joy & st::kJoyLeft) bits |= st::kJagLeft;
  if (joy & st::kJoyRight) bits |= st::kJagRight;
  if ((south && !ship_select) || p.rt > 0.5f) bits |= st::kJagA;
  if (east && !ship_select) bits |= st::kJagB;
  if (west && !ship_select) bits |= st::kJagC;
  if (p.Held(SDL_GAMEPAD_BUTTON_NORTH)) bits |= st::kJagOption;
  if (p.Held(SDL_GAMEPAD_BUTTON_START)) bits |= st::kJagPause;
  if (g_stick_blocked && !p.Held(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) g_stick_blocked = false;  // (R3 after the menu)
  const bool twin = g_twin_stick && LlamatronStage(*m);
  g_aim = -1;
  if (twin && p.rx * p.rx + p.ry * p.ry >= 0.5f * 0.5f) {
    // Clockwise from up, in eighths; the game's 0 is up-left.
    float a = std::atan2(p.rx, -p.ry);
    g_aim = (int(std::lround(a / (3.14159265f / 4))) + 1) & 7;
  }
  if (!ship_select && !g_stick_blocked && p.Held(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) bits |= st::kJagKey0 << 1;
  bits |= FromKeyboard();
  if (g_inject_frames > 0) {  // the menu's pick: alone on the keypad
    for (int i = 0; i < 10; ++i) bits &= ~(st::kJagKey0 << i);
    bits &= ~(st::kJagStar | st::kJagHash);
    if (g_inject_frames-- <= 3) bits |= st::kJagKey0 << g_inject_key;
  }
  m->SetJagpad(bits);
  // Once unpacked (the program unpacks itself over where it was loaded): the manual check's hook.
  if (Get16(*m, kCheck) == 0xB679 && Get32(*m, kCheck + 2) == kWanted) {
    Put16(m, kCheck, 0x4AFC);
    m->AddHook(kCheck, OnManualCheck);
  }
  if (Get16(*m, kSpriteA) == 0x42B9 && Get32(*m, kSpriteA + 2) == kClip) {  // clr.l $51338 (twice)
    Put16(m, kSpriteA, 0x4AFC);
    m->AddHook(kSpriteA, OnSpriteA);
  }
  if (Get16(*m, kSpriteB) == 0x42B9 && Get32(*m, kSpriteB + 2) == kClip) {
    Put16(m, kSpriteB, 0x4AFC);
    m->AddHook(kSpriteB, OnSpriteB);
  }
  if (Get16(*m, kSpriteC) == 0x42B9 && Get32(*m, kSpriteC + 2) == kClip) {
    Put16(m, kSpriteC, 0x4AFC);
    m->AddHook(kSpriteC, OnSpriteC);
  }
  if (Get16(*m, kDotPlot) == 0x4A41 && Get16(*m, kDotPlot + 2) == 0x6BFF) {  // tst.w d1 ; bmi.l
    Put16(m, kDotPlot, 0x4AFC);
    m->AddHook(kDotPlot, OnDot);
  }
  if (Get16(*m, kSwap) == 0x0CB9 && Get32(*m, kSwap + 2) == kDescA && Get32(*m, kSwap + 6) == kDrawDesc) {
    Put16(m, kSwap, 0x4AFC);
    m->AddHook(kSwap, OnSwap);
  }
  // Capturing in play on every stage (each stage type verified: CLAUDE.md), the remaster on.
  const bool capture = remaster::Enabled() && !games::Stock() && InLevel(*m);
  remaster_falcon::SetCapturing(capture);
  if (capture) {
    uint32_t b0 = Get32(*m, kBuffers), b1 = Get32(*m, kBuffers + 4), b2 = Get32(*m, kBuffers + 8);
    // The shown buffer itself (what the game still draws: the clean background, restored, plus anything not caught),
    // with its margins (never drawn into: clean too). (b0, the clean copy, has none of what isn't caught.)
    (void)b0;
    m->SetCleanSource(0, b1, b1 + kBufferBytes, 0);
    m->SetCleanSource(1, b2, b2 + kBufferBytes, 0);
    m->SetCleanMargin(16);
  } else {
    m->SetCleanMargin(0);
  }
  if (Get32(*m, kFireFollow) == 0x08390001 && Get32(*m, kFireFollow + 4) == kPadB) {
    Put16(m, kFireFollow, 0x4AFC);
    m->AddHook(kFireFollow, OnFireFollow);
  }
  if (Get16(*m, kStageStart) == 0x23C8 && Get32(*m, kStageStart + 2) == kStageAt) {
    Put16(m, kStageStart, 0x4AFC);
    m->AddHook(kStageStart, OnStageStart);
  }
  if (Get16(*m, kEntryCall) == 0x61FF && Get32(*m, kEntryCall + 2) == 0xBF60) {
    Put16(m, kEntryCall, 0x4AFC);
    m->AddHook(kEntryCall, OnEntryScreen);
  }
  // The divisor: 1 on every screen (2026-11-20; before: in play only), the original 2 with the setting off / STOCK
  // (only this build: the main loop reads it where we expect).
  if (Get16(*m, kDivisorRead) == 0x3039 && Get32(*m, kDivisorRead + 2) == kDivisor) {
    Put16(m, kDivisor, g_50fps && !games::Stock() ? 1 : 2);
  }

}

namespace {
// The buffer `m` showed its last frame from and where the display started in it; false: not one of the two.
bool Shown(const st::Machine& m, uint32_t* buffer, int* x, int* y) {
  const uint32_t base = m.FrameBase();
  for (int i = 1; i < 3; ++i) {
    uint32_t b = Get32(m, kBuffers + 4 * uint32_t(i));
    if (base >= b && base < b + kBufferBytes) {
      const uint32_t off = base - b;
      *buffer = b, *x = int(off % 640) + m.FrameHscroll(), *y = int(off / 640);
      return true;
    }
  }
  return false;
}
}  // namespace

// Run-ahead (setting remaster_ahead, F7: 0..3 frames; remaster.h): a copy of the machine runs that many frames on
// with the controls held, drawing its picture too (Machine::draw_in_sandbox), its hooks capturing into a list of its
// own; its newest frame is what is shown (picture and items together: the background has to match the sprites).
// The game's sound and everything else stay the machine's. Cost: the copy (~4 MB) and the frames run.
void AfterFrame(st::Machine* m) {
  DecodeQueued(m);
  uint32_t buffer = 0;
  int x = 0, y = 0;
  if (!remaster_falcon::Capturing() || !Shown(*m, &buffer, &x, &y)) return remaster_falcon::FrameShown(*m, 0, 0, 0);
  const int frames = remaster::Ahead();
  if (frames > 0 && !g_verify) {
    const uint64_t t0 = SDL_GetTicksNS();
    struct Timing {
      uint64_t ns = 0, worst = 0;
      int n = 0;
    };
    static Timing timing;
    static std::unique_ptr<st::Machine> fork;
    if (!fork) fork = std::make_unique<st::Machine>(*m);
    else *fork = *m;
    fork->sandbox = fork->ahead = fork->draw_in_sandbox = true;
    remaster_falcon::BeginAhead();
    std::vector<uint8_t> context = cpu::SaveContext();
    cpu::SetMachine(fork.get());
    bool ok = true;
    for (int i = 0; i < frames && ok; ++i) ok = fork->RunFrame();
    cpu::SetMachine(m);
    cpu::RestoreContext(context);
    remaster_falcon::EndAhead();
    DecodeQueued(fork.get());  // what the copy met first: shown this frame
    cpu::SetMachine(m);
    const uint64_t ns = SDL_GetTicksNS() - t0;
    timing.ns += ns, timing.worst = std::max(timing.worst, ns);
    if (++timing.n == 500)
      LOG("llamazap remaster: run-ahead %d frames, %.2f ms a frame (worst %.2f)", frames, timing.ns / 1e6 / timing.n,
          timing.worst / 1e6), timing = Timing{};
    uint32_t fb = 0;
    int fx = 0, fy = 0;
    if (ok && InLevel(*fork) && Shown(*fork, &fb, &fx, &fy)) return remaster_falcon::FrameShown(*fork, fb, fx, fy, true);
  }
  remaster_falcon::FrameShown(*m, buffer, x, y);
}

const char* Hint(st::Machine*) { return nullptr; }

void DrawOverlay(SDL_Renderer* r, float view_h) {
  if (!g_menu.open) return;
  SDL_BlendMode old;
  SDL_GetRenderDrawBlendMode(r, &old);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  auto box = [&](float x, float y, float w, float h, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    SDL_FRect f{x, y, w, h};
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderFillRect(r, &f);
  };
  auto outline = [&](float x, float y, float w, float h, uint8_t cr, uint8_t cg, uint8_t cb) {
    SDL_FRect f{x, y, w, h};
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderRect(r, &f);
  };
  auto text = [&](float x, float y, const std::string& t, uint8_t cr, uint8_t cg, uint8_t cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    glyphs::Text(r, x, y, t);
  };
  auto centred = [&](float cx, float y, const std::string& t, uint8_t cr, uint8_t cg, uint8_t cb) {
    text(cx - float(t.size()) * 4, y, t, cr, cg, cb);
  };
  if (g_icons_new) {  // (made from the machine as the menu opened)
    for (int t = 0; t < kIconTypes; ++t) {
      SDL_Texture*& tex = g_icon_tex[t];
      if (!tex) {
        tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, kIconW, kIconH);
        if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND), SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
      }
      if (tex && g_icon_px[t].size() == size_t(kIconW * kIconH)) SDL_UpdateTexture(tex, nullptr, g_icon_px[t].data(), kIconW * 4);
    }
    g_icons_new = false;
  }
  constexpr float kCellW = 100, kCellH = 36, kGap = 6, kW = 3 * kCellW + 2 * kGap, kH = 196;
  const float x0 = (st::kScreenW - kW) / 2, top = std::max(2.0f, (view_h - kH) / 2);
  box(0, 0, st::kScreenW, view_h, 0, 0, 0, 110);  // the game dimmed behind
  box(x0 - 4, top, kW + 8, kH, 0, 0, 0, 215);
  text(x0, top + 4, "POWER-UPS", 255, 220, 0);
  std::string tokens = "TOKENS " + std::to_string(std::max(0, g_menu.tokens));
  text(x0 + kW - float(tokens.size()) * 8, top + 4, tokens, 255, 255, 255);
  static const int kGrid[3][3] = {{2, 3, 4}, {5, 1, 6}, {7, 8, 9}};
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 3; ++col) {
      const int n = kGrid[row][col];
      const Cell& c = g_menu.cells[n];
      const float x = x0 + float(col) * (kCellW + kGap), y = top + 16 + float(row) * (kCellH + kGap);
      const bool picked = g_menu.pick == n, ok = c.offered && c.affordable;
      box(x, y, kCellW, kCellH, picked ? 90 : 25, picked ? 70 : 25, picked ? 0 : 35, 255);
      if (picked) outline(x, y, kCellW, kCellH, 255, 220, 0);
      const uint8_t v = ok ? 255 : c.offered ? 130 : 70;  // can't afford: grey; not on offer: dark
      text(x + 3, y + 3, n == 1 ? "R3" : std::to_string(n), 255, 220, 0);
      std::string cost = std::to_string(n) + (n == 1 ? " TOKEN" : " TOKENS");
      text(x + kCellW - 3 - float(cost.size()) * 8, y + 3, cost, c.affordable ? 120 : 220, c.affordable ? 200 : 80,
           c.affordable ? 120 : 80);
      // The icon on the left (1.4x), dimmed with the name; the name beside it, wrapped at 9 letters.
      if (c.type >= 0 && c.type < kIconTypes) {
        if (SDL_Texture* t = g_icon_tex[c.type]) {
          SDL_SetTextureColorMod(t, v, v, v);
          SDL_FRect d{x + 4, y + 15, kIconW * 1.4f, kIconH * 1.4f};
          SDL_RenderTexture(r, t, nullptr, &d);
        }
      }
      std::vector<std::string> lines(1);
      std::string word;
      for (size_t i = 0; i <= c.name.size(); ++i) {
        char ch = i < c.name.size() ? c.name[i] : ' ';
        if (ch != ' ') {
          word += ch;
          continue;
        }
        if (word.empty()) continue;
        if (!lines.back().empty() && lines.back().size() + 1 + word.size() > 9) lines.emplace_back();
        lines.back() += (lines.back().empty() ? "" : " ") + word.substr(0, 9);
        word.clear();
      }
      for (size_t i = 0; i < lines.size() && i < 2; ++i) text(x + 27, y + 14 + float(i) * 10, lines[i], v, v, v);
    }
  float y = top + 16 + 3 * (kCellH + kGap) + 2;
  const Cell& c = g_menu.cells[g_menu.pick];
  if (g_menu.pick) {
    for (size_t i = 0; i < c.about.size() && i < 4; ++i, y += 10)
      centred(st::kScreenW / 2, y, c.about[i], 200, 200, 255);
    const bool ok = c.offered && c.affordable;
    std::string state = !c.offered ? "NOT ON OFFER NOW" : !c.affordable ? "NOT ENOUGH TOKENS" : "LET GO OF {LB} TO BUY";
    centred(st::kScreenW / 2, top + kH - 12, remap::UiText(state), ok ? 255 : 220, ok ? 220 : 90, ok ? 0 : 90);
  } else {
    centred(st::kScreenW / 2, y + 10, "RIGHT STICK: PICK   R3: 1", 200, 200, 200);
    centred(st::kScreenW / 2, y + 22, remap::UiText("{B}: NONE   LET GO OF {LB}: PLAY"), 200, 200, 200);
  }
  SDL_SetRenderDrawBlendMode(r, old);
}


}  // namespace llamazap
