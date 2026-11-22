// Test: D2K_SAMPLE=<file> - a sampling profiler of the main thread (Windows). A thread suspends it every ~1 ms and
// counts its instruction pointer (as an offset into d2k.exe; other modules as "other"); at exit <file> gets one line
// per address, "<count> 0x<offset>", for Tools/d2k/sample_report.py (llvm-symbolizer + the PDB).
#include "sampler.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unordered_map>

namespace sampler {
namespace {

std::atomic<bool> g_stop{false};
std::thread g_thread;
std::unordered_map<uint64_t, uint64_t> g_hist;
uint64_t g_other = 0;
const char* g_file = nullptr;

}  // namespace

void Start() {
  g_file = std::getenv("D2K_SAMPLE");
  if (!g_file) return;
  HANDLE main = nullptr;
  DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main,
                  THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
  const uint64_t base = uint64_t(GetModuleHandleW(nullptr));
  // The image's size from its PE header.
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const uint64_t size = nt->OptionalHeader.SizeOfImage;
  g_thread = std::thread([main, base, size] {
    while (!g_stop) {
      if (SuspendThread(main) != DWORD(-1)) {
        CONTEXT c{};
        c.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(main, &c)) {
          const uint64_t ip = c.Rip;
          if (ip >= base && ip < base + size) ++g_hist[ip - base];
          else ++g_other;
        }
        ResumeThread(main);
      }
      Sleep(1);
    }
  });
}

void Stop() {
  if (!g_file || !g_thread.joinable()) return;
  g_stop = true;
  g_thread.join();
  if (FILE* f = std::fopen(g_file, "w")) {
    std::fprintf(f, "%llu other\n", static_cast<unsigned long long>(g_other));
    for (auto& [a, n] : g_hist)
      std::fprintf(f, "%llu 0x%llx\n", static_cast<unsigned long long>(n), static_cast<unsigned long long>(a));
    std::fclose(f);
  }
}

}  // namespace sampler
#else
namespace sampler {
void Start() {}
void Stop() {}
}  // namespace sampler
#endif
