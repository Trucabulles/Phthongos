#include "Midi.h"
#include "Tempo.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace phthongos::midi {
namespace {
[[noreturn]]void fail(const char* s){throw std::invalid_argument(s);}
std::uint64_t tick(float seconds,double rate){double v=static_cast<double>(seconds)*rate;double low=std::floor(v),f=v-low;double rounded=f>.5||(f==.5&&std::fmod(low,2.)!=0.)?low+1:low;return static_cast<std::uint64_t>(std::clamp(rounded,0.,4294967295.));}
struct Ordered {Event event;int priority;NoteId order;};
}
std::vector<std::uint8_t> exportParts(const std::vector<Note>& input,const std::vector<std::string>& names,double fixedBpm){
    if(fixedBpm!=0)validateTempo(fixedBpm);
    const auto micros=std::uint32_t(std::llround(60000000./(fixedBpm?fixedBpm:120)));
    const double tickRate=9600.*1e6/micros;
    std::vector<const Note*> audible;bool legacy=true;for(auto& n:input)if(!n.muted){audible.push_back(&n);if(n.part)legacy=false;}
    std::vector<std::vector<Event>> tracks={{{0,255,81,{std::uint8_t(micros>>16),std::uint8_t(micros>>8),std::uint8_t(micros)}},{0,255,47,{}}}};
    std::map<PartId,std::vector<const Note*>> groups;if(legacy)groups[0]=audible;else for(auto* n:audible)groups[n->key()].push_back(n);
    std::size_t melodic=0,drums=0;
    for(auto& [key,notes]:groups){std::vector<Event> track;int channel=0,port=0;
        std::string name="Phthongos research reconstruction";
        if(!legacy){auto* first=notes.front();int program=first->part?first->part->program:0;if(program>128)fail("Programme MIDI invalide.");
            if(program==128){port=static_cast<int>(drums++);channel=9;}else{auto slot=melodic%15;port=static_cast<int>(melodic/15);++melodic;channel=static_cast<int>(slot>=9?slot+1:slot);}if(port>127)fail("Trop de parties MIDI.");
            name=first->part?first->part->label:program<static_cast<int>(names.size())?names[program]:"Partie";
            track.push_back({0,255,3,{name.begin(),name.end()}});track.push_back({0,255,33,{static_cast<std::uint8_t>(port)}});
            if(program<128)track.push_back({0,static_cast<std::uint8_t>(192|channel),0,{static_cast<std::uint8_t>(program)}});
        }else track.push_back({0,255,3,{name.begin(),name.end()}});
        std::vector<Ordered> events;for(std::size_t i=0;i<notes.size();++i){auto& n=*notes[i];if(n.pitch<0||n.pitch>127||!std::isfinite(n.start)||!std::isfinite(n.end)||n.start<0||n.end<n.start||(!legacy&&(n.velocity==0||n.end<=n.start)))fail(legacy?"invalid MIDI note":"Note MIDI invalide.");
            auto a=tick(n.start,tickRate),b=tick(n.end,tickRate);if(!legacy)b=std::max(a+1,b);auto order=legacy?static_cast<NoteId>(i):n.id;
            events.push_back({{a,static_cast<std::uint8_t>(144|channel),0,{static_cast<std::uint8_t>(n.pitch),static_cast<std::uint8_t>(n.velocity)}},1,order});
            events.push_back({{b,static_cast<std::uint8_t>(128|channel),0,{static_cast<std::uint8_t>(n.pitch),0}},0,order});}
        std::stable_sort(events.begin(),events.end(),[](auto& a,auto& b){return std::make_tuple(a.event.tick,a.priority,a.order)<std::make_tuple(b.event.tick,b.priority,b.order);});
        std::uint64_t previous=0;for(auto& e:events){if(e.event.tick-previous>0xfffffff)fail(legacy?"MIDI delta exceeds SMF limit":"Intervalle MIDI trop long.");previous=e.event.tick;track.push_back(e.event);}track.push_back({events.empty()?0:events.back().event.tick,255,47,{}});tracks.push_back(std::move(track));
    }return encode(1,9600,tracks);
}
}
