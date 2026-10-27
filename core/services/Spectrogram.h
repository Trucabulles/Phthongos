#pragma once
#include <juce_core/juce_core.h>
namespace phthongos::services {
juce::var spectrogram(const juce::File&,double start,double end,std::size_t columns);
}
