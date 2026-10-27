#include "DocumentJson.h"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace phthongos::project {
namespace {
using juce::var;using juce::String;
[[noreturn]] void bad() {throw std::invalid_argument("Métadonnées du document invalides.");}
var obj(std::initializer_list<std::pair<const char*,var>> fields){auto* o=new juce::DynamicObject;for(auto& f:fields)o->setProperty(f.first,f.second);return var(o);}
var integer(std::uint64_t n){if(n>static_cast<std::uint64_t>(INT64_MAX))bad();return var(static_cast<juce::int64>(n));}
std::uint64_t uint(const var& v,std::uint64_t max=INT64_MAX){if(!v.isInt()&&!v.isInt64())bad();auto n=static_cast<juce::int64>(v);if(n<0||static_cast<std::uint64_t>(n)>max)bad();return static_cast<std::uint64_t>(n);}
std::uint64_t mapKey(const juce::Identifier& key,std::uint64_t max=INT64_MAX){auto s=key.toString().toStdString();if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)bad();std::size_t used=0;auto n=std::stoull(s,&used);if(used!=s.size()||n>max)bad();return n;}
double real(const var& v){if(!v.isInt()&&!v.isInt64()&&!v.isDouble())bad();auto n=static_cast<double>(v);if(!std::isfinite(n))bad();return n;}
bool boolean(const var& v){if(!v.isBool())bad();return static_cast<bool>(v);}
std::string text(const var& v){if(!v.isString())bad();return v.toString().toStdString();}
const juce::Array<var>& array(const var& v,std::size_t max=100000){auto* a=v.getArray();if(!a||static_cast<std::size_t>(a->size())>max)bad();return *a;}
const juce::NamedValueSet& fields(const var& v,std::size_t max=100000){auto* o=v.getDynamicObject();if(!o||static_cast<std::size_t>(o->getProperties().size())>max)bad();return o->getProperties();}
template<class T,class F>var list(const T& xs,F f){juce::Array<var> a;for(auto& x:xs)a.add(f(x));return var(a);}
Part readPartImpl(const var& v){fields(v);Part p;p.program=static_cast<PartId>(uint(v["program"],128));p.drum=boolean(v["is_drum"]);p.label=text(v["label"]);if(v.hasProperty("identity")&&!v["identity"].isVoid())p.identity=static_cast<PartId>(uint(v["identity"],65535));if(v.hasProperty("soundfont")){auto sf=v["soundfont"];p.soundfontPath=text(sf["path"]);p.soundfontHash=text(sf["sha256"]);p.soundfontBank=unsigned(uint(sf["bank"],16383));p.soundfontPreset=unsigned(uint(sf["preset"],127));if(!juce::File::isAbsolutePath(String::fromUTF8(p.soundfontPath.c_str()))||p.soundfontPath.size()>32768||p.soundfontHash.size()!=64)bad();}return p;}
var writePartImpl(const Part& p){auto r=obj({{"program",(int)p.program},{"is_drum",p.drum},{"label",String::fromUTF8(p.label.c_str())}});if(p.identity)r.getDynamicObject()->setProperty("identity",(int)*p.identity);if(!p.soundfontPath.empty())r.getDynamicObject()->setProperty("soundfont",obj({{"path",String::fromUTF8(p.soundfontPath.c_str())},{"sha256",String(p.soundfontHash)},{"bank",int(p.soundfontBank)},{"preset",int(p.soundfontPreset)}}));return r;}
Mapping readMapping(const var& v){Mapping m;m.level=real(v["level"]);m.contrast=real(v["contrast"]);m.min=static_cast<int>(uint(v["min"],255));m.max=static_cast<int>(uint(v["max"],255));return m;}
var writeMapping(const Mapping& m){return obj({{"level",m.level},{"contrast",m.contrast},{"min",m.min},{"max",m.max}});}
Fingerprint readFingerprint(const var& v){Fingerprint f;f.id=uint(v["id"]);f.pitch=static_cast<int>(uint(v["pitch"],127));f.program=static_cast<PartId>(uint(v["program"],128));f.start=static_cast<float>(real(v["start"]));f.end=static_cast<float>(real(v["end"]));if(v.hasProperty("part")&&!v["part"].isVoid())f.part=static_cast<PartId>(uint(v["part"],65535));return f;}
var writeFingerprint(const Fingerprint& f){auto r=obj({{"id",integer(f.id)},{"pitch",f.pitch},{"program",(int)f.program},{"start",(double)f.start},{"end",(double)f.end}});if(f.part)r.getDynamicObject()->setProperty("part",(int)*f.part);return r;}
void scope(const var& s,int depth=0){if(depth>64)bad();auto kind=text(s["kind"]);if(kind=="all")return;
    if(kind=="part"){auto p=uint(s["program"],32767);if(p>128&&p<256)bad();scope(s["scope"],depth+1);return;}
    if(kind=="notes"){auto& ids=array(s["ids"]);if(ids.isEmpty())bad();for(auto& i:ids)uint(i);return;}
    if(kind=="region"){auto a=real(s["start"]),b=real(s["end"]);auto low=uint(s["low"],127),high=uint(s["high"],127);if(a<0||b<=a||low>high)bad();return;}bad();}
void settings(const var& s){auto gap=real(s["gap_ms"]),sustain=real(s["sustain_min"]);uint(s["velocity_delta"],126);if(gap<0||gap>250||sustain<0||sustain>1)bad();}
Layer readLayer(const var& v){fields(v);Layer l;
    if(v.hasProperty("markers"))for(const auto& m:array(v["markers"],1000))l.markers.push_back({uint(m["id"],1000000),real(m["time"]),text(m["name"])});
    if(v.hasProperty("spectral_masks"))for(auto& entry:fields(v["spectral_masks"],513)){auto key=static_cast<PartId>(mapKey(entry.name,65535));auto& mask=l.masks[key];for(auto& stroke:array(entry.value,1000))mask.push_back(std::make_shared<const MaskStroke>(readMaskStroke(stroke)));validateMask(mask);}

    if(v.hasProperty("audio_sources"))for(auto& p:fields(v["audio_sources"],512)){auto path=text(p.value);if(path.empty()||path.size()>32768||!juce::File::isAbsolutePath(String::fromUTF8(path.c_str())))bad();l.audioSources.emplace(static_cast<PartId>(mapKey(p.name,32767)),std::move(path));}
    if(v.hasProperty("parts"))for(auto& p:fields(v["parts"],512))l.parts.emplace(static_cast<PartId>(mapKey(p.name,65535)),readPart(p.value));
    if(v.hasProperty("muted"))for(auto& n:array(v["muted"]))l.muted.insert(uint(n));
    if(v.hasProperty("deleted_parts"))for(auto& n:array(v["deleted_parts"],32768))l.deletedParts.insert(static_cast<PartId>(uint(n,65535)));
    if(v.hasProperty("part_colours"))for(auto& p:fields(v["part_colours"],512))l.partColours.emplace(static_cast<PartId>(mapKey(p.name,32767)),static_cast<unsigned>(uint(p.value,11)));
    if(v.hasProperty("locked_parts"))for(auto& n:array(v["locked_parts"],512))l.lockedParts.insert(static_cast<PartId>(uint(n,32767)));
    if(v.hasProperty("muted_parts"))for(auto& n:array(v["muted_parts"],512))l.mutedParts.insert(static_cast<PartId>(uint(n,65535)));
    if(v.hasProperty("dynamics"))for(auto& d:fields(v["dynamics"])){AppliedDynamics a;a.note=readFingerprint(d.value["note"]);a.score=real(d.value["score"]);a.velocity=static_cast<int>(uint(d.value["velocity"],255));a.mapping=readMapping(d.value["mapping"]);if(d.value.hasProperty("override_manual"))a.overrideManual=boolean(d.value["override_manual"]);l.dynamics.emplace(mapKey(d.name),a);}
    if(v.hasProperty("dynamics_mapping")&&!v["dynamics_mapping"].isVoid())l.dynamicsMapping=readMapping(v["dynamics_mapping"]);
    for(auto& batch:array(v["automatic"])){scope(batch["_scope"]);settings(batch["_settings"]);std::vector<std::pair<NoteId,NoteId>> merges;
        for(auto& pair:array(batch["merges"])){auto& a=array(pair,2);if(a.size()!=2)bad();merges.emplace_back(uint(a[0]),uint(a[1]));}
        l.automaticMerges.push_back(std::move(merges));l.automaticMetadata.push_back(juce::JSON::toString(obj({{"_scope",batch["_scope"]},{"_settings",batch["_settings"]}}),true,17).toStdString());}
    for(auto& n:fields(v["manual"]))l.manual.emplace(mapKey(n.name),n.value.isVoid()?std::optional<Note>{}:readNote(n.value));return l;
}
var writeLayer(const Layer& l){auto parts=obj({}),manual=obj({}),dynamics=obj({});
    for(auto& [k,p]:l.parts)parts.getDynamicObject()->setProperty(String(k),writePart(p));
    for(auto& [id,n]:l.manual)manual.getDynamicObject()->setProperty(String(id),n?writeNote(*n):var());
    for(auto& [id,d]:l.dynamics){auto entry=obj({{"note",writeFingerprint(d.note)},{"score",d.score},{"velocity",d.velocity},{"mapping",writeMapping(d.mapping)}});if(d.overrideManual)entry.getDynamicObject()->setProperty("override_manual",true);dynamics.getDynamicObject()->setProperty(String(id),entry);}
    juce::Array<var> automatic;for(std::size_t i=0;i<l.automaticMerges.size();++i){if(i>=l.automaticMetadata.size())bad();auto batch=juce::JSON::parse(String::fromUTF8(l.automaticMetadata[i].c_str()));
        batch.getDynamicObject()->setProperty("merges",list(l.automaticMerges[i],[](auto p){juce::Array<var> a;a.add(integer(p.first));a.add(integer(p.second));return var(a);}));automatic.add(batch);}
    auto result=obj({{"parts",parts},{"muted",list(l.muted,integer)},{"muted_parts",list(l.mutedParts,[](auto p){return var((int)p);})},{"dynamics",dynamics},{"dynamics_mapping",l.dynamicsMapping?writeMapping(*l.dynamicsMapping):var()},{"automatic",var(automatic)},{"manual",manual}});
    if(!l.audioSources.empty()){auto paths=obj({});for(auto& p:l.audioSources)paths.getDynamicObject()->setProperty(String(p.first),String::fromUTF8(p.second.c_str()));result.getDynamicObject()->setProperty("audio_sources",paths);}
    if(!l.markers.empty())result.getDynamicObject()->setProperty("markers",list(l.markers,[](auto& m){return obj({{"id",integer(m.id)},{"time",m.time},{"name",String::fromUTF8(m.name.c_str())}});}));
    if(!l.masks.empty())result.getDynamicObject()->setProperty("spectral_masks",writeMasks(l.masks));
    if(!l.partColours.empty()){auto colours=obj({});for(auto& p:l.partColours)colours.getDynamicObject()->setProperty(String(p.first),int(p.second));result.getDynamicObject()->setProperty("part_colours",colours);}
    if(!l.lockedParts.empty())result.getDynamicObject()->setProperty("locked_parts",list(l.lockedParts,[](auto p){return var((int)p);}));
    if(!l.deletedParts.empty())result.getDynamicObject()->setProperty("deleted_parts",list(l.deletedParts,[](auto p){return var((int)p);}));
    return result;
}
void checkFingerprint(const var& v){auto n=readFingerprint(v);if(!n.id||n.start<0||n.end<=n.start||!std::isfinite(n.start)||!std::isfinite(n.end)||(n.part&&(*n.part<256||*n.part>=32768)))bad();}
void cache(const var& v){if(v.isVoid())return;fields(v);if(v.hasProperty("version"))uint(v["version"],1);
    for(auto& f:array(v["fingerprints"]))checkFingerprint(f);
    for(auto& e:array(v["estimates"])){checkFingerprint(e["note"]);for(auto field:{"harmonic","attack","relative","score"})if(real(e[field])<0)bad();if(real(e["score"])>1)bad();boolean(e["usable"]);}}
}
Part readPart(const var& value){return readPartImpl(value);}
var writePart(const Part& value){return writePartImpl(value);}
MaskStroke readMaskStroke(const var& value){fields(value);MaskStroke s;s.erase=boolean(value["erase"]);s.timeRadius=real(value["time_radius"]);s.pitchRadius=real(value["pitch_radius"]);for(auto& p:array(value["points"],2048)){auto& pair=array(p,2);if(pair.size()!=2)bad();s.points.push_back({real(pair[0]),real(pair[1])});}validateMaskStroke(s);return s;}
var writeMaskStroke(const MaskStroke& s){return obj({{"erase",s.erase},{"time_radius",s.timeRadius},{"pitch_radius",s.pitchRadius},{"points",list(s.points,[](auto p){juce::Array<var> pair;pair.add(p.time);pair.add(p.pitch);return var(pair);})}});}
var writeMasks(const std::map<PartId,SpectralMask>& masks){auto result=obj({});for(auto& m:masks)result.getDynamicObject()->setProperty(String(m.first),list(m.second,[](auto& s){return writeMaskStroke(*s);}));return result;}
Action readAction(const var& v) {
    using K=Action::Kind;
    const std::map<std::string,K> kinds={{"add",K::add},{"modify",K::modify},{"delete",K::remove},{"restore",K::restore},
        {"move_group",K::moveGroup},{"merge",K::merge},{"split",K::split},{"extend",K::extend},{"link",K::link},{"unlink",K::unlink},
        {"toggle_mute",K::toggleMute},{"toggle_part_mute",K::togglePartMute},{"toggle_part_lock",K::togglePartLock},{"transfer",K::transfer},{"new_part",K::newPart},{"create_empty_part",K::createEmptyPart},{"set_part_colour",K::setPartColour},
        {"add_marker",K::addMarker},{"modify_marker",K::modifyMarker},{"delete_marker",K::deleteMarker},{"mask_stroke",K::maskStroke},{"clear_mask",K::clearMask},{"delete_part",K::deletePart},{"set_instrument",K::setInstrument},{"duplicate_notes",K::duplicateNotes},{"paste_notes",K::pasteNotes},{"append_midi",K::appendMidi},{"replace_voice_notes",K::replaceVoiceNotes},{"set_audio_source",K::setAudioSource},{"undo",K::undo},{"redo",K::redo},{"reset_clean",K::resetClean},{"reset_dynamics",K::resetDynamics}};
    auto it=kinds.find(text(v["kind"]));if(it==kinds.end())bad();Action a;a.kind=it->second;
    if(v.hasProperty("id"))a.id=uint(v["id"]);
    if(v.hasProperty("ids"))for(auto& n:array(v["ids"]))a.ids.push_back(uint(n));
    if(v.hasProperty("pitch"))a.pitch=static_cast<int>(uint(v["pitch"],INT_MAX));
    if(v.hasProperty("velocity"))a.velocity=static_cast<int>(uint(v["velocity"],255));
    if(v.hasProperty("start"))a.start=real(v["start"]);if(v.hasProperty("end"))a.end=real(v["end"]);
    if(v.hasProperty("at"))a.at=real(v["at"]);if(v.hasProperty("seconds"))a.seconds=real(v["seconds"]);
    if(v.hasProperty("semitones")){auto n=real(v["semitones"]);if(n<INT_MIN||n>INT_MAX||n!=std::floor(n))bad();a.semitones=static_cast<int>(n);}
    if(v.hasProperty("overlap_ms"))a.overlapMs=real(v["overlap_ms"]);
    if(v.hasProperty("program"))a.program=static_cast<PartId>(uint(v["program"],65535));
    if(v.hasProperty("name"))a.name=text(v["name"]);
    if(v.hasProperty("soundfont")&&!v["soundfont"].isVoid()){auto sf=v["soundfont"];a.soundfontPath=text(sf["path"]);a.soundfontHash=text(sf["sha256"]);a.soundfontBank=unsigned(uint(sf["bank"],16383));a.soundfontPreset=unsigned(uint(sf["preset"],127));if(!juce::File::isAbsolutePath(String::fromUTF8(a.soundfontPath.c_str()))||a.soundfontPath.size()>32768||a.soundfontHash.size()!=64)bad();}
    if(v.hasProperty("keep_name"))a.keepName=boolean(v["keep_name"]);
    if(v.hasProperty("colour"))a.colour=static_cast<unsigned>(uint(v["colour"],11));
    if(!v["part"].isVoid()){if(v["part"].isObject())a.part=readPart(v["part"]);else a.partKey=static_cast<PartId>(uint(v["part"],65535));}
    if(a.kind==K::maskStroke)a.stroke=readMaskStroke(v["stroke"]);
    if(a.kind==K::replaceVoiceNotes){for(auto& n:array(v["notes"]))a.imported.push_back(readNote(n));}
    if(a.kind==K::pasteNotes){
        if(!v["part"].isVoid())a.pastePart=static_cast<PartId>(uint(v["part"],32767));
        for(auto& n:array(v["notes"]))a.imported.push_back(readNote(n));
    }
    if(a.kind==K::appendMidi){a.seconds=real(v["offset"]);for(auto& n:array(v["notes"]))a.imported.push_back(readNote(n));
        for(auto& m:array(v["mappings"])){MidiMapping p;p.source=static_cast<PartId>(uint(m["source"],65535));
            if(!m["target"].isVoid())p.target=static_cast<PartId>(uint(m["target"],65535));
            if(!m["program"].isVoid())p.program=static_cast<PartId>(uint(m["program"],65535));
            if(m.hasProperty("name"))p.name=text(m["name"]);a.mappings.push_back(std::move(p));}}
    return a;
}
var writeView(const View& v){auto value=obj({{"notes",list(v.notes,writeNote)},{"muted_parts",list(v.mutedParts,[](auto p){return var((int)p);})},
    {"muted_notes",list(v.mutedNotes,integer)},{"dynamics_mapping",v.dynamicsMapping?writeMapping(*v.dynamicsMapping):var()},
    {"revision",integer(v.revision)},{"can_undo",v.canUndo},{"can_redo",v.canRedo},{"protected_retained",integer(v.protectedRetained)},{"has_changes",v.hasChanges}});if(!v.partColours.empty()){auto colours=obj({});for(auto& p:v.partColours)colours.getDynamicObject()->setProperty(String(p.first),int(p.second));value.getDynamicObject()->setProperty("part_colours",colours);}if(!v.lockedParts.empty())value.getDynamicObject()->setProperty("locked_parts",list(v.lockedParts,[](auto p){return var((int)p);}));if(!v.markers.empty())value.getDynamicObject()->setProperty("markers",list(v.markers,[](auto& m){return obj({{"id",integer(m.id)},{"time",m.time},{"name",String::fromUTF8(m.name.c_str())}});}));if(!v.masks.empty())value.getDynamicObject()->setProperty("spectral_masks",writeMasks(v.masks));if(!v.audioSources.empty()){auto paths=obj({});for(auto& p:v.audioSources)paths.getDynamicObject()->setProperty(String(p.first),String::fromUTF8(p.second.c_str()));value.getDynamicObject()->setProperty("audio_sources",paths);}return value;}
analysis::DynamicsCache readDynamicsCache(const var& v) {
    analysis::DynamicsCache out;
    if (v.isVoid() || v.isUndefined()) { out.version = 0; return out; }
    fields(v); out.version = v.hasProperty("version") ? static_cast<unsigned>(uint(v["version"], UINT32_MAX)) : 0;
    for (const auto& f : array(v["fingerprints"])) out.fingerprints.push_back(readFingerprint(f));
    for (const auto& e : array(v["estimates"])) out.estimates.push_back({readFingerprint(e["note"]),
        real(e["harmonic"]), real(e["attack"]), real(e["relative"]), real(e["score"]), boolean(e["usable"])});
    out.validate(); return out;
}
var writeDynamicsCache(const analysis::DynamicsCache& c) {
    return obj({{"version",static_cast<int>(c.version)}, {"fingerprints",list(c.fingerprints,writeFingerprint)},
        {"estimates",list(c.estimates,[](const auto& e){return obj({{"note",writeFingerprint(e.note)},
            {"harmonic",e.harmonic},{"attack",e.attack},{"relative",e.relative},{"score",e.score},{"usable",e.usable}});})}});
}
std::optional<CleanPreview> readCleanPreview(const var& v) {
    if (v.isVoid() || v.isUndefined()) return {};
    fields(v); scope(v["scope"]); settings(v["settings"]); CleanPreview out;
    out.revision=uint(v["revision"]);out.threshold=static_cast<float>(real(v["threshold"]));
    out.before=uint(v["before"],100000);out.after=uint(v["after"],100000);
    for (const auto& run : array(v["runs"])) {
        CleanRun r;for (const auto& id : array(run["ids"])) r.ids.push_back(uint(id));
        if(r.ids.empty())bad();r.proposed=boolean(run["proposed"]);out.runs.push_back(std::move(r));
    }
    for (const auto& edge : array(v["proposals"])) out.proposals.emplace_back(uint(edge["left"]),uint(edge["right"]));
    out.metadata=juce::JSON::toString(obj({{"_scope",v["scope"]},{"_settings",v["settings"]}}),true,17).toStdString();
    return out;
}
Note readNote(const var& v){fields(v);Note n;n.id=uint(v["id"]);if(!v["part"].isVoid())n.part=readPart(v["part"]);
    n.pitch=static_cast<int>(uint(v["pitch_midi"],127));n.velocity=static_cast<int>(uint(v["velocity_midi"],127));n.start=static_cast<float>(real(v["start_time_s"]));n.end=static_cast<float>(real(v["end_time_s"]));
    n.velocityNormalised=static_cast<float>(real(v["velocity_normalized"]));n.onsetProbability=static_cast<float>(real(v["ons_prob"]));n.startFrame=uint(v["start_fr"]);n.endFrame=uint(v["end_fr"]);n.frameDuration=uint(v["num_frame_duration"]);
    for(auto& p:array(v["pitch_bend_samples"],1000000)){auto d=real(p);if(d!=std::floor(d)||d< -128||d>127)bad();n.pitchBend.push_back(static_cast<std::int8_t>(d));}
    for(auto& p:array(v["source_ids"],1000000))n.sourceIds.push_back(uint(p));n.protectedNote=boolean(v["protected"]);if(v.hasProperty("muted"))n.muted=boolean(v["muted"]);
    auto vs=v.hasProperty("velocity_source")?text(v["velocity_source"]):"detected";if(vs=="detected")n.velocitySource=VelocitySource::detected;else if(vs=="manual")n.velocitySource=VelocitySource::manual;else if(vs=="audioEstimated")n.velocitySource=VelocitySource::audioEstimated;else bad();return n;
}
var writeNote(const Note& n){return obj({{"id",integer(n.id)},{"part",n.part?writePart(*n.part):var()},{"pitch_midi",n.pitch},{"velocity_midi",n.velocity},{"start_time_s",(double)n.start},{"end_time_s",(double)n.end},{"velocity_normalized",(double)n.velocityNormalised},{"ons_prob",(double)n.onsetProbability},{"start_fr",integer(n.startFrame)},{"end_fr",integer(n.endFrame)},{"num_frame_duration",integer(n.frameDuration)},{"pitch_bend_samples",list(n.pitchBend,[](auto p){return var((int)p);})},{"source_ids",list(n.sourceIds,integer)},{"protected",n.protectedNote},{"muted",n.muted},{"velocity_source",n.velocitySource==VelocitySource::detected?"detected":n.velocitySource==VelocitySource::manual?"manual":"audioEstimated"}});}
Editor readEditor(const var& v,const SourceSnapshot& source){fields(v);Document::State state;state.layer=readLayer(v["layer"]);state.revision=uint(v["revision"]);state.nextManual=uint(v["next_manual"],1000000);
    for(auto& l:array(v["undo"],100))state.undo.push_back(std::make_shared<const Layer>(readLayer(l)));
    for(auto& l:array(v["redo"],100))state.redo.push_back(std::make_shared<const Layer>(readLayer(l)));
    cache(v["dynamics_cache"]);return {Document::restore(std::move(state),source),v["dynamics_cache"].clone()};
}
var writeEditor(const Editor& e){auto state=e.document.state();for(auto* history:{&state.undo,&state.redo})if(history->size()>Document::historyLimit)history->erase(history->begin(),history->end()-Document::historyLimit);return obj({{"dynamics_cache",e.dynamicsCache.clone()},{"layer",writeLayer(state.layer)},{"undo",list(state.undo,[](auto& l){return writeLayer(*l);})},{"redo",list(state.redo,[](auto& l){return writeLayer(*l);})},{"revision",integer(state.revision)},{"next_manual",integer(state.nextManual)}});}
}
