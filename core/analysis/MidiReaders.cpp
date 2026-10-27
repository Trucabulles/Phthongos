#include "MidiReaders.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>
namespace phthongos::analysis {
namespace {
[[noreturn]]void fail(const char* s){throw std::invalid_argument(s);}
std::set<NoteId> idsOf(const std::vector<Note>& notes){std::set<NoteId> ids;for(auto& n:notes)ids.insert(n.id);return ids;}
std::optional<std::set<NoteId>> selection(const std::vector<Note>& notes,const std::optional<std::vector<NoteId>>& ids,bool allowEmpty,const char* error){
    if(!ids)return {};std::set<NoteId> selected(ids->begin(),ids->end());auto alive=idsOf(notes);
    if((!allowEmpty&&selected.empty())||selected.size()!=ids->size())fail(error);
    for(auto id:selected)if(!alive.count(id))fail(error);return selected;
}
}
bool Scale::contains(int pitch) const {
    static const std::vector<std::vector<int>> intervals={
        {0,2,4,5,7,9,11},{0,2,3,5,7,8,10},{0,2,3,5,7,8,11},{0,2,3,5,7,9,11},
        {0,2,3,5,7,9,10},{0,1,3,5,7,8,10},{0,2,4,6,7,9,11},{0,2,4,5,7,9,10},
        {0,1,3,5,6,8,10},{0,2,4,7,9},{0,3,5,7,10},{0,1,2,3,4,5,6,7,8,9,10,11}};
    auto& set=intervals.at(static_cast<std::size_t>(mode));int p=(pitch%12+12-tonic)%12;return std::find(set.begin(),set.end(),p)!=set.end();
}
std::vector<NoteId> scope(const std::vector<Note>& notes,const std::optional<std::vector<NoteId>>& ids,std::optional<PartId> part){
    if(part&&*part>128&&(*part<256||*part>=32768))fail("Partie invalide.");
    auto selected=selection(notes,ids,true,"Sélection dupliquée ou obsolète.");std::vector<NoteId> out;
    for(auto& n:notes)if((!part||n.key()==*part)&&(!selected||selected->count(n.id)))out.push_back(n.id);return out;
}
Tonality tonal(const std::vector<Note>& notes,const std::optional<std::vector<NoteId>>& ids,std::optional<Scale> chosen){
    if(chosen&&(chosen->tonic<0||chosen->tonic>11))fail("Tonique invalide.");
    auto selected=selection(notes,ids,false,"Sélection vide, dupliquée ou obsolète. Relancez la recherche.");
    std::vector<const Note*> examined;for(auto& n:notes)if(!(n.part&&n.part->drum)&&(!selected||selected->count(n.id)))examined.push_back(&n);
    if(examined.empty())fail("Aucune note à examiner.");std::array<double,12> chroma{};
    for(auto* n:examined){float length=n->end-n->start;double duration=length;if(!std::isfinite(duration)||duration<=0)fail("Durée de note invalide.");
        chroma[n->pitch%12]+=duration*(.25+.75*n->velocity/127.);}
    double total=0;int distinct=0;for(auto x:chroma){total+=x;if(x>0)++distinct;}for(auto& x:chroma)x/=total;
    Tonality result;
    for(int tonic=0;tonic<12;++tonic)for(auto mode:{Mode::major,Mode::minor}){Scale scale{tonic,mode};double coverage=0;
        for(int p=0;p<12;++p)if(scale.contains(p))coverage+=chroma[p];int third=mode==Mode::major?4:3;
        double score=.85*coverage+.08*chroma[tonic]+.04*chroma[(tonic+third)%12]+.03*chroma[(tonic+7)%12];result.suggestions.push_back({scale,score});}
    std::stable_sort(result.suggestions.begin(),result.suggestions.end(),[](auto& a,auto& b){return a.score>b.score;});
    result.ambiguous=distinct<5||result.suggestions[0].score-result.suggestions[1].score<.025;result.scale=chosen.value_or(result.suggestions[0].scale);result.suggestions.resize(5);
    for(auto* n:examined){result.examined.push_back(n->id);if(!result.scale.contains(n->pitch))result.outside.push_back(n->id);}return result;
}
ShortNotes shortNotes(const std::vector<Note>& notes,const std::optional<std::vector<NoteId>>& ids,double threshold){
    if(!std::isfinite(threshold)||threshold<1||threshold>1000)fail("Le seuil doit être compris entre 1 et 1000 ms.");
    auto selected=selection(notes,ids,true,"Sélection dupliquée ou obsolète. Relancez le diagnostic.");
    std::vector<const Note*> examined;for(auto& n:notes)if(!(n.part&&n.part->drum)&&(!selected||selected->count(n.id)))examined.push_back(&n);
    std::stable_sort(examined.begin(),examined.end(),[](auto* a,auto* b){if(a->start!=b->start)return a->start<b->start;if(a->start==0&&std::signbit(a->start)!=std::signbit(b->start))return std::signbit(a->start);return std::make_tuple(a->pitch,a->id)<std::make_tuple(b->pitch,b->id);});
    ShortNotes result{threshold,{},{}};for(auto* n:examined){float duration=n->end-n->start;if(!std::isfinite(duration)||duration<=0)fail("Durée de note invalide.");
        result.examined.push_back(n->id);if(duration<=static_cast<float>(threshold/1000.))result.hits.push_back({n->id,static_cast<double>(duration)*1000.,duration<.05f,n->protectedNote});}return result;
}
}
