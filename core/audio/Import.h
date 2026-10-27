#pragma once
#include "Pcm16.h"
namespace phthongos::audio {
// Shared ownership keeps staged PCM alive for concurrent immutable calculations.
// The deleter removes only our named PCM and its now-empty temporary directory.
std::shared_ptr<juce::File> ownCachedPcm(const juce::File&);
struct ImportedAudio {juce::File original,pcm;juce::String format;juce::var notice;unsigned rate=0,channels=0;double duration=0;std::shared_ptr<juce::File> cacheOwner;};
ImportedAudio importAudio(const juce::File&,const juce::File& cache);
juce::var audioView(const ImportedAudio&,std::uint64_t);
juce::var waveform(const juce::File&,double start,double end,std::size_t bins);
void publishNew(const juce::File& temporary,const juce::File& destination);
}
