#pragma once
#include <juce_core/juce_core.h>
namespace phthongos::diagnostics {
// No project paths, note data, audio samples or user text are recorded.
void start(const juce::File& directory);
void stop();
void event(const juce::String& kind,const juce::String& detail = {});
void guiHeartbeat(const juce::String& state);
void suspendGuiHeartbeat();
class Span {
public:
  explicit Span(const juce::String& operation,bool verbose=true);
  ~Span();
  void phase(const juce::String&);
  Span(const Span&)=delete;Span& operator=(const Span&)=delete;
private: std::uint64_t id=0;
};
}
