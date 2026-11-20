#include "remaster_falcon.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

#include "crt.h"
#include "log.h"
#include "remaster.h"

namespace remaster_falcon {
namespace {

constexpr int kScale = 4;
constexpr float kTeleport = 40;  // moved further than this between two frames: drawn where it is, not slid

bool g_capturing = false;
std::vector<Item> g_drawing;                     // the main loop's list so far
std::map<uint32_t, std::vector<Item>> g_done;    // per buffer: the list last drawn into it
std::vector<Item> g_cur, g_prev;                 // the frames shown: now and before
std::vector<int> g_match;                        // per item of g_cur: its item in g_prev (-1: new / jumped)
bool g_have = false;                             // g_cur is a captured frame
int g_disp_x = 0, g_disp_y = 0, g_dx = 0, g_dy = 0;  // where the display started; moved since the frame before
bool g_have_disp = false;
uint32_t g_shown_buffer = 0;
bool g_clean_dirty = true;

std::unordered_map<uint64_t, Image> g_images;
struct Baked {
  SDL_Texture* tex = nullptr;
  uint64_t pal = 0;  // the palette it was made with
};
std::unordered_map<uint64_t, Baked> g_baked;
SDL_Texture* g_target = nullptr;
SDL_Texture* g_clean = nullptr;
int g_clean_w = 0, g_clean_h = 0;
uint32_t g_pal[256];
uint64_t g_pal_hash = 0;

uint64_t Hash(const uint32_t* p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
  return h;
}

void Match() {
  g_match.assign(g_cur.size(), -1);
  std::vector<bool> taken(g_prev.size(), false);
  for (size_t i = 0; i < g_cur.size(); ++i) {
    const Item& a = g_cur[i];
    int best = -1;
    float best_d = kTeleport * kTeleport;
    for (size_t j = 0; j < g_prev.size(); ++j) {
      const Item& b = g_prev[j];
      if (taken[j] || b.key != a.key || b.dot != a.dot) continue;
      // The previous position as it would be now with the background's move (things fixed to the landscape).
      float dx = a.x - b.x, dy = a.y - b.y, d = dx * dx + dy * dy;
      if (d < best_d) best_d = d, best = int(j);
    }
    if (best >= 0) taken[size_t(best)] = true, g_match[i] = best;
  }
}

SDL_Texture* TextureFor(SDL_Renderer* r, uint64_t id) {
  auto im = g_images.find(id);
  if (im == g_images.end() || im->second.w <= 0) return nullptr;
  Baked& b = g_baked[id];
  if (b.tex && b.pal == g_pal_hash) return b.tex;
  const Image& image = im->second;
  if (!b.tex) {
    b.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, image.w, image.h);
    if (!b.tex) return nullptr;
    SDL_SetTextureBlendMode(b.tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(b.tex, SDL_SCALEMODE_NEAREST);
  }
  std::vector<uint32_t> px(image.px.size());
  for (size_t i = 0; i < px.size(); ++i) px[i] = image.px[i] == kClear ? 0 : g_pal[image.px[i] & 255];
  SDL_UpdateTexture(b.tex, nullptr, px.data(), image.w * 4);
  b.pal = g_pal_hash;
  return b.tex;
}

// The playfield: the first run of lines from the clean source.
void PlayfieldRows(const st::Machine& m, int* y0, int* y1) {
  const auto& lines = m.CleanLines();
  *y0 = *y1 = 0;
  int y = 0, n = int(lines.size());
  while (y < n && !lines[size_t(y)]) ++y;
  *y0 = y;
  while (y < n && lines[size_t(y)]) ++y;
  *y1 = y;
}

}  // namespace

void Start() {
  g_capturing = false, g_have = false, g_have_disp = false;
  g_drawing.clear(), g_done.clear(), g_cur.clear(), g_prev.clear(), g_match.clear();
  g_images.clear();
  for (auto& [id, b] : g_baked)
    if (b.tex) SDL_DestroyTexture(b.tex);
  g_baked.clear();
}

void SetCapturing(bool on) {
  if (!on && g_capturing) g_have = false, g_drawing.clear(), g_done.clear();
  g_capturing = on;
}
bool Capturing() { return g_capturing; }

void Add(const Item& item) { g_drawing.push_back(item); }

void EndFrame(uint32_t buffer) {
  g_done[buffer] = std::move(g_drawing);
  g_drawing.clear();
}

bool HaveImage(uint64_t id) { return g_images.count(id) != 0; }
void SetImage(uint64_t id, Image image) {
  g_images[id] = std::move(image);
  auto b = g_baked.find(id);
  if (b != g_baked.end()) b->second.pal = 0;  // (made again)
}

void FrameShown(const st::Machine& m, uint32_t buffer, int disp_x, int disp_y) {
  for (int i = 0; i < 256; ++i) g_pal[i] = m.FalconColour(i);
  g_pal_hash = Hash(g_pal, 256);
  g_clean_dirty = true;
  auto done = g_done.find(buffer);
  if (!g_capturing || done == g_done.end()) {
    g_have = false, g_have_disp = false;
    return;
  }
  // The background's move: it repeats every 320 pixels (Llamazap), so the short way round.
  if (g_have_disp) {
    g_dx = disp_x - g_disp_x, g_dy = disp_y - g_disp_y;
    g_dx = int(std::remainder(float(g_dx), 320.0f));
    if (std::abs(g_dx) > 32 || std::abs(g_dy) > 32) g_dx = g_dy = 0;  // a cut: not slid
  } else {
    g_dx = g_dy = 0;
  }
  g_disp_x = disp_x, g_disp_y = disp_y, g_have_disp = true;
  g_prev = std::move(g_cur);
  g_cur = done->second;
  g_shown_buffer = buffer;
  Match();
  g_have = true;
}

bool Active() { return g_capturing && g_have && remaster::Enabled(); }

void Draw(SDL_Renderer* r, const st::Machine& m, const SDL_FRect& view, float alpha) {
  const int w = m.FrameWidth(), h = m.FrameHeight(), margin = m.CleanMargin() & ~15, cw = w + 2 * margin;
  const auto& clean = m.CleanFrame();
  if (clean.size() != size_t(cw) * size_t(h)) return;
  const int W = w * kScale, H = h * kScale;
  if (!g_target) {
    g_target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);
    if (!g_target) {
      LOG("remaster_falcon: no render target (%s)", SDL_GetError());
      return;
    }
  }
  if (!g_clean || g_clean_w != cw || g_clean_h != h) {
    if (g_clean) SDL_DestroyTexture(g_clean);
    g_clean = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, cw, h);
    if (!g_clean) return;
    SDL_SetTextureScaleMode(g_clean, SDL_SCALEMODE_NEAREST);
    g_clean_w = cw, g_clean_h = h, g_clean_dirty = true;
  }
  if (g_clean_dirty) SDL_UpdateTexture(g_clean, nullptr, clean.data(), cw * 4), g_clean_dirty = false;
  alpha = std::clamp(alpha, 0.0f, 1.0f);

  int lw = 0, lh = 0;
  SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
  SDL_GetRenderLogicalPresentation(r, &lw, &lh, &mode);
  SDL_SetRenderTarget(r, g_target);
  SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
  SDL_RenderClear(r);

  // The background: the playfield's lines slid by the part of the scroll still to come (they show the frame's
  // position; between frames the picture is (1 - alpha) of the move behind it), the rest as they are.
  const float shift = -(1 - alpha) * float(g_dx);  // (the display went dx right: the picture was further right)
  int py0 = 0, py1 = 0;
  PlayfieldRows(m, &py0, &py1);
  const auto& lines = m.CleanLines();
  for (int y = 0; y < h;) {
    int e = y + 1;
    while (e < h && lines[size_t(e)] == lines[size_t(y)]) ++e;
    float s = lines[size_t(y)] ? -shift : 0;  // content drawn s pixels to the right
    float k = std::floor(s), frac = s - k;
    SDL_FRect src{float(margin) - k, float(y), float(w + 1), float(e - y)};
    src.x = std::clamp(src.x, 0.0f, float(cw - w - 1));
    SDL_FRect dst{std::round(frac * kScale), float(y * kScale), float((w + 1) * kScale), float((e - y) * kScale)};
    SDL_Rect clip{0, y * kScale, W, (e - y) * kScale};
    SDL_SetRenderClipRect(r, &clip);
    SDL_RenderTexture(r, g_clean, &src, &dst);
    y = e;
  }

  // The items, in the playfield.
  SDL_Rect clip{0, py0 * kScale, W, (py1 - py0) * kScale};
  SDL_SetRenderClipRect(r, &clip);
  for (size_t i = 0; i < g_cur.size(); ++i) {
    const Item& it = g_cur[i];
    float x = it.x, y = it.y;
    if (i < g_match.size() && g_match[i] >= 0) {
      const Item& o = g_prev[size_t(g_match[i])];
      x = o.x + (x - o.x) * alpha, y = o.y + (y - o.y) * alpha;
    }
    if (it.dot) {
      uint32_t c = g_pal[it.colour];
      SDL_SetRenderDrawColor(r, uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c), 255);
      SDL_FRect d{std::round(x * kScale), std::round(y * kScale), float(2 * kScale), float(2 * kScale)};
      SDL_RenderFillRect(r, &d);
      continue;
    }
    auto im = g_images.find(it.image);
    if (im == g_images.end()) continue;
    SDL_Texture* t = TextureFor(r, it.image);
    if (!t) continue;
    const Image& image = im->second;
    SDL_FRect d{std::round((x + float(image.ox)) * kScale), std::round((y + float(image.oy)) * kScale),
                float(image.w * kScale), float(image.h * kScale)};
    SDL_RenderTexture(r, t, nullptr, &d);
  }
  SDL_SetRenderClipRect(r, nullptr);
  SDL_SetRenderTarget(r, nullptr);
  SDL_SetRenderLogicalPresentation(r, lw, lh, mode);
  SDL_SetTextureScaleMode(g_target, crt::Enabled() ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_PIXELART);
  crt::DrawPicture(r, g_target, view, float(w), float(h));
}

void ComposeSoftware(const st::Machine& m, std::vector<uint32_t>* out) {
  const int w = m.FrameWidth(), h = m.FrameHeight(), margin = m.CleanMargin() & ~15, cw = w + 2 * margin;
  const auto& clean = m.CleanFrame();
  out->assign(size_t(w) * size_t(h), 0xFF000000u);
  if (clean.size() != size_t(cw) * size_t(h)) return;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) (*out)[size_t(y) * w + x] = clean[size_t(y) * cw + x + margin];
  if (!g_have) return;
  int py0 = 0, py1 = 0;
  PlayfieldRows(m, &py0, &py1);
  auto put = [&](int x, int y, uint32_t c) {
    if (x >= 0 && x < w && y >= py0 && y < py1) (*out)[size_t(y) * w + x] = c;
  };
  for (const Item& it : g_cur) {
    int x = int(std::lround(it.x)), y = int(std::lround(it.y));
    if (it.dot) {
      for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) put(x + i, y + j, g_pal[it.colour]);
      continue;
    }
    auto im = g_images.find(it.image);
    if (im == g_images.end()) continue;
    const Image& image = im->second;
    for (int j = 0; j < image.h; ++j)
      for (int i = 0; i < image.w; ++i) {
        uint16_t v = image.px[size_t(j) * image.w + i];
        if (v != kClear) put(x + image.ox + i, y + image.oy + j, g_pal[v & 255]);
      }
  }
}

void LogFrame() {
  LOG("remaster_falcon: %zu items, display at (%d, %d), moved (%d, %d)", g_cur.size(), g_disp_x, g_disp_y, g_dx, g_dy);
  for (size_t i = 0; i < g_cur.size() && i < 40; ++i) {
    const Item& it = g_cur[i];
    LOG("  %s key %012llX at (%.0f, %.0f) image %016llX%s", it.dot ? "dot" : "sprite", (unsigned long long)it.key, it.x,
        it.y, (unsigned long long)it.image, i < g_match.size() && g_match[i] >= 0 ? " (slid)" : "");
  }
}

void Shutdown() {
  Start();
  if (g_target) SDL_DestroyTexture(g_target), g_target = nullptr;
  if (g_clean) SDL_DestroyTexture(g_clean), g_clean = nullptr;
}

}  // namespace remaster_falcon
