#include "rc/audio_levels.hpp"
#include "rc/shutter_sound.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>

int main() {
    if (rc::kShutterVolumeLevels != std::array<unsigned, 4>{0, 60, 75, 90}) {
        std::cerr << "Unexpected shutter volume mapping\n";
        return 1;
    }
    // Exact fingerprint of the original, untrimmed PCM.
    if (rc::kShutterSampleRate != 16000 || rc::kShutterFrames != 13501) {
        std::cerr << "Unexpected shutter audio format or duration\n";
        return 1;
    }
    uint32_t hash = 2166136261u;
    int peak = 0;
    for (size_t i = 0; i < rc::kShutterFrames; ++i) {
        const auto value = static_cast<uint16_t>(rc::kShutterPcm[i]);
        hash = (hash ^ (value & 255u)) * 16777619u;
        hash = (hash ^ (value >> 8)) * 16777619u;
        if ((i < 320 || i >= rc::kShutterFrames - 320) && value != 0) {
            std::cerr << "Missing original boundary silence\n";
            return 1;
        }
        const int amplitude = std::abs(static_cast<int>(rc::kShutterPcm[i]));
        if (amplitude > peak)
            peak = amplitude;
    }
    if (hash != 0xf0e9dcf9u || peak < 1000) {
        std::cerr << "Audio differs from original PCM\n";
        return 1;
    }
    return 0;
}
