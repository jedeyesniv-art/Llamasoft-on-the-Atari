// Test: D2K_SAMPLE=<file> - samples the main thread's instruction pointer (sampler.cpp).
#pragma once

namespace sampler {
void Start();   // call on the main thread; does nothing without D2K_SAMPLE
void Stop();    // writes the file
}  // namespace sampler
