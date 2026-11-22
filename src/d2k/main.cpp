// main.cpp - Defender 2000 (Atari Jaguar, Llamasoft 1996) on PC: d2k.exe, an SDL3 front end over the Virtual
// Jaguar core with our native renderer (d2k.cpp). Moved here from the Tempest collection (C:\Tempest2k) 2026-11-22,
// where it was t2k.exe renamed; this is its Defender 2000 half only. Notes: D2K.md.
//
//   d2k.exe [rom]                         play in a window
//   d2k.exe rom --headless --frames=N     run N frames with no window
//     --shot=F:<file.bmp>                 save the frame after frame F (repeatable)
//     --press=F:<btn>[:len]               hold a RetroPad button from frame F (default 6 frames); also thrust / reverse
//     --wav=<file.wav>                    dump the audio
//     --opt=key=value                     set a core option
//     --userdata=<dir>                    save folder (default userdata\defender2000 next to the exe)
//     --set=key=value                     override a d2k_settings.txt value
//     --d2kcheck=F:<prefix>               our software render vs the core's picture at frame F
//   From the ST launcher: --from-launcher (the menu says "Return to launcher"), --master=<gain> --even=0|1 (its
//   volume and loudness levelling), --fullscreen=0|1 (the launcher's, until the game's own setting is saved).
//   Keys in the window: Esc / pad Back = settings menu, F4 high-res art, F6 wide screen, F7 smooth motion,
//   F8 native / emulated picture, F9 CRT, F5 reloads the settings, F11 fullscreen.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "controls.h"
#include "crt.h"
#include "d2k.h"
#include "d2k_menu.h"
#include "gpulog.h"
#include "host.h"
#include "jagsurvey.h"
#include "menu.h"
#include "perf.h"
#include "sampler.h"
#include "runlog.h"
#include "settings.h"
#include "volume.h"
#include "xbr.h"

extern "C" uint8_t jagMemSpace[];

namespace fs = std::filesystem;

namespace {

struct Shot { int frame; std::string path; };
struct Press { int frame; int btn; int len; };

// Keypad names for scripted presses: kp0..kp9, kpstar, kphash -> 100 + bit.
int KeypadByName(const std::string& n) {
  if (n.size() == 3 && n[0] == 'k' && n[1] == 'p' && n[2] >= '0' && n[2] <= '9') return 100 + (n[2] - '0');
  if (n == "kpstar") return 110;
  if (n == "kphash") return 111;
  return -1;
}

int BtnByName(const std::string& n) {
  if (int k = KeypadByName(n); k >= 0) return k;
  if (n == "thrust") return 200;    // the arcade buttons (d2k::Arcade)
  if (n == "reverse") return 201;
  static const char* names[] = {"b", "y", "select", "start", "up", "down", "left", "right",
                                "a", "x", "l", "r", "l2", "r2", "l3", "r3"};
  for (int i = 0; i < 16; ++i)
    if (n == names[i]) return i;
  return -1;
}

bool WriteBmp(const std::string& path, const host::Frame& f) {
  if (f.width == 0) return false;
  std::ofstream o(path, std::ios::binary);
  if (!o) return false;
  uint32_t w = f.width, h = f.height, size = 54 + w * h * 4;
  uint8_t hdr[54] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) { std::memcpy(hdr + at, &v, 4); };
  put32(2, size); put32(10, 54); put32(14, 40); put32(18, w); put32(22, uint32_t(-int32_t(h)));
  hdr[26] = 1; hdr[28] = 32;
  o.write(reinterpret_cast<char*>(hdr), 54);
  o.write(reinterpret_cast<const char*>(f.pixels.data()), std::streamsize(w) * h * 4);
  return true;
}

bool WriteWav(const std::string& path, const std::vector<int16_t>& s, uint32_t rate) {
  std::ofstream o(path, std::ios::binary);
  if (!o) return false;
  uint32_t data = uint32_t(s.size() * 2), v;
  uint16_t v16;
  o.write("RIFF", 4); v = 36 + data; o.write((char*)&v, 4); o.write("WAVEfmt ", 8);
  v = 16; o.write((char*)&v, 4); v16 = 1; o.write((char*)&v16, 2); v16 = 2; o.write((char*)&v16, 2);
  o.write((char*)&rate, 4); v = rate * 4; o.write((char*)&v, 4); v16 = 4; o.write((char*)&v16, 2);
  v16 = 16; o.write((char*)&v16, 2); o.write("data", 4); o.write((char*)&data, 4);
  o.write(reinterpret_cast<const char*>(s.data()), data);
  return true;
}

// The cartridge image: given, else the first .jag / .j64 / .rom with "Defender" in its name next to the exe, in
// game\ there, the working folder, or the nearest Private\ up the tree (the repo: build\d2k.exe).
std::string FindRom(const std::string& given) {
  if (!given.empty()) return given;
  const fs::path base = fs::path(SDL_GetBasePath());
  std::vector<fs::path> dirs = {base, base / "game", fs::current_path()};
  for (fs::path p = base; !p.empty() && dirs.size() < 12; p = p.parent_path()) {
    dirs.push_back(p / "Private");
    dirs.push_back(p / "Private" / "d2k");
    if (p == p.parent_path()) break;
  }
  for (auto& d : dirs) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(d, ec)) {
      auto name = e.path().filename().string();
      auto ext = e.path().extension().string();
      for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
      if ((ext == ".jag" || ext == ".j64" || ext == ".rom") && name.find("Defender") != std::string::npos)
        return e.path().string();
    }
  }
  return {};
}

// The buttons (d2k_menu): Jaguar B fire, C smart bomb, A lightning laser (Plus); Thrust / Reverse are the arcade's,
// done on the d-pad (d2k::Arcade). Setting d2k_stick_lr off: the stick moves up / down only (arcade); the d-pad
// keeps the game's own turn-and-thrust and works its menus. Keyboard: Z fire, C smart bomb, X laser, Space thrust,
// Shift reverse, O Option, P / Enter Pause, arrows. The core maps RetroPad B -> Jag B, A -> Jag A, Y -> Jag C,
// SELECT -> Pause, START -> Option.
// Outside play (d2k::InPlay: title, menus, tables, between levels) the pad's bindings don't apply: fixed menu buttons
// - A / Start select (Jaguar B), B back (Jaguar C: level select -> mode menu), Y Option, d-pad / stick move - so a
// remapped layout (fire on RT, thrust on A) still works the menus.
host::Input ReadPad(SDL_Gamepad* pad, uint32_t held, const bool* keys) {
  host::Input in;
  auto set = [&](int b, bool on) { if (on) in.buttons |= uint16_t(1u << b); };
  if (!d2k::InPlay()) {
    d2k::Arcade(false, false);   // (lets go of a held Reverse)
    if (pad) {
      auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
      set(host::B, btn(SDL_GAMEPAD_BUTTON_SOUTH) || btn(SDL_GAMEPAD_BUTTON_START));
      set(host::Y, btn(SDL_GAMEPAD_BUTTON_EAST));
      set(host::START, btn(SDL_GAMEPAD_BUTTON_NORTH));
      set(host::UP, btn(SDL_GAMEPAD_BUTTON_DPAD_UP));
      set(host::DOWN, btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
      set(host::LEFT, btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
      set(host::RIGHT, btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
      const int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
      set(host::LEFT, lx < -16000);
      set(host::RIGHT, lx > 16000);
      set(host::UP, ly < -16000);
      set(host::DOWN, ly > 16000);
    }
    if (keys) {
      set(host::UP, keys[SDL_SCANCODE_UP]);
      set(host::DOWN, keys[SDL_SCANCODE_DOWN]);
      set(host::LEFT, keys[SDL_SCANCODE_LEFT]);
      set(host::RIGHT, keys[SDL_SCANCODE_RIGHT]);
      set(host::B, keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE]);
      set(host::Y, keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_BACKSPACE]);
      set(host::A, keys[SDL_SCANCODE_X]);
      set(host::START, keys[SDL_SCANCODE_O]);
    }
    return in;
  }
  set(host::B, d2k::AutoFire(controls::FnHeld(held, d2k_menu::kFire) || (keys && keys[SDL_SCANCODE_Z])));
  set(host::Y, controls::FnHeld(held, d2k_menu::kBomb));
  set(host::A, controls::FnHeld(held, d2k_menu::kLaser));
  set(host::START, controls::FnHeld(held, d2k_menu::kOption));
  set(host::SELECT, controls::FnHeld(held, d2k_menu::kPause));
  bool thrust = controls::FnHeld(held, d2k_menu::kThrust), reverse = controls::FnHeld(held, d2k_menu::kReverse);
  const bool stick_lr = settings::Str("d2k_stick_lr", "game") != "off";
  if (pad) {
    auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
    set(host::UP, btn(SDL_GAMEPAD_BUTTON_DPAD_UP));
    set(host::DOWN, btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    set(host::LEFT, btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    set(host::RIGHT, btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    const int lx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    if (stick_lr) {
      set(host::LEFT, lx < -16000);
      set(host::RIGHT, lx > 16000);
    }
    set(host::UP, ly < -16000);
    set(host::DOWN, ly > 16000);
  }
  if (keys) {
    set(host::UP, keys[SDL_SCANCODE_UP]);
    set(host::DOWN, keys[SDL_SCANCODE_DOWN]);
    set(host::LEFT, keys[SDL_SCANCODE_LEFT]);
    set(host::RIGHT, keys[SDL_SCANCODE_RIGHT]);
    set(host::Y, keys[SDL_SCANCODE_C]);
    set(host::A, keys[SDL_SCANCODE_X]);
    set(host::START, keys[SDL_SCANCODE_O]);
    set(host::SELECT, keys[SDL_SCANCODE_P] || keys[SDL_SCANCODE_RETURN]);
    thrust = thrust || keys[SDL_SCANCODE_SPACE];
    reverse = reverse || keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
  }
  const int dir = d2k::Arcade(thrust, reverse);
  set(host::LEFT, dir < 0);
  set(host::RIGHT, dir > 0);
  if (dir) in.buttons &= uint16_t(~(1u << (dir < 0 ? host::RIGHT : host::LEFT)));   // (the arcade button wins)
  return in;
}

// The cartridge EEPROM (high scores, keys, options) lives in <userdata>\defender2000.eeprom. Written when it changes
// (checked every second) and at exit; the previous file is kept as .bak.
struct Eeprom {
  fs::path path;
  std::vector<uint8_t> last;

  void Load(const fs::path& dir) {
    path = dir / "defender2000.eeprom";
    size_t n = 0;
    uint8_t* mem = host::SaveRam(&n);
    // No file yet: keep the core's all-zero buffer. Not 0xFF like a factory-blank chip: the game's save only
    // writes words that differ from its copy (zero when the chip was invalid), so from an all-ones chip the first
    // save leaves stale words, the next boot fails the checksum and resets the settings.
    std::ifstream f(path, std::ios::binary);
    if (mem && n && f) {
      std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
      if (d.size() == n) std::memcpy(mem, d.data(), n);
      else std::fprintf(stderr, "eeprom: %s is %zu bytes, expected %zu - not loaded\n", path.string().c_str(),
                        d.size(), n);
    }
    if (mem && n) last.assign(mem, mem + n);
  }

  void SaveIfChanged() {
    size_t n = 0;
    uint8_t* mem = host::SaveRam(&n);
    if (!mem || !n || (last.size() == n && std::memcmp(last.data(), mem, n) == 0)) return;
    std::error_code ec;
    if (fs::exists(path)) fs::copy_file(path, path.string() + ".bak", fs::copy_options::overwrite_existing, ec);
    fs::path tmp = path.string() + ".tmp";
    {
      std::ofstream o(tmp, std::ios::binary);
      o.write(reinterpret_cast<const char*>(mem), std::streamsize(n));
    }
    fs::rename(tmp, path, ec);
    last.assign(mem, mem + n);
  }
};

fs::path StatePath(const fs::path& dir) {
  return dir / "states" / ("d2k_slot" + std::to_string(int(settings::Num("state_slot", 1))) + ".state");
}

std::string SlotInfo(const fs::path& dir) {
  std::error_code ec;
  auto p = StatePath(dir);
  std::string slot = "slot " + std::to_string(int(settings::Num("state_slot", 1)));
  if (!fs::exists(p, ec)) return slot + ": empty";
  auto t = std::chrono::clock_cast<std::chrono::system_clock>(fs::last_write_time(p, ec));
  std::time_t tt = std::chrono::system_clock::to_time_t(t);
  char buf[64];
  std::strftime(buf, sizeof buf, "%d %b %H:%M", std::localtime(&tt));
  return slot + ": " + buf;
}

const char* kSettingsHeader = "# Defender 2000 (d2k.exe) settings. Edit while the game runs and press F5 to reload.\n";

}  // namespace

int main(int argc, char** argv) {
  std::string rom, wav, userdata;
  bool headless = false;
  int frames = 600;
  std::vector<Shot> shots;
  std::vector<Press> presses;
  std::vector<std::pair<std::string, std::string>> sets;
  int save_at = -1, load_at = -1, reset_at = -1;
  std::vector<std::pair<int, std::string>> d2k_checks;
  std::string save_file, load_file, gpulog_spec, survey_spec, start_state, winshot_file;
  int winshot_at = -1;
  std::string winshots_prefix;   // --winshots=F0:F1:<prefix>: every present from frame F0 to F1 -> <prefix>_<n>.bmp, quit
  int winshots_f0 = -1, winshots_f1 = -1;
  struct Poke { int frame; uint32_t addr, value; };
  std::vector<Poke> pokes;   // --poke=F:hexaddr:hexword: write a RAM word before frame F (tests)
  int peek0 = -1, peek1 = -1;
  uint32_t peek_addr = 0;   // --peek=F0:F1:hexaddr: a RAM long each frame (stderr)
  bool novsync = false;   // --novsync: present unthrottled (performance tests)
  bool from_launcher = false;   // --from-launcher: started by the ST launcher (the menu says "Return to launcher")
  int menu_test = 0;   // --menu (1) / --menu=all (2): open the menu at start (tests; winshot counts presents)
  int win_w = 1280, win_h = 960;   // --winsize=WxH
  float master = 1.f;   // --master=<gain>: the launcher's master volume (already squared)
  bool even = true;     // --even=0|1: the launcher's "even out game volumes"
  int fullscreen_default = 0;   // --fullscreen=0|1: the launcher's mode, until the game's own setting is saved
  std::vector<std::pair<std::string, std::string>> opts;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto val = [&](const char* p) { return a.substr(std::strlen(p)); };
    if (a == "--headless") headless = true;
    else if (a.rfind("--frames=", 0) == 0) frames = std::atoi(val("--frames=").c_str());
    else if (a.rfind("--winsize=", 0) == 0) {
      auto v = val("--winsize="); auto c = v.find('x');
      win_w = std::atoi(v.substr(0, c).c_str());
      win_h = std::atoi(v.substr(c + 1).c_str());
    }
    else if (a.rfind("--wav=", 0) == 0) wav = val("--wav=");
    else if (a.rfind("--userdata=", 0) == 0) userdata = val("--userdata=");
    else if (a.rfind("--master=", 0) == 0) master = float(std::atof(val("--master=").c_str()));
    else if (a.rfind("--even=", 0) == 0) even = std::atoi(val("--even=").c_str()) != 0;
    else if (a.rfind("--fullscreen=", 0) == 0) fullscreen_default = std::atoi(val("--fullscreen=").c_str());
    else if (a.rfind("--shot=", 0) == 0) {
      auto v = val("--shot="); auto c = v.find(':');
      shots.push_back({std::atoi(v.substr(0, c).c_str()), v.substr(c + 1)});
    } else if (a.rfind("--press=", 0) == 0) {
      auto v = val("--press="); auto c1 = v.find(':'); auto c2 = v.find(':', c1 + 1);
      std::string b = v.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
      presses.push_back({std::atoi(v.substr(0, c1).c_str()), BtnByName(b),
                         c2 == std::string::npos ? 6 : std::atoi(v.substr(c2 + 1).c_str())});
    } else if (a.rfind("--set=", 0) == 0) {
      auto v = val("--set="); auto e = v.find('=');
      sets.push_back({v.substr(0, e), v.substr(e + 1)});
    } else if (a.rfind("--savestate=", 0) == 0 || a.rfind("--loadstate=", 0) == 0) {
      bool sv = a[2] == 's';
      auto v = val("--savestate="); auto c = v.find(':');
      (sv ? save_at : load_at) = std::atoi(v.substr(0, c).c_str());
      (sv ? save_file : load_file) = v.substr(c + 1);
    } else if (a.rfind("--state=", 0) == 0) {
      start_state = val("--state=");
    } else if (a.rfind("--poke=", 0) == 0) {
      Poke p{};
      std::sscanf(val("--poke=").c_str(), "%d:%x:%x", &p.frame, &p.addr, &p.value);
      pokes.push_back(p);
    } else if (a.rfind("--peek=", 0) == 0) {
      auto v = val("--peek=");
      std::sscanf(v.c_str(), "%d:%d:%x", &peek0, &peek1, &peek_addr);
    } else if (a == "--novsync") {
      novsync = true;
    } else if (a == "--from-launcher") {
      from_launcher = true;
    } else if (a == "--menu" || a == "--menu=all") {
      menu_test = a == "--menu" ? 1 : 2;
    } else if (a.rfind("--winshots=", 0) == 0) {
      auto v = val("--winshots="); auto c1 = v.find(':'); auto c2 = v.find(':', c1 + 1);
      winshots_f0 = std::atoi(v.substr(0, c1).c_str());
      winshots_f1 = std::atoi(v.substr(c1 + 1, c2 - c1 - 1).c_str());
      winshots_prefix = v.substr(c2 + 1);
    } else if (a.rfind("--winshot=", 0) == 0) {
      auto v = val("--winshot="); auto c = v.find(':');
      winshot_at = std::atoi(v.substr(0, c).c_str());
      winshot_file = v.substr(c + 1);
    } else if (a.rfind("--d2kcheck=", 0) == 0) {   // --d2kcheck=F:<prefix>: our renderer vs the core
      std::string v = val("--d2kcheck=");
      d2k_checks.push_back({std::atoi(v.c_str()), v.substr(v.find(':') + 1)});
    } else if (a.rfind("--survey=", 0) == 0) {
      survey_spec = val("--survey=");
    } else if (a.rfind("--gpulog=", 0) == 0) {
      gpulog_spec = val("--gpulog=");
    } else if (a.rfind("--reset=", 0) == 0) {   // --reset=F: the console reset before frame F (menu "Restart the game")
      reset_at = std::atoi(val("--reset=").c_str());
    } else if (a.rfind("--opt=", 0) == 0) {
      auto v = val("--opt="); auto e = v.find('=');
      opts.push_back({v.substr(0, e), v.substr(e + 1)});
    } else if (a[0] != '-') rom = a;
  }

  if (!SDL_Init(headless ? 0 : (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  // Saves / settings next to the program unless given (the launcher passes its userdata\defender2000).
  if (userdata.empty()) userdata = (fs::path(SDL_GetBasePath()) / "userdata" / "defender2000").string();
  rom = FindRom(rom);
  if (rom.empty()) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Defender 2000",
                             "Put your Defender 2000 cartridge image (.jag) in game\\ next to the program.", nullptr);
    return 1;
  }
  fs::create_directories(userdata);
  if (from_launcher) runlog::Start(fs::absolute(userdata), "d2k");   // no console: <userdata>\logs\d2k.log
  settings::SetDefaultText(kSettingsHeader);
  settings::Load((fs::path(userdata) / "d2k_settings.txt").string());
  volume::Init("d2k", master, even);
  for (auto& [k, v] : sets) settings::Set(k, v);
  d2k_menu::Install(from_launcher);
  for (auto& [k, v] : opts) host::SetOption(k, v);
  std::string err;
  if (!host::Load(rom, fs::absolute(userdata).string(), &err)) {
    std::fprintf(stderr, "load: %s\n", err.c_str());
    return 1;
  }
  std::fprintf(stderr, "loaded %s: %.3f fps, %.0f Hz audio\n", rom.c_str(), host::Fps(), host::SampleRate());
  if (!d2k::IsDefender(rom)) std::fprintf(stderr, "d2k: %s doesn't look like Defender 2000 (by its name)\n", rom.c_str());
  d2k::Install();
  const fs::path udir = fs::absolute(userdata);
  if (!start_state.empty()) {   // --state=<file>: start from a save state (tests)
    std::ifstream sf(start_state, std::ios::binary);
    std::vector<uint8_t> st((std::istreambuf_iterator<char>(sf)), {});
    host::RunFrame();
    std::fprintf(stderr, "state %s: %s\n", start_state.c_str(), host::LoadState(st) ? "loaded" : "FAILED");
  }
  if (!survey_spec.empty()) {
    auto c1 = survey_spec.find(':'), c2 = survey_spec.find(':', c1 + 1);
    if (!jagsurvey::Install(std::atoi(survey_spec.substr(0, c1).c_str()),
                            std::atoi(survey_spec.substr(c1 + 1, c2 - c1 - 1).c_str()), survey_spec.substr(c2 + 1).c_str()))
      std::fprintf(stderr, "survey: can't write %s\n", survey_spec.substr(c2 + 1).c_str());
  }
  if (!gpulog_spec.empty()) {
    auto c1 = gpulog_spec.find(':'), c2 = gpulog_spec.find(':', c1 + 1);
    if (!gpulog::Install(std::atoi(gpulog_spec.substr(0, c1).c_str()),
                         std::atoi(gpulog_spec.substr(c1 + 1, c2 - c1 - 1).c_str()), gpulog_spec.substr(c2 + 1).c_str()))
      std::fprintf(stderr, "gpulog: install failed\n");
  }
  Eeprom eeprom;
  eeprom.Load(udir);

  if (headless) {
    std::vector<int16_t> all_audio;
    auto t0 = std::chrono::steady_clock::now();
    sampler::Start();   // (test: D2K_SAMPLE=<file>)
    for (int f = 0; f < frames; ++f) {
      host::Input in;
      bool thrust = false, reverse = false;
      for (auto& p : presses)
        if (p.btn >= 0 && f >= p.frame && f < p.frame + p.len) {
          if (p.btn == 200) thrust = true;
          else if (p.btn == 201) reverse = true;
          else if (p.btn >= 100) in.keypad |= uint16_t(1u << (p.btn - 100));
          else in.buttons |= uint16_t(1u << p.btn);
        }
      if (const int dir = d2k::Arcade(thrust, reverse)) in.buttons |= uint16_t(1u << (dir < 0 ? host::LEFT : host::RIGHT));
      const bool b = in.buttons & (1u << host::B);   // (Classic auto fire, as from the pad)
      in.buttons = uint16_t((in.buttons & ~(1u << host::B)) | (d2k::AutoFire(b) ? 1u << host::B : 0u));
      host::SetInput(0, in);
      if (f == reset_at) host::Reset();
      gpulog::SetFrame(f);
      jagsurvey::SetFrame(f);
      if (f == load_at) {
        std::ifstream lf(load_file, std::ios::binary);
        std::vector<uint8_t> st((std::istreambuf_iterator<char>(lf)), {});
        std::fprintf(stderr, "load state %zu bytes: %s\n", st.size(), host::LoadState(st) ? "ok" : "FAILED");
      }
      for (auto& p : pokes)
        if (p.frame == f) {
          jagMemSpace[p.addr & 0xFFFFFF] = uint8_t(p.value >> 8);
          jagMemSpace[(p.addr + 1) & 0xFFFFFF] = uint8_t(p.value);
        }
      d2k::BeforeFrame();
      host::RunFrame();
      d2k::AfterFrame();
      for (auto& [cf, cp] : d2k_checks)
        if (cf == f) d2k::Check(cp);
      if (f == save_at) {
        auto st = host::SaveState();
        std::ofstream(save_file, std::ios::binary).write(reinterpret_cast<const char*>(st.data()), std::streamsize(st.size()));
        std::fprintf(stderr, "saved state %zu bytes\n", st.size());
      }
      if (f >= peek0 && f <= peek1) {
        const uint8_t* m = jagMemSpace + (peek_addr & 0xFFFFFF);
        std::fprintf(stderr, "peek %5d %06X: %02X%02X%02X%02X\n", f, peek_addr, m[0], m[1], m[2], m[3]);
      }
      if (!wav.empty()) all_audio.insert(all_audio.end(), host::Audio().begin(), host::Audio().end());
      host::Audio().clear();
      for (auto& s : shots)
        if (s.frame == f) {
          const auto& fr = host::LastFrame();
          std::fprintf(stderr, "shot %d: %ux%u -> %s\n", f, fr.width, fr.height, s.path.c_str());
          WriteBmp(s.path, fr);
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    sampler::Stop();
    std::fprintf(stderr, "%d frames in %.2f s (%.0f fps, %.1fx real time)\n", frames, secs, frames / secs,
                 frames / secs / host::Fps());
    if (!wav.empty()) WriteWav(wav, all_audio, uint32_t(host::SampleRate()));
    eeprom.SaveIfChanged();
    host::Unload();
    SDL_Quit();
    return 0;
  }

  // (The window's icon: the exe's own, res/d2k.rc.)
  SDL_Window* win = SDL_CreateWindow("Defender 2000", win_w, win_h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (settings::Num("fullscreen", fullscreen_default) != 0) SDL_SetWindowFullscreen(win, true);
  // SDL's GPU renderer (D3D12) for the CRT shaders; the default renderer if it isn't available (no CRT then).
  SDL_Renderer* ren = crt::CreateRenderer(win);
  if (ren && !crt::Init(ren)) std::fprintf(stderr, "crt: unavailable on renderer %s\n", SDL_GetRendererName(ren));
  SDL_HideCursor();
  SDL_SetRenderVSync(ren, novsync ? 0 : 1);
  SDL_Texture* tex = nullptr;
  unsigned tw = 0, th = 0;
  // Sharp bilinear (the emulated picture): scaled up k times with nearest into `big`, then fitted with linear
  // filtering (setting picture_filter sharp | smooth | xbr). x4: the xBR 4x version (premultiplied alpha).
  struct Sharp { SDL_Texture* big = nullptr; int k = 0, w = 0, h = 0; SDL_Texture* x4 = nullptr; };
  Sharp pic_sharp;

  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, int(host::SampleRate())};
  SDL_AudioStream* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (audio) SDL_ResumeAudioStreamDevice(audio);

  auto set_fullscreen = [&](bool on) {
    SDL_SetWindowFullscreen(win, on);
    settings::Save("fullscreen", on ? "1" : "0");
  };

  SDL_Gamepad* pad = nullptr;
  const double frame_ns = 1e9 / host::Fps();
  uint64_t next = SDL_GetTicksNS();
  bool running = true;
  bool prev_back = false;
  uint32_t prev_held = 0;
  uint64_t frame_count = 0;
  menu::SetSlotInfo([&] { return SlotInfo(udir); });
  // T2K_PERF=1: every ~5 s, the average time a shown frame spends emulating, composing and presenting (perf.h).
  uint64_t perf_t1 = 0, perf_t2 = 0;
  uint64_t cap_slot = 0;   // frame_cap: the next present's slot on a fixed grid
  static const bool pace_log = std::getenv("PACE_LOG") != nullptr;
  uint64_t pace_draw = 0;
  double pace_t = 0;
  sampler::Start();   // (test: D2K_SAMPLE=<file>)
  while (running) {
    // Frame rate (setting frame_cap: 0 = the display's, 60 = at most 60 pictures a second - less work, e.g. the Steam
    // Deck at 90 Hz). Slots on a fixed 60 Hz grid; each picture goes to the first refresh after its slot (on 90 Hz:
    // one, then two refreshes - 60 on average, never sliding to 45). The picture is drawn for the time it is shown
    // (now is read after the wait), so motion stays right. Without vsync it simply waits for the slot.
    if (const double cap = settings::Num("frame_cap", 0); cap >= 24 && !novsync) {
      const uint64_t step = uint64_t(1e9 / cap), t = SDL_GetTicksNS();
      if (cap_slot == 0 || t > cap_slot + 4 * step) cap_slot = t;   // start / resync after a stall
      if (t + 1000000 < cap_slot) SDL_DelayPrecise(cap_slot - t - 1000000);
      cap_slot += step;
    } else {
      cap_slot = 0;
    }
    uint64_t now = SDL_GetTicksNS();
    // The blend's time: the loop starts just after the last present returned, so on the display's refresh grid -
    // give or take the return's own jitter (~1 ms) and a stall now and then. A model of the grid (period from the
    // display, phase pulled 10% towards each measurement, k refreshes since the last one) gives even steps; a
    // measurement far off the grid restarts it (no vsync, a long stall, another display).
    uint64_t blend_now = now;
    if (!novsync) {
      static double grid = 0, period = 0;
      if (period == 0) {
        const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));
        period = dm && dm->refresh_rate > 20 ? 1e9 / double(dm->refresh_rate) : 1e9 / 60.0;
      }
      const double m = double(now);
      const double k = std::max(1.0, std::round((m - grid) / period));
      const double pred = grid + k * period, err = m - pred;
      if (grid == 0 || std::fabs(err) > 0.35 * period || k > 8) {
        grid = m;
      } else {
        grid = pred + 0.1 * err;
        period += 0.002 * err / k;
      }
      blend_now = uint64_t(grid);
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
      else if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) SDL_HideCursor();   // no mouse use: keep it out of view
      else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) SDL_ShowCursor();
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE && !menu::IsOpen())
        menu::Open();
      else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F5) {
        settings::Reload();
        menu::Toast("Settings reloaded", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F9) {
        int m = (int(settings::Num("crt", 2)) + 1) % 3;
        settings::Save("crt", std::to_string(m));
        crt::LoadSettings();
        menu::Toast(!crt::Available() ? "CRT unavailable (needs the GPU renderer)"
                    : m == 2 ? "CRT++" : m == 1 ? "CRT+" : "CRT off", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F8) {
        const bool on = settings::Num("d2k_native", 1) == 0;
        settings::Save("d2k_native", on ? "1" : "0");
        menu::Toast(on ? "Native picture" : "Emulated picture", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F7) {
        const bool on = settings::Num("d2k_interpolate", 1) == 0;
        settings::Save("d2k_interpolate", on ? "1" : "0");
        menu::Toast(on ? "Smooth motion on" : "Smooth motion off", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F6) {
        // 2 Defender 2000 only -> 1 all modes -> 0 off -> 2
        const int ws = int(settings::Num("d2k_widescreen", 2)), nx = ws == 2 ? 1 : ws == 1 ? 0 : 2;
        settings::Save("d2k_widescreen", std::to_string(nx));
        menu::Toast(nx == 2 ? "Wide screen: Defender 2000 only" : nx == 1 ? "Wide screen: all modes"
                                                                        : "Wide screen off (4:3)", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F4) {
        const bool on = settings::Num("d2k_hires", 1) == 0;
        settings::Save("d2k_hires", on ? "1" : "0");
        menu::Toast(on ? "High-res art on" : "High-res art off (original)", now);
      } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F11)
        set_fullscreen(!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN));
      else if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
      else if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
        SDL_CloseGamepad(pad);
        pad = nullptr;
      }
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    bool back = pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
    uint32_t held = controls::Held(pad);
    uint32_t pressed = held & ~prev_held;
    prev_held = held;

    if (menu::IsOpen()) {
      menu::Nav nav = menu::ReadNav(pad, keys);
      for (int b = 0; b < controls::kButtonCount; ++b)
        if (pressed >> b & 1) { nav.new_button = b; break; }
      switch (menu::Update(nav, now)) {
        case menu::kQuit: running = false; break;
        case d2k_menu::kRestart:
          host::Reset();
          host::Audio().clear();
          menu::Toast("Game restarted", now);
          break;
        case menu::kToggleFullscreen: set_fullscreen(!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)); break;
        case menu::kSaveState: {
          auto st = host::SaveState();
          std::error_code ec;
          fs::create_directories(StatePath(udir).parent_path(), ec);
          bool ok = false;
          {
            std::ofstream o(StatePath(udir), std::ios::binary);
            o.write(reinterpret_cast<const char*>(st.data()), std::streamsize(st.size()));
            ok = bool(o) && !st.empty();
          }
          menu::Toast(ok ? "State saved (" + SlotInfo(udir) + ")" : "Save failed", now);
          break;
        }
        case menu::kLoadState: {
          std::ifstream f(StatePath(udir), std::ios::binary);
          std::vector<uint8_t> st((std::istreambuf_iterator<char>(f)), {});
          bool ok = !st.empty() && host::LoadState(st);
          menu::Toast(ok ? "State loaded (" + SlotInfo(udir) + ")" : "No state in this slot", now);
          host::Audio().clear();
          break;
        }
        default: break;
      }
      next = now;   // paused: no catching up afterwards
    } else {
      if (back && !prev_back) menu::Open();
    }
    prev_back = back;

    // Emulate as many frames as real time asks for (at most 4 to catch up).
    int todo = 0;
    if (!menu::IsOpen()) {
      while (blend_now >= next && todo < 4) { next += uint64_t(frame_ns); ++todo; }   // (the blend's time: t stays in step)
      if (now > next + uint64_t(frame_ns * 8)) next = now;  // stalled: resync
    }
    // Original mode (setting native): every improvement off - the emulator's own picture, plain bilinear 4:3.
    const bool native = settings::Num("native", 0) != 0;
    const uint64_t perf_t0 = perf::Now();
    perf::Count("frames emulated", todo);
    for (int i = 0; i < todo; ++i) {
      host::Input in = ReadPad(pad, held, keys);
      // Test: --press in a window run too (RetroPad buttons, at the frame count; not the arcade / keypad names).
      for (auto& p : presses)
        if (p.btn >= 0 && p.btn < 100 && int(frame_count) >= p.frame && int(frame_count) < p.frame + p.len)
          in.buttons |= uint16_t(1u << p.btn);
      host::SetInput(0, in);
      host::SetVideoEnabled(i == todo - 1);   // (catch-up frames: unseen)
      d2k::BeforeFrame();
      host::RunFrame();
      d2k::AfterFrame();
      host::SetVideoEnabled(true);
      if (++frame_count % 60 == 0) eeprom.SaveIfChanged();
      auto& a = host::Audio();
      if (audio && !a.empty()) {
        // Keep the queue short: drop audio if more than ~100 ms is waiting.
        if (SDL_GetAudioStreamQueued(audio) < int(host::SampleRate() * 4 / 10))
          volume::Put(audio, a.data(), a.size());
      }
      a.clear();
    }

    perf_t1 = SDL_GetTicksNS();
    perf::Add("emulate", perf_t1 - perf_t0);
    perf::Add("input + events", perf_t0 - now);
    int ww, wh;
    SDL_GetRenderOutputSize(ren, &ww, &wh);
    // The CRT (setting crt, F9): everything drawn into a window-sized scene, which the CRT then draws to the window.
    crt::LoadSettings();
    const bool use_crt = !native && crt::Mode() > 0;
    static SDL_Texture* scene = nullptr;
    static int scene_w = 0, scene_h = 0;
    if (!scene || scene_w != ww || scene_h != wh) {
      if (scene) SDL_DestroyTexture(scene);
      scene = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, ww, wh);
      SDL_SetTextureBlendMode(scene, SDL_BLENDMODE_NONE);
      SDL_SetTextureScaleMode(scene, SDL_SCALEMODE_LINEAR);
      scene_w = ww; scene_h = wh;
    }
    SDL_Texture* const frame_target = scene;
    SDL_SetRenderTarget(ren, frame_target);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    float crt_src_w = 0, crt_src_h = 0;   // the window in game pixels (scanlines / mask follow the game's lines)
    const auto& fr = host::LastFrame();
    if (fr.width) {
      if (!tex || tw != fr.width || th != fr.height) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, fr.width, fr.height);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        tw = fr.width; th = fr.height;
        todo = 1;
      }
      float sw = float(wh) * 4.f / 3.f, sh = float(wh);
      if (sw > ww) { sw = float(ww); sh = sw * 3.f / 4.f; }
      SDL_FRect dst{(ww - sw) / 2, (wh - sh) / 2, sw, sh};
      const bool d2k_native = !native && settings::Num("d2k_native", 1) != 0;
      const std::string filter = native ? "smooth" : settings::Str("picture_filter", "sharp");
      const bool use_xbr = filter == "xbr";
      const bool sharp = filter == "sharp";
      static std::string filter_was;
      static bool d2k_native_was = true;
      const bool dirty = todo > 0 || filter != filter_was || d2k_native != d2k_native_was;   // (a change in the menu)
      filter_was = filter;
      d2k_native_was = d2k_native;
      const uint64_t perf_pic = perf::Now();
      if (d2k_native) {
        // Our picture, positions interpolated between the game's updates. The blend's time t: how far real time is
        // into the current frame's slot (the frame due at next - frame_ns was just emulated); menu: the latest.
        const double t = menu::IsOpen() ? 1.0 : 1.0 - double(int64_t(next) - int64_t(blend_now)) / frame_ns;
        if (pace_log) pace_draw = SDL_GetTicksNS(), pace_t = t;
        d2k::Draw(ren, dst, SDL_FRect{0, 0, float(ww), float(wh)}, float(std::clamp(t, 0.0, 1.0)));
      } else {
        if (dirty) {
          static std::vector<uint32_t> argb;
          argb.resize(fr.pixels.size());
          for (size_t i = 0; i < argb.size(); ++i) {
            uint32_t p = fr.pixels[i] & 0xFFFFFF;
            argb[i] = p ? (p | 0xFF000000u) : 0;
          }
          SDL_UpdateTexture(tex, nullptr, argb.data(), int(fr.width * 4));
          if (use_xbr) {
            static std::vector<uint32_t> up;
            xbr::Scale4x(argb.data(), int(fr.width), int(fr.height), up);
            if (!pic_sharp.x4 || pic_sharp.w != int(fr.width) || pic_sharp.h != int(fr.height) || pic_sharp.k != -4) {
              if (pic_sharp.x4) SDL_DestroyTexture(pic_sharp.x4);
              pic_sharp.x4 = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                               int(fr.width) * 4, int(fr.height) * 4);
              SDL_SetTextureBlendMode(pic_sharp.x4, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
              SDL_SetTextureScaleMode(pic_sharp.x4, SDL_SCALEMODE_LINEAR);
              pic_sharp.w = int(fr.width); pic_sharp.h = int(fr.height); pic_sharp.k = -4;
            }
            SDL_UpdateTexture(pic_sharp.x4, nullptr, up.data(), int(fr.width) * 4 * 4);
          }
        }
        const int k = std::clamp(int(std::ceil(dst.h / float(fr.height))), 1, 12);
        if (use_xbr && pic_sharp.x4) {
          SDL_RenderTexture(ren, pic_sharp.x4, nullptr, &dst);
        } else if (sharp) {
          if (pic_sharp.k < 0) pic_sharp.k = 0;   // was the xBR texture's: rebuild the sharp target
          bool fresh = false;
          if (!pic_sharp.big || pic_sharp.k != k || pic_sharp.w != int(fr.width) || pic_sharp.h != int(fr.height)) {
            if (pic_sharp.big) SDL_DestroyTexture(pic_sharp.big);
            pic_sharp.big = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                              int(fr.width) * k, int(fr.height) * k);
            SDL_SetTextureBlendMode(pic_sharp.big, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(pic_sharp.big, SDL_SCALEMODE_LINEAR);
            pic_sharp.k = k; pic_sharp.w = int(fr.width); pic_sharp.h = int(fr.height);
            fresh = true;
          }
          if (pic_sharp.big && (dirty || fresh)) {
            SDL_SetRenderTarget(ren, pic_sharp.big);
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
            SDL_RenderClear(ren);
            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
            SDL_RenderTexture(ren, tex, nullptr, nullptr);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_SetRenderTarget(ren, frame_target);
          }
          SDL_RenderTexture(ren, pic_sharp.big ? pic_sharp.big : tex, nullptr, &dst);
        } else {
          SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
          SDL_RenderTexture(ren, tex, nullptr, &dst);
        }
      }
      perf::Add("picture", perf::Now() - perf_pic);
      crt_src_w = ww * float(fr.width) / dst.w;
      crt_src_h = wh * float(fr.height) / dst.h;
    }
    const uint64_t perf_crt = perf::Now();
    SDL_SetRenderTarget(ren, nullptr);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (use_crt) crt::DrawPicture(ren, scene, SDL_FRect{0, 0, float(ww), float(wh)}, crt_src_w, crt_src_h);
    else SDL_RenderTexture(ren, scene, nullptr, nullptr);
    if (menu::Shown()) menu::Draw(ren, ww, wh);
    menu::DrawToast(ren, ww, wh, now);
    static int presents = 0;
    if (menu_test && presents++ == 0) menu::Open(menu_test == 2);
    if (winshots_f0 >= 0 && int(frame_count) >= winshots_f0) {   // --winshots: every present in a frame range
      static int n = 0;
      if (SDL_Surface* shot = SDL_RenderReadPixels(ren, nullptr)) {
        SDL_SaveBMP(shot, (winshots_prefix + "_" + std::to_string(n++) + ".bmp").c_str());
        std::fprintf(stderr, "winshots %d: frame %d\n", n - 1, int(frame_count));
        perf::DiscardFrame();
        SDL_DestroySurface(shot);
      }
      if (int(frame_count) >= winshots_f1) running = false;
    }
    if (winshot_at >= 0 && (menu_test ? presents : int(frame_count)) >= winshot_at) {   // --winshot=N:<bmp>
      if (SDL_Surface* shot = SDL_RenderReadPixels(ren, nullptr)) {
        SDL_SaveBMP(shot, winshot_file.c_str());
        perf::DiscardFrame();
        SDL_DestroySurface(shot);
      }
      running = false;
    }
    perf_t2 = SDL_GetTicksNS();
    perf::Add("crt + menu", perf_t2 - perf_crt);
    perf::Add("compose", perf_t2 - perf_t1);
    SDL_RenderPresent(ren);
    {
      const uint64_t t3 = SDL_GetTicksNS();
      if (pace_log)   // test: PACE_LOG=1 - per present: frames emulated, draw time, t, when the present returned (ms)
        std::fprintf(stderr, "pace todo %d draw %.3f t %.3f presented %.3f\n", todo, double(pace_draw) / 1e6, pace_t,
                     double(t3) / 1e6);
      perf::Add("present (+vsync)", t3 - perf_t2);
      if (running) perf::EndFrame(t3 - now, ww, wh);
    }
  }
  perf::Flush();
  sampler::Stop();

  eeprom.SaveIfChanged();
  host::Unload();
  if (pad) SDL_CloseGamepad(pad);
  if (audio) SDL_DestroyAudioStream(audio);
  crt::Shutdown();
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
