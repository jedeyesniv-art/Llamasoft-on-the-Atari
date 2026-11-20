#include "llamazap.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
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
// frames shorter: timed by drawn frames), every frame drawn at the 68030's own speed.
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
int g_aim = -1;  // the right stick's direction in the game's numbering, -1 = not pushed


uint16_t Get16(const st::Machine& m, uint32_t a) { return uint16_t(m.Ram()[a] << 8 | m.Ram()[a + 1]); }
uint32_t Get32(const st::Machine& m, uint32_t a) { return uint32_t(Get16(m, a)) << 16 | Get16(m, a + 2); }
void Put16(st::Machine* m, uint32_t a, uint16_t v) { m->Ram()[a] = uint8_t(v >> 8), m->Ram()[a + 1] = uint8_t(v); }

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

// The keypad on the right stick (owner 2026-11-20), flicked: up-left 2, up 3, up-right 4, left 5, right 6,
// down-left 7, down 8, down-right 9; R3 = 1 (key 1 = power-up 0, a new ship: kept off the stick so it isn't
// bought by accident). In play the keys buy power-ups (1-9: power-ups 0-8, table $11CC8). Buying 1 shows SELECT
// SHIP: $2F594 >= 0 counts down ~300 frames, and a key's column on the Jaguar keypad picks the ship ($13046:
// 1 4 7 * Phoenix, 2 5 8 0 Falcon, 3 6 9 # Condor; first all keys must be up: $49DB8). Then the d-pad picks
// instead (left 4 Phoenix, up 5 Falcon, right 6 Condor; those three don't move the ship then), the right stick
// and R3 are off.
constexpr uint32_t kShipSelect = 0x2F594;
uint32_t KeypadFromStick(const pad::State& p) {
  float x = p.rx, y = p.ry;
  if (x * x + y * y < 0.6f * 0.6f) return 0;
  int col = x < -0.38f ? 0 : x > 0.38f ? 2 : 1, row = y < -0.38f ? 0 : y > 0.38f ? 2 : 1;
  static const int kKey[3][3] = {{2, 3, 4}, {5, 0, 6}, {7, 8, 9}};
  int key = kKey[row][col];
  return key ? st::kJagKey0 << key : 0;
}

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
  std::string name;
  std::vector<std::string> about;
};
struct Menu {
  bool open = false;
  int pick = 0;  // 1..9, 0 none
  int tokens = 0;
  Cell cells[10];
};
Menu g_menu;
bool g_lb_was = false;
bool g_stick_blocked = false;  // after the menu: the right stick / R3 are no keys until let go
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

}  // namespace

void Reset() {
  LoadSettings();
  g_menu = Menu{};
  g_lb_was = g_stick_blocked = false;
  g_inject_frames = 0;
}

bool MenuOpen() { return g_menu.open; }

void LoadSettings() {
  g_50fps = settings::Get("llamazap_50fps", 1) != 0;
  g_stage = std::clamp(int(settings::Get("llamazap_stage", 0)), 0, 25);
  g_twin_stick = settings::Get("llamazap_twin_stick", 1) != 0;
}

// The pad as the Jaguar pad: left stick / d-pad the directions, A = A (fire), B = B (jump / lock the fire angle),
// X = C (fire), RT = A too, Y = Option, Start = Pause, the right stick and R3 the keypad (R3 = 1).
void Update(st::Machine* m) {
  const pad::State& p = pad::Now();
  uint32_t bits = 0;
  const bool ship_select = int16_t(Get16(*m, kShipSelect)) >= 0;
  if (MenuFrame(m, p, ship_select)) {
    m->SetJagpad(0);
    return;
  }
  pad::State q = p;  // ship select: the d-pad's left / up / right are the ships, not moves
  if (ship_select) {
    for (auto [b, key] : {std::pair{SDL_GAMEPAD_BUTTON_DPAD_LEFT, 4}, std::pair{SDL_GAMEPAD_BUTTON_DPAD_UP, 5},
                          std::pair{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 6}})
      if (p.Held(b)) bits |= st::kJagKey0 << key, q.buttons &= ~(1u << b);
  }
  uint8_t joy = pad::JoyBits(q);  // left stick or d-pad
  if (joy & st::kJoyUp) bits |= st::kJagUp;
  if (joy & st::kJoyDown) bits |= st::kJagDown;
  if (joy & st::kJoyLeft) bits |= st::kJagLeft;
  if (joy & st::kJoyRight) bits |= st::kJagRight;
  if (p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.rt > 0.5f) bits |= st::kJagA;
  if (p.Held(SDL_GAMEPAD_BUTTON_EAST)) bits |= st::kJagB;
  if (p.Held(SDL_GAMEPAD_BUTTON_WEST)) bits |= st::kJagC;
  if (p.Held(SDL_GAMEPAD_BUTTON_NORTH)) bits |= st::kJagOption;
  if (p.Held(SDL_GAMEPAD_BUTTON_START)) bits |= st::kJagPause;
  if (g_stick_blocked && p.rx * p.rx + p.ry * p.ry < 0.3f * 0.3f && !p.Held(SDL_GAMEPAD_BUTTON_RIGHT_STICK))
    g_stick_blocked = false;
  const bool twin = g_twin_stick && LlamatronStage(*m);
  g_aim = -1;
  if (twin && p.rx * p.rx + p.ry * p.ry >= 0.5f * 0.5f) {
    // Clockwise from up, in eighths; the game's 0 is up-left.
    float a = std::atan2(p.rx, -p.ry);
    g_aim = (int(std::lround(a / (3.14159265f / 4))) + 1) & 7;
  }
  if (!ship_select && !g_stick_blocked && !twin) {
    if (p.Held(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) bits |= st::kJagKey0 << 1;
    bits |= KeypadFromStick(p);
  }
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
  // The divisor: 1 in play, the original 2 elsewhere (only this build: the main loop reads it where we expect).
  if (Get16(*m, kDivisorRead) == 0x3039 && Get32(*m, kDivisorRead + 2) == kDivisor) {
    Put16(m, kDivisor, g_50fps && !games::Stock() && InLevel(*m) ? 1 : 2);
  }
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
      // The name, wrapped at 12 letters.
      std::vector<std::string> lines(1);
      std::string word;
      for (size_t i = 0; i <= c.name.size(); ++i) {
        char ch = i < c.name.size() ? c.name[i] : ' ';
        if (ch != ' ') {
          word += ch;
          continue;
        }
        if (word.empty()) continue;
        if (!lines.back().empty() && lines.back().size() + 1 + word.size() > 12) lines.emplace_back();
        lines.back() += (lines.back().empty() ? "" : " ") + word.substr(0, 12);
        word.clear();
      }
      for (size_t i = 0; i < lines.size() && i < 2; ++i)
        centred(x + kCellW / 2, y + 14 + float(i) * 10, lines[i], v, v, v);
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
