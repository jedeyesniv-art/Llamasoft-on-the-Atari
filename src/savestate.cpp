#include "savestate.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <map>

#include "cpu.h"
#include "log.h"
#include "machine.h"

namespace fs = std::filesystem;

namespace savestate {
namespace {

constexpr char kMagic[4] = {'L', 'L', 'S', 'T'};
constexpr uint32_t kFormat = 1;

struct Module {
  std::function<void(std::vector<uint8_t>*)> save;
  std::function<void(const std::vector<uint8_t>&)> load;
};
std::map<std::string, Module>& Modules() {
  static std::map<std::string, Module> m;
  return m;
}

// PackBits: a control byte 0..127 = that many + 1 bytes follow as they are; 129..255 = the next byte 257 - c
// times. RAM and the picture are mostly runs.
std::vector<uint8_t> Pack(const std::vector<uint8_t>& in) {
  std::vector<uint8_t> out;
  out.reserve(in.size() / 4);
  size_t i = 0;
  while (i < in.size()) {
    size_t run = 1;
    while (i + run < in.size() && run < 128 && in[i + run] == in[i]) ++run;
    if (run >= 3) {
      out.push_back(uint8_t(257 - run)), out.push_back(in[i]);
      i += run;
      continue;
    }
    size_t lit = 0;  // literals up to the next run of 3
    while (i + lit < in.size() && lit < 128) {
      if (i + lit + 2 < in.size() && in[i + lit] == in[i + lit + 1] && in[i + lit] == in[i + lit + 2]) break;
      ++lit;
    }
    out.push_back(uint8_t(lit - 1));
    out.insert(out.end(), in.begin() + long(i), in.begin() + long(i + lit));
    i += lit;
  }
  return out;
}

bool Unpack(const uint8_t* p, size_t n, size_t want, std::vector<uint8_t>* out) {
  out->clear();
  out->reserve(want);
  size_t i = 0;
  while (i < n && out->size() < want) {
    uint8_t c = p[i++];
    if (c < 128) {
      size_t len = size_t(c) + 1;
      if (i + len > n) return false;
      out->insert(out->end(), p + i, p + i + len);
      i += len;
    } else if (c > 128) {
      if (i >= n) return false;
      out->insert(out->end(), size_t(257 - c), p[i++]);
    }
  }
  return out->size() == want;
}

struct Out {
  std::vector<uint8_t> b;
  void Raw(const void* p, size_t n) { b.insert(b.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n); }
  template <class T>
  void Put(T v) { Raw(&v, sizeof v); }
  void Str(const std::string& s) { Put(uint32_t(s.size())), Raw(s.data(), s.size()); }
  void Blob(const std::vector<uint8_t>& v) { Put(uint64_t(v.size())), Raw(v.data(), v.size()); }
};

struct In {
  const uint8_t* p;
  size_t n, at = 0;
  bool ok = true;
  bool Raw(void* dst, size_t len) {
    if (!ok || at + len > n) return ok = false;
    std::memcpy(dst, p + at, len), at += len;
    return true;
  }
  template <class T>
  T Get() {
    T v{};
    Raw(&v, sizeof v);
    return v;
  }
  std::string Str() {
    uint32_t len = Get<uint32_t>();
    if (!ok || at + len > n) return ok = false, std::string();
    std::string s(reinterpret_cast<const char*>(p + at), len);
    at += len;
    return s;
  }
  std::vector<uint8_t> Blob() {
    uint64_t len = Get<uint64_t>();
    if (!ok || at + len > n) return ok = false, std::vector<uint8_t>();
    std::vector<uint8_t> v(p + at, p + at + len);
    at += size_t(len);
    return v;
  }
};

std::vector<uint8_t> ReadFile(const fs::path& path) {
  std::vector<uint8_t> v;
  FILE* f = nullptr;
  if (_wfopen_s(&f, path.wstring().c_str(), L"rb") != 0 || !f) return v;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n > 0) {
    v.resize(size_t(n));
    if (std::fread(v.data(), 1, v.size(), f) != v.size()) v.clear();
  }
  std::fclose(f);
  return v;
}

// The header, then the body unpacked: the picture, the machine, the CPU, the modules.
struct Parsed {
  std::string game, program;
  std::time_t when = 0;
  std::vector<uint8_t> body;
};
bool Parse(const fs::path& path, Parsed* out, std::string* error) {
  std::vector<uint8_t> file = ReadFile(path);
  if (file.empty()) return *error = "empty slot", false;
  In in{file.data(), file.size()};
  char magic[4] = {};
  in.Raw(magic, 4);
  if (!in.ok || std::memcmp(magic, kMagic, 4) != 0) return *error = "not a state", false;
  if (in.Get<uint32_t>() != kFormat) return *error = "from another version", false;
  out->game = in.Str(), out->program = in.Str();
  out->when = std::time_t(in.Get<int64_t>());
  uint64_t raw = in.Get<uint64_t>();
  if (!in.ok || raw > (1ull << 30)) return *error = "damaged", false;
  if (!Unpack(file.data() + in.at, file.size() - in.at, size_t(raw), &out->body)) return *error = "damaged", false;
  return true;
}

}  // namespace

bool Register(const std::string& name, std::function<void(std::vector<uint8_t>*)> save,
              std::function<void(const std::vector<uint8_t>&)> load) {
  Modules()[name] = {std::move(save), std::move(load)};
  return true;
}

fs::path SlotPath(const fs::path& userdata, const std::string& game_id, int slot) {
  return userdata / "states" / game_id / ("slot" + std::to_string(slot) + ".state");
}

bool Save(const fs::path& path, st::Machine& m, const std::string& game, const std::string& program,
          std::string* error, const Info* picture) {
  const uint64_t t0 = SDL_GetTicksNS();
  Out body;
  if (picture && picture->w > 0 && picture->h > 0 && picture->pixels.size() == size_t(picture->w) * size_t(picture->h)) {
    body.Put(uint32_t(picture->w)), body.Put(uint32_t(picture->h));
    body.Raw(picture->pixels.data(), picture->pixels.size() * 4);
  } else {
    body.Put(uint32_t(m.FrameWidth())), body.Put(uint32_t(m.FrameHeight()));
    body.Raw(m.Frame(), size_t(m.FrameWidth()) * size_t(m.FrameHeight()) * 4);
  }
  std::vector<uint8_t> machine;
  m.SaveState(&machine);
  body.Blob(machine);
  body.Blob(cpu::SaveCpu());
  body.Put(uint32_t(Modules().size()));
  for (auto& [name, mod] : Modules()) {
    std::vector<uint8_t> v;
    mod.save(&v);
    body.Str(name), body.Blob(v);
  }
  Out file;
  file.Raw(kMagic, 4), file.Put(kFormat), file.Str(game), file.Str(program);
  file.Put(int64_t(std::time(nullptr))), file.Put(uint64_t(body.b.size()));
  std::vector<uint8_t> packed = Pack(body.b);
  file.Raw(packed.data(), packed.size());

  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  fs::path tmp = path;
  tmp += ".new";
  FILE* f = nullptr;
  if (_wfopen_s(&f, tmp.wstring().c_str(), L"wb") != 0 || !f) return *error = "can't write it", false;
  const bool written = std::fwrite(file.b.data(), 1, file.b.size(), f) == file.b.size();
  std::fclose(f);
  if (!written) return fs::remove(tmp, ec), *error = "can't write it", false;
  fs::rename(tmp, path, ec);
  if (ec) return *error = "can't write it", false;
  LOG("savestate: %s saved (%zu KB, %zu KB packed, %.1f ms)", path.filename().string().c_str(), body.b.size() / 1024,
      packed.size() / 1024, (SDL_GetTicksNS() - t0) / 1e6);
  return true;
}

bool Load(const fs::path& path, st::Machine& m, const std::string& game, const std::string& program,
          std::string* error) {
  const uint64_t t0 = SDL_GetTicksNS();
  Parsed p;
  if (!Parse(path, &p, error)) return false;
  if (p.game != game || p.program != program) return *error = "from another game", false;
  In in{p.body.data(), p.body.size()};
  uint32_t w = in.Get<uint32_t>(), h = in.Get<uint32_t>();
  in.at += size_t(w) * h * 4;  // (the picture: the machine has its own)
  std::vector<uint8_t> machine = in.Blob(), cpu_state = in.Blob();
  std::map<std::string, std::vector<uint8_t>> modules;
  uint32_t count = in.Get<uint32_t>();
  for (uint32_t i = 0; i < count && in.ok; ++i) {
    std::string name = in.Str();
    modules[name] = in.Blob();
  }
  if (!in.ok) return *error = "damaged", false;
  // The CPU first (it can only fail on its size), then the machine (all or nothing), then the modules.
  std::vector<uint8_t> cpu_before = cpu::SaveCpu();
  if (!cpu::LoadCpu(cpu_state)) return *error = "from another version", false;
  if (!m.LoadState(machine, error)) {
    cpu::LoadCpu(cpu_before);
    return false;
  }
  for (auto& [name, mod] : Modules())
    if (auto it = modules.find(name); it != modules.end()) mod.load(it->second);
  LOG("savestate: %s loaded (%.1f ms)", path.filename().string().c_str(), (SDL_GetTicksNS() - t0) / 1e6);
  return true;
}

bool Peek(const fs::path& path, const std::string& game, Info* info) {
  Parsed p;
  std::string error;
  if (!Parse(path, &p, &error) || p.game != game) return false;
  In in{p.body.data(), p.body.size()};
  uint32_t w = in.Get<uint32_t>(), h = in.Get<uint32_t>();
  if (!in.ok || w == 0 || h == 0 || w > 1024 || h > 512) return false;
  info->when = p.when, info->w = int(w), info->h = int(h);
  info->pixels.resize(size_t(w) * h);
  return in.Raw(info->pixels.data(), info->pixels.size() * 4);
}

}  // namespace savestate
