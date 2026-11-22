// external.cpp - see external.h. The waiting is the Tempest collection launcher's (C:\Tempest2k src/launcher.cpp,
// verified there on Windows and the Steam Deck): the game's window is pushed to the front when it appears and once
// more a second later (after it goes fullscreen), and again while we still have the front in its first 30 s.
#include "external.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "log.h"

namespace external {
namespace {

// A process's visible top-level window, if it has one yet.
HWND FindProcessWindow(DWORD pid) {
  struct Want {
    DWORD pid;
    HWND hwnd;
  } want{pid, nullptr};
  EnumWindows(
      [](HWND h, LPARAM p) -> BOOL {
        auto* w = reinterpret_cast<Want*>(p);
        DWORD owner = 0;
        GetWindowThreadProcessId(h, &owner);
        if (owner == w->pid && IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) {
          w->hwnd = h;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&want));
  return want.hwnd;
}

std::wstring Wide(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
  std::wstring w(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
  return w;
}

}  // namespace

bool Run(SDL_Window* window, const std::filesystem::path& exe, const std::vector<std::string>& args,
         std::string* error, int* exit_code) {
  AllowSetForegroundWindow(ASFW_ANY);  // we have the foreground, so the game may take it
  std::wstring cmd = L"\"" + exe.wstring() + L"\"";
  for (const std::string& a : args) cmd += L" \"" + Wide(a) + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &si,
                      &pi)) {
    if (error) *error = "couldn't start " + exe.string() + " (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  CloseHandle(pi.hThread);
  LOG("external: started %s (pid %lu)", exe.filename().string().c_str(), pi.dwProcessId);
  HWND me = window ? static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                             SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))
                   : nullptr;
  const uint64_t started = SDL_GetTicksNS();
  uint64_t first_push = 0, last_push = 0;
  int pushed = 0;
  while (WaitForSingleObject(pi.hProcess, 50) != WAIT_OBJECT_0) {
    const uint64_t t = SDL_GetTicksNS() - started;
    if (pushed < 2 && t < 20000000000ull && (pushed == 0 || t > first_push + 1000000000ull)) {
      if (HWND h = FindProcessWindow(pi.dwProcessId)) {
        SetForegroundWindow(h);
        if (pushed++ == 0) first_push = t;
      }
    }
    const uint64_t now = SDL_GetTicksNS();
    if (me && t < 30000000000ull && now - last_push > 500000000ull && GetForegroundWindow() == me) {
      if (HWND h = FindProcessWindow(pi.dwProcessId)) {
        last_push = now;
        SetForegroundWindow(h);
      }
    }
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);  // (the pad's presses were the game's)
  }
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  if (exit_code) *exit_code = int(code);
  LOG("external: %s ended (exit code %lu) after %.1f s", exe.filename().string().c_str(), code,
      (SDL_GetTicksNS() - started) / 1e9);
  if (window) {
    SDL_RaiseWindow(window);
    if (me) SetForegroundWindow(me);
  }
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  return true;
}

}  // namespace external
