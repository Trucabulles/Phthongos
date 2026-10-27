#pragma once
#include "../analysis/Dynamics.h"
#include <juce_core/juce_core.h>

namespace phthongos::audio {
// Bounded random access to the immutable PCM cache; no device, model or GUI.
class Pcm16 {
public:
    explicit Pcm16(const juce::File&);
    unsigned rate() const { return rate_; }
    unsigned channels() const { return channels_; }
    std::uint64_t frames() const { return frames_; }
    std::vector<float> mono(std::uint64_t first, std::size_t count);
    analysis::StereoPcm stereo();
private:
    juce::FileInputStream input_;
    unsigned rate_ = 0, channels_ = 0;
    std::uint64_t offset_ = 0, frames_ = 0;
    std::vector<std::uint8_t> read(std::uint64_t, std::size_t);
};
}
