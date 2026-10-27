#pragma once
#include "../analysis/Acoustic.h"
#include <juce_core/juce_core.h>
namespace phthongos::services {
juce::var acousticEvidence(const analysis::AcousticEvidence&);
juce::var verifyContinuity(const juce::var& preview,const analysis::StereoPcm&,unsigned rate,const std::vector<Note>&);
}
