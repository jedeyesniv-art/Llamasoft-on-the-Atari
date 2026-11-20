// Llamazap (Jeff Minter, Atari Falcon030, 1993): the pad as the Jaguar pad the game reads on the Falcon's enhanced
// joystick port (st::Machine::SetJagpad), and its manual check answered as a player with the manual would.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

namespace st {
class Machine;
}

namespace llamazap {

void Reset();
// Rereads llamazap_50fps (Reset does too).
void LoadSettings();
// Once a frame: pad 1 (pad::Now) -> the Jaguar pad.
void Update(st::Machine* m);
// The power-up menu (LB held in play) is open: the game is stopped (main runs no frames).
bool MenuOpen();
// The menu over the picture (logical 320 x view_h), when open.
void DrawOverlay(SDL_Renderer* r, float view_h);
// After every RunFrame: the remaster's images drawn, the frame shown handed to remaster_falcon.
void AfterFrame(st::Machine* m);
// A line over the picture, or null.
const char* Hint(st::Machine* m);

}  // namespace llamazap
