#pragma once
#include "../analysis/Dynamics.h"
#include "Metronome.h"
#include <juce_core/juce_core.h>
#include <memory>
#include <map>
namespace phthongos::audio {
class Sampler {
public:
 Sampler(const juce::File& piano,const juce::File& bank,unsigned rate);
 ~Sampler();Sampler(const Sampler&)=delete;Sampler& operator=(const Sampler&)=delete;
 static juce::var inspectSoundfont(const juce::File&,const juce::File&);
 unsigned program(const Part&);
 void start(unsigned program,int pitch,int velocity,std::uint64_t occurrence,unsigned channel=0);
 void level(unsigned channel,float db);
 void stop(std::uint64_t);void reset();void effects(unsigned);void render(float*,float*,int);
private:struct Impl;std::unique_ptr<Impl> impl;
};
class Engine {
public:
 static constexpr unsigned block=8;
 Engine(unsigned,std::unique_ptr<Sampler>,std::unique_ptr<Sampler>);
 unsigned rate,mode=0,reverb=0;std::uint64_t id=0,version=0,position=0,clipped=0;bool playing=false,recording=false;
 std::shared_ptr<const analysis::StereoPcm> pcm;
 std::array<std::uint16_t,128> active{};
 float audioGain=1,midiGain=1;
 Metronome click;std::uint64_t countRemaining=0,countTotal=0;double countPeriod=24000;int countNumerator=4;
 void preCount(unsigned bars);
 void keyDown(unsigned,int,int,float);void keyUp();
 void midiNote(unsigned,int,int,unsigned,float);void midiSustain(unsigned,bool);void midiPanic();
 void setReverb(unsigned);void silence();void seek(std::uint64_t);void pause();void play();
 void levels(const std::map<int,float>&);
 unsigned auditionProgram(const std::optional<Part>& part,unsigned fallback){return part?auditionSynth->program(*part):fallback;}
 void notes(const std::vector<Note>&);void audition(unsigned,int,int);void cancelAudition();
 std::array<bool,128> keys()const;std::array<float,2> next();
private:
 struct LiveNote{std::uint64_t occurrence;int pitch;unsigned channel;bool down;};
 std::map<unsigned,LiveNote> liveNotes;std::array<bool,16> livePedal{};std::uint64_t liveOccurrence=0,liveTail=0;
 std::map<int,float> voiceLevels;std::map<int,unsigned> voiceChannels;
 void applyLevels();void beginAudition(unsigned,int,int,float,bool);
 struct Span{unsigned program,channel;std::uint64_t start,end;int pitch,velocity;};
 struct Event{std::uint64_t frame;std::size_t index;bool on;};
 std::vector<Span> spans;std::vector<Event> events;std::size_t cursor=0,offset=block,auditionOffset=block;
 std::unique_ptr<Sampler> synth,auditionSynth;
 std::array<std::array<float,2>,block> cache{},auditionCache{};
 std::uint64_t auditionLeft=0,auditionReleaseFrames=0,endLeft=0,endTotal=0,endQuiet=0;int auditionPitch=0;bool auditionHeld=false;
 std::uint64_t auditionOccurrence=0;unsigned transitionLeft=0,transitionFrames=1;std::array<float,2> auditionLast{},transitionFrom{};
 std::array<float,2> nextPerformance();
 std::array<float,2> nextAudition();std::array<float,2> nextEnd();
};
}
