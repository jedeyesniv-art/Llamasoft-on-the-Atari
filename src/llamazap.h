// Llamazap (Jeff Minter, Atari Falcon030, 1993): the pad as the Jaguar pad the game reads on the Falcon's enhanced
// joystick port (st::Machine::SetJagpad), and its manual check answered as a player with the manual would.
#pragma once

#include <cstdint>

namespace st {
class Machine;
}

namespace llamazap {

void Reset();
// Once a frame: pad 1 (pad::Now) -> the Jaguar pad.
void Update(st::Machine* m);
// A line over the picture, or null.
const char* Hint(st::Machine* m);

}  // namespace llamazap
