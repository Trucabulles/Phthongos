#pragma once
#include "Pht.h"
#include "DocumentJson.h"
#include "../midi/Midi.h"
namespace phthongos::project {
struct AudioInfo { double duration=0;std::uint32_t sampleRate=0;std::uint16_t channels=0;std::size_t frames=0; };
enum class SourceKind { none, midi, muscriptor, legacy, basicPitch };
struct Prepared {
    Container container;
    SourceSnapshot source;
    Editor editor;
    AudioInfo audio;
    SourceKind kind=SourceKind::none;
    std::optional<midi::Source> midi;
    double duration=0;
};
// Validates and constructs a complete replacement off to the side. It has no
// live-session pointer and performs no cache writes or audio-device calls.
Prepared prepare(Container,const std::vector<std::string>& gmNames,const std::string& runtimeManifestSha256);
std::vector<Note> muscriptorNotes(const juce::var&,const std::vector<std::string>&);
AudioInfo pcmInfo(const std::vector<std::uint8_t>&);
}
