// C++ port of MIDI BPM Detection, copyright (c) 2023 Vincent Alsteen.
// MIT license: see LICENSE.txt. Local changes and provenance: SOURCE.md.
#pragma once
#include <cstdint>
#include <vector>
namespace phthongos::midi::bpm {
struct Onset {std::int64_t nanos=0;int pitch=60,velocity=90;};
struct Config {
  float center=90;unsigned range=40,sampleRate=450;
  double deviationMs=24;
  float resolutionMs=.6f,cutoffMs=100,factor=40;
  float normal=1,age=.7f,toVelocity=.7f,fromVelocity=.7f;
  float inRange=.75f,multiplier=.66f,subdivision=.7f,octave=.6f,pitch=.6f,highTempo=.2f;
};
struct Histogram {std::vector<float> bins;float bpm=0;std::int64_t shortest=0;unsigned sampleRate=450;double bpmAt(std::size_t index)const;};
// Input is chronological. At most the last 10000 note-ons are considered,
// as in the original. This batch API does not retain or prune input events.
Histogram detect(const std::vector<Onset>&,const Config& = {});
}
