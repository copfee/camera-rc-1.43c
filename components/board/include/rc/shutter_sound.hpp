#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace rc {
constexpr unsigned kShutterSampleRate = 16000;
constexpr size_t kShutterFrames = kShutterSampleRate / 10; // 100 ms, including a silent tail.
using ShutterSound = std::array<int16_t, kShutterFrames * 2>;

// Original two-click shutter effect. No recordings, filesystem or random state.
inline ShutterSound make_shutter_sound() {
    ShutterSound pcm{};
    uint32_t noise = 0x831143c;
    for (size_t frame = 0; frame < kShutterFrames; ++frame) {
        const double ms = 1000.0 * frame / kShutterSampleRate;
        double value = 0;
        for (const double onset : {0.0, 38.0}) {
            const double t = ms - onset;
            if (t < 0 || t >= 28)
                continue;
            noise = noise * 1664525U + 1013904223U;
            const double grain = static_cast<int>(noise >> 16) / 32767.5 - 1.0;
            const double envelope = std::min(t / 1.5, 1.0) * std::exp(-t / 5.0) * (1.0 - t / 28.0);
            value += 6000.0 * envelope * (0.65 * grain + 0.35 * std::sin(t * 6.9115));
        }
        pcm[frame * 2] = pcm[frame * 2 + 1] = static_cast<int16_t>(value);
    }
    return pcm;
}
} // namespace rc
