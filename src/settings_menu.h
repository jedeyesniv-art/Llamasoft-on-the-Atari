// The settings panel (owner 2026-11-03): every setting worth changing, in the 8x8 system font, drawn on the left
// half of the screen so the picture to the right shows what a change does. Opened by the Settings disk on the
// carousel (every section) or in a game by Back / F1 (the emulation is suspended; general, CRT and that game's
// section). Up / down pick, left / right change (held: repeats), A toggles, Y back to the default, LB / RB jump
// a section; B / Back / Esc / F1 resume (the Settings disk: back to the disks), X (keyboard X) quits a game to
// the disks (owner 2026-11-20: B was pressed by accident, it only closes the panel now).
// Changes are saved at once (settings.txt) and applied through the callback.
#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <string>

#include "savestate.h"

namespace menu {

enum class Result { kNone, kClose, kQuit };

// Rereads every module's settings after a change.
void SetApply(std::function<void()> apply);
// For the fullscreen row.
void SetWindow(SDL_Window* window);

// game_id empty: the Settings disk (all sections); else that game's (general, CRT, its own).
void Open(const std::string& game_id, const std::string& title);
void Close();
bool IsOpen();

Result HandleEvent(const SDL_Event& e);
// Once per displayed frame: the pad's d-pad and stick (held = repeat).
void Update(SDL_Gamepad* pad, double dt);
// Savestates (the SAVE STATES section, in a game): a save / load asked for on the panel, for main to do between
// frames; then SetStateStatus with the outcome (the row shows it). SetStatePeek: how the panel reads a slot (its
// time and picture), set while a game runs (null: no section - the docs readers).
struct StateRequest {
  bool save = false;
  int slot = 1;
};
bool TakeStateRequest(StateRequest* r);
void SetStateStatus(const std::string& status);
void SetStatePeek(std::function<bool(int, savestate::Info*)> peek);
// Over whatever is drawn, at the output's resolution (the logical presentation is set aside meanwhile).
void Render(SDL_Renderer* r);

}  // namespace menu
