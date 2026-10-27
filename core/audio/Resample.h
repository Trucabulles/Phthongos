#pragma once
#include "Pcm16.h"
namespace phthongos::audio {
analysis::StereoPcm readStereo(const juce::File&,unsigned rate,const juce::File& speex);
}
