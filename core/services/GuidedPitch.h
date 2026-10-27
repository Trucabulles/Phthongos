#pragma once
#include "../analysis/Dynamics.h"
#include "../document/Mask.h"
#include "../analysis/MidiReaders.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <functional>
#include <optional>
namespace phthongos::services {
struct GuidedOptions { std::optional<analysis::Scale> scale; };
// Estimates for a dominant melodic source, never a MIDI mutation.
juce::var guidedPitch(const analysis::StereoPcm&,unsigned rate,double start,double end,int low,int high,
                     const SpectralMask&,bool useMask,const std::atomic_bool&,std::function<void(double)> progress={},const GuidedOptions& options={});
}
