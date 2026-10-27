// C++ port of MIDI BPM Detection, copyright (c) 2023 Vincent Alsteen.
// MIT license: see LICENSE.txt. Local changes and provenance: SOURCE.md.
#include "Detector.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace phthongos::midi::bpm {
namespace {
constexpr double billion=1000000000.;
std::int64_t beat(float bpm){return std::llround(double(60.f/bpm)*billion);}
std::int64_t sampleDuration(unsigned rate,std::size_t i){return std::int64_t(double(i)/rate*billion);}
std::int64_t sampleIndex(unsigned rate,std::int64_t ns){return std::llround(double(ns)*rate/billion);}
unsigned folds(std::int64_t numerator,std::int64_t denominator){
  auto ratio=std::uint64_t(numerator/denominator+(numerator%denominator!=0));unsigned n=0;for(auto r=ratio-1;r;r>>=1)++n;return n;
}
float foldedScore(unsigned n){return std::ldexp(1.f,-int(n>0?n-1:0));}
float score(float value,float weight){float x=std::log10(value*9.f+1.f)*weight;return std::isfinite(x)&&x>0?x:0;}
}
double Histogram::bpmAt(std::size_t i)const{return 60000000000./double(shortest+sampleDuration(sampleRate,i));}
Histogram detect(const std::vector<Onset>& notes,const Config& c){
  if(!std::isfinite(c.center)||c.center<1||c.center>600||c.range<1||c.range>600||c.sampleRate<1||c.sampleRate>10000||
     !std::isfinite(c.deviationMs)||c.deviationMs<=0||c.deviationMs>1000||!std::isfinite(c.resolutionMs)||c.resolutionMs<=0||
     !std::isfinite(c.cutoffMs)||c.cutoffMs<0||c.cutoffMs>2000||!std::isfinite(c.factor)||c.factor<0||c.factor>100)
    throw std::invalid_argument("Invalid MIDI tempo detector configuration");
  for(float w:{c.normal,c.age,c.toVelocity,c.fromVelocity,c.inRange,c.multiplier,c.subdivision,c.octave,c.pitch,c.highTempo})
    if(!std::isfinite(w)||w<0||w>20)throw std::invalid_argument("Invalid MIDI tempo detector weight");
  const float lowBpm=std::max(1.f,c.center-float(c.range/2));
  const auto shortest=beat(lowBpm+float(c.range)),longest=beat(lowBpm);
  Histogram out;out.shortest=shortest;out.sampleRate=c.sampleRate;
  out.bins.resize(std::size_t(sampleIndex(c.sampleRate,longest-shortest)),0);
  if(notes.size()<2)return out;
  const std::size_t first=notes.size()>10000?notes.size()-10000:0;
  for(std::size_t i=first;i<notes.size();++i){if(notes[i].nanos<0||notes[i].pitch<0||notes[i].pitch>127||notes[i].velocity<0||notes[i].velocity>127||
      (i>first&&notes[i].nanos<notes[i-1].nanos))throw std::invalid_argument("Invalid or unordered MIDI onset");}
  const auto newest=notes.back().nanos,span=newest-notes[first].nanos;if(span<=0)return out;
  const auto cutoff=std::int64_t(c.cutoffMs*1000000.f),step=sampleDuration(c.sampleRate,1);
  auto index=[&](std::int64_t ns){auto i=sampleIndex(c.sampleRate,ns)-sampleIndex(c.sampleRate,shortest);return i>=0&&i<std::int64_t(out.bins.size())?i:-1;};
  struct Kernel{std::int64_t offset;float amplitude;};std::vector<Kernel> kernel;
  for(auto t=-cutoff;t<=cutoff;t+=step){
    auto i=std::size_t(std::round((float(t)+c.cutoffMs*1000000.f)/(c.resolutionMs*1000000.f)));
    auto ns=std::int64_t(float(i)*c.resolutionMs*1000000.f-c.cutoffMs*1000000.f);
    double ms=double(ns)/1000000.;float normal=float(std::exp(-.5*std::pow(ms/c.deviationMs,2))/(c.deviationMs*std::sqrt(2*3.14159265358979323846)))*c.factor;
    float value=c.normal>0?std::log10(normal*9.f*2.f+1.f)*c.normal:0;
    kernel.push_back({t,std::pow(10.f,value)});
  }
  const float nan=std::numeric_limits<float>::quiet_NaN();
  for(std::size_t from=first;from<notes.size();++from)for(std::size_t to=from+1;to<notes.size();++to){
    auto interval=notes[to].nanos-notes[from].nanos;
    float direct=1,multiple=nan,subdivision=nan;
    if(interval>longest){direct=nan;auto n=folds(interval,longest);interval/=std::int64_t(1ULL<<n);multiple=foldedScore(n);}
    else if(interval<shortest){if(interval<=1000000)continue;direct=nan;auto required=folds(shortest,interval),applied=std::min(required,9u);interval*=std::int64_t(1ULL<<applied);if(required<=9)subdivision=foldedScore(applied);}
    if(index(interval)<0)continue;
    const auto& a=notes[from];const auto& b=notes[to];
    int pitchDistance=std::abs(b.pitch%12-a.pitch%12);pitchDistance=std::min(pitchDistance,12-pitchDistance);
    float age=float((span-(newest-b.nanos))/1000)/float(span/1000);
    float high=1.f-(float(interval/1000)-float(shortest/1000))/(float(longest/1000)-float(shortest/1000));
    float intensity=score(float(b.velocity)/127.f,c.toVelocity)+score(float(a.velocity)/127.f,c.fromVelocity)+score(age,c.age)+
      score(1.f-float(std::abs(b.pitch/12-a.pitch/12))/11.f,c.octave)+score(1.f-float(pitchDistance)/12.f,c.pitch)+
      score(multiple,c.multiplier)+score(subdivision,c.subdivision)+score(direct,c.inRange)+score(high,c.highTempo);
    // Factor the power outside the kernel loop; only floating-point roundoff differs.
    float amplitude=std::pow(10.f,intensity);
    for(const auto& k:kernel){auto i=index(interval+k.offset);if(i>=0)out.bins[std::size_t(i)]+=amplitude*k.amplitude;}
  }
  float maximum=0;std::size_t best=0;for(std::size_t i=0;i<out.bins.size();++i)if(out.bins[i]>=maximum){maximum=out.bins[i];best=i;}
  if(maximum>0)out.bpm=60000000000.f/float(shortest+sampleDuration(c.sampleRate,best));
  return out;
}
}
