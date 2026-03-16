#pragma once

#include <cstddef>

namespace lockless {

// MSVC doesn't implement std::hardware_destructive_interference_size even in C++20
// (it's in <new> but guarded behind /experimental:hardwareInterferenceSize which
// is not worth enabling). 64 bytes is correct for x86/x64 and most ARM64 chips.
// the rare chips with 128-byte lines will still be correct (just over-padded).
static constexpr size_t CACHE_LINE_SIZE = 64;

} // namespace lockless
