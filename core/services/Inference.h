#pragma once
#include "Resources.h"
#include <functional>
#include <memory>
namespace phthongos::services {
class Inference {
public:
 Inference(juce::File runtime,juce::File data);
 ~Inference();
 juce::var status();void download(const juce::String&);void declareModel(const juce::String&,const juce::File&);void cancelDownload(){downloadCancelled=true;}void configure(const juce::var&);
 juce::var transcribe(const juce::File&,std::uint64_t,const std::function<void(juce::var)>&);
 void cancel(){cancelled=true;}
 void resetCancellation(){cancelled=false;}
 std::atomic_bool& cancellation(){return cancelled;}
private:
 juce::File runtime,data;std::mutex mutex;juce::var settings;juce::DynamicLibrary dll;std::atomic_bool cancelled{false};
 std::atomic_bool downloading{false},downloadCancelled{false};juce::var downloadState;
 juce::File modelPath(const juce::var&,const juce::String&)const;
 void load();juce::var devices();int device(const juce::var&,const juce::String&);
};
}
