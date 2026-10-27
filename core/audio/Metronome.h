#pragma once
#include "../midi/RhythmicGrid.h"
#include <stdexcept>
namespace phthongos::audio {
// Sample-domain click scheduler. No GUI timers or external sample bank.
class Metronome {
public:
 struct Tick{std::uint64_t frame;bool accent;};
 bool enabled=false;float gain=.25f;unsigned rate=48000;rhythm::Grid grid;
 std::vector<Tick> ticks;
 void configure(unsigned sampleRate,bool on,float db,const rhythm::Grid& map){
  if(sampleRate<8000||sampleRate>384000||!std::isfinite(db)||db< -60||db>0||map.tempos.empty()||map.meters.empty())throw std::invalid_argument("Invalid metronome configuration");
  double last=-1;for(auto t:map.tempos){if(!std::isfinite(t.beat)||!std::isfinite(t.seconds)||!std::isfinite(t.quarter)||t.quarter<.1||t.quarter>6||t.seconds<last)throw std::invalid_argument("Invalid metronome tempo map");last=t.seconds;}
  if(!std::isfinite(map.origin)||std::abs(map.origin)>1200)throw std::invalid_argument("Invalid metronome origin");
  last=-1;for(auto m:map.meters){if(!std::isfinite(m.beat)||m.beat<last||m.numerator<1||m.numerator>64||m.denominator<1||m.denominator>64||(m.denominator&(m.denominator-1)))throw std::invalid_argument("Invalid metronome meter");last=m.beat;}
  rate=sampleRate;enabled=on;gain=db<=-60?0:std::pow(10.f,db/20.f);grid=map;ticks.clear();
  for(std::size_t j=0;j<grid.meters.size();++j){auto m=grid.meters[j];double end=j+1<grid.meters.size()?grid.meters[j+1].beat:grid.beatAt(1200)+1;double step=4./m.denominator;
   for(std::int64_t n=0;n<200000;++n){double beat=m.beat+n*step;if(beat>=end-1e-8)break;double seconds=grid.secondsAt(beat);if(seconds>1200)break;if(seconds>=0)ticks.push_back({std::uint64_t(std::llround(seconds*rate)),n%m.numerator==0});}
  }
  std::sort(ticks.begin(),ticks.end(),[](auto a,auto b){return a.frame<b.frame;});reset();
 }
 void reset(){cursor=0;lastFrame=~std::uint64_t(0);age=rate;}
 float pulse(std::uint64_t elapsed,bool stressed)const{
  double t=double(elapsed)/rate;if(t>=.05)return 0;
  // A fuller, lower-pitched click stays identifiable over instrument playback.
  // Smooth attack/release and a bounded waveform avoid a hard sample edge.
  double envelope=std::min(t/.0015,1.)*std::exp(-t*70)*(1-t/.05);
  double phase=2*3.141592653589793*(stressed?1100:800)*t;
  double tone=.78*std::sin(phase)+.22*std::sin(2*phase);
  return float(tone*envelope*(stressed?.95:.8))*gain;
 }
 float sample(std::uint64_t frame){
  if(!enabled)return 0;
  if(lastFrame==~std::uint64_t(0)||frame!=lastFrame+1){cursor=std::lower_bound(ticks.begin(),ticks.end(),frame,[](auto tick,auto f){return tick.frame<f;})-ticks.begin();age=rate;}
  lastFrame=frame;
  while(cursor<ticks.size()&&ticks[cursor].frame<=frame){accent=ticks[cursor].accent;age=frame-ticks[cursor].frame;++cursor;}
  return pulse(age++,accent);
 }
private:std::size_t cursor=0;std::uint64_t lastFrame=~std::uint64_t(0),age=48000;bool accent=false;
};
}
