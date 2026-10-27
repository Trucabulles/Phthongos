#pragma once
#include "Engine.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <functional>
namespace phthongos::audio {
class Player {
public:
 Player(juce::File piano,juce::File speex,std::function<juce::File()> bank);
 ~Player();
 void prepare(std::uint64_t,const juce::File&,double midiDuration);
 void notes(std::uint64_t,std::uint64_t,const std::vector<Note>&,double timelineDuration=0);
 void control(std::uint64_t,const juce::String&,double);
 void key(std::uint64_t,int,unsigned,float,std::optional<Part> part={});
 void midi(std::uint64_t,const juce::String&,unsigned,int,int,unsigned,float,std::optional<Part> part={});
 void levels(std::uint64_t,const std::map<int,float>&);
 juce::var record(std::uint64_t,bool,double,unsigned preCount=0);
 void metronome(std::uint64_t,bool,float,const rhythm::Grid&);
 void mix(std::uint64_t,unsigned,float,float);void reverb(std::uint64_t,double);void audition(std::uint64_t,int,int,unsigned,std::optional<Part> part={});
 void initialize(std::uint64_t);void reload();
 juce::var snapshot();juce::var outputs();void select(const juce::var&);
private:struct Impl;std::unique_ptr<Impl> impl;
};
}
