// external.h - a game that is not an ST game, run as its own program next to ours (d2k.exe: Defender 2000 on the
// Atari Jaguar, 2026-11-22). The launcher waits for it underneath, giving it the front, then comes back.
#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

namespace external {

// Starts `exe` with `args` (each quoted) in its own folder and waits until it ends, keeping our window responsive
// (events pumped and dropped). False (and *error) if it couldn't be started. *exit_code = the program's.
bool Run(SDL_Window* window, const std::filesystem::path& exe, const std::vector<std::string>& args,
         std::string* error, int* exit_code);

}  // namespace external
