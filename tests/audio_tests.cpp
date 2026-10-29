#include "rc/shutter_sound.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    if (rc::kShutterSampleRate != 16000 || rc::kShutterFrames < 12800 || rc::kShutterFrames > 16000) {
        std::cerr << "Unexpected shutter audio format or duration\n";
        return 1;
    }
    int peak = 0;
    for (size_t i = 0; i < rc::kShutterFrames; ++i) {
        if ((i < 320 || i >= rc::kShutterFrames - 320) && rc::kShutterPcm[i] != 0) {
            std::cerr << "Missing silence at the playback boundaries\n";
            return 1;
        }
        const int value = std::abs(static_cast<int>(rc::kShutterPcm[i]));
        if (value > peak)
            peak = value;
    }
    if (peak < 1000) {
        std::cerr << "Shutter audio is silent\n";
        return 1;
    }
    return 0;
}
