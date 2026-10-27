#pragma once
#include "../document/Document.h"
#include <juce_core/juce_core.h>
#include <atomic>
namespace phthongos::basicPitch {
inline constexpr const char* modelHash="2c3c1d144bfa61ad236e92e169c13535c880469a12a047d4e73451f2c059a0ec";
inline constexpr unsigned rate=22050,window=43844,hop=36164,bins=88,keptFrames=142;
struct Options {
  double noteSensitivity=.7,splitSensitivity=.5,minNoteMs=125;
  int minPitch=21,maxPitch=108,energyTolerance=11;
  bool inferOnsets=true,melodiaTrick=true;
  static Options read(const juce::var&);
  juce::var json() const;
};
struct Cache {
  std::size_t samples=0;
  std::vector<float> notes,onsets;
  std::size_t frames()const{return notes.size()/bins;}
  void validate()const;
};
juce::var encodeCache(const Cache&);
Cache decodeCache(const juce::var&);
void validateVoiceCaches(const juce::var&);
double frameTime(std::size_t);
juce::var decode(const Cache&,const Options&,const std::atomic_bool* cancelled=nullptr);
std::vector<Note> restore(const juce::var&,const std::vector<std::string>&);
std::vector<std::uint8_t> pack(const std::vector<float>&);
std::vector<float> unpack(const std::vector<std::uint8_t>&);
}
