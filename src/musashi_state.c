/* Savestates (cpu_musashi.cpp SaveCpu / LoadCpu): Musashi's CPU state up to its first pointer. Everything in
   m68ki_cpu_core before cyc_instruction is plain data (registers, flags, the interrupt and PMMU state, the cycle
   counts); from there on it's pointers to tables and our callbacks, which a new process has elsewhere: a load keeps
   the live ones. Compiled with Musashi (CMakeLists.txt), the only place its struct is visible. */
#include <stddef.h>
#include <string.h>

#include "m68kcpu.h"

size_t musashi_state_size(void) { return offsetof(m68ki_cpu_core, cyc_instruction); }
void musashi_state_get(void* out) { memcpy(out, &m68ki_cpu, musashi_state_size()); }
void musashi_state_set(const void* in) { memcpy(&m68ki_cpu, in, musashi_state_size()); }
