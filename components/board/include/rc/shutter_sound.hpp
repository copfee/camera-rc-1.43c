#pragma once
#include "rc/shutter_pcm.hpp"
#include <cstddef>

namespace rc {
constexpr unsigned kShutterSampleRate = 16000;
constexpr size_t kShutterFrames = sizeof(kShutterPcm) / sizeof(kShutterPcm[0]);
} // namespace rc
