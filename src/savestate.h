// Savestates (owner 2026-11-20): a game's whole state in a file, per game, four slots - from the settings panel's
// SAVE STATES section or F2 / F4 in a game. A file holds the picture (the panel's thumbnail), the machine
// (Machine::SaveState: RAM without the hooks' patches, every device), the CPU (cpu::SaveCpu: no pointers, so
// another process can load it) and what game modules keep in C++ that matters to play (Register). Packed with a
// simple run-length scheme. A file from another build, game or machine is refused, never half loaded.
#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <type_traits>
#include <ctime>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace st {
class Machine;
}

namespace savestate {

constexpr int kSlots = 4;

// A game module's own state that matters to play (Photon Storm's analog heading and shots...): written with each
// state, given back on a load (load gets the bytes save wrote; absent from an older file = not called).
// Returns true (for static registration).
bool Register(const std::string& name, std::function<void(std::vector<uint8_t>*)> save,
              std::function<void(const std::vector<uint8_t>&)> load);

// For a module's fields: one list serves both ways, e.g.
//   template <class Io> void Fields(Io& io) { io(g_keys)(g_taps)(g_heading); }
// with savestate::Writer / Reader. Plain values, arrays of them, std::vector / deque of them.
struct Writer {
  std::vector<uint8_t>* out;
  template <class T>
  Writer& operator()(const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    out->insert(out->end(), p, p + sizeof v);
    return *this;
  }
  template <class T>
  Writer& operator()(const std::vector<T>& v) {
    (*this)(uint32_t(v.size()));
    for (const T& e : v) (*this)(e);
    return *this;
  }
  template <class T>
  Writer& operator()(const std::deque<T>& v) {
    (*this)(uint32_t(v.size()));
    for (const T& e : v) (*this)(e);
    return *this;
  }
};
struct Reader {
  const std::vector<uint8_t>& in;
  size_t at = 0;
  bool ok = true;
  template <class T>
  Reader& operator()(T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (!ok || at + sizeof v > in.size()) return ok = false, *this;
    std::memcpy(&v, in.data() + at, sizeof v), at += sizeof v;
    return *this;
  }
  template <class T>
  Reader& operator()(std::vector<T>& v) {
    uint32_t n = 0;
    (*this)(n);
    if (!ok || n > in.size()) return ok = false, *this;
    v.resize(n);
    for (T& e : v) (*this)(e);
    return *this;
  }
  template <class T>
  Reader& operator()(std::deque<T>& v) {
    uint32_t n = 0;
    (*this)(n);
    if (!ok || n > in.size()) return ok = false, *this;
    v.resize(n);
    for (T& e : v) (*this)(e);
    return *this;
  }
};
// Registers a module's Fields (a template taking Writer / Reader) under `name`.
template <class F>
bool RegisterFields(const std::string& name, F fields) {
  return Register(
      name, [fields](std::vector<uint8_t>* out) { Writer w{out}; fields(w); },
      [fields](const std::vector<uint8_t>& in) { Reader r{in}; fields(r); });
}

// userdata\states\<game id>\slot<N>.state
std::filesystem::path SlotPath(const std::filesystem::path& userdata, const std::string& game_id, int slot);

// What the panel shows of a slot: when it was saved and its picture (ARGB). False: empty or unreadable.
struct Info {
  std::time_t when = 0;
  int w = 0, h = 0;
  std::vector<uint32_t> pixels;
};
// Writes the machine's state (between frames). `game` / `program` identify it; false + why.
// `picture`: what the slot shows (the remaster's sprites composed in); null or the wrong size: the machine's frame.
bool Save(const std::filesystem::path& path, st::Machine& m, const std::string& game, const std::string& program,
          std::string* error, const Info* picture = nullptr);
// Loads one into `m` (the same game, running). Nothing changes unless it all fits; false + why.
bool Load(const std::filesystem::path& path, st::Machine& m, const std::string& game, const std::string& program,
          std::string* error);

bool Peek(const std::filesystem::path& path, const std::string& game, Info* info);

}  // namespace savestate
