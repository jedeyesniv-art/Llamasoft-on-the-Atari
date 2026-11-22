// volume.cpp - see volume.h.
#include "volume.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "settings.h"

namespace volume {
namespace {

// Each game's loudness in play (ITU BS.1770 integrated, LUFS: tools/loudness.py over headless --wav recordings of about
// a minute of play with the music on, logs/loud/, 2026-11-18). The gain brings it to kTarget (at most kMaxGainDb).
// In Llamasoft on the Atari (2026-11-22) only d2k is used, levelled to the ST games' -23 LUFS (games.cpp NormalGainDb).
struct Measured {
  const char* game;
  float lufs;
};
const Measured kMeasured[] = {
    {"d2k", -24.05f},   // frames 1400-6000 (Defender 2000 mode, firing), peak -8.8
};
constexpr float kTarget = -23.f;   // LUFS (EBU R128, as the ST games)
constexpr float kMaxGainDb = 14.f;

float g_master = 1.f;
bool g_even = true;
float g_game = 1.f;
float g_last = -1.f;   // the gain the last buffer ended at (-1: none yet)
std::vector<int16_t> g_buf;

std::string Trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Soft limiter: straight up to the knee, then a tanh curve that approaches full scale (never reaches it).
inline float Limit(float y) {
  constexpr float kKnee = 0.708f;   // -3 dBFS
  const float a = std::fabs(y);
  if (a <= kKnee) return y;
  const float over = kKnee + (1.f - kKnee) * std::tanh((a - kKnee) / (1.f - kKnee));
  return y < 0 ? -over : over;
}

}  // namespace

float Normalise(const char* game) {
  for (const Measured& m : kMeasured)
    if (std::strcmp(m.game, game) == 0) return std::pow(10.f, std::fmin(kTarget - m.lufs, kMaxGainDb) / 20.f);
  return 1.f;
}

void Init(const char* game, float master, bool even) {
  // The ST launcher's master volume (its "volume" squared) and "even out game volumes", passed on the command line.
  g_master = std::fmax(0.f, std::fmin(1.5f, master));
  g_even = even;
  g_game = Normalise(game);
  g_last = -1.f;
  std::fprintf(stderr, "volume: %s master %.2f, loudness %s (x%.3f), own %.2f -> gain %.3f\n", game, g_master,
               g_even ? "evened" : "as made", g_game, settings::Num("volume", 1.0), Gain());
}

float Gain() {
  return g_master * (g_even ? g_game : 1.f) * float(std::fmax(0.0, settings::Num("volume", 1.0)));
}

void Put(SDL_AudioStream* s, const int16_t* d, size_t n) {
  if (!s || n == 0) return;
  const float g1 = Gain(), g0 = g_last < 0 ? g1 : g_last;
  g_last = g1;
  if (g0 == 1.f && g1 == 1.f) {   // as made: untouched
    SDL_PutAudioStreamData(s, d, int(n * sizeof(int16_t)));
    return;
  }
  g_buf.resize(n);
  const float step = (g1 - g0) / float(n);
  for (size_t i = 0; i < n; ++i) {
    const float y = Limit(float(d[i]) / 32768.f * (g0 + step * float(i)));
    g_buf[i] = int16_t(std::lrintf(y * 32767.f));
  }
  SDL_PutAudioStreamData(s, g_buf.data(), int(n * sizeof(int16_t)));
}

}  // namespace volume
