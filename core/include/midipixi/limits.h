#pragma once

namespace mpx {

// Compile-time capacity limits. Everything in the engine is statically sized so
// the same code runs on the MCU without heap allocation.
inline constexpr int kMaxLayers = 8;
inline constexpr int kMaxVoices = 16;   // per layer
inline constexpr int kNumEnvs = 2;      // per layer (Env 1 = "amp" envelope, decides voice lifetime)
inline constexpr int kNumLfos = 3;      // per layer
inline constexpr int kMaxModRows = 24;  // per layer
inline constexpr int kNumMacros = 8;
inline constexpr int kMaxPixels = 1024;
inline constexpr int kNameLen = 32;     // including terminating zero

} // namespace mpx
