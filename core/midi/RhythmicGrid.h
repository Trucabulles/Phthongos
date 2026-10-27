#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace phthongos::rhythm {
struct Tempo { double beat=0, seconds=0, quarter=.5; };
struct Meter { double beat=0; int numerator=4, denominator=4; };
struct Line { double seconds=0; int level=0; std::int64_t bar=0; int pulse=0; };
// A read-only coordinate transform. It has no access to MIDI notes or editing.
struct Grid {
    int mode=0, subdivision=1; // musical / seconds / none; 1/4,1/8,1/16,1/8T,1/16T
    double origin=0;
    std::vector<Tempo> tempos{{0,0,.5}};
    std::vector<Meter> meters{{0,4,4}};
    double beatAt(double seconds) const {
        auto it=std::upper_bound(tempos.begin(),tempos.end(),seconds-origin,[](double s,const Tempo& t){return s<t.seconds;});
        auto& t=*(it==tempos.begin()?it:--it);
        return t.beat+(seconds-origin-t.seconds)/t.quarter;
    }
    double secondsAt(double beat) const {
        auto it=std::upper_bound(tempos.begin(),tempos.end(),beat,[](double b,const Tempo& t){return b<t.beat;});
        auto& t=*(it==tempos.begin()?it:--it);
        return origin+t.seconds+(beat-t.beat)*t.quarter;
    }
    static std::vector<Line> secondsLines(double start,double span,double width) {
        if(span<=0||width<=0)return {};
        const double pixels=width/span;
        double major=600;
        for(auto step:{.001,.002,.005,.01,.02,.05,.1,.25,.5,1.,2.,5.,10.,20.,30.,60.,120.,300.,600.})
            if(step*pixels>=80){major=step;break;}
        double step=major;
        if(major/4*pixels>=12)step=major/4;else if(major/2*pixels>=12)step=major/2;
        std::vector<Line> out;
        auto first=std::int64_t(std::ceil((start-1e-9)/step)),last=std::int64_t(std::floor((start+span+1e-9)/step));
        for(auto n=first;n<=last&&out.size()<4096;++n){double s=n*step;bool main=std::abs(s/major-std::round(s/major))<1e-7;out.push_back({s,main?0:2,0,0});}
        return out;
    }
    std::vector<Line> lines(double start,double span,double width) const {
        if(mode==2)return {};
        if(mode==1)return secondsLines(start,span,width);
        if(span<=0||width<=0||tempos.empty()||meters.empty())return {};
        double pixels=width/span,shortest=tempos.front().quarter;
        for(auto& t:tempos)shortest=std::min(shortest,t.quarter);
        const double a=std::max(0.,beatAt(start)),b=beatAt(start+span);
        if(b<0)return {};
        const double intervals[]={1,.5,.25,1./3,1./6};
        double interval=intervals[std::clamp(subdivision,0,4)];
        if(interval*shortest*pixels<10)interval=subdivision>=3?1./3:.5;
        if(interval*shortest*pixels<10)interval=1;
        std::map<std::int64_t,Line> unique;
        std::int64_t barBase=1;
        for(std::size_t m=0;m<meters.size();++m){
            auto& meter=meters[m];double length=meter.numerator*4./meter.denominator;
            if(length<=0)continue;
            double end=m+1<meters.size()?meters[m+1].beat:INFINITY;
            auto bars=std::isfinite(end)?std::int64_t(std::ceil((end-meter.beat)/length-1e-9)):0;
            if(end>a&&meter.beat<=b){
                std::int64_t stride=1;
                while(length*stride*shortest*pixels<80&&stride<(1<<24))stride*=2;
                auto first=std::max<std::int64_t>(0,std::int64_t(std::floor((a-meter.beat)/length)));
                auto last=std::int64_t(std::floor((std::min(b,end)-meter.beat)/length));
                first=first/stride*stride;
                auto add=[&](double beat,int level,std::int64_t bar,int pulse){
                    if(beat>=end-1e-8)return;
                    double s=secondsAt(beat);if(s<start-1e-8||s>start+span+1e-8)return;
                    auto key=std::int64_t(std::llround(s*1e8));auto it=unique.find(key);
                    if(it==unique.end()||level<it->second.level)unique[key]={s,level,bar,pulse};
                };
                for(auto bar=first;bar<=last&&unique.size()<4096;bar+=stride){
                    double base=meter.beat+bar*length;add(base,0,barBase+bar,1);
                    if(stride>1)continue;
                    // Work only in the visible fraction, even with a long meter.
                    if(shortest*pixels>=14){
                        auto begin=std::max(0,int(std::ceil((a-base)-1e-8)));
                        for(int q=begin;q<length&&base+q<=b;++q){double unit=q*meter.denominator/4.;add(base+q,1,barBase+bar,std::abs(unit-std::round(unit))<1e-7?int(std::round(unit))+1:0);}
                    }
                    if(interval<1){
                        auto begin=std::max(0,int(std::ceil((a-base)/interval-1e-8)));
                        for(int n=begin;n*interval<length-1e-8&&base+n*interval<=b;++n){
                            double rel=n*interval;
                            int level=std::abs(rel-std::round(rel))<1e-7?1:
                                std::abs(rel*(subdivision>=3?3:2)-std::round(rel*(subdivision>=3?3:2)))<1e-7?2:3;
                            double unit=rel*meter.denominator/4.;
                            add(base+rel,level,barBase+bar,level==1&&std::abs(unit-std::round(unit))<1e-7?int(std::round(unit))+1:0);
                        }
                    }
                }
            }
            barBase+=bars;
        }
        std::vector<Line> out;for(auto& entry:unique)out.push_back(entry.second);return out;
    }
};
}
