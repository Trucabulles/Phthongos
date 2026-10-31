#include "Desktop.h"
#include "../midi/Tempo.h"
#include "../midi/TempoEstimate.h"
#include "Resources.h"
#include "Inference.h"
#include "BasicPitchInference.h"
#include "Readers.h"
#include "Cleanup.h"
#include "Spectrogram.h"
#include "Acoustic.h"
#include "Verification.h"
#include "GuidedPitch.h"
#include "../project/Prepared.h"
#include "../project/Validation.h"
#include "../audio/Player.h"
#include "../audio/Import.h"
#include "../audio/Resample.h"
#include "../analysis/MidiReaders.h"
#include <algorithm>
#include <juce_cryptography/juce_cryptography.h>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
namespace phthongos::services {
namespace {
using juce::var;using juce::String;using juce::File;
var obj(std::initializer_list<std::pair<const char*,var>> xs){auto* p=new juce::DynamicObject;for(auto& x:xs)p->setProperty(x.first,x.second);return var(p);}
var integer(std::uint64_t x){return var(static_cast<juce::int64>(x));}
bool nil(const var& v){return v.isVoid()||v.isUndefined();}
std::uint64_t uint(const var& v){if((!v.isInt()&&!v.isInt64())||(juce::int64)v<0)throw std::invalid_argument("Identifiant invalide.");return std::uint64_t((juce::int64)v);}
double number(const var& v){if(!v.isInt()&&!v.isInt64()&&!v.isDouble())throw std::invalid_argument("Valeur numérique invalide.");auto x=(double)v;if(!std::isfinite(x))throw std::invalid_argument("Valeur numérique invalide.");return x;}
std::vector<var> items(const var& v){if(auto* a=v.getArray())return std::vector<var>(a->begin(),a->end());return {};}
void set(var o,const char* key,var value){o.getDynamicObject()->setProperty(key,value);}
var part(const Part& p){return project::writePart(p);}
var bytes(const std::vector<std::uint8_t>& input){juce::Array<var> out;out.ensureStorageAllocated(int(input.size()));for(auto b:input)out.add(int(b));return var(out);}
std::vector<std::uint8_t> readBytes(const File& file,std::uint64_t maximum){if(!file.existsAsFile()||file.getSize()<0||std::uint64_t(file.getSize())>maximum)throw std::invalid_argument("Fichier absent ou trop volumineux.");juce::MemoryBlock data;if(!file.loadFileAsData(data))throw std::runtime_error("Lecture du fichier impossible.");auto* p=static_cast<const std::uint8_t*>(data.getData());return {p,p+data.getSize()};}
void writeBytes(const File& file,const std::vector<std::uint8_t>& data){auto out=file.createOutputStream();if(!out||!out->write(data.data(),data.size()))throw std::runtime_error("Écriture du fichier impossible.");out->flush();if(out->getStatus().failed())throw std::runtime_error("Écriture du fichier impossible.");}
var sourceJson(const midi::Source& source){auto value=obj({{"source_path",String::fromUTF8(source.path.c_str())},{"name",String::fromUTF8(source.name.c_str())},{"bytes",bytes(source.bytes)}});if(source.createdEmpty)set(value,"created_empty",true);return value;}
var noteArray(const std::vector<Note>& notes){juce::Array<var> out;for(auto& n:notes)out.add(project::writeNote(n));return var(out);}
var plainNotes(const std::vector<Note>& notes){juce::Array<var> out;for(auto& n:notes){auto v=project::writeNote(n);for(auto* key:{"id","part","source_ids","protected","velocity_source","muted"})v.getDynamicObject()->removeProperty(key);out.add(v);}return var(out);}
float threshold(const var& args){auto sensitivity=args.hasProperty("sensitivity")&&!nil(args["sensitivity"])?number(args["sensitivity"]):100.;if(sensitivity<0||sensitivity>100)throw std::invalid_argument("La sensibilité doit être comprise entre 0 et 100 %.");return float(1-sensitivity/100.);}
std::optional<PartId> chosenPart(const var& args){if(nil(args["part"]))return {};auto value=uint(args["part"]);if(value>32767||(value>128&&value<256))throw std::invalid_argument("Partie MIDI invalide.");return PartId(value);}
std::optional<std::vector<NoteId>> chosenIds(const var& args){if(nil(args["ids"]))return {};if(!args["ids"].isArray())throw std::invalid_argument("Sélection invalide.");std::vector<NoteId> out;for(auto& i:items(args["ids"]))out.push_back(uint(i));return out;}
Mapping mapping(const var& value){Mapping m;m.level=number(value["level"]);m.contrast=number(value["contrast"]);m.min=int(uint(value["min"]));m.max=int(uint(value["max"]));analysis::validateMapping(m);return m;}
var origin(const audio::ImportedAudio& a){return obj({{"sha256",fileHash(a.pcm)},{"name",a.original.getFileName()}});}
void noOverwrite(const File& destination,const File& audio,const std::optional<midi::Source>& source){if(audio!=File()&&destination==audio)throw std::invalid_argument("L’audio source ne peut pas être remplacé.");if(source&&!source->path.empty()&&destination==File(String::fromUTF8(source->path.c_str())))throw std::invalid_argument("Le MIDI source ne peut pas être remplacé ; choisissez un nouvel export.");if(destination.exists())throw std::invalid_argument("Ce fichier existe déjà. Choisissez un autre nom pour préserver l’export précédent.");}
}
struct Desktop::Impl {
 File runtime,cache,data,piano;Banks banks;Inference inference;BasicPitchInference basic;audio::Player player;
 std::mutex mutex,eventMutex,gridMutex;std::vector<var> events;
 std::atomic_bool guidedCancel{false},guidedRunning{false};var guidedReport;std::uint64_t guidedCounter=0;std::optional<PartId> guidedDestination;
 std::atomic_bool verificationCancel{false},verificationRunning{false};String gridKey;std::shared_ptr<const VerificationGrid> grid;
 std::uint64_t audioId=0,analysisId=0,nextToken=0,pendingToken=0,pendingAudio=0,pendingAnalysis=0,pendingRevision=0;
 double fixedBpm=0;std::uint64_t tempoRevision=0;String sourceRuntimeHash;
 bool hasAudio=false,midiOnly=false,busy=false;audio::ImportedAudio audio;var audioJson,mu,bp,profile,clean,voiceTranscriptions=obj({}),measurements=obj({}),candidates=var(juce::Array<var>{});
 std::array<std::vector<std::uint8_t>,3> maps;std::size_t mapFrames=0;
 project::SourceKind kind=project::SourceKind::none;SourceSnapshot source;project::Editor editor;
 std::optional<midi::Source> midi;std::vector<Note> pendingNotes;File lastExport,protectedProject;
 Impl(File r,File c):runtime(std::move(r)),cache(std::move(c)),data(cache.getParentDirectory()),piano(runtime.getSiblingFile("player-assets/piano.sf2")),banks(piano,data,
    data==runtime.getSiblingFile("data")&&!runtime.getSiblingFile("portable.flag").existsAsFile()?File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("Phthongos/soundbank-settings.json"):File(),
    data==runtime.getSiblingFile("data")&&!runtime.getSiblingFile("portable.flag").existsAsFile()?File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("player-assets/MuseScore_General_HQ.sf2"):File()),inference(runtime,data),basic(runtime),player(piano,runtime.getChildFile("research_speex.dll"),[this]{return banks.selected();}){
  auto names=juce::JSON::parse(piano.getSiblingFile("gmPrograms.json").loadFileAsString());if(!names.isArray()||names.size()!=129)throw std::runtime_error("Noms d’instruments absents.");for(auto& n:items(names))source.gmNames.push_back(n.toString().toStdString());
 }
 struct Snapshot {std::uint64_t audioId=0,analysisId=0,revision=0;float threshold=0;double duration=0;bool midiOnly=false;audio::ImportedAudio audio;std::vector<Note> notes;var cache;std::set<PartId> lockedParts;};
 std::optional<Part> auditionVoice(const var& args){std::lock_guard<std::mutex> lock(mutex);if(nil(args["voice"]))return {};int key=int(args["voice"]);for(auto& p:editor.document.view(source).parts)if(p.key()==key)return p;return {};}
 void require(const var& args,bool analysis=true,bool revision=false){if(busy)throw std::invalid_argument("Analyse en cours.");if(!hasAudio)throw std::invalid_argument("Aucun audio chargé.");if(uint(args["audioId"])!=audioId)throw std::invalid_argument("Audio obsolète.");if(analysis&&(kind==project::SourceKind::none||uint(args["analysisId"])!=analysisId))throw std::invalid_argument("La transcription a changé. Rechargez la sélection.");if(revision&&uint(args["revision"])!=editor.document.revision())throw std::invalid_argument("La transcription a changé. Rechargez la sélection.");}
 void filter(float t){if(kind==project::SourceKind::legacy){source.notes.clear();for(auto& n:source.allNotes)if(n.onsetProbability>=t)source.notes.push_back(n);}}

 static String voiceSignature(const std::vector<Note>& notes,int key){juce::Array<var> selected;for(const auto& n:notes)if(n.key()==key)selected.add(project::writeNote(n));auto json=juce::JSON::toString(var(selected),true,17);return juce::SHA256(json.toRawUTF8(),json.getNumBytesAsUTF8()).toHexString();}
 int effectiveVoice(int key)const{auto state=editor.document.state();auto it=state.layer.parts.find(PartId(key));return it==state.layer.parts.end()?key:int(it->second.key());}
 String voiceCacheKey(int key)const{if(auto* o=voiceTranscriptions.getDynamicObject())for(auto& p:o->getProperties())if(effectiveVoice(p.name.toString().getIntValue())==key)return p.name.toString();return {};}
 var voiceSummary(const phthongos::View& view)const{auto out=obj({});if(auto* o=voiceTranscriptions.getDynamicObject())for(auto& p:o->getProperties()){
  int key=effectiveVoice(p.name.toString().getIntValue());if(std::none_of(view.parts.begin(),view.parts.end(),[key](auto& part){return part.key()==key;}))continue;
  auto value=p.value;out.getDynamicObject()->setProperty(String(key),obj({{"engine",value["engine"]},{"settings",value["settings"].clone()},{"source_path",value["source_path"]},{"has_changes",value["signature"].toString()!=voiceSignature(view.notes,key)}}));
 }return out;}
 void registerPrimaryVoice(){
  if(kind!=project::SourceKind::basicPitch)return;auto state=editor.document.state();std::set<int> keys;for(auto& n:source.notes)keys.insert(n.key());
  auto path=bp["source_path"].toString();if(keys.empty()){keys.insert(256);state.layer.parts.emplace(256,Part{PartId(256),0,false,"Piano"});}for(int key:keys){
   if(path.isNotEmpty())state.layer.audioSources[PartId(key)]=path.toStdString();
   voiceTranscriptions.getDynamicObject()->setProperty(String(key),obj({{"engine","basic_pitch"},{"settings",bp["settings"].clone()},{"source_path",path},{"audio_sha256",bp["audio_sha256"]},{"signature",voiceSignature(source.notes,key)},{"primary",true}}));
  }editor.document=Document::restore(std::move(state),source);
 }
 var appendTranscription(std::vector<Note> notes,const String& path,var cacheEntry=var()){
  if(notes.empty())throw std::invalid_argument("Aucune note détectée : aucune voix ajoutée.");auto before=editor.document.view(source);std::set<int> existing;for(auto& p:before.parts)existing.insert(p.key());
  std::map<int,Part> incoming;for(auto& n:notes)incoming.emplace(n.key(),n.part.value_or(Part{{},0,false,"Piano"}));
  Action action;action.kind=Action::Kind::appendMidi;action.imported=std::move(notes);
  for(auto& p:incoming){MidiMapping map;map.source=PartId(p.first);map.program=p.second.program;map.audioSource=path.toStdString();map.name=File(path).getFileNameWithoutExtension().substring(0,80).toStdString();if(incoming.size()>1)map.name+=" — "+p.second.label;action.mappings.push_back(std::move(map));}
  auto staged=editor;staged.document.act(source,staged.document.revision(),action);auto view=staged.document.view(source);auto caches=voiceTranscriptions.clone();juce::Array<var> added;
  for(auto& p:view.parts)if(!existing.count(p.key())){added.add(int(p.key()));if(!nil(cacheEntry)){auto entry=cacheEntry.clone();set(entry,"signature",voiceSignature(view.notes,p.key()));caches.getDynamicObject()->setProperty(String(p.key()),entry);}}
  editor=std::move(staged);voiceTranscriptions=caches;clean=var();auto value=result(0);set(value,"transcribed_parts",var(added));return value;
 }
 var result(float t){filter(t);auto v=editor.document.view(source);juce::Array<var> parts;for(auto& p:v.parts)parts.add(part(p));double duration=editor.document.timelineDuration(audio.duration);for(auto& n:v.notes)duration=std::max(duration,double(n.end));auto value=obj({{"audio_id",integer(audioId)},{"analysis_id",integer(analysisId)},{"threshold",double(t)},{"sensitivity",(1-double(t))*100},{"notes",plainNotes(v.notes)},{"editing",project::writeView(v)},{"duration",duration},{"engine",kind==project::SourceKind::midi?"midi":kind==project::SourceKind::muscriptor?"muscriptor":kind==project::SourceKind::basicPitch?"basic_pitch":"legacy"},{"instrument_target",kind==project::SourceKind::muscriptor?(nil(mu["instrument_target"])?var("all"):mu["instrument_target"]):var()},{"model",kind==project::SourceKind::basicPitch?var("basic_pitch"):mu["model"]},{"basic_pitch",bp.clone()},{"device",mu["raw"]["device"]},{"midi_parts",var(parts)}});set(value,"voice_transcriptions",voiceSummary(v));if(midi)set(value,"midi_name",String::fromUTF8(midi->name.c_str()));return value;}
 Snapshot snapshot(const var& args,bool revision=true){std::lock_guard<std::mutex> lock(mutex);require(args,true,revision);float t=threshold(args);filter(t);return {audioId,analysisId,editor.document.revision(),t,audio.duration,midiOnly,audio,editor.document.view(source).notes,editor.dynamicsCache.clone(),editor.document.state().layer.lockedParts};}
 void current(const Snapshot& s){if(!hasAudio||audioId!=s.audioId||analysisId!=s.analysisId||editor.document.revision()!=s.revision)throw std::invalid_argument("La session a changé pendant le calcul.");}
 void reset(){sourceRuntimeHash.clear();fixedBpm=0;++tempoRevision;guidedReport=var();guidedDestination.reset();protectedProject=File();auto names=source.gmNames;source={};source.gmNames=std::move(names);editor={};midi.reset();mu=var();bp=var();voiceTranscriptions=obj({});profile=var();clean=var();measurements=obj({});mapFrames=0;maps={};candidates=var(juce::Array<var>{});pendingNotes.clear();pendingToken=0;kind=project::SourceKind::none;}
 void useMidi(midi::Source value){auto parsed=value.parse(source.gmNames);source.notes=parsed.notes;source.allNotes=parsed.notes;source.sourceCount=parsed.notes.size();midi=std::move(value);kind=project::SourceKind::midi;}
 void midiAudio(const String& name,double duration){midiOnly=true;audio={};audio.duration=std::max(duration,.1);if(audio.duration>1200)throw std::invalid_argument("Durée MIDI invalide (maximum 20 minutes).");audioJson=obj({{"midi_only",true},{"id",integer(audioId)},{"name",name},{"path",""},{"duration",audio.duration},{"sample_rate",0},{"channels",0},{"peaks",var(juce::Array<var>{})},{"format",String::fromUTF8("MIDI · sans audio")},{"notice",String::fromUTF8("Associez un audio source pour activer les analyses acoustiques.")}});}
 var dynamical(const Snapshot& s,const var& args,bool cached){auto cache=project::readDynamicsCache(s.cache);auto ids=analysis::scope(s.notes,chosenIds(args),chosenPart(args));std::set<NoteId> chosen(ids.begin(),ids.end());juce::Array<var> estimates;int protectedNotes=0,percussion=0,unmeasurable=0;
  auto encoded=project::writeDynamicsCache(cache);auto all=items(encoded["estimates"]);for(auto& n:s.notes)if(chosen.count(n.id)){if(n.part&&n.part->program==128){++percussion;continue;}if(s.lockedParts.count(n.key())){++protectedNotes;continue;}bool found=false;for(std::size_t i=0;i<cache.estimates.size();++i)if(cache.estimates[i].usable&&analysis::sameFingerprint(cache.estimates[i].note,analysis::fingerprint(n))){estimates.add(all[i]);found=true;break;}if(!found)++unmeasurable;}
  var references=obj({});{std::lock_guard<std::mutex> lock(mutex);current(s);for(auto& n:source.notes)references.getDynamicObject()->setProperty(String(static_cast<juce::int64>(n.id)),n.velocity);}
  return obj({{"reference_velocities",references},{"revision",integer(s.revision)},{"cached",cached},{"estimates",var(estimates)},{"protected",protectedNotes},{"percussion",percussion},{"unmeasurable",unmeasurable}});
 }
 var request(const String&,const var&);
};
Desktop::Desktop(juce::File runtime,juce::File cache):impl(std::make_unique<Impl>(std::move(runtime),std::move(cache))){}
Desktop::~Desktop()=default;
juce::var Desktop::request(const juce::String& cmd,const juce::var& args){return impl->request(cmd,args);}
juce::var Desktop::Impl::request(const juce::String& command,const juce::var& args){
 if(command=="estimate_notes_tempo"){
  std::vector<midi::TempoOnset> onsets;auto key=chosenPart(args);String scope;
  {std::lock_guard<std::mutex> lock(mutex);require(args,true,true);filter(threshold(args));auto view=editor.document.view(source);
   if(key){auto found=std::find_if(view.parts.begin(),view.parts.end(),[&](const Part& p){return p.key()==*key;});if(found==view.parts.end())throw std::invalid_argument("Partie introuvable.");scope=String::fromUTF8(found->label.c_str());}
   // Individual note exclusions affect detection; playback mute/visibility does not.
   for(const auto& n:view.notes)if((!key||n.key()==*key)&&!view.mutedNotes.count(n.id))onsets.push_back({n.start,n.pitch,n.velocity});
  }
  auto estimate=midi::estimateTempo(onsets);juce::Array<var> candidates,histogram;
  for(auto c:estimate.candidates)candidates.add(obj({{"bpm",c.bpm},{"score",c.score}}));for(auto weight:estimate.histogram)histogram.add(weight);
  return obj({{"bpm",estimate.bpm},{"support",estimate.support},{"attacks",int(estimate.attacks)},{"seconds",estimate.seconds},{"reason",String(estimate.reason)},{"candidates",var(candidates)},{"histogram",var(histogram)},{"shortestSeconds",estimate.shortestSeconds},{"sampleRate",int(estimate.sampleRate)},{"engine","midi-bpm-cpp/e14a86c"},{"source","notes"},{"part",key?var(int(*key)):var()},{"scope_label",scope}});
 }
 if(command=="project_tempo"||command=="set_project_tempo"){
  std::lock_guard<std::mutex> lock(mutex);require(args,false);
  if(command=="set_project_tempo"){
   auto bpm=number(args["bpm"]);if(bpm!=0)midi::validateTempo(bpm);
   if(fixedBpm!=bpm){fixedBpm=bpm;++tempoRevision;}
  }
  double initial=120;bool variable=false;
  juce::Array<var> tempoMap;std::map<double,var> meterMap;
  meterMap[0]=obj({{"beat",0.},{"numerator",4},{"denominator",4}});
  if(midi){auto parsed=midi->parse(source.gmNames);if(!(parsed.division&32768)){
   std::map<std::uint64_t,midi::Clock::Point> effective;
   for(auto& point:parsed.clock.points)effective[point.tick]=point;
   auto step=effective.begin()->second.step;initial=60./(step*parsed.division);
   for(auto& entry:effective){auto& point=entry.second;
    if(point.tick>0&&std::abs(point.step-step)>1e-12)variable=true;
    if(!fixedBpm)tempoMap.add(obj({{"beat",double(point.tick)/parsed.division},{"seconds",point.seconds},{"quarter_seconds",point.step*parsed.division}}));
   }
   std::vector<midi::Event> signatures;
   for(auto& track:parsed.tracks)for(auto& event:track)if(event.isMeta(88)&&event.data.size()==4&&event.data[0]>0&&event.data[1]<=7)signatures.push_back(event);
   std::stable_sort(signatures.begin(),signatures.end(),[](auto& a,auto& b){return a.tick<b.tick;});
   for(auto& event:signatures){double beat=fixedBpm?parsed.clock.seconds(event.tick)*fixedBpm/60.:double(event.tick)/parsed.division;
    meterMap[beat]=obj({{"beat",beat},{"numerator",int(event.data[0])},{"denominator",1<<event.data[1]}});
   }
  }}
  if(tempoMap.isEmpty())tempoMap.add(obj({{"beat",0.},{"seconds",0.},{"quarter_seconds",60./(fixedBpm?fixedBpm:initial)}}));
  juce::Array<var> meterMapJson;for(auto& entry:meterMap)meterMapJson.add(entry.second);
  return obj({{"bpm",fixedBpm?fixedBpm:initial},{"fixed",fixedBpm!=0},{"source_bpm",initial},{"variable",variable},{"has_midi",midi.has_value()&&!midi->createdEmpty},{"tempo_map",var(tempoMap)},{"meter_map",var(meterMapJson)}});
 }
 if(command=="guided_cancel"){guidedCancel=true;return {};}
 if(command=="guided_analyze"){
  if(guidedRunning.exchange(true))throw std::invalid_argument("Une analyse guidée est déjà en cours.");struct Running{std::atomic_bool& flag;~Running(){flag=false;}} running{guidedRunning};guidedCancel=false;
  auto s=snapshot(args);if(s.midiOnly)throw std::invalid_argument("Associez un audio source avant l’analyse guidée.");
  int low=int(uint(args["low"])),high=int(uint(args["high"]));if(low<0||high>127||low>high)throw std::invalid_argument("Registre invalide.");bool masked=(bool)args["useMask"];auto destination=chosenPart(args);PartId key=destination.value_or(allMaskParts);SpectralMask mask;
  {std::lock_guard<std::mutex> lock(mutex);current(s);auto state=editor.document.state();auto i=state.layer.masks.find(key);if(i!=state.layer.masks.end())mask=i->second;guidedReport=var();}
  double a=number(args["start"]),b=number(args["end"]);if((bool)args["maskExtent"]&&masked){a=s.duration;b=0;for(auto& stroke:mask)if(!stroke->erase)for(auto& p:stroke->points){a=std::min(a,p.time-stroke->timeRadius);b=std::max(b,p.time+stroke->timeRadius);}a=std::max(0.,a);b=std::min(s.duration,b);}
  auto pcm=audio::readStereo(s.audio.pcm,22050,runtime.getChildFile("research_speex.dll"));
  GuidedOptions options;if((bool)args["useScale"])options.scale=readScale(args["scale"]);
  auto report=guidedPitch(pcm,22050,a,b,low,high,mask,masked,guidedCancel,[this,id=s.audioId](double progress){std::lock_guard<std::mutex> lock(eventMutex);events.push_back(obj({{"event","guided-progress"},{"payload",obj({{"audio_id",integer(id)},{"progress",progress}})}}));},options);
  std::lock_guard<std::mutex> lock(mutex);current(s);if(guidedCancel.load())throw std::invalid_argument("Analyse guidée annulée.");set(report,"token",integer(++guidedCounter));set(report,"audio_id",integer(s.audioId));set(report,"analysis_id",integer(s.analysisId));set(report,"revision",integer(s.revision));set(report,"part",destination?var(int(*destination)):var());guidedDestination=destination;guidedReport=report.clone();return report;
 }
 if(command=="guided_accept"||command=="guided_ignore"){
  std::lock_guard<std::mutex> lock(mutex);require(args,true,true);if(nil(guidedReport)||uint(args["token"])!=uint(guidedReport["token"])||audioId!=uint(guidedReport["audio_id"])||analysisId!=uint(guidedReport["analysis_id"])||editor.document.revision()!=uint(guidedReport["revision"]))throw std::invalid_argument("Propositions périmées : relancez l’analyse guidée.");
  std::set<String> chosen;for(auto& id:items(args["ids"]))if(!id.isString()||!chosen.insert(id.toString()).second)throw std::invalid_argument("Sélection de propositions invalide.");if(chosen.empty())throw std::invalid_argument("Sélectionnez une proposition.");
  auto currentNotes=editor.document.view(source).notes;std::vector<Note> proposed;juce::Array<var> remaining;std::size_t found=0,duplicates=0;
  for(auto& p:items(guidedReport["notes"]))if(chosen.count(p["id"].toString())){++found;Note n;n.pitch=int(uint(p["pitch"]));n.start=float(number(p["start"]));n.end=float(number(p["end"]));n.velocity=int(uint(p["velocity"]));n.part=Part{{},0,false,"Ligne guidée"};bool duplicate=false;if(guidedDestination)for(auto& old:currentNotes)if(old.key()==*guidedDestination&&old.pitch==n.pitch&&std::min(old.end,n.end)-std::max(old.start,n.start)>.7*std::min(old.end-old.start,n.end-n.start))duplicate=true;if(duplicate)++duplicates;else{proposed.push_back(n);}}else remaining.add(p);
  if(found!=chosen.size())throw std::invalid_argument("Proposition absente ou déjà traitée.");
  if(command=="guided_accept"&&!proposed.empty()){
    Action action;action.kind=Action::Kind::appendMidi;action.imported=std::move(proposed);MidiMapping route;route.source=0;route.target=guidedDestination;route.program=0;route.name="Ligne guidée";action.mappings={route};
    auto previousParts=editor.document.view(source).parts;editor.document.act(source,uint(args["revision"]),action);
    if(!guidedDestination)for(auto& p:editor.document.view(source).parts)if(std::none_of(previousParts.begin(),previousParts.end(),[&](auto& old){return p.key()==old.key();})){guidedDestination=p.key();break;}
  }
  set(guidedReport,"notes",var(remaining));set(guidedReport,"revision",integer(editor.document.revision()));return obj({{"result",result(threshold(args))},{"report",guidedReport.clone()},{"duplicates",integer(duplicates)}});
 }
 if(command=="project_save_info"){
  std::lock_guard<std::mutex> lock(mutex);if(!hasAudio)throw std::invalid_argument("Aucun projet chargé.");
  auto folder=audio.original!=File()?audio.original.getParentDirectory():File::getSpecialLocation(File::userDocumentsDirectory);
  auto name=File::getCurrentWorkingDirectory().getChildFile(audioJson["name"].toString()).getFileNameWithoutExtension();if(name.isEmpty())name="Phthongos";
  auto suggestion=protectedProject!=File()?protectedProject.getSiblingFile(protectedProject.getFileNameWithoutExtension()+"_PHT8.pht").getNonexistentSibling():folder.getChildFile(name+".pht");
  return obj({{"protected_path",protectedProject==File()?var():var(protectedProject.getFullPathName())},{"suggested_path",suggestion.getFullPathName()}});
 }
 if(command=="host_events"){std::lock_guard<std::mutex> lock(eventMutex);juce::Array<var> out;for(auto& e:events)out.add(e);events.clear();return var(out);}
 if(command=="muscriptor_status")return inference.status();
 if(command=="muscriptor_settings"){inference.configure(args["settings"]);return {};}
 if(command=="muscriptor_cancel"){inference.cancel();return {};}
 if(command=="soundbank_status")return banks.status();
 if(command=="soundbank_cancel"){banks.cancel();return {};}
 if(command=="third_model_declare"){inference.declareModel(args["model"].toString(),File(args["path"].toString()));return inference.status();}
 if(command=="third_model_download"){inference.download(args["model"].toString());return inference.status();}
 if(command=="third_model_cancel"){inference.cancelDownload();return {};}
 if(command=="soundfont_add"){auto list=banks.addCustom(File(args["path"].toString()));return banks.status();}
 if(command=="soundfont_remove"){banks.removeCustom(args["path"].toString());return banks.status();}
 if(command=="soundbank_select"){banks.select(nil(args["path"])?File():File(args["path"].toString()));player.reload();return banks.status();}
 if(command=="soundbank_download"){banks.download();player.reload();return banks.status();}
 if(command=="audio_outputs")return player.outputs();
 if(command=="audio_output_select"){player.select(args["id"]);return {};}
 if(command=="player_key"){auto pitch=int(args["pitch"]),program=int(args["program"]);auto db=double(args["db"]);if(pitch< -1||pitch>127||program<0||program>128||!std::isfinite(db)||db< -60||db>0)throw std::invalid_argument("Note de clavier invalide.");player.key(uint(args["audioId"]),pitch,unsigned(program),float(db),auditionVoice(args));return {};}
 if(command=="player_midi"){
  auto action=args["action"].toString();auto channel=int(args["channel"]),pitch=int(args["pitch"]),velocity=int(args["velocity"]),program=int(args["program"]);auto db=double(args["db"]);
  if((action!="note"&&action!="sustain"&&action!="panic")||channel<0||channel>15||pitch<0||pitch>127||velocity<0||velocity>127||program<0||program>128||!std::isfinite(db)||db< -60||db>0)throw std::invalid_argument("Message MIDI entrant invalide.");
  player.midi(uint(args["audioId"]),action,unsigned(channel),pitch,velocity,unsigned(program),float(db),auditionVoice(args));return {};
 }
 if(command=="player_voice_levels"){std::map<int,float> levels;if(auto* values=args["levels"].getDynamicObject())for(const auto& entry:values->getProperties()){auto db=double(entry.value);if(!std::isfinite(db)||db< -60||db>0)throw std::invalid_argument("Volume de voix invalide.");levels[entry.name.toString().getIntValue()]=float(db);}player.levels(uint(args["audioId"]),levels);return {};}
 if(command=="player_record")return player.record(uint(args["audioId"]),bool(args["start"]),nil(args["position"])?0:number(args["position"]),nil(args["preCount"])?0:unsigned(uint(args["preCount"])));
 if(command=="player_metronome"){
  rhythm::Grid clickMap;clickMap.origin=nil(args["origin"])?0:number(args["origin"]);clickMap.tempos.clear();clickMap.meters.clear();
  for(auto t:items(args["tempos"]))clickMap.tempos.push_back({number(t["beat"]),number(t["seconds"]),number(t["quarter"])});
  for(auto m:items(args["meters"]))clickMap.meters.push_back({number(m["beat"]),int(m["numerator"]),int(m["denominator"])});
  player.metronome(uint(args["audioId"]),bool(args["enabled"]),float(number(args["db"])),clickMap);return {};
 }
 if(command=="player_snapshot")return player.snapshot();
 if(command=="player_prepare"){audio::ImportedAudio a;bool only=false;std::uint64_t id=0;{std::lock_guard<std::mutex> lock(mutex);require(args,false);a=audio;only=midiOnly;id=audioId;}player.prepare(id,only?File():a.pcm,a.duration);return {};}
 if(command=="player_notes"){std::vector<Note> notes;std::uint64_t id=0;double end=0;{std::lock_guard<std::mutex> lock(mutex);require(args,false);id=audioId;if(!nil(args["analysisId"])){require(args);filter(threshold(args));end=editor.document.timelineDuration(audio.duration);auto chosen=chosenPart(args);for(auto& n:editor.document.view(source).notes){end=std::max(end,double(n.end));if(!n.muted&&(!chosen||n.key()==*chosen))notes.push_back(n);}}}player.notes(id,uint(args["version"]),notes,end);return {};}
 if(command=="player_initialize_position"){player.initialize(uint(args["audioId"]));return {};}
 if(command=="player_control"){player.control(uint(args["audioId"]),args["action"].toString(),nil(args["position"])?0:number(args["position"]));return {};}
 if(command=="player_mix"){player.mix(uint(args["audioId"]),unsigned(uint(args["mode"])),float(number(args["audioDb"])),float(number(args["midiDb"])));return {};}
 if(command=="player_reverb"){player.reverb(uint(args["audioId"]),number(args["amount"]));return {};}
 if(command=="player_audition"){player.audition(uint(args["audioId"]),int(uint(args["pitch"])),int(uint(args["velocity"])),nil(args["program"])?0:unsigned(uint(args["program"])),auditionVoice(args));return {};}
 if(command=="cancel_midi_verification"){verificationCancel=true;return {};}
 if(command=="open_audio"){
  auto staged=audio::importAudio(File(args["path"].toString()),cache);auto view=audio::audioView(staged,0);
  std::lock_guard<std::mutex> lock(mutex);if(busy)throw std::invalid_argument("Attendez la fin de l’analyse.");reset();++audioId;hasAudio=true;midiOnly=false;audio=std::move(staged);audioJson=view;set(audioJson,"id",integer(audioId));return audioJson.clone();
 }
 if(command=="import_midi_audio"){
  File path(args["midiPath"].toString());midi::Source input;input.path=path.getFullPathName().toStdString();input.name=path.getFileName().toStdString();input.bytes=readBytes(path,8000000);std::vector<std::string> names;{std::lock_guard<std::mutex> lock(mutex);names=source.gmNames;}auto parsed=input.parse(names);double duration=.1;for(auto& n:parsed.notes)duration=std::max(duration,double(n.end));if(duration>1200)throw std::invalid_argument("Durée MIDI invalide (maximum 20 minutes).");
  bool only=nil(args["audioPath"]);audio::ImportedAudio staged;var view;if(!only){staged=audio::importAudio(File(args["audioPath"].toString()),cache);view=audio::audioView(staged,0);}
  std::lock_guard<std::mutex> lock(mutex);if(busy)throw std::invalid_argument("Attendez la fin de l’analyse.");reset();++audioId;++analysisId;hasAudio=true;useMidi(std::move(input));if(only)midiAudio(path.getFileName(),duration);else{midiOnly=false;audio=std::move(staged);audioJson=view;set(audioJson,"id",integer(audioId));}return obj({{"audio",audioJson.clone()},{"result",result(0)}});
 }
 if(command=="associate_audio"){
  auto previous=snapshot(args);auto staged=audio::importAudio(File(args["path"].toString()),cache);auto view=audio::audioView(staged,0);std::lock_guard<std::mutex> lock(mutex);current(previous);if(!nil(args["sourcePart"])){Action link;link.kind=Action::Kind::setAudioSource;link.partKey=PartId(uint(args["sourcePart"]));link.name=File(args["path"].toString()).getFullPathName().toStdString();auto state=editor.document.state();if(state.layer.audioSources[link.partKey]!=link.name)editor.document.act(source,editor.document.revision(),link);}
  ++audioId;editor.dynamicsCache=var();audio=std::move(staged);midiOnly=false;audioJson=view;set(audioJson,"id",integer(audioId));return obj({{"audio",audioJson.clone()},{"result",result(previous.threshold)}});
 }
 if(command=="create_empty_midi_project"){
  std::lock_guard<std::mutex> lock(mutex);if(busy||hasAudio)throw std::invalid_argument("La session contient déjà un projet.");
  reset();++audioId;++analysisId;hasAudio=true;useMidi(midi::Source::blank("Sans titre.mid"));midiAudio("Sans titre.mid",8.);
  Document::State initial;initial.layer.parts.emplace(256,Part{PartId(256),0,false,"Piano"});editor.document=Document::restore(std::move(initial),source);
  return obj({{"audio",audioJson.clone()},{"result",result(0)}});
 }
 if(command=="create_blank_midi"){
  std::lock_guard<std::mutex> lock(mutex);require(args,false);if(kind!=project::SourceKind::none||midiOnly)throw std::invalid_argument("Le projet a changé ou contient déjà un MIDI.");auto name=audio.original.getFileNameWithoutExtension()+".mid";useMidi(midi::Source::blank(name.toStdString()));editor={};++analysisId;return result(0);
 }
 if(command=="select_notes"){std::lock_guard<std::mutex> lock(mutex);require(args);return result(threshold(args));}
 if(command=="part_delete_info"){
  std::lock_guard<std::mutex> lock(mutex);require(args,true,true);auto key=chosenPart(args);if(!key)throw std::invalid_argument("Choisissez une partie existante.");
  auto all=source;if(!source.allNotes.empty())all.notes=source.allNotes;auto view=editor.document.view(all);
  auto part=std::find_if(view.parts.begin(),view.parts.end(),[&](const Part& p){return p.key()==*key;});if(part==view.parts.end())throw std::invalid_argument("Partie introuvable.");
  return obj({{"count",integer(std::count_if(view.notes.begin(),view.notes.end(),[&](const Note& n){return n.key()==*key;}))},{"name",String::fromUTF8(part->label.c_str())}});
 }
 if(command=="edit_notes"){
  std::lock_guard<std::mutex> lock(mutex);require(args,true,true);float t=threshold(args);filter(t);auto action=project::readAction(args["action"]);if(!action.soundfontPath.empty()){auto catalog=audio::Sampler::inspectSoundfont(piano,File(String::fromUTF8(action.soundfontPath.c_str())));if(catalog["sha256"].toString()!=String(action.soundfontHash))throw std::invalid_argument("Empreinte SF2 incorrecte.");bool found=false;for(auto p:items(catalog["presets"]))if(unsigned(int(p["bank"]))==action.soundfontBank&&unsigned(int(p["preset"]))==action.soundfontPreset)found=true;if(!found)throw std::invalid_argument("Preset SF2 introuvable.");}auto before=editor.document.revision();editor.document.act(source,uint(args["revision"]),action);if(editor.document.revision()!=before)clean=var();return result(t);
 }
 if(command=="edit_notes_batch"){
  std::lock_guard<std::mutex> lock(mutex);require(args,true,true);
  auto values=args["actions"].getArray();
  if(!values||values->isEmpty()||values->size()>100000)throw std::invalid_argument("Lot de corrections vide ou trop volumineux.");
  std::vector<Action> actions;actions.reserve(std::size_t(values->size()));
  for(const auto& value:*values)actions.push_back(project::readAction(value));
  std::optional<std::string> newVoice;if(!nil(args["new_voice"])){if(!args["new_voice"].isString())throw std::invalid_argument("Nom de voix invalide.");newVoice=args["new_voice"].toString().toStdString();}
  float t=threshold(args);filter(t);auto before=editor.document.view(source).parts;
  editor.document.actBatch(source,uint(args["revision"]),actions,newVoice);clean=var();auto value=result(t);
  if(newVoice)for(const auto& p:editor.document.view(source).parts)if(std::none_of(before.begin(),before.end(),[&](const Part& old){return old.key()==p.key();})){set(value,"created_part",int(p.key()));break;}
  return value;
 }
 if(command=="waveform_detail"){
  audio::ImportedAudio a;std::uint64_t id;{std::lock_guard<std::mutex> lock(mutex);require(args,false);if(midiOnly)throw std::invalid_argument("Aucun audio associé.");a=audio;id=audioId;}auto peaks=audio::waveform(a.pcm,number(args["start"]),number(args["end"]),std::size_t(uint(args["bins"])));{std::lock_guard<std::mutex> lock(mutex);if(id!=audioId)throw std::invalid_argument("Audio obsolète.");}return peaks;
 }
 if(command=="spectrogram"){
  audio::ImportedAudio a;std::uint64_t id;{std::lock_guard<std::mutex> lock(mutex);require(args,false);if(midiOnly)throw std::invalid_argument("Aucun audio associé.");a=audio;id=audioId;}auto image=spectrogram(a.pcm,number(args["start"]),number(args["end"]),std::size_t(uint(args["columns"])));{std::lock_guard<std::mutex> lock(mutex);if(id!=audioId)throw std::invalid_argument("Audio obsolète.");}return image;
 }
 if(command=="tonal_analysis"||command=="short_note_analysis"){var view;{std::lock_guard<std::mutex> lock(mutex);require(args,true,true);view=result(threshold(args));}return readMidi(command,args,view);}
 if(command=="audio_calculation_status"){
  audio::ImportedAudio a;var previous;std::uint64_t id;auto kind=args["kind"].toString();if(kind!="dynamics"&&kind!="verification"&&kind!="cleanup"&&kind!="transcription")throw std::invalid_argument("Analyse audio inconnue.");{std::lock_guard<std::mutex> lock(mutex);require(args,false);id=audioId;a=audio;previous=measurements[juce::Identifier(kind)];if(nil(previous))previous=measurements["transcription"];}
  var matched;if(!nil(previous))matched=fileHash(a.pcm)==previous["sha256"].toString();{std::lock_guard<std::mutex> lock(mutex);if(id!=audioId)throw std::invalid_argument("L’audio a changé pendant la vérification.");}return obj({{"matched",matched},{"previous_name",previous["name"]}});
 }
 if(command=="suggest_export_path"){
  std::lock_guard<std::mutex> lock(mutex);require(args,false);auto folder=lastExport!=File()?lastExport:midiOnly&&midi.has_value()&&!midi->path.empty()?File(String::fromUTF8(midi->path.c_str())).getParentDirectory():audio.original.getParentDirectory();if(folder==File())folder=File::getSpecialLocation(File::userDocumentsDirectory);auto name=midiOnly&&midi.has_value()?File::getCurrentWorkingDirectory().getChildFile(String::fromUTF8(midi->name.c_str())).getFileNameWithoutExtension():audio.original.getFileNameWithoutExtension();auto chosen=File::createLegalFileName(args["name"].toString()).substring(0,120).trimCharactersAtEnd(" .");return folder.getChildFile((chosen.isNotEmpty()?chosen:name+"_"+args["stamp"].toString())+".mid").getFullPathName();
 }
 if(command=="preview_midi_addition"){
  auto view=snapshot(args);File path(args["path"].toString());midi::Source input;input.name=path.getFileName().toStdString();input.path=path.getFullPathName().toStdString();input.bytes=readBytes(path,8000000);std::vector<std::string> names;{std::lock_guard<std::mutex> lock(mutex);names=source.gmNames;}auto parsed=input.parse(names);std::map<PartId,std::pair<Part,int>> groups;for(auto& n:parsed.notes){if(!n.part)throw std::invalid_argument("Partie MIDI absente.");auto& entry=groups[n.key()];entry.first=*n.part;++entry.second;}if(groups.size()>512)throw std::invalid_argument("Plus de 512 parties dans le MIDI.");juce::Array<var> parts;for(auto& p:groups)parts.add(obj({{"source",int(p.first)},{"part",part(p.second.first)},{"count",p.second.second}}));
  std::lock_guard<std::mutex> lock(mutex);current(view);pendingToken=++nextToken;pendingAudio=view.audioId;pendingAnalysis=view.analysisId;pendingRevision=view.revision;pendingNotes=parsed.notes;return obj({{"token",integer(pendingToken)},{"name",path.getFileName()},{"notes",noteArray(parsed.notes)},{"parts",var(parts)}});
 }
 if(command=="apply_midi_addition"){
  std::lock_guard<std::mutex> lock(mutex);require(args,true,true);if(!pendingToken||uint(args["token"])!=pendingToken||pendingAudio!=audioId||pendingAnalysis!=analysisId||pendingRevision!=editor.document.revision())throw std::invalid_argument("Aperçu obsolète. Ouvrez à nouveau le MIDI à ajouter.");auto action=project::readAction(obj({{"kind","append_midi"},{"notes",noteArray(pendingNotes)},{"offset",args["offset"]},{"mappings",args["mappings"]}}));filter(threshold(args));editor.document.act(source,uint(args["revision"]),action);pendingNotes.clear();pendingToken=0;clean=var();return result(threshold(args));
 }
 if(command=="export_midi_voices"){
  auto view=snapshot(args,false);File destination(args["path"].toString());std::optional<midi::Source> input;std::vector<std::string> names;double bpm=0;std::uint64_t version=0;
  {std::lock_guard<std::mutex> lock(mutex);current(view);input=midi;names=source.gmNames;bpm=fixedBpm;version=tempoRevision;}
  if(destination.exists()||!destination.getParentDirectory().isDirectory())throw std::invalid_argument("Choisissez un nouveau dossier pour les voix exportées.");
  std::map<PartId,std::vector<Note>> voices;for(const auto& n:view.notes)if(!n.muted)voices[n.key()].push_back(n);
  if(voices.empty())throw std::invalid_argument("Aucune note audible à exporter.");
  juce::TemporaryFile staging(destination);auto folder=staging.getFile();if(folder.createDirectory().failed())throw std::runtime_error("Impossible de créer le dossier d’export.");
  juce::Array<var> files;int count=0;juce::StringArray used;
  auto projectName=File::createLegalFileName(args["name"].toString()).substring(0,120).trimCharactersAtEnd(" .");
  for(const auto& [key,notes]:voices){auto label=notes.front().part?notes.front().part->label:std::string("Piano");
    auto voiceName=File::createLegalFileName(String::fromUTF8(label.c_str())).substring(0,80).trimCharactersAtEnd(" .");if(voiceName.isEmpty())voiceName="Voice";
    auto base=projectName.isEmpty()?String(int(key))+"-"+voiceName:projectName+(voices.size()>1?" - "+voiceName:String());
    auto filename=base+".mid";
    for(int suffix=2;used.contains(filename,true);++suffix)filename=base+" ("+String(suffix)+").mid";
    used.add(filename);
    auto content=input?input->exportNotes(notes,names,bpm):midi::exportParts(notes,names,bpm);writeBytes(folder.getChildFile(filename),content);
    files.add(obj({{"part",int(key)},{"path",destination.getChildFile(filename).getFullPathName()},{"notes",int(notes.size())}}));count+=int(notes.size());}
  {std::lock_guard<std::mutex> lock(mutex);current(view);if(version!=tempoRevision)throw std::invalid_argument("Le tempo a changé pendant l’export. Relancez l’export.");
#if JUCE_WINDOWS
   audio::publishNew(folder,destination);
#else
   if(destination.exists()||!folder.moveFileTo(destination))throw std::runtime_error("Impossible de publier le dossier exporté.");
#endif
   if(!(bool)args["transient"])lastExport=destination.getParentDirectory();}
  return obj({{"files",var(files)},{"notes",count},{"path",destination.getFullPathName()}});
 }
 if(command=="export_midi"){
  auto view=snapshot(args,false);std::optional<midi::Source> input;std::vector<std::string> names;double bpm=0;std::uint64_t tempoVersion=0;File path(args["path"].toString());if(!path.hasFileExtension("mid;midi"))throw std::invalid_argument("Choisissez un fichier .mid ou .midi.");{std::lock_guard<std::mutex> lock(mutex);current(view);input=midi;names=source.gmNames;bpm=fixedBpm;tempoVersion=tempoRevision;noOverwrite(path,audio.original,input);}
  auto selected=chosenPart(args);if(selected)view.notes.erase(std::remove_if(view.notes.begin(),view.notes.end(),[&](const auto& n){return n.key()!=*selected;}),view.notes.end());
  if(std::none_of(view.notes.begin(),view.notes.end(),[](auto& n){return !n.muted;}))throw std::invalid_argument("Aucune note audible à exporter.");
  auto content=input?input->exportNotes(view.notes,names,bpm):midi::exportParts(view.notes,names,bpm);juce::TemporaryFile temporary(path);writeBytes(temporary.getFile(),content);{std::lock_guard<std::mutex> lock(mutex);current(view);if(tempoVersion!=tempoRevision)throw std::invalid_argument("Le tempo a changé pendant l’export. Relancez l’export.");noOverwrite(path,audio.original,midi);audio::publishNew(temporary.getFile(),path);if(!(bool)args["transient"])lastExport=path.getParentDirectory();}return int(std::count_if(view.notes.begin(),view.notes.end(),[](auto& n){return !n.muted;}));
 }
 if(command=="estimate_dynamics"||command=="cached_dynamics"){
  auto s=snapshot(args);bool cached=false;if(!nil(s.cache)){auto existing=project::readDynamicsCache(s.cache);cached=existing.matches(s.notes);}if(cached&&!bool(args["force"]))return dynamical(s,args,true);if(command=="cached_dynamics")return {};
  if(s.midiOnly)throw std::invalid_argument("Associez un audio source pour cette analyse.");auto reference=origin(s.audio);auto pcm=audio::readStereo(s.audio.pcm,22050,runtime.getChildFile("research_speex.dll"));auto calculated=analysis::dynamics(pcm,22050,s.notes);s.cache=project::writeDynamicsCache(calculated);
  {std::lock_guard<std::mutex> lock(mutex);current(s);editor.dynamicsCache=s.cache.clone();set(measurements,"dynamics",reference);}return dynamical(s,args,false);
 }
 if(command=="apply_dynamics"){
  std::lock_guard<std::mutex> lock(mutex);require(args,true,true);auto t=threshold(args);filter(t);auto view=editor.document.view(source);auto ids=analysis::scope(view.notes,chosenIds(args),{});editor.document.applyDynamics(source,uint(args["revision"]),ids,mapping(args["mapping"]),project::readDynamicsCache(editor.dynamicsCache));clean=var();return result(t);
 }
 if(command=="preview_clean"){
  auto s=snapshot(args,false);std::optional<LegacySupport> support;{std::lock_guard<std::mutex> lock(mutex);current(s);if(kind==project::SourceKind::legacy){support=LegacySupport{};support->frames=mapFrames;support->notes.resize(maps[0].size()/4);for(std::size_t i=0;i<support->notes.size();++i){std::uint32_t bits=0;for(int j=0;j<4;++j)bits|=std::uint32_t(maps[0][i*4+j])<<(j*8);std::memcpy(&support->notes[i],&bits,4);}}}
  auto preview=cleanupPreview(s.notes,s.revision,s.threshold,args["scope"],args["settings"],support);bool acoustic=(bool)args["acoustic"];
  if(acoustic&&support)throw std::invalid_argument("Essai acoustique réservé à MuScriptor.");
  auto pending=preview.clone();if(acoustic){for(auto r:items(pending["runs"]))set(r,"proposed",false);set(pending,"proposals",var(juce::Array<var>{}));set(pending,"after",pending["before"]);auto d=pending["diagnostics"];set(d,"review",(int)d["review"]+(int)d["proposed"]);set(d,"proposed",0);}
  {std::lock_guard<std::mutex> lock(mutex);current(s);clean=pending.clone();}if(!acoustic||preview["runs"].size()==0)return preview;
  if(s.midiOnly)throw std::invalid_argument("Associez un audio source pour cette analyse.");auto reference=origin(s.audio);auto pcm=audio::readStereo(s.audio.pcm,22050,runtime.getChildFile("research_speex.dll"));auto checked=verifyContinuity(preview,pcm,22050,s.notes);
  {std::lock_guard<std::mutex> lock(mutex);current(s);if(juce::JSON::toString(clean,true,17)!=juce::JSON::toString(pending,true,17))throw std::invalid_argument("La recherche a changé pendant la vérification audio. Relancez-la.");clean=checked.clone();set(measurements,"cleanup",reference);}return checked;
 }
 if(command=="apply_clean"){
  std::lock_guard<std::mutex> lock(mutex);require(args,true,true);float t=threshold(args);filter(t);auto before=editor.document.view(source).notes.size();auto checked=project::readCleanPreview(clean);std::vector<NoteId> ids;for(auto& i:items(args["runIds"]))ids.push_back(uint(i));editor.document.applyCleanRuns(source,t,uint(args["revision"]),ids,checked);
  if(!checked)clean=var();else{auto value=clean.clone();set(value,"revision",integer(checked->revision));set(value,"before",integer(checked->before));set(value,"after",integer(checked->after));std::set<NoteId> chosen(ids.begin(),ids.end());juce::Array<var> runs,edges;for(auto& r:items(clean["runs"]))if(!chosen.count(uint(r["ids"][0])))runs.add(r);for(auto& edge:items(clean["proposals"]))if(!chosen.count(uint(edge["left"])))edges.add(edge);set(value,"runs",var(runs));set(value,"proposals",var(edges));clean=value;}
  auto after=editor.document.view(source).notes.size();return obj({{"view",result(t)},{"preview",clean.clone()},{"before",integer(before)},{"after",integer(after)},{"merged_runs",integer(ids.size())}});
 }
 if(command=="verify_midi_audio"){
  if(verificationRunning.exchange(true))throw std::invalid_argument("Une vérification audio est déjà en cours.");struct Running{std::atomic_bool& flag;~Running(){flag=false;}} guard{verificationRunning};verificationCancel=false;
  auto s=snapshot(args);auto ids=analysis::scope(s.notes,chosenIds(args),chosenPart(args));std::optional<std::pair<int,int>> band;if(!nil(args["pitchRange"])){auto bounds=items(args["pitchRange"]);if(bounds.size()!=2)throw std::invalid_argument("Registre invalide.");auto lo=uint(bounds[0]),hi=uint(bounds[1]);if(lo>hi||hi>127)throw std::invalid_argument("Registre invalide.");band={{int(lo),int(hi)}};}
  double a=nil(args["start"])?0:number(args["start"]),b=nil(args["end"])?s.duration:number(args["end"]);if(a<0||b<=a||b>s.duration+.001)throw std::invalid_argument("Choisissez un intervalle compris dans l’audio source.");
  if(!nil(args["ids"])){if(ids.empty())throw std::invalid_argument("Sélectionnez des notes pour cette portée.");a=std::numeric_limits<double>::infinity();b=0;for(auto& n:s.notes)if(std::find(ids.begin(),ids.end(),n.id)!=ids.end()){a=std::min(a,double(n.start));b=std::max(b,double(n.end));}a=std::max(a,0.);b=std::min(b,s.duration);if(b<=a)throw std::invalid_argument("La sélection ne recouvre pas l’audio source.");}
  std::vector<NoteId> selected,complete;for(auto& n:s.notes)if(std::find(ids.begin(),ids.end(),n.id)!=ids.end()&&n.start<b&&n.end>a){selected.push_back(n.id);if(n.start>=a&&n.end<=b&&(!band||(n.pitch>=band->first&&n.pitch<=band->second)))complete.push_back(n.id);}
  if(s.midiOnly)throw std::invalid_argument("Associez un audio source pour cette analyse.");auto reference=origin(s.audio);double lo=std::max(a-6.,0.),hi=std::min(b+6.,s.duration);String key=String(static_cast<juce::int64>(s.audioId))+":"+s.audio.pcm.getFullPathName()+":"+String(lo,17)+":"+String(hi,17);std::shared_ptr<const VerificationGrid> computed;{std::lock_guard<std::mutex> lock(gridMutex);if(key==gridKey)computed=grid;}bool cached=bool(computed);
  auto pcm=audio::readStereo(s.audio.pcm,44100,runtime.getChildFile("research_speex.dll"));if(!computed)computed=std::make_shared<const VerificationGrid>(verificationSpectrum(pcm,44100,lo,hi,verificationCancel));auto report=verifyMidi(*computed,pcm,44100,s.notes,selected,band,verificationCancel,&complete);
  auto prune=[&](const char* field,auto outside){auto* list=report[field].getArray();for(int i=list->size();i-->0;)if(outside((*list)[i]))list->remove(i);};prune("timing",[&](const var& item){return (double)item["at"]<a||(double)item["at"]>=b||(double)item["proposed_end"]>b;});prune("missing",[&](const var& item){return (double)item["start"]<a||(double)item["start"]>=b;});
  set(report,"start",a);set(report,"end",b);set(report,"audio_id",integer(s.audioId));set(report,"analysis_id",integer(s.analysisId));set(report,"revision",integer(s.revision));set(report,"threshold",double(s.threshold));set(report,"cached",cached);
  auto* warnings=report["warnings"].getArray();warnings->add(String::fromUTF8("L’analyse fournit des indices acoustiques qui peuvent être erronés. Écoutez et vérifiez chaque proposition avant de l’appliquer. Aucune correction automatique."));if(report["timing"].size()>=128)warnings->add(String::fromUTF8("Les suggestions de durée et de réattaque sont limitées à 128 résultats ; analyser une zone plus courte pour poursuivre."));
  if(band){warnings->add(String::fromUTF8("Registre examiné : MIDI ")+String(band->first)+String::fromUTF8("–")+String(band->second)+String::fromUTF8(". Les harmoniques et les autres parties restent utilisées comme contexte."));}if(chosenPart(args))warnings->add(String::fromUTF8("Les propositions manquantes ne sont pas attribuables automatiquement à une voix : toutes les parties MIDI servent à expliquer le spectre."));
  {std::lock_guard<std::mutex> lock(mutex);current(s);if(verificationCancel.load())throw std::invalid_argument("Vérification annulée ou notes modifiées pendant le calcul.");set(measurements,"verification",reference);std::lock_guard<std::mutex> cacheLock(gridMutex);gridKey=key;grid=computed;}return report;
 }

 if(command=="redecode_voice"){
  basicPitch::Cache cached;var entry;String key;int target=int(uint(args["part"]));std::uint64_t id,revision;std::vector<std::string> names;
  {std::lock_guard<std::mutex> lock(mutex);require(args,true,true);key=voiceCacheKey(target);if(key.isEmpty())throw std::invalid_argument("Cache Basic Pitch indisponible : lancez Analyser.");entry=voiceTranscriptions[juce::Identifier(key)].clone();id=audioId;revision=editor.document.revision();names=source.gmNames;
   if((bool)entry["primary"]){cached.samples=std::size_t(uint(bp["input_samples"]));cached.notes=basicPitch::unpack(maps[0]);cached.onsets=basicPitch::unpack(maps[1]);}
  }
  if(!(bool)entry["primary"])cached=basicPitch::decodeCache(entry["cache"]);
  auto settings=basicPitch::Options::read(args["options"]);auto calculated=basicPitch::decode(cached,settings);auto notes=basicPitch::restore(calculated,names);
  std::lock_guard<std::mutex> lock(mutex);require(args,true,true);if(audioId!=id||editor.document.revision()!=revision)throw std::invalid_argument("La session a changé pendant le calcul.");
  Action action;action.kind=Action::Kind::replaceVoiceNotes;action.partKey=PartId(target);action.imported=std::move(notes);auto staged=editor;staged.document.act(source,revision,action);
  auto view=staged.document.view(source);set(entry,"signature",voiceSignature(view.notes,target));set(entry,"settings",settings.json());editor=std::move(staged);voiceTranscriptions.getDynamicObject()->setProperty(key,entry);clean=var();++analysisId;
  auto value=result(0);set(value,"reused_inference",true);return value;
 }
 if(command=="analyze_muscriptor"||command=="analyze_transcription"||command=="redecode_basic_pitch"){
  auto configured=inference.status()["settings"];bool cacheOnly=command=="redecode_basic_pitch";bool append=bool(args["append"]);bool useBasic=(command=="analyze_transcription"||cacheOnly)&&configured["engine"].toString()=="basic_pitch";
  if(cacheOnly&&!useBasic)throw std::invalid_argument("Moteur de transcription invalide.");
  audio::ImportedAudio input;std::uint64_t id;std::vector<std::string> names;
  {std::lock_guard<std::mutex> lock(mutex);require(args,false);if(midiOnly||(!append&&kind==project::SourceKind::midi))throw std::invalid_argument("Projet en mode Édition : le MIDI importé est protégé. Ouvrez une nouvelle transcription pour utiliser un modèle.");busy=true;inference.resetCancellation();input=audio;id=audioId;names=source.gmNames;}
  struct Busy{Impl& self;~Busy(){std::lock_guard<std::mutex> lock(self.mutex);self.busy=false;}} guard{*this};auto reference=origin(input);
  if(useBasic){
   auto digest=fileHash(input.pcm);basicPitch::Cache cached;bool reuse=false;
   {std::lock_guard<std::mutex> lock(mutex);if(kind==project::SourceKind::basicPitch&&bp["audio_sha256"].toString()==digest){cached.samples=std::size_t(uint(bp["input_samples"]));cached.notes=basicPitch::unpack(maps[0]);cached.onsets=basicPitch::unpack(maps[1]);reuse=true;}}
   if(cacheOnly&&!reuse)throw std::invalid_argument("Cache Basic Pitch indisponible : lancez Analyser.");
   if(!reuse)cached=basic.run(input.pcm,inference.cancellation(),[this,id](float amount){std::lock_guard<std::mutex> lock(eventMutex);events.push_back(obj({{"event","muscriptor-progress"},{"payload",obj({{"audio_id",integer(id)},{"phase","transcribe"},{"progress",double(amount)}})}}));});
   auto calculated=basicPitch::decode(cached,basicPitch::Options::read(configured["basic_pitch"]),&inference.cancellation());set(calculated,"audio_sha256",digest);
   set(calculated,"source_path",input.original.getFullPathName());
   auto notes=basicPitch::restore(calculated,names);
   var appendedCache;if(append)appendedCache=obj({{"engine","basic_pitch"},{"settings",configured["basic_pitch"].clone()},{"source_path",input.original.getFullPathName()},{"audio_sha256",digest},{"primary",false},{"cache",basicPitch::encodeCache(cached)}});
   auto noteMap=basicPitch::pack(cached.notes),onsetMap=basicPitch::pack(cached.onsets);
   std::lock_guard<std::mutex> lock(mutex);if(!hasAudio||audioId!=id)throw std::invalid_argument("Audio modifié pendant l’analyse.");if(append)return appendTranscription(std::move(notes),input.original.getFullPathName(),appendedCache);auto retainedTempo=fixedBpm;reset();fixedBpm=retainedTempo;
   kind=project::SourceKind::basicPitch;bp=calculated;mapFrames=cached.frames();maps[0]=std::move(noteMap);maps[1]=std::move(onsetMap);source.notes=notes;source.allNotes=notes;source.sourceCount=notes.size();++analysisId;set(measurements,"transcription",reference);registerPrimaryVoice();auto value=result(0);set(value,"reused_inference",reuse);return value;
  }
  auto calculated=inference.transcribe(input.pcm,id,[this](var event){std::lock_guard<std::mutex> lock(eventMutex);if(events.size()>=4096)events.erase(events.begin(),events.begin()+2048);events.push_back(event);});auto notes=project::muscriptorNotes(calculated,names);
  std::lock_guard<std::mutex> lock(mutex);if(!hasAudio||audioId!=id)throw std::invalid_argument("Audio modifié pendant l’analyse.");if(append)return appendTranscription(std::move(notes),input.original.getFullPathName());auto retainedTempo=fixedBpm;reset();fixedBpm=retainedTempo;kind=project::SourceKind::muscriptor;mu=calculated;source.notes=notes;source.allNotes=notes;source.sourceCount=notes.size();++analysisId;set(measurements,"transcription",reference);{auto state=editor.document.state();for(const auto& n:source.notes)state.layer.audioSources[n.key()]=input.original.getFullPathName().toStdString();editor.document=Document::restore(std::move(state),source);}return result(0);
 }
 if(command=="open_project"){
  File path(args["path"].toString());std::vector<std::string> names;{std::lock_guard<std::mutex> lock(mutex);if(busy)throw std::invalid_argument("Attendez la fin de l’analyse.");names=source.gmNames;}
  auto loaded=project::prepare(project::read(path),names,fileHash(runtime.getChildFile("manifest.json")).toStdString());auto meta=loaded.container.metadata;bool only=(bool)meta["midi_only"];audio::ImportedAudio staged;var view;
  if(!only){auto directory=cache.getChildFile("project-"+juce::Uuid().toString());if(directory.createDirectory().failed())throw std::runtime_error("Cache audio indisponible.");staged.pcm=directory.getChildFile("audio.wav");staged.cacheOwner=audio::ownCachedPcm(staged.pcm);writeBytes(staged.pcm,loaded.container.data[0]);staged.original=meta["source_path"].toString().isEmpty()?staged.pcm:File(meta["source_path"].toString());staged.format=meta["source_format"].toString();staged.duration=loaded.audio.duration;staged.rate=loaded.audio.sampleRate;staged.channels=loaded.audio.channels;view=audio::audioView(staged,0);set(view,"name",meta["source_name"]);set(view,"path",meta["source_path"]);}
  std::lock_guard<std::mutex> lock(mutex);if(busy)throw std::invalid_argument("Attendez la fin de l’analyse.");reset();sourceRuntimeHash=meta["runtime_sha256"].toString();++audioId;++analysisId;hasAudio=true;fixedBpm=meta.hasProperty("tempo_bpm")?number(meta["tempo_bpm"]):0;kind=loaded.kind;source=std::move(loaded.source);editor=std::move(loaded.editor);midi=std::move(loaded.midi);mu=meta["muscriptor"].clone();bp=meta["basic_pitch"].clone();voiceTranscriptions=meta["voice_transcriptions"].isObject()?meta["voice_transcriptions"].clone():obj({});if(voiceTranscriptions.getDynamicObject()->getProperties().size()==0)registerPrimaryVoice();profile=meta["profile"];candidates=meta["candidates"].clone();mapFrames=std::size_t(uint(meta["frames"]));for(std::size_t i=0;i<3;++i)maps[i]=std::move(loaded.container.data[i+1]);if(meta["audio_measurements"].isObject())measurements=meta["audio_measurements"].clone();
  if(only)midiAudio(meta["source_name"].toString(),loaded.audio.duration);else{midiOnly=false;audio=std::move(staged);audioJson=view;set(audioJson,"id",integer(audioId));}protectedProject=loaded.container.originVersion<8?path:File();lastExport=path.getParentDirectory();auto t=float(1-number(meta["sensitivity"])/100);return obj({{"audio",audioJson.clone()},{"result",kind==project::SourceKind::none?var():result(t)},{"ui",meta["ui"].clone()},{"path",path.getFullPathName()},{"upgraded_from",int(loaded.container.originVersion)}});
 }
 if(command=="save_project"){
  std::lock_guard<std::mutex> lock(mutex);require(args,false);if(kind!=project::SourceKind::none)require(args,true,!nil(args["revision"]));File path(args["path"].toString());if(path==protectedProject)throw std::invalid_argument("Choisissez un nouveau nom pour enregistrer en PHT8 ; le projet original est conservé.");if(!path.hasFileExtension("pht"))throw std::invalid_argument("Choisissez un fichier .pht.");if(audio.original!=File()&&path==audio.original)throw std::invalid_argument("Le projet ne peut pas remplacer son audio.");
  project::Container container;if(!midiOnly)container.data[0]=readBytes(audio.pcm,700000000);for(std::size_t i=0;i<3;++i)container.data[i+1]=maps[i];auto hash=juce::SHA256(container.data[0].data(),container.data[0].size()).toHexString();auto runtimeHash=sourceRuntimeHash.isNotEmpty()?sourceRuntimeHash:fileHash(runtime.getChildFile("manifest.json"));
  container.metadata=obj({{"version",8},{"voice_transcriptions",voiceTranscriptions.clone()},{"tempo_bpm",fixedBpm},{"midi_only",midiOnly},{"timeline_duration",audio.duration},{"muscriptor",mu.clone()},{"basic_pitch",bp.clone()},{"imported_midi",midi?sourceJson(*midi):var()},{"profile",profile},{"sensitivity",args["ui"]["sensitivity"]},{"frames",integer(mapFrames)},{"candidates",candidates.clone()},{"editor",project::writeEditor(editor)},{"source_path",audioJson["path"]},{"source_name",audioJson["name"]},{"source_format",audioJson["format"]},{"pcm_sha256",hash},{"runtime_sha256",runtimeHash},{"ui",args["ui"].clone()}});if(measurements.getDynamicObject()->getProperties().size())set(container.metadata,"audio_measurements",measurements.clone());
  double savedDuration=audio.duration;if(kind==project::SourceKind::midi||kind==project::SourceKind::basicPitch)for(const auto& n:source.allNotes)savedDuration=std::max(savedDuration,double(n.end));
  if(kind==project::SourceKind::muscriptor)for(const auto& n:items(mu["raw"]["notes"]))savedDuration=std::max(savedDuration,number(n["offset"]));
  savedDuration=editor.document.timelineDuration(savedDuration);auto savedUi=container.metadata["ui"].clone();auto pos=savedUi["position"];
  if((pos.isInt()||pos.isInt64()||pos.isDouble())&&std::isfinite(double(pos)))set(savedUi,"position",std::clamp(double(pos),0.,savedDuration));
  set(container.metadata,"ui",savedUi);
  auto checked=project::prepare(std::move(container),source.gmNames,runtimeHash.toStdString());project::saveAtomic(path,checked.container,(bool)args["create_only"]);return {};
 }
 if(command=="export_wav"){
  auto s=snapshot(args);auto options=args["options"],scope=obj({{"part",options["part"]},{"ids",var()}});auto selected=chosenPart(scope);std::vector<Note> notes;double duration=s.duration;for(auto& n:s.notes)if(!n.muted&&(!selected||n.key()==*selected)){notes.push_back(n);duration=std::max(duration,double(n.end));}if(notes.empty())throw std::invalid_argument("Aucune note audible à exporter.");if(!std::isfinite(duration)||duration<=0||duration>1200)throw std::invalid_argument("Durée d’export WAV invalide (maximum 20 minutes).");
  auto db=number(options["midiDb"]),amount=number(options["reverb"]);if(db< -60||db>12||amount<0||amount>100)throw std::invalid_argument("Réglages d’export WAV invalides.");File path(args["path"].toString());if(!path.hasFileExtension("wav"))throw std::invalid_argument("Choisissez un fichier .wav.");{std::lock_guard<std::mutex> lock(mutex);current(s);noOverwrite(path,audio.original,midi);}
  auto bank=banks.selected();bool pianoFallback=bank==File()&&std::any_of(notes.begin(),notes.end(),[](auto& n){return n.part&&n.part->program!=0&&n.part->soundfontPath.empty();});
  constexpr unsigned rate=48000;audio::Engine engine(rate,std::make_unique<audio::Sampler>(piano,bank,rate),std::make_unique<audio::Sampler>(piano,bank,rate));engine.pcm=std::make_shared<const analysis::StereoPcm>(std::size_t(std::ceil(duration*rate)),std::array<float,2>{0.f,0.f});engine.notes(notes);engine.mode=1;engine.midiGain=db<=-60?0.f:std::pow(10.f,float(db)/20.f);engine.setReverb(unsigned(std::round(amount)));
  juce::TemporaryFile temporary(path);auto output=temporary.getFile().createOutputStream();if(!output)throw std::runtime_error("Création du WAV impossible.");output->write("RIFF",4);output->writeInt(0);output->write("WAVEfmt ",8);output->writeInt(16);output->writeShort(1);output->writeShort(2);output->writeInt(rate);output->writeInt(rate*6);output->writeShort(6);output->writeShort(24);output->write("data",4);output->writeInt(0);std::uint64_t frames=0,maximum=engine.pcm->size()+std::uint64_t(rate)*4+1;engine.play();while(engine.playing){if(frames>=maximum)throw std::runtime_error("La fin du rendu WAV n’a pas été atteinte.");for(float sample:engine.next()){if(!std::isfinite(sample))throw std::invalid_argument("Échantillon audio invalide pendant le rendu WAV.");auto value=std::int32_t(std::round(std::clamp(sample,-1.f,1.f)*8388607.f));output->writeByte(char(value&255));output->writeByte(char((value>>8)&255));output->writeByte(char((value>>16)&255));}++frames;}
  auto size=std::uint32_t(frames*6);output->setPosition(4);output->writeInt(int(size+36));output->setPosition(40);output->writeInt(int(size));output->flush();if(output->getStatus().failed())throw std::runtime_error("Écriture du WAV impossible.");output.reset();audio::publishNew(temporary.getFile(),path);{std::lock_guard<std::mutex> lock(mutex);lastExport=path.getParentDirectory();}return obj({{"notes",integer(notes.size())},{"frames",integer(frames)},{"duration",double(frames)/rate},{"clipped_samples",integer(engine.clipped)},{"piano_fallback",pianoFallback},{"notice",pianoFallback?String::fromUTF8("Banque General MIDI absente : WAV rendu au piano de secours, comme la lecture."):String()}});
 }
 throw std::invalid_argument("Commande inconnue : "+command.toStdString());
}
}
