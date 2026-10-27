#include "Cleanup.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>
namespace phthongos::services {
namespace {
using juce::var;using juce::String;
var obj(std::initializer_list<std::pair<const char*,var>> fields){auto* o=new juce::DynamicObject;for(auto& f:fields)o->setProperty(f.first,f.second);return var(o);}
var integer(std::uint64_t v){return var((juce::int64)v);}
bool key(int p){return (p>=0&&p<=128)||(p>=256&&p<32768);}
void scopeValid(const var& s,int depth=0){
    if(depth>64)throw std::invalid_argument("Zone de nettoyage invalide.");const auto k=s["kind"].toString();
    if(k=="all")return;
    if(k=="part"){if(!key((int)s["program"]))throw std::invalid_argument("Partie invalide.");scopeValid(s["scope"],depth+1);return;}
    if(k=="notes"){auto* ids=s["ids"].getArray();if(!ids||ids->isEmpty()||ids->size()>100000)throw std::invalid_argument("Sélectionnez des notes.");return;}
    if(k=="region"){const double a=s["start"],b=s["end"];const int low=s["low"],high=s["high"];
        if(std::isfinite(a)&&std::isfinite(b)&&a>=0&&b>a&&low>=0&&high<=127&&low<=high)return;}
    throw std::invalid_argument("Zone de nettoyage invalide.");
}
bool contains(const var& s,const Note& n){
    const auto k=s["kind"].toString();if(k=="all")return true;
    if(k=="part")return n.key()==(int)s["program"]&&contains(s["scope"],n);
    if(k=="notes"){for(auto& id:*s["ids"].getArray())if((juce::int64)id==(juce::int64)n.id)return true;return false;}
    return n.start>=(double)s["start"]&&n.end<=(double)s["end"]&&n.pitch>=(int)s["low"]&&n.pitch<=(int)s["high"];
}
bool timeLess(float a,float b){if(a==0&&b==0&&std::signbit(a)!=std::signbit(b))return std::signbit(a);return a<b;}
struct Run{std::vector<NoteId> ids;int pitch;float start,end;var label;juce::Array<var> edges;};
}
var cleanupPreview(const std::vector<Note>& original,std::uint64_t revision,float threshold,const var& scope,const var& settings,const std::optional<LegacySupport>& maps){
    scopeValid(scope);const double gapLimit=settings["gap_ms"];const float sustainMin=(float)(double)settings["sustain_min"];const int velocityDelta=settings["velocity_delta"];
    if(!std::isfinite(gapLimit)||gapLimit<0||gapLimit>250||!std::isfinite(sustainMin)||sustainMin<0||sustainMin>1||velocityDelta<0||velocityDelta>126)
        throw std::invalid_argument("Paramètres de nettoyage invalides.");
    if(maps&&(!maps->frames||maps->frames>18000||maps->notes.size()!=88*maps->frames))throw std::invalid_argument("Cartes absentes.");
    std::map<std::string,int> counts;for(auto* k:{"percussion_notes","scope_notes","examined_pairs","out_of_scope","overlap","touching","gap_too_large","short_boundaries","protected","sustain","velocity","review","proposed","runs"})counts[k]=0;
    for(auto& n:original)if(contains(scope,n)){++counts["scope_notes"];if(!maps&&n.part&&n.part->program==128)++counts["percussion_notes"];}
    std::vector<Run> runs;
    auto process=[&](const std::vector<Note>& lane){
        std::optional<Run> current;
        auto flush=[&]{if(current){runs.push_back(std::move(*current));current.reset();}};
        for(std::size_t i=1;i<lane.size();++i){auto& a=lane[i-1];auto& b=lane[i];
            if(a.pitch!=b.pitch||a.key()!=b.key()){flush();continue;}++counts["examined_pairs"];
            const double gap=(static_cast<double>(b.start)-a.end)*1000.;bool eligible=false;float sustain=0,onset=0;
            if(!contains(scope,a)||!contains(scope,b))++counts["out_of_scope"];
            else if(gap<(maps?0.:-.001))++counts["overlap"];
            else if(maps&&gap==0)++counts["touching"];
            else if(gap>gapLimit)++counts["gap_too_large"];
            else{
                ++counts["short_boundaries"];if(!maps&&std::abs(gap)<=.001)++counts["touching"];
                if(a.protectedNote||b.protectedNote||a.muted||b.muted)++counts["protected"];
                else{
                    if(maps){
                        if(a.pitch<21||a.pitch>108)throw std::invalid_argument("Hauteur hors des cartes historiques.");
                        auto mean=[&](std::size_t lo,std::size_t hi){lo=std::min(lo,maps->frames);hi=std::min(hi,maps->frames);if(hi<=lo)return 0.f;
                            float sum=0;for(auto j=lo;j<hi;++j)sum+=maps->notes[(a.pitch-21)*maps->frames+j];return sum/static_cast<float>(hi-lo);};
                        sustain=std::min(mean(std::max(a.endFrame>3?a.endFrame-3:0,a.startFrame),a.endFrame),mean(b.startFrame,std::min(b.startFrame+3,b.endFrame)));
                        onset=b.onsetProbability/std::max(a.onsetProbability,1e-8f);
                    }
                    if(maps&&(!(sustain>=sustainMin)))++counts["sustain"];
                    else if(std::abs(a.velocity-b.velocity)>velocityDelta)++counts["velocity"];
                    else eligible=true;
                }
            }
            if(eligible){
                if(!current)current=Run{{a.id},a.pitch,a.start,a.end,maps?var():var(String::fromUTF8(a.part?a.part->label.c_str():"Piano")),{}};
                current->ids.push_back(b.id);current->end=b.end;
                auto reason=maps?"Série de fragments rapprochés, soutien sur les deux flancs ; sans validation acoustique WAV, à contrôler à l’écoute.":
                    "Même hauteur et même partie, fragments voisins rapprochés. Indice MIDI uniquement : aucune attaque ni continuité acoustique mesurée dans le WAV.";
                current->edges.add(obj({{"acoustic",var()},{"left",integer(a.id)},{"right",integer(b.id)},{"pitch",b.pitch},{"start",(double)b.start},
                    {"gap_ms",maps?gap:std::max(gap,0.)},{"onset_ratio",(double)onset},{"sustain",(double)sustain},{"reason",String::fromUTF8(reason)},{"run_length",0}}));
            }else flush();
        }flush();
    };
    if(maps){auto notes=original;std::stable_sort(notes.begin(),notes.end(),[](auto& a,auto& b){if(a.key()!=b.key())return a.key()<b.key();if(a.pitch!=b.pitch)return a.pitch<b.pitch;return timeLess(a.start,b.start);});process(notes);}
    else{
        std::map<std::pair<PartId,int>,std::vector<Note>> lanes;for(auto& n:original)if(!n.part||n.part->program!=128)lanes[{n.key(),n.pitch}].push_back(n);
        for(auto& item:lanes){auto& lane=item.second;std::stable_sort(lane.begin(),lane.end(),[](auto& a,auto& b){if(a.start==b.start&&std::signbit(a.start)==std::signbit(b.start))return a.id<b.id;return timeLess(a.start,b.start);});process(lane);}
        std::stable_sort(runs.begin(),runs.end(),[](auto& a,auto& b){if(a.start!=b.start||std::signbit(a.start)!=std::signbit(b.start))return timeLess(a.start,b.start);return std::tie(a.pitch,a.ids.front())<std::tie(b.pitch,b.ids.front());});
    }
    juce::Array<var> encodedRuns,proposals;auto lengths=obj({});
    for(auto& run:runs){const auto length=run.ids.size();bool proposed=length>=3;auto name=String((juce::int64)length);auto* properties=lengths.getDynamicObject();properties->setProperty(name,(int)properties->getProperty(name)+1);
        for(auto& edge:run.edges)edge.getDynamicObject()->setProperty("run_length",(int)length);
        if(proposed){for(auto& edge:run.edges){auto p=edge.clone();p.getDynamicObject()->setProperty("left",integer(run.ids.front()));proposals.add(p);}counts["proposed"]+=static_cast<int>(length-1);}
        else counts["review"]+=static_cast<int>(length-1);
        juce::Array<var> ids;for(auto id:run.ids)ids.add(integer(id));
        encodedRuns.add(obj({{"part_label",run.label},{"ids",var(ids)},{"pitch",run.pitch},{"start",(double)run.start},{"end",(double)run.end},{"proposed",proposed},{"boundaries",var(run.edges)}}));
    }
    counts["runs"]=static_cast<int>(runs.size());auto diagnostics=obj({});for(auto& c:counts)diagnostics.getDynamicObject()->setProperty(String(c.first),c.second);diagnostics.getDynamicObject()->setProperty("run_lengths",lengths);
    return obj({{"midi_only",!maps},{"revision",integer(revision)},{"threshold",(double)threshold},{"before",integer(original.size())},{"diagnostic_before",integer(original.size())},
        {"after",integer(original.size()-proposals.size())},{"protected_skipped",counts["protected"]},{"diagnostics",diagnostics},{"proposals",var(proposals)},{"runs",var(encodedRuns)},
        {"scope",scope.clone()},{"settings",settings.clone()}});
}
}
