#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>
namespace phthongos::project {
// PHT8 container: JSON metadata, original PCM WAV and three little-endian f32
// maps. Parsing the envelope never creates files or changes a live session.
struct Container {
    juce::var metadata;
    std::array<std::vector<std::uint8_t>,4> data;
    unsigned originVersion=8;
};
void validateEnvelope(const Container&);
Container decode(const std::vector<std::uint8_t>&);
std::vector<std::uint8_t> encode(const Container&);
void saveAtomic(const juce::File&,const Container&,bool createOnly=false);
Container read(const juce::File&);
}
