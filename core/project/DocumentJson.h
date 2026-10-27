#pragma once
#include "../document/Document.h"
#include "../analysis/Dynamics.h"
#include <juce_core/juce_core.h>
namespace phthongos::project {
MaskStroke readMaskStroke(const juce::var&);
juce::var writeMaskStroke(const MaskStroke&);
juce::var writeMasks(const std::map<PartId,SpectralMask>&);
juce::var writePart(const Part&);
Part readPart(const juce::var&);
Note readNote(const juce::var&);
juce::var writeNote(const Note&);
Action readAction(const juce::var&);
juce::var writeView(const View&);
struct Editor { Document document; juce::var dynamicsCache; };
Editor readEditor(const juce::var&, const SourceSnapshot&);
juce::var writeEditor(const Editor&);
analysis::DynamicsCache readDynamicsCache(const juce::var&);
juce::var writeDynamicsCache(const analysis::DynamicsCache&);
std::optional<CleanPreview> readCleanPreview(const juce::var&);
}
