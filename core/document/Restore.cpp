#include "Document.h"
#include "Text.h"
#include <cmath>
#include <stdexcept>
namespace phthongos {
namespace {
[[noreturn]] void invalid(const char* s) { throw std::invalid_argument(s); }
bool key(PartId p) {return p<=128||(p>=256&&p<32768);}
void mapping(const Mapping& m) {
    if(m.min<1||m.min>m.max||m.max>127||!std::isfinite(m.level)||m.level<m.min||m.level>m.max||!std::isfinite(m.contrast)||m.contrast<0||m.contrast>250)
        invalid("Dynamique : niveau dans la plage min–max (1–127), contraste 0–250 %.");
}
}
Document Document::restore(State state,const SourceSnapshot& source) {
    if(state.undo.size()>100||state.redo.size()>100||state.nextManual>1000000)invalid("Historique trop volumineux.");
    auto sourceCount=source.sourceCount?source.sourceCount:source.allNotes.size();
    auto validId=[&](NoteId id){return id>0&&(id<=sourceCount||(id>manualBase&&id<=manualBase+state.nextManual));};
    std::set<PartId> sourceParts;for(auto& n:source.allNotes)if(n.part&&n.part->identity)sourceParts.insert(*n.part->identity);
    auto validate=[&](const Layer& layer){
        if(layer.markers.size()>1000)invalid("Trop de repères (1000 maximum).");std::set<std::uint64_t> markerIds;
        for(const auto& m:layer.markers)if(!m.id||m.id>1000000||!markerIds.insert(m.id).second||!std::isfinite(m.time)||m.time<0||m.time>1200||m.name.size()>256)invalid("Repère invalide.");
        if(layer.masks.size()>513)invalid("Trop de masques enregistrés.");for(auto& m:layer.masks){if(m.first!=allMaskParts&&!key(m.first))invalid("Partie du masque invalide.");validateMask(m.second);}
        auto allowed=sourceParts;std::set<PartId> identities;
        if(layer.parts.size()>512)invalid("Trop de parties.");
        for(auto& [k,p]:layer.parts){if(!key(k)||p.program>128||p.drum!=(p.program==128)||detail::trim(p.label).empty()||p.label.size()>256||!p.identity||*p.identity<256||*p.identity>=32768||!identities.insert(*p.identity).second)invalid("Métadonnées de partie invalides.");allowed.insert(*p.identity);}
        if(layer.audioSources.size()>512)invalid("Source audio invalide.");for(const auto& p:layer.audioSources)if(!key(p.first)||p.second.empty()||p.second.size()>32768)invalid("Source audio invalide.");
        for(auto& entry:layer.parts){auto& p=entry.second;if(p.soundfontPath.size()>32768||(!p.soundfontPath.empty()&&(p.soundfontHash.size()!=64||p.soundfontBank>16383||p.soundfontPreset>127)))invalid("Déclaration SF2 invalide.");}
        if(layer.partColours.size()>512)invalid("Couleurs de voix invalides.");
        for(auto& p:layer.partColours)if(!key(p.first)||p.second>=12||(p.first>=256&&!allowed.count(p.first))||layer.deletedParts.count(p.first))invalid("Couleurs de voix invalides.");
        if(layer.deletedParts.size()>32768)invalid("Parties supprimées invalides.");
        for(auto p:layer.deletedParts)if(!key(p))invalid("Parties supprimées invalides.");
        if(layer.lockedParts.size()>512)invalid("Verrous de voix invalides.");
        for(auto p:layer.lockedParts)if(!key(p)||(p>=256&&!allowed.count(p)))invalid("Verrous de voix invalides.");
        if(layer.mutedParts.size()>512)invalid("Parties muettes invalides.");
        for(auto p:layer.mutedParts)if(!key(p)||(p>=256&&!allowed.count(p)))invalid("Parties muettes invalides.");
        if(layer.muted.size()>100000)invalid("Notes muettes invalides.");for(auto id:layer.muted)if(!validId(id))invalid("Notes muettes invalides.");
        if(layer.manual.size()>100000||layer.automaticMerges.size()>100000)invalid("Corrections trop volumineuses.");
        if(layer.dynamicsMapping)mapping(*layer.dynamicsMapping);
        if(layer.dynamics.size()>100000)invalid("Dynamique trop volumineuse.");
        for(auto& [id,d]:layer.dynamics){mapping(d.mapping);auto& n=d.note;
            if(!validId(id)||id!=n.id||d.velocity<1||d.velocity>127||!std::isfinite(d.score)||d.score<0||d.score>1||!std::isfinite(n.start)||!std::isfinite(n.end)||n.start<0||n.end<=n.start||n.pitch<0||n.pitch>127||n.program>128)invalid("Dynamique enregistrée invalide.");}
        for(auto& batch:layer.automaticMerges)for(auto [a,b]:batch)if(!validId(a)||!validId(b)||a==b)invalid("Fusion invalide.");
        for(auto& [id,opt]:layer.manual){if(!validId(id))invalid("Identifiant invalide.");if(!opt)continue;auto& n=*opt;
            if(n.part&&(n.part->program>128||n.part->drum!=(n.part->program==128)||n.part->label.size()>256||(n.part->identity&&!allowed.count(*n.part->identity))))invalid("Correction manuelle invalide.");
            if(n.id!=id||!n.protectedNote||n.pitch<0||n.pitch>127||n.velocity<1||n.velocity>127||!std::isfinite(n.start)||!std::isfinite(n.end)||n.start<0||n.end<=n.start||n.end>1200.001||!std::isfinite(n.velocityNormalised)||!std::isfinite(n.onsetProbability))invalid("Correction manuelle invalide.");
            for(auto ancestor:n.sourceIds)if(ancestor==0||ancestor>sourceCount)invalid("Correction manuelle invalide.");
        }
    };
    validate(state.layer);for(auto& l:state.undo){if(!l)invalid("Historique invalide.");validate(*l);}for(auto& l:state.redo){if(!l)invalid("Historique invalide.");validate(*l);}
    // Older PHT files may retain 100 snapshots. Validate them before keeping
    // the 25 nearest states; the current layer and counters stay unchanged.
    for(auto* history:{&state.undo,&state.redo})if(history->size()>historyLimit)history->erase(history->begin(),history->end()-historyLimit);
    Document doc;doc.layer_=std::make_shared<const Layer>(std::move(state.layer));doc.undo_=std::move(state.undo);doc.redo_=std::move(state.redo);doc.revision_=state.revision;doc.nextManual_=state.nextManual;
    doc.view(source);return doc;
}
}
