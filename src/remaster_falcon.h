// The remaster for the Falcon (Llamazap; REMASTER.md has the ST version, remaster.cpp). The game runs as it is,
// but its sprite and dot draws in play are caught by hooks (llamazap.cpp) and not drawn into its screen: recorded
// here per frame. The background comes from the game's own clean copy (Machine::CleanFrame: the playfield's lines
// read from the background buffer, with a margin to slide it). Draw composes at 4x: the clean picture slid by
// the part of a frame's scroll still to come, the items moved between the last two frames shown, at the display's
// rate; then the CRT look (crt::DrawPicture with the Falcon's 384 x 240).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

#include "machine.h"

namespace remaster_falcon {

struct Item {
  uint64_t key = 0;  // identity between frames: the drawing code (items of a key take the nearest of the frame before)
  float x = 0, y = 0;  // on the screen: the image's corner (its own offset added when drawn), a dot's top-left
  uint64_t image = 0;  // sprites: the image id
  bool dot = false;
  uint8_t colour = 0;  // dots: the colour index (2 x 2 pixels)
};

// A sprite image: colour indices, kClear = not drawn; ox / oy from where the game was told to draw it.
constexpr uint16_t kClear = 0xFFFF;
struct Image {
  int w = 0, h = 0, ox = 0, oy = 0;
  std::vector<uint16_t> px;
};

void Start();  // a game starts: nothing kept
void SetCapturing(bool on);  // the hooks capture (play on a stage done); off: the next frames are the game's own
bool Capturing();
// Capture (from the hooks, inside the CPU's run).
void Add(const Item& item);
void EndFrame(uint32_t buffer);  // the main loop finished drawing into `buffer` (shown from the next VBL)
bool HaveImage(uint64_t id);
void SetImage(uint64_t id, Image image);
// After every RunFrame: the buffer shown (its first line's address in it) and where the display started in it
// (x: pixels from the line's start incl. the fine scroll, y: lines).
void FrameShown(const st::Machine& m, uint32_t buffer, int disp_x, int disp_y);
bool Active();  // something to draw this frame (capturing, the remaster on, a frame captured)
// The picture into `view` (logical coordinates): positions `alpha` (0..1) of the way from the frame shown before.
void Draw(SDL_Renderer* r, const st::Machine& m, const SDL_FRect& view, float alpha);
// Tests: this frame composed at 1x, whole pixels, alpha 1 (should equal the game's own picture).
void ComposeSoftware(const st::Machine& m, std::vector<uint32_t>* out);
void LogFrame();  // tests: this frame's items
void Shutdown();

}  // namespace remaster_falcon
