#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <deque>
#include <mutex>
#include <atomic>
#include <stdexcept>
#include "Localisation.h"

// Hardware callbacks only enqueue data; all UI and editor work runs on the message thread.
class MidiInputController : private juce::MidiInputCallback, private juce::AsyncUpdater, private juce::Timer {
  std::unique_ptr<juce::MidiInput> input;
  std::atomic<juce::MidiInput*> accepted{nullptr};
  std::mutex mutex;std::deque<juce::MidiMessage> queue;bool overflow=false;
  juce::String selected;juce::File preferences;
  void handleIncomingMidiMessage(juce::MidiInput* source,const juce::MidiMessage& m) override {
    if(source!=accepted.load())return;
    if(!m.isNoteOnOrOff()&&!m.isController())return;
    {std::lock_guard<std::mutex> lock(mutex);if(source!=accepted.load())return;if(queue.size()<2048)queue.push_back(m.withTimeStamp(std::isfinite(m.getTimeStamp())&&m.getTimeStamp()>0?m.getTimeStamp():juce::Time::getMillisecondCounterHiRes()/1000.));else overflow=true;}triggerAsyncUpdate();
  }
  void handleAsyncUpdate() override {
    std::deque<juce::MidiMessage> messages;bool lost=false;{std::lock_guard<std::mutex> lock(mutex);messages.swap(queue);lost=overflow;overflow=false;}
    if(lost){if(onReset)onReset();return;}
    for(const auto& m:messages)if(onMessage)onMessage(m);
  }
  void close(){accepted=nullptr;if(input)input->stop();input.reset();drain();if(onReset)onReset();}
  void reconnect(){if(selected.isEmpty())return;for(const auto& d:juce::MidiInput::getAvailableDevices())if(d.identifier==selected){input=juce::MidiInput::openDevice(selected,this);if(input){accepted=input.get();input->start();if(onConnection)onConnection(true);}return;}}
  void timerCallback() override {
    bool present=false;for(const auto& d:juce::MidiInput::getAvailableDevices())present|=d.identifier==selected;
    if(input&&!present){close();if(onConnection)onConnection(false);}else if(!input&&present)reconnect();
  }
public:
  std::function<void(const juce::MidiMessage&)> onMessage;std::function<void()> onReset;std::function<void(bool)> onConnection;
  explicit MidiInputController(const juce::File& file):preferences(file){selected=juce::JSON::parse(file)["input"].toString();}
  ~MidiInputController() override {stopTimer();onMessage={};onReset={};onConnection={};close();}
  void start(){reconnect();startTimer(1000);}
  void drain(){cancelPendingUpdate();handleAsyncUpdate();}
  juce::String identifier()const{return selected;}
  bool connected()const{return input!=nullptr;}
  void choose(const juce::String& id){
    if(id==selected)return;
    auto* object=new juce::DynamicObject;object->setProperty("input",id);juce::var value(object);
    if(preferences.getParentDirectory().createDirectory().failed())throw std::runtime_error(tr(juce::String::fromUTF8("Enregistrement des préférences MIDI impossible.")).toStdString());
    juce::TemporaryFile temp(preferences);if(!temp.getFile().replaceWithText(juce::JSON::toString(value))||!temp.overwriteTargetFileWithTemporary())throw std::runtime_error(tr(juce::String::fromUTF8("Enregistrement des préférences MIDI impossible.")).toStdString());
    close();selected=id;reconnect();if(onConnection)onConnection(connected());
  }
};
