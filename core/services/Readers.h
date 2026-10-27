#pragma once
#include <juce_core/juce_core.h>
#include "../analysis/MidiReaders.h"
namespace phthongos::services {
// One immutable selected-notes response is sufficient. No backend, GUI, files,
// callbacks or mutable session is reachable from this service.
analysis::Scale readScale(const juce::var&);
juce::var readMidi(const juce::String& command,const juce::var& args,const juce::var& selected);
}
