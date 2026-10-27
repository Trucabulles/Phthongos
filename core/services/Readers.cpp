#include "Readers.h"
#include "../analysis/MidiReaders.h"
#include "../project/DocumentJson.h"
#include <stdexcept>
#include <algorithm>
namespace phthongos::services {
namespace {
using juce::var;using juce::String;
var obj(std::initializer_list<std::pair<const char*,var>> fields){auto* p=new juce::DynamicObject;for(auto& f:fields)p->setProperty(f.first,f.second);return var(p);}
var integer(std::uint64_t n){return var(static_cast<juce::int64>(n));}
template<class T,class F>var list(const T& xs,F f){juce::Array<var> a;for(auto& x:xs)a.add(f(x));return var(a);}
bool nil(const var& v){return v.isVoid()||v.isUndefined();}
std::uint64_t uint(const var& v){if((!v.isInt()&&!v.isInt64())||static_cast<juce::int64>(v)<0)throw std::invalid_argument("Identifiant invalide.");return static_cast<std::uint64_t>(static_cast<juce::int64>(v));}
const std::array<const char*,12> modes={"major","minor","harmonic_minor","melodic_minor","dorian","phrygian","lydian","mixolydian","locrian","major_pentatonic","minor_pentatonic","chromatic"};
} // namespace
analysis::Scale readScale(const var& v){analysis::Scale scale;auto tonic=uint(v["tonic"]);if(tonic>11)throw std::invalid_argument("Tonique invalide.");scale.tonic=static_cast<int>(tonic);
    auto mode=v["mode"].toString();auto i=std::find_if(modes.begin(),modes.end(),[&](auto* s){return mode==s;});if(i==modes.end())throw std::invalid_argument("Mode de gamme invalide.");scale.mode=static_cast<analysis::Mode>(i-modes.begin());return scale;}
namespace {
var writeScale(analysis::Scale s){return obj({{"tonic",s.tonic},{"mode",modes.at(static_cast<std::size_t>(s.mode))}});}
}
var readMidi(const String& command,const var& args,const var& selected){
    bool tonal=command=="tonal_analysis";if(!tonal&&command!="short_note_analysis")throw std::invalid_argument("Service MIDI inconnu.");
    if(uint(selected["editing"]["revision"])!=uint(args["revision"]))throw std::invalid_argument(tonal?"Notes modifiées. Relancez le diagnostic tonal.":"Notes modifiées. Relancez le diagnostic de durée.");
    std::vector<Note> notes;auto* a=selected["editing"]["notes"].getArray();if(!a)throw std::invalid_argument("Instantané MIDI invalide.");for(auto& n:*a)notes.push_back(project::readNote(n));
    std::optional<std::vector<NoteId>> ids;if(!nil(args["ids"])){auto* array=args["ids"].getArray();if(!array)throw std::invalid_argument("Sélection invalide.");ids=std::vector<NoteId>{};for(auto& id:*array)ids->push_back(uint(id));}
    std::optional<PartId> part;if(!nil(args["part"])){auto p=uint(args["part"]);if(p>65535)throw std::invalid_argument("Partie invalide.");part=static_cast<PartId>(p);}
    ids=analysis::scope(notes,ids,part);
    if(tonal){std::optional<analysis::Scale> chosen;if(!nil(args["scale"]))chosen=readScale(args["scale"]);auto r=analysis::tonal(notes,ids,chosen);
        return obj({{"scale",writeScale(r.scale)},{"examined_ids",list(r.examined,integer)},{"out_ids",list(r.outside,integer)},
                    {"suggestions",list(r.suggestions,[](auto& s){return obj({{"scale",writeScale(s.scale)},{"score",s.score}});})},{"ambiguous",r.ambiguous}});}
    auto v=args["thresholdMs"];if(!v.isInt()&&!v.isInt64()&&!v.isDouble())throw std::invalid_argument("Seuil de durée invalide.");auto r=analysis::shortNotes(notes,ids,static_cast<double>(v));
    return obj({{"threshold_ms",r.thresholdMs},{"examined_ids",list(r.examined,integer)},{"notes",list(r.hits,[](auto& h){return obj({{"id",integer(h.id)},{"duration_ms",h.durationMs},{"under_50",h.under50},{"protected",h.protectedNote}});})}});
}
}
