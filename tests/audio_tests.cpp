#include "rc/shutter_sound.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    const auto pcm = rc::make_shutter_sound();
    bool first_click = false, second_click = false;
    for (size_t frame = 0; frame < rc::kShutterFrames; ++frame) {
        const int sample = pcm[frame * 2];
        if (sample != pcm[frame * 2 + 1] || std::abs(sample) > 6000) {
            std::cerr << "Stereo mismatch or unsafe PCM peak at frame " << frame << '\n';
            return 1;
        }
        const double ms = 1000.0 * frame / rc::kShutterSampleRate;
        if (ms < 28 && std::abs(sample) > 100)
            first_click = true;
        if (ms >= 38 && ms < 66 && std::abs(sample) > 100)
            second_click = true;
        if ((ms >= 28 && ms < 38) || ms >= 66) {
            if (sample != 0) {
                std::cerr << "Expected silence between clicks and before PA shutdown\n";
                return 1;
            }
        }
    }
    if (!first_click || !second_click || pcm.front() != 0 || pcm.back() != 0 ||
        rc::kShutterFrames * 1000 / rc::kShutterSampleRate != 100) {
        std::cerr << "Invalid shutter effect timing or envelope\n";
        return 1;
    }
    return 0;
}
