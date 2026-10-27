#include "Document.h"
#include "Text.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace phthongos {
namespace {
[[noreturn]] void fail(const char* s) { throw std::invalid_argument(s); }
bool validKey(PartId p) { return p <= 128 || (p >= 256 && p < 32768); }
using detail::trim;

Part readable(Part p,const SourceSnapshot& source) {
    if(p.label=="Instrument GM "+std::to_string(p.program+1) && p.program<source.gmNames.size())
        p.label=source.gmNames[p.program];
    return p;
}
void sortNotes(std::vector<Note>& notes) {
    std::sort(notes.begin(),notes.end(),[](const Note& a,const Note& b){
        // Source timestamps are finite. Preserve Rust total_cmp's signed-zero order.
        if(a.start==0 && b.start==0 && std::signbit(a.start)!=std::signbit(b.start))
            return std::signbit(a.start);
        return std::tie(a.start,a.pitch,a.id)<std::tie(b.start,b.pitch,b.id);
    });
}
std::set<NoteId> unique(const std::vector<NoteId>& ids,const std::vector<Note>& notes) {
    std::set<NoteId> chosen(ids.begin(),ids.end());
    if(chosen.empty() || chosen.size()!=ids.size()) fail("Sélection vide, dupliquée ou obsolète.");
    std::size_t found=0;for(const auto& n:notes)found+=chosen.count(n.id);
    if(found!=chosen.size())fail("Sélection vide, dupliquée ou obsolète.");
    return chosen;
}
bool fingerprintMatches(const Fingerprint& f,const Note& n) {
    return f.id==n.id && f.pitch==n.pitch && f.start==n.start && f.end==n.end &&
        f.program==(n.part?n.part->program:0) && f.part==(n.part?n.part->identity:std::optional<PartId>{});
}
bool changed(const Layer& l) {
    return !l.partColours.empty()||!l.lockedParts.empty()||!l.markers.empty()||!l.masks.empty()||!l.parts.empty()||!l.deletedParts.empty()||!l.mutedParts.empty()||!l.muted.empty()||
        !l.automaticMerges.empty()||!l.manual.empty()||!l.dynamics.empty();
}
}

Document::Document():layer_(std::make_shared<const Layer>()){}
std::vector<Part> Document::parts(const SourceSnapshot& source) const {
    std::map<PartId,Part> out;
    for(const auto& n:source.notes)out[n.key()]=n.part.value_or(Part{{},0,false,"Piano"});
    // A manually drawn voice is a destination too, even without a source MIDI.
    for(const auto& entry:layer_->manual)if(entry.second){const auto& n=*entry.second;
        if(!out.count(n.key())&&std::none_of(layer_->parts.begin(),layer_->parts.end(),[&](const auto& p){return p.second.key()==n.key();}))
            out.emplace(n.key(),n.part.value_or(Part{{},0,false,"Piano"}));
    }
    for(const auto& p:layer_->parts)out[p.first]=p.second;
    std::vector<Part> result;for(const auto& p:out)if(!layer_->deletedParts.count(p.first)&&!layer_->deletedParts.count(p.second.key()))result.push_back(readable(p.second,source));
    return result;
}
std::set<PartId> Document::reservedPartIds(const SourceSnapshot& source) const {
    std::set<PartId> used;
    for(const auto& n:source.allNotes)used.insert(n.key());
    auto scan=[&](const Layer& l){used.insert(l.deletedParts.begin(),l.deletedParts.end());for(const auto& p:l.parts){used.insert(p.first);used.insert(p.second.key());}};
    scan(*layer_);for(const auto& l:undo_)scan(*l);for(const auto& l:redo_)scan(*l);
    return used;
}
PartId Document::freshPartId(const SourceSnapshot& source) const {
    const auto used=reservedPartIds(source);
    for(unsigned i=256;i<32768;++i)if(!used.count(static_cast<PartId>(i)))return static_cast<PartId>(i);
    fail("Plus d’identifiants de parties disponibles.");
}
std::vector<Note> Document::resolve(const SourceSnapshot& source) const {
    auto notes=source.notes;
    std::set<NoteId> protectedIds;
    for(const auto& v:layer_->manual){protectedIds.insert(v.first);if(v.second)
        protectedIds.insert(v.second->sourceIds.begin(),v.second->sourceIds.end());}
    for(auto& n:notes)n.protectedNote=protectedIds.count(n.id)>0;
    for(const auto& batch:layer_->automaticMerges)for(const auto& pair:batch){
        auto a=std::find_if(notes.begin(),notes.end(),[&](const Note& n){return n.id==pair.first;});
        auto b=std::find_if(notes.begin(),notes.end(),[&](const Note& n){return n.id==pair.second;});
        if(a==notes.end()||b==notes.end()||a==b||a->protectedNote||b->protectedNote||a->key()!=b->key())continue;
        a->end=std::max(a->end,b->end);a->endFrame=std::max(a->endFrame,b->endFrame);
        a->frameDuration=a->endFrame>=a->startFrame?a->endFrame-a->startFrame:0;
        a->sourceIds.insert(a->sourceIds.end(),b->sourceIds.begin(),b->sourceIds.end());notes.erase(b);
    }
    // All overrides suppress matching base IDs/ancestry. Keep the original ordered
    // last-override-wins rule without repeatedly scanning the entire note vector.
    notes.erase(std::remove_if(notes.begin(),notes.end(),[&](const Note& n){if(layer_->manual.count(n.id))return true;
        return std::any_of(n.sourceIds.begin(),n.sourceIds.end(),[&](NoteId id){return protectedIds.count(id)>0;});}),notes.end());
    std::map<NoteId,Note> overrides;
    std::map<NoteId,std::set<NoteId>> ancestry;
    for(const auto& v:layer_->manual)if(v.second){
        std::set<NoteId> victims;
        for(auto id:v.second->sourceIds){auto i=ancestry.find(id);if(i!=ancestry.end())victims.insert(i->second.begin(),i->second.end());}
        for(auto id:victims){for(auto ancestor:overrides.at(id).sourceIds)ancestry[ancestor].erase(id);overrides.erase(id);}
        auto n=*v.second;n.velocitySource=VelocitySource::manual;overrides[n.id]=n;
        for(auto id:n.sourceIds)ancestry[id].insert(n.id);
    }
    for(const auto& v:overrides)notes.push_back(v.second);
    for(auto& n:notes){
        auto d=layer_->dynamics.find(n.id);
        if(d!=layer_->dynamics.end() && (!n.protectedNote||d->second.overrideManual) && fingerprintMatches(d->second.note,n)){
            n.velocity=d->second.velocity;n.velocityNormalised=static_cast<float>(n.velocity)/127.0f;
            n.velocitySource=VelocitySource::audioEstimated;
        }
        auto p=layer_->parts.find(n.key());if(p!=layer_->parts.end())n.part=p->second;
        if(n.part)n.part=readable(*n.part,source);
        n.muted=layer_->muted.count(n.id)||layer_->mutedParts.count(n.key());
    }
    sortNotes(notes);return notes;
}
View Document::view(const SourceSnapshot& source) const {
    View v;v.audioSources=layer_->audioSources;v.partColours=layer_->partColours;v.markers=layer_->markers;v.masks=layer_->masks;v.notes=resolve(source);v.parts=parts(source);v.revision=revision_;
    v.canUndo=!undo_.empty();v.canRedo=!redo_.empty();v.hasChanges=changed(*layer_);
    v.lockedParts=layer_->lockedParts;v.mutedNotes=layer_->muted;v.mutedParts=layer_->mutedParts;v.dynamicsMapping=layer_->dynamicsMapping;
    std::set<NoteId> present;for(const auto& n:source.notes)present.insert(n.id);
    for(const auto& n:v.notes)if(n.protectedNote&&!n.sourceIds.empty()&&
        std::none_of(n.sourceIds.begin(),n.sourceIds.end(),[&](NoteId id){return present.count(id)>0;}))++v.protectedRetained;
    return v;
}
void Document::checkpoint(std::shared_ptr<Layer> next) {
    undo_.push_back(layer_);if(undo_.size()>historyLimit)undo_.erase(undo_.begin());
    redo_.clear();layer_=std::move(next);++revision_;
}
void Document::act(const SourceSnapshot& source,std::uint64_t expected,const Action& action) {
    if(expected!=revision_)fail("La transcription a changé. Rechargez la sélection.");
    // Cheap transaction copy: immutable history pointers, not 100 deep layer copies.
    // Invalid commands never alter the live document or its manual-ID counter.
    auto candidate=*this;candidate.execute(source,action);
    if(action.kind!=Action::Kind::undo&&action.kind!=Action::Kind::redo&&action.kind!=Action::Kind::togglePartLock)
        checkLockedNotes(source,candidate);
    *this=std::move(candidate);
}
void Document::actBatch(const SourceSnapshot& source,std::uint64_t expected,const std::vector<Action>& actions,const std::optional<std::string>& newVoice) {
    if(expected!=revision_)fail("La transcription a changé. Rechargez la sélection.");
    if(actions.empty()||actions.size()>100000)fail("Lot de corrections vide ou trop volumineux.");
    auto candidate=*this;std::optional<Part> target;
    if(newVoice){
        if(std::none_of(actions.begin(),actions.end(),[](const Action& a){return a.kind==Action::Kind::add;}))fail("La nouvelle voix ne contient aucune note à ajouter.");
        auto key=candidate.freshPartId(source);Action create;create.kind=Action::Kind::createEmptyPart;create.name=*newVoice;
        candidate.execute(source,create);target=candidate.layer_->parts.at(key);
    }
    auto destinations=candidate.parts(source);std::vector<Action> additions;
    for(const auto& action:actions){
        using K=Action::Kind;
        if(action.kind==K::add){auto a=action;if(target)a.part=target;additions.push_back(std::move(a));continue;}
        if(action.kind!=K::remove&&action.kind!=K::merge&&action.kind!=K::split&&action.kind!=K::extend)
            fail("Action interdite dans un lot de corrections.");
        candidate.execute(source,action);candidate.undo_.clear();candidate.redo_.clear();
    }
    if(!additions.empty()){
        if(candidate.nextManual_+additions.size()>1000000||candidate.layer_->manual.size()+additions.size()>100000)fail("Trop de corrections manuelles.");
        auto next=std::make_shared<Layer>(*candidate.layer_);
        for(const auto& a:additions){
            if(a.pitch<0||a.pitch>127||a.velocity<1||a.velocity>127||!std::isfinite(a.start)||!std::isfinite(a.end)||a.start<0||a.end>1200||a.end-a.start<.001)
                fail("Note invalide dans le lot de corrections.");
            if(a.part){const auto& p=*a.part;
                if(p.program>128||p.drum!=(p.program==128)||(p.identity&&(*p.identity<256||*p.identity>=32768)))fail("Partie invalide.");
                if(p.identity&&std::none_of(destinations.begin(),destinations.end(),[&](const Part& d){return d.key()==p.key();}))fail("Partie destination introuvable.");
            }
            Note n;n.id=manualBase+(++candidate.nextManual_);n.part=a.part;n.pitch=a.pitch;n.velocity=a.velocity;
            n.start=static_cast<float>(a.start);n.end=static_cast<float>(a.end);
            if(n.end<=n.start)fail("Note trop courte à cette position.");
            n.velocityNormalised=static_cast<float>(a.velocity)/127.f;n.velocitySource=VelocitySource::manual;n.protectedNote=true;next->manual[n.id]=std::move(n);
        }
        candidate.checkpoint(next);
    }
    checkLockedNotes(source,candidate);
    // Creation and all corrections are published together, with no intermediate history.
    candidate.undo_=undo_;candidate.undo_.push_back(layer_);
    if(candidate.undo_.size()>historyLimit)candidate.undo_.erase(candidate.undo_.begin());
    candidate.redo_.clear();candidate.revision_=revision_+1;
    *this=std::move(candidate);
}
void Document::checkLockedNotes(const SourceSnapshot& source,const Document& candidate) const {
    if(layer_->lockedParts.empty())return;
    // Compare all source candidates too: hiding a note at the current sensitivity
    // must not allow a bulk operation to modify a locked voice behind the scenes.
    auto all=source;if(!source.allNotes.empty())all.notes=source.allNotes;
    auto collect=[&](const Document& d){std::map<NoteId,Note> out;for(auto& n:d.resolve(all))if(layer_->lockedParts.count(n.key()))out.emplace(n.id,n);return out;};
    auto before=collect(*this),after=collect(candidate);
    if(before.size()!=after.size())fail("Une voix est verrouillée. Déverrouillez-la avant de la modifier.");
    for(const auto& [id,n]:before){auto at=after.find(id);if(at==after.end())fail("Une voix est verrouillée. Déverrouillez-la avant de la modifier.");auto& m=at->second;
        if(n.key()!=m.key()||n.pitch!=m.pitch||n.start!=m.start||n.end!=m.end||n.velocity!=m.velocity||n.pitchBend!=m.pitchBend||
            (n.part?n.part->program:0)!=(m.part?m.part->program:0)||n.sourceIds!=m.sourceIds)
            fail("Une voix est verrouillée. Déverrouillez-la avant de la modifier.");
    }
    auto oldParts=parts(source),newParts=candidate.parts(source);
    for(const auto& p:oldParts)if(layer_->lockedParts.count(p.key())){auto it=std::find_if(newParts.begin(),newParts.end(),[&](auto& q){return q.key()==p.key();});
        if(it==newParts.end()||it->program!=p.program||it->label!=p.label||it->soundfontPath!=p.soundfontPath||it->soundfontHash!=p.soundfontHash||it->soundfontBank!=p.soundfontBank||it->soundfontPreset!=p.soundfontPreset)fail("Une voix est verrouillée. Déverrouillez-la avant de la modifier.");}
}
double Document::timelineDuration(double base) const {
    for(const auto& m:layer_->markers)base=std::max(base,m.time);
    for(const auto& v:layer_->manual)if(v.second&&std::isfinite(v.second->end))base=std::max(base,static_cast<double>(v.second->end));
    return base;
}
void Document::execute(const SourceSnapshot& source,const Action& a) {
    using K=Action::Kind;
    if(a.kind==K::add && a.part){
        if(a.part->program>128 || a.part->drum!=(a.part->program==128) ||
           (a.part->identity && (*a.part->identity<256||*a.part->identity>=32768)))fail("Partie invalide.");
        if(a.part->identity){auto ps=parts(source);if(std::none_of(ps.begin(),ps.end(),[&](const Part& p){return p.identity==a.part->identity;}))fail("Partie MIDI absente de la source.");}
    }
    if(a.kind==K::undo || a.kind==K::redo){
        auto& from=a.kind==K::undo?undo_:redo_;auto& to=a.kind==K::undo?redo_:undo_;
        if(!from.empty()){to.push_back(layer_);if(to.size()>historyLimit)to.erase(to.begin());layer_=from.back();from.pop_back();++revision_;}return;
    }
    auto current=resolve(source);
    auto next=std::make_shared<Layer>(*layer_);
    auto find=[&](NoteId id)->Note{auto i=std::find_if(current.begin(),current.end(),[&](const Note& n){return n.id==id;});
        if(i==current.end())fail("Note obsolète.");return *i;};
    auto remember=[&](Note n){next->dynamics.erase(n.id);n.protectedNote=true;next->manual[n.id]=std::move(n);};
    auto suppress=[&](const Note& n){for(auto id:n.sourceIds)next->manual[id]=std::nullopt;next->manual[n.id]=std::nullopt;};
    auto choosePart=[&](PartId key,const char* error="Partie destination introuvable."){auto ps=parts(source);auto i=std::find_if(ps.begin(),ps.end(),[&](const Part& p){return p.key()==key;});
        if(i==ps.end())fail(error);return *i;};
    switch(a.kind){
    case K::addMarker:case K::modifyMarker:case K::deleteMarker:{
        auto found=std::find_if(next->markers.begin(),next->markers.end(),[&](const auto& m){return m.id==a.id;});
        if(a.kind!=K::addMarker&&found==next->markers.end())fail("Repère introuvable.");
        if(a.kind==K::deleteMarker){next->markers.erase(found);break;}
        if(!std::isfinite(a.at)||a.at<0||a.at>1200||a.name.size()>256)fail("Repère invalide.");
        if(a.kind==K::modifyMarker){found->time=a.at;found->name=a.name;}
        else{if(next->markers.size()>=1000)fail("Trop de repères (1000 maximum).");std::uint64_t id=0;
            auto scan=[&](const Layer& l){for(const auto& m:l.markers)id=std::max(id,m.id);};scan(*layer_);for(const auto& l:undo_)scan(*l);for(const auto& l:redo_)scan(*l);
            if(id>=1000000)fail("Trop de repères (1000 maximum).");next->markers.push_back({id+1,a.at,a.name});}
        std::sort(next->markers.begin(),next->markers.end(),[](auto& a,auto& b){return std::tie(a.time,a.id)<std::tie(b.time,b.id);});break;
    }
    case K::add:case K::modify:{
        if(a.pitch<0||a.pitch>127||a.velocity<1||a.velocity>127||!std::isfinite(a.start)||!std::isfinite(a.end)||
           a.start<0||a.end<=a.start||a.end>1200||a.end-a.start<.001)
            fail("Note invalide : hauteur 21–108, vélocité 1–127, durée positive (maximum 20 minutes).");
        Note n;if(a.kind==K::modify)n=find(a.id);else{++nextManual_;n.id=manualBase+nextManual_;n.part=a.part;}
        n.pitch=a.pitch;n.velocity=a.velocity;n.start=static_cast<float>(a.start);n.end=static_cast<float>(a.end);
        n.velocityNormalised=static_cast<float>(a.velocity)/127.0f;n.velocitySource=VelocitySource::manual;remember(n);break;
    }
    case K::remove:
        if(a.ids.empty()||std::any_of(a.ids.begin(),a.ids.end(),[&](NoteId id){return std::none_of(current.begin(),current.end(),[&](const Note& n){return n.id==id;});}))
            fail("Sélection obsolète ou vide.");
        for(auto id:a.ids)suppress(find(id));break;
    case K::restore:
        if(a.ids.empty())fail("Sélectionnez une note.");for(auto id:a.ids)next->manual.erase(id);break;
    case K::split:{
        auto left=find(a.id);
        if(!std::isfinite(a.at)||a.at-static_cast<double>(left.start)<.01||static_cast<double>(left.end)-a.at<.01)
            fail("Placez la coupure à au moins 10 ms des deux bords.");
        if(nextManual_>=1000000||next->manual.size()>=99999)fail("Trop de corrections manuelles.");
        auto right=left;left.end=right.start=static_cast<float>(a.at);right.sourceIds.clear();right.id=manualBase+(++nextManual_);
        if(next->muted.count(a.id))next->muted.insert(right.id);remember(left);remember(right);break;
    }
    case K::extend:{auto n=find(a.id);if(!std::isfinite(a.end)||a.end<=n.end||a.end>1200)fail("Prolongation invalide.");
        n.end=static_cast<float>(a.end);remember(n);break;}
    case K::moveGroup:{
        auto ids=unique(a.ids,current);if(!std::isfinite(a.seconds))fail("Sélection vide, dupliquée ou obsolète.");
        for(const auto& n:current)if(ids.count(n.id)){
            const auto pitch=static_cast<std::int64_t>(n.pitch)+a.semitones;
            if(pitch<0||pitch>127||static_cast<double>(n.start)+a.seconds<-1e-7||static_cast<double>(n.end)+a.seconds>1200)fail("Déplacement hors des limites MIDI.");
        }
        if(a.seconds==0&&a.semitones==0)return;
        for(auto n:current)if(ids.count(n.id)){n.start=static_cast<float>(std::max(0.,n.start+a.seconds));n.end=static_cast<float>(n.end+a.seconds);n.pitch+=a.semitones;remember(n);}break;
    }
    case K::toggleMute:{auto ids=unique(a.ids,current);bool on=std::any_of(ids.begin(),ids.end(),[&](NoteId id){return !next->muted.count(id);});
        for(auto id:ids)if(on)next->muted.insert(id);else next->muted.erase(id);break;}
    case K::togglePartMute:{
        auto ps=parts(source);if(std::none_of(ps.begin(),ps.end(),[&](const Part& p){return p.key()==a.partKey;}))fail("Choisissez une partie existante.");
        if(!next->mutedParts.erase(a.partKey))next->mutedParts.insert(a.partKey);break;
    }
    case K::merge:{
        std::set<NoteId> ids(a.ids.begin(),a.ids.end());if(ids.size()<2||ids.size()!=a.ids.size())fail("Sélectionnez au moins deux notes distinctes à fusionner.");
        if(std::count_if(current.begin(),current.end(),[&](const Note& n){return ids.count(n.id)>0;})!=static_cast<std::ptrdiff_t>(ids.size()))
            fail("Sélection obsolète. Sélectionnez à nouveau les notes.");
        std::vector<Note> selected;for(const auto& n:current)if(ids.count(n.id))selected.push_back(n);
        auto n=selected.front();for(const auto& x:selected)if(x.pitch!=n.pitch||x.key()!=n.key())fail("Fusion impossible : sélectionnez des notes de même hauteur et de même partie MIDI.");
        float end=0;for(const auto& x:selected)end=std::max(end,x.end);
        for(const auto& x:current)if(!ids.count(x.id)&&x.key()==n.key()&&x.pitch==n.pitch&&x.start<end-.000001f&&x.end>n.start+.000001f)
            fail("Fusion impossible : une note intermédiaire n’est pas sélectionnée.");
        if(nextManual_>=1000000)fail("Trop de notes manuelles dans ce projet.");
        n.end=end;std::set<NoteId> ancestors;bool allMuted=true;
        for(const auto& x:selected){n.endFrame=std::max(n.endFrame,x.endFrame);ancestors.insert(x.sourceIds.begin(),x.sourceIds.end());allMuted&=next->muted.count(x.id)>0;suppress(x);}
        n.frameDuration=n.endFrame>=n.startFrame?n.endFrame-n.startFrame:0;n.sourceIds.assign(ancestors.begin(),ancestors.end());
        n.id=manualBase+(++nextManual_);n.velocitySource=VelocitySource::manual;if(allMuted)next->muted.insert(n.id);remember(n);break;
    }
    case K::link:case K::unlink:{
        if(a.kind==K::link&&(!std::isfinite(a.overlapMs)||a.overlapMs<0||a.overlapMs>500))fail("Chevauchement invalide (0 à 500 ms).");
        auto ids=unique(a.ids,current);std::map<PartId,std::vector<Note>> lanes;for(const auto& n:current)lanes[n.key()].push_back(n);
        bool any=false;
        for(auto& lane:lanes){auto& ns=lane.second;std::stable_sort(ns.begin(),ns.end(),[](const Note& x,const Note& y){return x.start<y.start;});
            std::size_t group=0;while(group<ns.size()){
                auto following=group+1;while(following<ns.size()&&ns[following].start<=static_cast<double>(ns[group].start)+.001)++following;
                if(following==ns.size())break;
                double attack=ns[following].start;auto finish=following+1;while(finish<ns.size()&&ns[finish].start<=attack+.001)++finish;
                if(std::any_of(ns.begin()+following,ns.begin()+finish,[&](const Note& n){return ids.count(n.id)>0;}))
                    for(auto i=group;i<following;++i)if(ids.count(ns[i].id)){
                        auto n=ns[i];double end=a.kind==K::link?std::max(attack+a.overlapMs/1000.,static_cast<double>(n.end)):std::min(attack,static_cast<double>(n.end));
                        if(end>1200)fail("La liaison dépasserait 20 minutes.");
                        if(std::abs(end-n.end)>1e-6){n.end=static_cast<float>(end);remember(n);any=true;}
                    }
                group=following;
            }
        }
        if(!any)return;break;
    }
    case K::transfer:{
        std::set<NoteId> ids(a.ids.begin(),a.ids.end());if(ids.empty()||ids.size()!=a.ids.size())fail("Sélection vide ou dupliquée.");
        auto target=choosePart(a.program);
        if(std::count_if(current.begin(),current.end(),[&](const Note& n){return ids.count(n.id)>0;})!=static_cast<std::ptrdiff_t>(ids.size()))
            fail("Sélection obsolète. Sélectionnez à nouveau les notes.");
        bool any=false;
        for(auto n:current)if(ids.count(n.id)&&n.key()!=a.program){for(auto id:n.sourceIds)next->manual[id]=std::nullopt;n.part=target;n.velocitySource=VelocitySource::manual;remember(n);any=true;}
        if(!any)return;break;
    }
    case K::createEmptyPart:{
        auto name=trim(a.name);if(name.empty()||name.size()>256)fail("Nom de partie invalide.");
        if(a.program>128)fail("Instrument MIDI invalide.");if(a.colour>=12)fail("Couleur de voix invalide.");
        if(next->parts.size()>=512)fail("Maximum de 512 parties atteintes.");
        Part p{freshPartId(source),a.program,a.program==128,name};next->parts[p.key()]=p;next->partColours[p.key()]=a.colour;break;
    }
    case K::setPartColour:{
        choosePart(a.partKey,"Partie introuvable.");if(a.colour>=12)fail("Couleur de voix invalide.");
        if(next->lockedParts.count(a.partKey))fail("Une voix est verrouillée. Déverrouillez-la avant de la modifier.");
        auto old=next->partColours.find(a.partKey);if(old!=next->partColours.end()&&old->second==a.colour)return;
        next->partColours[a.partKey]=a.colour;break;
    }
    case K::newPart:{
        auto name=trim(a.name);if(name.empty()||name.size()>256)fail("Nom de partie invalide.");
        auto ids=unique(a.ids,current);if(next->parts.size()>=512)fail("Maximum de 512 parties atteintes.");
        auto first=std::find_if(current.begin(),current.end(),[&](const Note& n){return ids.count(n.id)>0;});
        auto program=first->part?first->part->program:0;Part p{freshPartId(source),static_cast<PartId>(program),program==128,name};next->parts[*p.identity]=p;
        for(auto n:current)if(ids.count(n.id)){n.part=p;for(auto id:n.sourceIds)next->manual[id]=std::nullopt;remember(n);}break;
    }
    case K::deletePart:{
        choosePart(a.partKey,"Partie introuvable.");
        // Muted and low-confidence notes still belong to the part.
        auto all=source;if(!source.allNotes.empty())all.notes=source.allNotes;auto every=resolve(all);
        for(const auto& n:every)if(n.key()==a.partKey){
            suppress(n);next->muted.erase(n.id);next->dynamics.erase(n.id);
        }
        // Retired overrides may still carry this identity (e.g. before a merge).
        // Remove that metadata too so persistence never references a removed part.
        for(auto& entry:next->manual)if(entry.second){auto key=entry.second->key();auto alias=layer_->parts.find(key);
            if((alias==layer_->parts.end()?key:alias->second.key())==a.partKey)entry.second=std::nullopt;
        }
        next->audioSources.erase(a.partKey);next->partColours.erase(a.partKey);next->deletedParts.insert(a.partKey);next->masks.erase(a.partKey);next->mutedParts.erase(a.partKey);
        for(auto it=next->parts.begin();it!=next->parts.end();){
            if(it->second.key()==a.partKey){next->deletedParts.insert(it->first);it=next->parts.erase(it);}else ++it;
        }break;
    }
    case K::togglePartLock:{
        choosePart(a.partKey,"Partie introuvable.");
        if(!next->lockedParts.erase(a.partKey))next->lockedParts.insert(a.partKey);break;
    }
    case K::setInstrument:{
        if(a.program>128)fail("Instrument MIDI invalide.");auto p=choosePart(a.partKey,"Partie introuvable.");if(p.program==a.program&&p.soundfontPath==a.soundfontPath&&p.soundfontHash==a.soundfontHash&&p.soundfontBank==a.soundfontBank&&p.soundfontPreset==a.soundfontPreset)return;
        auto origin=a.partKey;for(const auto& v:next->parts)if(v.second.key()==a.partKey){origin=v.first;break;}
        if(!p.identity)p.identity=freshPartId(source);
        p.soundfontPath=a.soundfontPath;p.soundfontHash=a.soundfontHash;p.soundfontBank=a.soundfontBank;p.soundfontPreset=a.soundfontPreset;
        p.program=a.program;p.drum=a.program==128;if(!a.keepName)p.label=p.program<source.gmNames.size()?source.gmNames[p.program]:"Instrument GM "+std::to_string(p.program+1);
        if(p.key()!=a.partKey){auto it=next->audioSources.find(a.partKey);if(it!=next->audioSources.end()){next->audioSources[p.key()]=it->second;next->audioSources.erase(it);}}
        if(p.key()!=a.partKey&&next->mutedParts.erase(a.partKey))next->mutedParts.insert(p.key());
        if(p.key()!=a.partKey){auto colour=next->partColours.find(a.partKey);if(colour!=next->partColours.end()){next->partColours[p.key()]=colour->second;next->partColours.erase(colour);}}
        if(p.key()!=a.partKey){auto oldMask=next->masks.find(a.partKey);if(oldMask!=next->masks.end()){next->masks[p.key()]=oldMask->second;next->masks.erase(oldMask);}}
        next->parts[origin]=p;for(auto& v:next->manual)if(v.second&&v.second->key()==a.partKey)v.second->part=p;break;
    }
    case K::maskStroke:{
        if(a.partKey!=allMaskParts&&!validKey(a.partKey))fail("Partie du masque invalide.");
        if(a.partKey!=allMaskParts)choosePart(a.partKey,"Partie du masque introuvable.");
        validateMaskStroke(a.stroke);auto& mask=next->masks[a.partKey];
        if(mask.empty()&&a.stroke.erase)return;
        mask.push_back(std::make_shared<const MaskStroke>(a.stroke));validateMask(mask);if(next->masks.size()>513)fail("Trop de masques.");break;
    }
    case K::clearMask:{if(!next->masks.erase(a.partKey))return;break;}
    case K::pasteNotes:case K::duplicateNotes:{
        auto imported=a.imported;
        const bool duplicate=a.kind==K::duplicateNotes;
        if(duplicate){auto ids=unique(a.ids,current);if(!std::isfinite(a.seconds))fail("Déplacement hors des limites MIDI.");if(a.seconds==0&&a.semitones==0)return;
            for(auto n:current)if(ids.count(n.id)){
                const auto pitch=std::int64_t(n.pitch)+a.semitones;if(pitch<0||pitch>127)fail("Déplacement hors des limites MIDI.");
                n.pitch=int(pitch);n.muted=layer_->muted.count(n.id)>0;imported.push_back(n);
            }
        }
        if(!std::isfinite(a.at)||a.at<0||a.at>1200||imported.empty()||imported.size()>100000||
           nextManual_+imported.size()>1000000||next->manual.size()+imported.size()>100000)
            fail("Collage vide, trop volumineux ou position invalide.");
        auto existing=parts(source);
        auto destination=[&](PartId key)->std::optional<Part>{
            auto it=std::find_if(existing.begin(),existing.end(),[&](const Part& p){return p.key()==key;});
            if(it!=existing.end())return *it;
            if(existing.empty()&&key==0&&!next->deletedParts.count(0))return {};
            fail("La partie d’origine n’existe plus. Sélectionnez une partie destination.");
        };
        double first=std::numeric_limits<double>::infinity();
        for(const auto& n:imported){
            if(!std::isfinite(n.start)||!std::isfinite(n.end)||n.start<0||n.end<=n.start||n.end>1200||
               n.pitch<0||n.pitch>127||n.velocity<1||n.velocity>127)
                fail("Notes copiées invalides.");
            first=std::min(first,static_cast<double>(n.start));
        }
        const auto shift=duplicate?a.seconds:a.at-first;
        for(auto n:imported){
            auto p=destination(a.pastePart.value_or(n.key()));
            const double start=n.start+shift,end=n.end+shift;
            if(start<0||end>1200||end<=start)fail("Le collage dépasse la durée maximale de 20 minutes.");
            n.start=static_cast<float>(start);n.end=static_cast<float>(end);
            if(n.end<=n.start)fail("Note trop courte à cette position de collage.");
            n.id=manualBase+(++nextManual_);n.sourceIds.clear();n.part=p;
            n.startFrame=n.endFrame=n.frameDuration=0;
            n.velocityNormalised=static_cast<float>(n.velocity)/127.f;n.velocitySource=VelocitySource::manual;
            if(n.muted)next->muted.insert(n.id);remember(n);
        }break;
    }
    case K::appendMidi:{
        if(!std::isfinite(a.seconds)||a.imported.empty()||a.imported.size()>100000||nextManual_+a.imported.size()>1000000||next->manual.size()+a.imported.size()>100000)
            fail("Import trop volumineux ou décalage invalide.");
        std::map<PartId,Part> sources;for(const auto& n:a.imported){
            if(!std::isfinite(n.start)||!std::isfinite(n.end)||n.start+a.seconds<0||n.end+a.seconds>1200||n.end<=n.start||n.pitch<0||n.pitch>127||n.velocity<1||n.velocity>127)
                fail("Le décalage place des notes avant zéro ou après 20 minutes.");
            if(!n.part)fail("Partie source absente.");sources.emplace(n.key(),*n.part);
        }
        // Instrument changes retain a source-key alias: reserve both the alias
        // and its effective identity, including undo/redo and deleted parts.
        auto existing=parts(source);auto used=reservedPartIds(source);
        std::map<PartId,Part> targets;std::vector<Part> created;
        for(const auto& m:a.mappings){auto i=sources.find(m.source);if(i==sources.end())fail("Partie source obsolète.");if(targets.count(m.source))fail("Affectation dupliquée.");
            Part p;if(m.target){auto at=std::find_if(existing.begin(),existing.end(),[&](const Part& v){return v.key()==*m.target;});if(at==existing.end())fail("Voix destination obsolète.");p=*at;}
            else{auto name=trim(m.name);auto program=m.program.value_or(i->second.program);if(name.empty()||name.size()>256||program>128)fail("Nom de voix ou instrument invalide.");
                unsigned id=256;while(id<32768&&used.count(static_cast<PartId>(id)))++id;if(id==32768)fail("Trop de voix MIDI.");used.insert(static_cast<PartId>(id));
                p=Part{static_cast<PartId>(id),program,program==128,name};created.push_back(p);
                if(!m.audioSource.empty()){if(m.audioSource.size()>32768)fail("Source audio invalide.");next->audioSources[p.key()]=m.audioSource;}}
            targets[m.source]=p;
        }
        if(targets.size()!=sources.size()||existing.size()+created.size()>512)fail("Affectez chaque partie et limitez le projet à 512 voix.");
        for(auto n:a.imported){auto part=targets.at(n.key());n.id=manualBase+(++nextManual_);n.sourceIds.clear();n.muted=false;n.part=part;
            n.start=static_cast<float>(n.start+a.seconds);n.end=static_cast<float>(n.end+a.seconds);if(n.end<=n.start)fail("Note trop courte après décalage.");remember(n);}
        for(const auto& p:created)next->parts[*p.identity]=p;break;
    }
    case K::setAudioSource:{choosePart(a.partKey);if(a.name.empty()||a.name.size()>32768)fail("Source audio invalide.");next->audioSources[a.partKey]=a.name;break;}
    case K::replaceVoiceNotes:{
        auto destination=choosePart(a.partKey);if(next->lockedParts.count(a.partKey))fail("Une voix est verrouillée. Déverrouillez-la avant de la modifier.");
        if(a.imported.size()>100000||nextManual_+a.imported.size()>1000000)fail("Import trop volumineux ou décalage invalide.");
        auto all=source;all.notes=source.allNotes;std::set<NoteId> removed;
        for(const auto& n:resolve(all))if(n.key()==a.partKey){removed.insert(n.id);for(auto id:n.sourceIds)removed.insert(id);if(n.id>manualBase&&n.sourceIds.empty())next->manual.erase(n.id);else suppress(n);}
        for(auto id:removed){next->dynamics.erase(id);next->muted.erase(id);}
        for(auto n:a.imported){if(!std::isfinite(n.start)||!std::isfinite(n.end)||n.start<0||n.end>1200||n.end<=n.start||n.pitch<0||n.pitch>127||n.velocity<1||n.velocity>127)fail("Notes Basic Pitch invalides.");
            n.id=manualBase+(++nextManual_);n.sourceIds.clear();n.part=destination;n.muted=false;remember(n);}
        if(next->manual.size()>100000)fail("Corrections trop volumineuses.");break;
    }
    case K::resetClean:next->automaticMerges.clear();next->automaticMetadata.clear();break;
    case K::resetDynamics:next->dynamics.clear();next->dynamicsMapping.reset();break;
    case K::undo:case K::redo:break;
    }
    checkpoint(std::move(next));
}
} // namespace phthongos
