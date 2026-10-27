#pragma once
#include "../document/Document.h"
#include <juce_core/juce_core.h>
namespace phthongos::services {
struct LegacySupport { std::size_t frames=0;std::vector<float> notes; };
juce::var cleanupPreview(const std::vector<Note>&,std::uint64_t revision,float threshold,
                          const juce::var& scope,const juce::var& settings,const std::optional<LegacySupport>&);
}
