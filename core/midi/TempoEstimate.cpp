#include "TempoEstimate.h"
#include "../../vendor/midi-bpm/Detector.h"
#include <algorithm>
#include <cmath>
#include <numeric>
namespace phthongos::midi {
TempoEstimate estimateTempo(const std::vector<double>& starts){std::vector<TempoOnset> notes;for(auto t:starts)notes.push_back({t,60,90});return estimateTempo(notes);}
TempoEstimate estimateTempo(const std::vector<TempoOnset>& input){
  TempoEstimate out;std::vector<TempoOnset> notes;
  for(auto n:input)if(std::isfinite(n.start)&&n.start>=0&&n.start<=1200&&n.pitch>=0&&n.pitch<=127&&n.velocity>0&&n.velocity<=127)notes.push_back(n);
  std::stable_sort(notes.begin(),notes.end(),[](auto a,auto b){return a.start<b.start;});
  double anchor=-1;
  for(auto n:notes)if(anchor<0||n.start-anchor>.035){++out.attacks;anchor=n.start;}
  if(!notes.empty())out.seconds=notes.back().start-notes.front().start;
  if(out.attacks<2){out.reason="few_attacks";return out;}
  if(out.seconds<.08){out.reason="short_span";return out;}
  // Original metadata weights and Gaussian parameters. A wider search range is
  // needed for free recording; half/double alternatives remain available in UI.
  bpm::Config config;config.center=125;config.range=150;
  // Cover a long/dense take with bounded local analyses instead of doing O(N^2)
  // work on every note in a 20-minute performance. No recorded event is edited.
  std::vector<std::pair<std::size_t,std::size_t>> windows;
  for(std::size_t first=0;first<notes.size();){std::size_t end=first+1;
    while(end<notes.size()&&end-first<256&&notes[end].start-notes[first].start<=8)++end;
    windows.emplace_back(first,end);first=end;
  }
  const std::size_t count=std::min<std::size_t>(12,windows.size());
  for(std::size_t w=0;w<count;++w){auto bounds=windows[count==1?0:w*(windows.size()-1)/(count-1)];
    std::vector<bpm::Onset> events;const double origin=notes[bounds.first].start;
    for(auto i=bounds.first;i<bounds.second;++i)events.push_back({std::llround((notes[i].start-origin)*1e9),notes[i].pitch,notes[i].velocity});
    auto hist=bpm::detect(events,config);if(hist.bpm<=0)continue;
    const float maximum=*std::max_element(hist.bins.begin(),hist.bins.end());
    if(out.histogram.empty()){out.histogram.resize(hist.bins.size());out.shortestSeconds=double(hist.shortest)/1e9;out.sampleRate=hist.sampleRate;}
    for(std::size_t i=0;i<hist.bins.size();++i)out.histogram[i]+=double(hist.bins[i])/maximum;
  }
  if(out.histogram.empty()){out.reason="few_intervals";return out;}
  auto& h=out.histogram;double maximum=*std::max_element(h.begin(),h.end());for(auto& x:h)x/=maximum;
  std::vector<std::size_t> peaks;
  for(std::size_t i=0;i<h.size();++i)if(h[i]>0&&(i==0||h[i]>=h[i-1])&&(i+1==h.size()||h[i]>h[i+1]))peaks.push_back(i);
  std::stable_sort(peaks.begin(),peaks.end(),[&](auto a,auto b){return h[a]==h[b]?a>b:h[a]>h[b];});
  for(auto i:peaks){double tempo=60./(out.shortestSeconds+double(std::int64_t(double(i)/out.sampleRate*1e9))/1e9);
    bool duplicate=false;for(auto candidate:out.candidates)duplicate|=std::abs(std::log2(candidate.bpm/tempo))<.045;
    if(!duplicate)out.candidates.push_back({std::round(tempo*10)/10,h[i]});if(out.candidates.size()==8)break;
  }
  if(out.candidates.empty())return out;
  out.bpm=out.candidates.front().bpm;out.reason="estimated";
  out.support=std::clamp(1-std::accumulate(h.begin(),h.end(),0.)/double(h.size()),0.,1.);
  return out;
}
}
