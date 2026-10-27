#pragma once
#include <juce_core/juce_core.h>
#include <memory>
namespace phthongos::services {
// Full native service contract used by the unchanged JUCE application.
class Desktop {
public:
 Desktop(juce::File runtime,juce::File cache);
 ~Desktop();
 juce::var request(const juce::String&,const juce::var&);
private:struct Impl;std::unique_ptr<Impl> impl;
};
}
