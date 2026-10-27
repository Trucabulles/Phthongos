#include "Midi.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>

namespace phthongos::midi {
namespace {
[[noreturn]] void fail(const char* s) { throw std::invalid_argument(s); }
struct Reader {
    const std::vector<std::uint8_t>& bytes; std::size_t at, end;
    std::uint8_t byte() { if(at>=end)fail("MIDI invalide : données tronquées.");return bytes[at++]; }
    std::uint32_t be(unsigned n) { std::uint32_t v=0;while(n--)v=(v<<8)|byte();return v; }
    std::uint32_t vlq() { std::uint32_t v=0;for(int i=0;i<4;++i){auto b=byte();v=(v<<7)|(b&127);if(!(b&128))return v;}fail("MIDI invalide : entier variable trop long."); }
    std::vector<std::uint8_t> take(std::size_t n) { if(n>end-at)fail("MIDI invalide : données tronquées.");auto begin=bytes.begin()+at;at+=n;return {begin,begin+n}; }
};
void be(std::vector<std::uint8_t>& b,std::uint32_t v,int n) { for(int i=n-1;i>=0;--i)b.push_back(static_cast<std::uint8_t>(v>>(i*8))); }
void vlq(std::vector<std::uint8_t>& b,std::uint64_t v) {
    if(v>0xfffffff)fail("Intervalle MIDI trop long.");
    std::uint8_t buf[4];int n=0;buf[n++]=v&127;
    while((v>>=7)!=0)buf[n++]=128|(v&127);while(n)b.push_back(buf[--n]);
}
std::size_t utf8Length(unsigned char c) { return c<128?1:c<224?2:c<240?3:4; }
std::string label(const std::vector<std::uint8_t>& b) {
    // Input track names use UTF-8; replace malformed sequences as Rust does.
    std::string out;std::size_t i=0,chars=0;
    while(i<b.size()&&chars++<160){
        auto c=b[i];auto n=utf8Length(c);bool valid=c<128||(c>=194&&c<=244);std::size_t consumed=1;
        if(valid&&n>1){
            for(std::size_t j=1;j<n;++j){
                if(i+j>=b.size()||(b[i+j]&192)!=128){valid=false;consumed=j;break;}
                if(j==1&&((c==224&&b[i+j]<160)||(c==237&&b[i+j]>=160)||(c==240&&b[i+j]<144)||(c==244&&b[i+j]>=144))){valid=false;consumed=1;break;}
            }
        }
        if(valid){out.append(reinterpret_cast<const char*>(&b[i]),n);i+=n;}else{out+=u8"\ufffd";i+=consumed;}
    }
    while(out.size()>200){auto p=out.size()-1;while(p&&(static_cast<unsigned char>(out[p])&192)==128)--p;out.resize(p);}return out;
}
Event port(std::uint64_t tick,std::uint8_t p) { return {tick,255,33,{p}}; }
struct Ordered { Event event; unsigned priority; std::uint64_t order; };
using OrderedTrack=std::vector<Ordered>;
}
double Clock::seconds(std::uint64_t tick) const {
    auto i=std::upper_bound(points.begin(),points.end(),tick,[](auto t,const Point& p){return t<p.tick;});
    auto& p=*(i==points.begin()?i:--i);return p.seconds+(tick-p.tick)*p.step;
}
std::uint64_t Clock::tick(double seconds) const {
    auto i=std::upper_bound(points.begin(),points.end(),seconds,[](auto t,const Point& p){return t<p.seconds;});
    auto& p=*(i==points.begin()?i:--i);return p.tick+static_cast<std::uint64_t>(std::max(0.,std::round((seconds-p.seconds)/p.step)));
}
Source Source::blank(std::string name) {
    return {true,{},std::move(name),{77,84,104,100,0,0,0,6,0,0,0,1,1,224,77,84,114,107,0,0,0,4,0,255,47,0}};
}
Parsed Source::parse(const std::vector<std::string>& gmNames) const {
    if(bytes.size()>8000000||name.size()>1024||path.size()>32768)fail("MIDI trop volumineux.");
    Reader r{bytes,0,bytes.size()};if(r.be(4)!=0x4d546864)fail("MIDI invalide : en-tête absent.");
    auto header=r.be(4);if(header<6||header>r.end-r.at)fail("MIDI invalide : en-tête tronqué.");
    Parsed out;out.format=static_cast<std::uint16_t>(r.be(2));auto count=r.be(2);out.division=static_cast<std::uint16_t>(r.be(2));r.take(header-6);
    if(out.format==2)fail("MIDI type 2 : séquences indépendantes non prises en charge. Exportez un MIDI type 0 ou 1.");
    if(out.format>2||!count||count>1024)fail("Nombre de pistes MIDI invalide.");
    std::size_t eventCount=0;
    for(unsigned track=0;track<count;++track) {
        if(r.be(4)!=0x4d54726b)fail("MIDI invalide : piste absente.");auto length=r.be(4);
        if(length>r.end-r.at)fail("MIDI invalide : piste tronquée.");Reader tr{bytes,r.at,r.at+length};r.at+=length;
        std::vector<Event> events;std::uint64_t tick=0;std::uint8_t running=0;
        while(tr.at<tr.end){tick+=tr.vlq();auto status=tr.byte();
            if(status<128){if(!running)fail("MIDI invalide : running status absent.");--tr.at;status=running;}
            Event e{tick,status,0,{}};
            if(status<240){running=status;auto n=((status&240)==192||(status&240)==208)?1:2;e.data=tr.take(n);for(auto b:e.data)if(b>127)fail("MIDI invalide : donnée de canal.");}
            else if(status==255){e.meta=tr.byte();auto n=tr.vlq();e.data=tr.take(n);}
            else if(status==240||status==247){running=0;auto n=tr.vlq();e.data=tr.take(n);}
            else fail("MIDI invalide : statut non pris en charge.");
            if(++eventCount>1000000)fail("Nombre de pistes MIDI invalide.");
            events.push_back(std::move(e));
        }out.tracks.push_back(std::move(events));
    }
    using Tempo=std::tuple<std::uint64_t,std::size_t,std::size_t,std::uint32_t>;
    using Program=std::tuple<std::uint64_t,std::size_t,std::size_t,std::uint8_t,std::uint8_t,PartId>;
    std::vector<Tempo> tempos;std::vector<Program> programs;
    for(std::size_t t=0;t<out.tracks.size();++t){std::uint8_t p=0;auto& events=out.tracks[t];
        for(std::size_t i=0;i<events.size();++i){auto& e=events[i];
            if(e.isMeta(81)){if(e.data.size()!=3)fail("MIDI invalide : tempo.");auto v=(e.data[0]<<16)|(e.data[1]<<8)|e.data[2];if(!v)fail("Tempo MIDI nul.");tempos.emplace_back(e.tick,t,i,v);}
            if(e.isMeta(33)){if(e.data.size()!=1||e.data[0]>127)fail("MIDI invalide : port.");p=e.data[0];}
            if(e.channel()&&(e.status&240)==192)programs.emplace_back(e.tick,t,i,p,static_cast<std::uint8_t>(e.status&15),e.data[0]);
        }
    }
    if(!(out.division&32768)){
        if(!out.division)fail("Résolution MIDI nulle.");double step=.5/out.division,seconds=0;std::uint64_t previous=0;
        out.clock.points.push_back({0,0,step});std::sort(tempos.begin(),tempos.end());
        for(auto [tick,t,i,tempo]:tempos){seconds+=(tick-previous)*step;step=tempo/1e6/out.division;out.clock.points.push_back({tick,seconds,step});previous=tick;}
    }else{
        auto fps=-static_cast<std::int8_t>(out.division>>8);double rate=fps==29?30000./1001.:fps;
        if(fps!=24&&fps!=25&&fps!=29&&fps!=30)fail("MIDI invalide : fréquence SMPTE.");
        auto sub=out.division&255;if(!sub)fail("Résolution SMPTE nulle.");out.clock.points.push_back({0,0,1./(rate*sub)});
    }
    std::sort(programs.begin(),programs.end());
    std::map<std::tuple<std::size_t,int,int>,PartId> routeIds;
    struct Held { std::uint64_t tick;int velocity;Part part; };
    struct Completed { Note note;std::uint64_t start,end;std::uint8_t off; };
    std::vector<Completed> completed;
    for(std::size_t t=0;t<out.tracks.size();++t){auto& events=out.tracks[t];std::string trackName="Piste "+std::to_string(t+1);
        for(auto& e:events)if(e.isMeta(3)){trackName=label(e.data);break;}
        std::uint8_t p=0;std::map<std::tuple<int,int,int>,std::deque<Held>> held;
        for(std::size_t i=0;i<events.size();++i){auto& e=events[i];if(e.isMeta(33))p=e.data[0];if(!e.note())continue;
            int channel=e.status&15,key=e.data[0],vel=e.data[1];
            if((e.status&240)==144&&vel){auto routeKey=std::make_tuple(t,p,channel);auto it=routeIds.find(routeKey);
                PartId id;if(it==routeIds.end()){if(routeIds.size()>=32512)fail("Trop de parties MIDI.");id=static_cast<PartId>(256+routeIds.size());routeIds[routeKey]=id;}else id=it->second;
                PartId program=channel==9?128:0;
                if(channel!=9)for(auto itp=programs.rbegin();itp!=programs.rend();++itp){auto [tick,pt,pi,pp,ch,pr]=*itp;
                    if(pp==p&&ch==channel&&std::make_tuple(tick,pt,pi)<=std::make_tuple(e.tick,t,i)){program=pr;break;}}
                std::string instrument=program<gmNames.size()?gmNames[program]:"Instrument "+std::to_string(program);
                Part part{id,program,channel==9,trackName+u8" · "+instrument};
                out.routes.emplace(id,Route{t,static_cast<std::uint8_t>(channel),p,part});held[{p,channel,key}].push_back({e.tick,vel,part});
            }else{auto& queue=held[{p,channel,key}];if(queue.empty())continue;auto h=queue.front();queue.pop_front();
                if(e.tick<=h.tick)fail("Une note MIDI a une durée nulle. Corrigez-la avant l’import.");
                double a=out.clock.seconds(h.tick),b=out.clock.seconds(e.tick);if(b>1200)fail("MIDI de plus de 20 minutes : limite du lecteur interne.");
                if(static_cast<float>(b)<=static_cast<float>(a))fail("Une note est trop courte pour la précision temporelle de l’éditeur.");
                Note n;n.part=h.part;n.pitch=key;n.velocity=h.velocity;n.velocityNormalised=static_cast<float>(h.velocity)/127.f;n.start=static_cast<float>(a);n.end=static_cast<float>(b);
                completed.push_back({n,h.tick,e.tick,static_cast<std::uint8_t>((e.status&240)==128?vel:0)});
                if(completed.size()>100000)fail("Plus de 100 000 notes MIDI.");
            }
        }for(auto& entry:held)if(!entry.second.empty())fail("Le MIDI contient des notes sans fin. Corrigez-le avant l’import.");
    }
    if(completed.empty()&&!createdEmpty)fail("Le MIDI ne contient aucune note.");
    std::stable_sort(completed.begin(),completed.end(),[](const auto& a,const auto& b){return std::make_tuple(a.note.start,a.note.key(),a.note.pitch)<std::make_tuple(b.note.start,b.note.key(),b.note.pitch);});
    for(auto& n:completed){n.note.id=out.notes.size()+1;n.note.sourceIds={n.note.id};out.notes.push_back(n.note);out.noteTicks.emplace_back(n.start,n.end,n.off);}
    return out;
}
std::vector<std::uint8_t> encode(std::uint16_t format,std::uint16_t division,const std::vector<std::vector<Event>>& tracks) {
    if(tracks.empty()||tracks.size()>65535)fail("Nombre de pistes MIDI invalide.");
    std::vector<std::uint8_t> out={'M','T','h','d',0,0,0,6};be(out,format,2);be(out,static_cast<std::uint32_t>(tracks.size()),2);be(out,division,2);
    for(auto& events:tracks){std::vector<std::uint8_t> data;std::uint64_t previous=0;
        for(auto& e:events){if(e.tick<previous)fail("Ordre MIDI invalide.");vlq(data,e.tick-previous);previous=e.tick;data.push_back(e.status);
            if(e.status==255)data.push_back(e.meta);if(e.status>=240)vlq(data,e.data.size());data.insert(data.end(),e.data.begin(),e.data.end());}
        out.insert(out.end(),{'M','T','r','k'});be(out,static_cast<std::uint32_t>(data.size()),4);out.insert(out.end(),data.begin(),data.end());
    }return out;
}
std::vector<std::uint8_t> Source::exportNotes(const std::vector<Note>& notes,const std::vector<std::string>& names,double fixedBpm) const {
    if(!std::isfinite(fixedBpm)||(fixedBpm!=0&&(fixedBpm<10||fixedBpm>600)))fail("Tempo invalide : 10 à 600 BPM.");
    auto parsed=parse(names);const bool retime=fixedBpm!=0;
    auto target=parsed.clock;
    if(retime){
        if(parsed.division&32768)parsed.division=480;
        if(createdEmpty)parsed.division=9600;
        const auto micros=std::uint32_t(std::llround(60000000./fixedBpm));
        target.points={{0,0,micros/1e6/parsed.division}};
    }
    auto convert=[&](std::uint64_t tick){return retime?target.tick(parsed.clock.seconds(tick)):tick;};
    std::vector<OrderedTrack> output;std::vector<std::uint64_t> ends;
    for(auto& events:parsed.tracks){OrderedTrack track;std::uint8_t p=0;std::uint64_t end=0;
        for(std::size_t i=0;i<events.size();++i){auto e=events[i];end=convert(e.tick);if(e.isMeta(33))p=e.data[0];
            if(retime&&e.isMeta(81))continue;
            e.tick=convert(e.tick);
            if(!e.note()&&!e.isMeta(47)){if(e.channel()||e.status==240||e.status==247)track.push_back({port(e.tick,p),0,i*2});track.push_back({e,0,i*2+1});}}
        output.push_back(std::move(track));ends.push_back(end);
    }
    auto routes=parsed.routes;std::set<std::pair<int,int>> used;for(auto& r:routes)used.insert({r.second.port,r.second.channel});
    for(auto& n:notes){if(n.muted)continue;auto key=n.key();auto program=n.part?n.part->program:0;
        auto existing=routes.find(key);if(existing!=routes.end()&&existing->second.part.program==program)continue;
        const Route* inherited=nullptr;for(auto id:n.sourceIds)if(id&&id<=parsed.notes.size()){auto it=parsed.routes.find(parsed.notes[id-1].key());if(it!=parsed.routes.end()){inherited=&it->second;break;}}
        int p=-1,ch=-1;for(int pp=0;pp<128&&p<0;++pp)for(int cc=0;cc<16;++cc)if((cc==9)==(program==128)&&!used.count({pp,cc})){p=pp;ch=cc;break;}
        if(p<0)fail("Trop de parties MIDI pour les ports disponibles.");used.insert({p,ch});
        Part part=n.part.value_or(Part{{},0,false,"Piano"});OrderedTrack track={{port(0,static_cast<std::uint8_t>(p)),0,0}};
        if(n.part)track.push_back({Event{0,255,3,{part.label.begin(),part.label.end()}},0,1});
        if(program<128)track.push_back({Event{0,static_cast<std::uint8_t>(192|ch),0,{static_cast<std::uint8_t>(program)}},0,2});
        if(inherited){std::uint8_t sourcePort=0;auto& original=parsed.tracks[inherited->track];
            for(std::size_t i=0;i<original.size();++i){auto e=original[i];if(e.isMeta(33))sourcePort=e.data[0];
                if(e.channel()&&(e.status&15)==inherited->channel&&sourcePort==inherited->port&&!e.note()&&(e.status&240)!=192){e.tick=convert(e.tick);e.status=static_cast<std::uint8_t>((e.status&240)|ch);track.push_back({e,0,3+i});}}}
        routes[key]={output.size(),static_cast<std::uint8_t>(ch),static_cast<std::uint8_t>(p),part};output.push_back(std::move(track));ends.push_back(0);
    }
    for(auto& n:notes){if(n.muted)continue;if(n.pitch<0||n.pitch>127||n.velocity<=0||n.velocity>127||!std::isfinite(n.start)||!std::isfinite(n.end)||n.start<0||n.end<=n.start)fail("Note MIDI invalide.");
        auto route=routes.find(n.key());if(route==routes.end())fail("Partie MIDI destination absente du fichier source.");auto& r=route->second;
        bool original=n.id&&n.id<=parsed.notes.size();auto index=original?static_cast<std::size_t>(n.id-1):0;
        auto a=!retime&&original&&parsed.notes[index].start==n.start?std::get<0>(parsed.noteTicks[index]):target.tick(n.start);
        auto b=!retime&&original&&parsed.notes[index].end==n.end?std::get<1>(parsed.noteTicks[index]):target.tick(n.end);b=std::max(a+1,b);
        auto off=original?std::get<2>(parsed.noteTicks[index]):0;
        for(int isOn=0;isOn<2;++isOn){auto t=isOn?a:b;unsigned priority=isOn?2:1;auto& track=output[r.track];
            track.push_back({port(t,r.port),priority,n.id*2});track.push_back({Event{t,static_cast<std::uint8_t>((isOn?144:128)|r.channel),0,{static_cast<std::uint8_t>(n.pitch),static_cast<std::uint8_t>(isOn?n.velocity:off)}},priority,n.id*2+1});}
    }
    if(retime){auto micros=std::uint32_t(std::llround(60000000./fixedBpm));output[0].push_back({Event{0,255,81,{std::uint8_t(micros>>16),std::uint8_t(micros>>8),std::uint8_t(micros)}},0,0});}
    std::vector<std::vector<Event>> tracks;for(std::size_t i=0;i<output.size();++i){auto& ordered=output[i];std::stable_sort(ordered.begin(),ordered.end(),[](auto& a,auto& b){return std::make_tuple(a.event.tick,a.priority,a.order)<std::make_tuple(b.event.tick,b.priority,b.order);});
        std::vector<Event> track;for(auto& e:ordered)track.push_back(std::move(e.event));track.push_back({std::max(ends[i],track.empty()?0:track.back().tick),255,47,{}});tracks.push_back(std::move(track));}
    return encode(tracks.size()>1?1:parsed.format,parsed.division,tracks);
}
} // namespace phthongos::midi
