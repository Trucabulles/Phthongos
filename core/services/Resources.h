#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <mutex>
namespace phthongos::services {
class Banks {
public:
 Banks(juce::File piano,juce::File data,juce::File sharedSettings={},juce::File documentsBank={});
 juce::File selected()const;juce::var status()const;
 juce::var customBanks()const;juce::var addCustom(const juce::File&);void removeCustom(const juce::String&);
 void select(const juce::File&);void download();void cancel(){cancelled=true;}
 static void verify(const juce::File&,const std::function<void()>& check=[]{});
private:
 juce::File piano,data,sharedSettings,storage;mutable std::mutex mutex;std::mutex operation;juce::var settings,progress;
 std::atomic_bool busy{false},cancelled{false};
 void check()const;void stage(const char*,std::uint64_t=0,std::uint64_t=0);void save(const juce::File&);
};
void saveJson(const juce::File&,const juce::var&);
juce::String fileHash(const juce::File&);
}
