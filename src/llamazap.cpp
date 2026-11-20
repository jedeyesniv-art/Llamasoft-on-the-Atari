#include "llamazap.h"

#include <SDL3/SDL.h>

#include <utility>

#include "cpu.h"
#include "games.h"
#include "log.h"
#include "machine.h"
#include "pad.h"
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
// down ($1316A, which also shows the new screen). All the game's logic is in the VBL (routine *$2F98C = $11FE4 in
// play): with 1 the scroll is identical frame for frame, and the Falcon's own 16 MHz draws all 50 (~70% of the
// instructions still in the wait loop). The game itself has it as a hidden key: 1-9 = the divisor ($10AFA).
constexpr uint32_t kDivisor = 0x49DDC, kDivisorRead = 0x1403C, kVblRoutine = 0x2F98C, kPlayRoutine = 0x11FE4;
bool g_50fps = true;

uint16_t Get16(const st::Machine& m, uint32_t a) { return uint16_t(m.Ram()[a] << 8 | m.Ram()[a + 1]); }
uint32_t Get32(const st::Machine& m, uint32_t a) { return uint32_t(Get16(m, a)) << 16 | Get16(m, a + 2); }
void Put16(st::Machine* m, uint32_t a, uint16_t v) { m->Ram()[a] = uint8_t(v >> 8), m->Ram()[a + 1] = uint8_t(v); }

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

}  // namespace

void Reset() { LoadSettings(); }

void LoadSettings() { g_50fps = settings::Get("llamazap_50fps", 1) != 0; }

// The pad as the Jaguar pad: left stick / d-pad the directions, A = A (fire), B = B (jump / lock the fire angle),
// X = C (fire), RT = A too, Y = Option, Start = Pause, the right stick and R3 the keypad (R3 = 1).
void Update(st::Machine* m) {
  const pad::State& p = pad::Now();
  uint32_t bits = 0;
  const bool ship_select = int16_t(Get16(*m, kShipSelect)) >= 0;
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
  if (!ship_select) {
    if (p.Held(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) bits |= st::kJagKey0 << 1;
    bits |= KeypadFromStick(p);
  }
  bits |= FromKeyboard();
  m->SetJagpad(bits);
  // Once unpacked (the program unpacks itself over where it was loaded): the manual check's hook.
  if (Get16(*m, kCheck) == 0xB679 && Get32(*m, kCheck + 2) == kWanted) {
    Put16(m, kCheck, 0x4AFC);
    m->AddHook(kCheck, OnManualCheck);
  }
  if (Get16(*m, kEntryCall) == 0x61FF && Get32(*m, kEntryCall + 2) == 0xBF60) {
    Put16(m, kEntryCall, 0x4AFC);
    m->AddHook(kEntryCall, OnEntryScreen);
  }
  // The divisor: 1 in play, the original 2 elsewhere (only this build: the main loop reads it where we expect).
  if (Get16(*m, kDivisorRead) == 0x3039 && Get32(*m, kDivisorRead + 2) == kDivisor) {
    bool play = Get32(*m, kVblRoutine) == kPlayRoutine;
    Put16(m, kDivisor, g_50fps && !games::Stock() && play ? 1 : 2);
  }
}

const char* Hint(st::Machine*) { return nullptr; }

}  // namespace llamazap
