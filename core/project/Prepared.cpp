#include "Prepared.h"
#include "../midi/Tempo.h"
#include "Validation.h"
#include "BasicPitch.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <tuple>
namespace phthongos::project {
namespace {
using juce::var;
[[noreturn]]void fail(const char* s){throw std::invalid_argument(s);}
bool nil(const var& v){return v.isVoid()||v.isUndefined();}
std::uint64_t uint(const var& v,std::uint64_t max=INT64_MAX){if((!v.isInt()&&!v.isInt64())||static_cast<juce::int64>(v)<0||static_cast<std::uint64_t>(static_cast<juce::int64>(v))>max)fail("Métadonnées incompatibles.");return static_cast<std::uint64_t>(static_cast<juce::int64>(v));}
double real(const var& v){if(!v.isDouble()&&!v.isInt()&&!v.isInt64())fail("Métadonnées incompatibles.");auto n=static_cast<double>(v);if(!std::isfinite(n))fail("Métadonnées incompatibles.");return n;}
std::string str(const var& v){if(!v.isString())fail("Métadonnées incompatibles.");return v.toString().toStdString();}
bool boolean(const var& v){if(!v.isBool())fail("Métadonnées incompatibles.");return static_cast<bool>(v);}
const juce::Array<var>& array(const var& v,int maximum){auto* a=v.getArray();if(!a||a->size()>maximum)fail("Métadonnées incompatibles.");return *a;}
bool profile(const var& v){return v.isString()&&(v.toString()=="versatile"||v.toString()=="piano"||v.toString()=="guitar");}
void sort(std::vector<Note>& notes){std::stable_sort(notes.begin(),notes.end(),[](auto& a,auto& b){if(a.start!=b.start)return a.start<b.start;if(a.start==0&&std::signbit(a.start)!=std::signbit(b.start))return std::signbit(a.start);return std::make_tuple(a.pitch,a.id)<std::make_tuple(b.pitch,b.id);});}
std::string modelHash(const std::string& model){
    if(model=="small")return "925f55af65a20ebc4f8b45ceaf095a12b72493d436cb112623cd0041a1af23d4";
    if(model=="medium")return "3850cc9e5b436b17a09bd25b8f2615cb3366ab96a71e7b50f73a793a917fdf03";
    if(model=="large")return "35a750fb1ab1e77195cdc2c0b9b4aeea2f4d59f11f729f02af9920c4854ef72e";
    fail("Modèle de transcription invalide.");
}
struct MuNote {double start,end;int pitch,program;bool drum;std::string instrument;};
std::vector<Note> muNotes(const var& mu,const std::vector<std::string>& gm){
    if(mu.hasProperty("instrument_target")&&str(mu["instrument_target"])!="all"&&str(mu["instrument_target"])!="voice")fail("Cible instrumentale invalide.");
    if(str(mu["model_sha256"])!=modelHash(str(mu["model"])))fail("Provenance du modèle invalide.");str(mu["preprocessing"]);
    auto raw=mu["raw"];if(uint(raw["sample_rate"])!=16000||uint(raw["input_samples"])==0)fail("Transcription invalide.");
    auto device=raw["device"];str(device["name"]);str(device["backend"]);boolean(device["integrated"]);uint(device["memory_total"]);
    std::vector<MuNote> notes;for(auto& n:array(raw["notes"],1000000)){
        MuNote note{real(n["onset"]),real(n["offset"]),static_cast<int>(uint(n["pitch"],127)),static_cast<int>(uint(n["program"],128)),boolean(n["is_drum"]),str(n["instrument"])};
        if(note.start<0||note.end<=note.start||note.end>86400||note.drum!=(note.program==128))fail("Événement MuScriptor invalide.");notes.push_back(std::move(note));
    }
    std::stable_sort(notes.begin(),notes.end(),[](auto& a,auto& b){if(a.start!=b.start)return a.start<b.start;if(a.start==0&&std::signbit(a.start)!=std::signbit(b.start))return std::signbit(a.start);return std::make_tuple(a.program,a.pitch,a.end)<std::make_tuple(b.program,b.pitch,b.end);});
    // Keep the reference post-processing: overlap only, same program and pitch;
    // touching notes retain both attacks. Raw JSON stays intact in the container.
    for(std::size_t i=0;i<notes.size();++i){auto j=i+1;while(j<notes.size()&&notes[j].start<notes[i].end){if(notes[i].program==notes[j].program&&notes[i].pitch==notes[j].pitch){notes[i].end=std::max(notes[i].end,notes[j].end);notes.erase(notes.begin()+j);}else ++j;}}
    std::vector<Note> out;for(auto& v:notes){Note n;n.id=out.size()+1;n.sourceIds={n.id};n.start=static_cast<float>(v.start);n.end=static_cast<float>(v.end);n.pitch=v.pitch;n.velocity=100;n.velocityNormalised=100.f/127.f;
        n.part=Part{{},static_cast<PartId>(v.program),v.drum,v.program<static_cast<int>(gm.size())?gm[v.program]:v.instrument};out.push_back(std::move(n));}return out;
}
void map(const std::vector<std::uint8_t>& data,std::size_t frames){if(data.size()!=88*frames*4)fail("Dimensions de cartes incorrectes.");
    for(std::size_t i=0;i<data.size();i+=4){std::uint32_t bits=0;for(int j=0;j<4;++j)bits|=std::uint32_t(data[i+j])<<(j*8);float v;std::memcpy(&v,&bits,4);if(!std::isfinite(v))fail("invalid [1,88,T] map");}}
float time(std::size_t frame){float samples=static_cast<float>(frame*384);float seconds=samples/16000.f;float centre=seconds+.012f;return std::max(0.f,centre-.048f);}
int velocity(float x){float v=127.f*x;float low=std::floor(v);float fraction=v-low;float rounded=(fraction>.5f||(fraction==.5f&&std::fmod(low,2.f)!=0.f))?low+1:low;return static_cast<int>(std::clamp(rounded,0.f,127.f));}
void candidates(SourceSnapshot& source,const juce::Array<var>& cs,std::size_t frames,float threshold){
    source.sourceCount=static_cast<std::size_t>(cs.size());
    for(int i=0;i<cs.size();++i){auto c=cs[i];auto pitch=uint(c["pitch_index"]),start=uint(c["start_fr"]),length=uint(c["length"]);float vel=static_cast<float>(real(c["velocity_normalized"])),onset=static_cast<float>(real(c["ons_prob"]));
        if(pitch>=88||start>=frames||length>frames-start||!std::isfinite(vel)||!std::isfinite(onset))fail("Candidate invalide.");if(length<=1)continue;
        Note n;n.id=static_cast<NoteId>(i)+1;n.sourceIds={n.id};n.pitch=static_cast<int>(pitch+21);n.startFrame=start;n.endFrame=start+length;n.frameDuration=length;
        n.start=time(start);n.end=time(start+length);n.velocityNormalised=vel;n.velocity=velocity(vel);n.onsetProbability=start<=3&&length>3?.7f:onset;
        if(n.onsetProbability>=0)source.allNotes.push_back(n);if(n.onsetProbability>=threshold)source.notes.push_back(n);
    }sort(source.notes);sort(source.allNotes);
}
}
std::vector<Note> muscriptorNotes(const juce::var& mu,const std::vector<std::string>& names){return muNotes(mu,names);}
AudioInfo pcmInfo(const std::vector<std::uint8_t>& b){
    auto le=[&](std::size_t at,int n){if(at>b.size()||static_cast<std::size_t>(n)>b.size()-at)fail("WAV invalide.");std::uint32_t v=0;for(int j=0;j<n;++j)v|=std::uint32_t(b[at+j])<<(j*8);return v;};
    auto tag=[&](std::size_t at,const char* s){return at<=b.size()&&b.size()-at>=4&&std::memcmp(b.data()+at,s,4)==0;};
    if(!tag(0,"RIFF")||!tag(8,"WAVE"))fail("WAV invalide.");AudioInfo info;bool fmt=false,data=false;std::uint32_t bytes=0;
    for(std::size_t at=12;at<b.size();){if(b.size()-at<8)fail("WAV invalide.");auto n=le(at+4,4);auto payload=at+8;if(n>b.size()-payload)fail("Données WAV incomplètes.");
        if(tag(at,"fmt ")){if(n<16)fail("WAV invalide.");auto format=le(payload,2);info.channels=static_cast<std::uint16_t>(le(payload+2,2));info.sampleRate=le(payload+4,4);auto bits=le(payload+14,2);
            if(format==65534){if(n<40||le(payload+16,2)<22||le(payload+18,2)!=16)fail("WAV invalide.");format=le(payload+24,4);}
            if(format!=1||bits!=16)fail("Ce prototype accepte les WAV PCM 16 bits. Convertissez le fichier dans ce format.");if(!info.channels||!info.sampleRate)fail("WAV invalide.");fmt=true;
        }else if(tag(at,"data")&&!data){if(!fmt)fail("WAV invalide.");bytes=n;data=true;}at=payload+n;if(n&1){if(at<b.size())++at;}
    }
    if(!fmt||!data)fail("WAV invalide.");info.frames=bytes/2/info.channels;info.duration=static_cast<double>(info.frames)/info.sampleRate;
    if(!info.frames||info.duration<.1)fail("Le WAV doit durer au moins 0,1 seconde.");if(info.duration>419.95)fail("Ce prototype est limité à un bloc de moins de 420 secondes.");return info;
}
Prepared prepare(Container container,const std::vector<std::string>& names,const std::string& /*runtimeHash*/){
    auto tempo=container.metadata["tempo_bpm"];if(!tempo.isVoid()&&!tempo.isUndefined()){auto bpm=real(tempo);if(bpm!=0)midi::validateTempo(bpm);}
    validateEnvelope(container);
    Prepared p;p.container=std::move(container);auto m=p.container.metadata;
    // Envelope integrity is checked at decode(); callers with an in-memory
    // container must obey the same version/hash contract before logical adoption.
    if(!m.isObject()||uint(m["version"])!=8)fail("Fichier .pht invalide ou version non prise en charge (PHT 8 requis).");
    if(m.hasProperty("audio_measurements")&&nil(m["audio_measurements"]))fail("Empreinte de mesure audio invalide.");
    checkAudioOrigins(m["audio_measurements"]);str(m["source_path"]);str(m["source_name"]);str(m["source_format"]);str(m["pcm_sha256"]);str(m["runtime_sha256"]);
    auto bp=m["basic_pitch"];auto frames=uint(m["frames"],nil(bp)?18000:40000);auto& cs=array(m["candidates"],1000000);double sensitivity=real(m["sensitivity"]);if(sensitivity<0||sensitivity>100)fail("La sensibilité doit être comprise entre 0 et 100 %.");float threshold=static_cast<float>(1.-sensitivity/100.);
    bool midiOnly=m.hasProperty("midi_only")?boolean(m["midi_only"]):false;
    double storedDuration=m.hasProperty("timeline_duration")?real(m["timeline_duration"]):0;auto midi=m["imported_midi"],mu=m["muscriptor"],profileName=m["profile"];
    // Cached analysis is part of the project, not a request to run the installed
    // engine again. Its runtime hash records provenance, not format compatibility.
    // Envelope, audio, maps, candidates and editor state remain validated below.
    if(midiOnly){if(nil(midi)||!p.container.data[0].empty())fail("Projet MIDI seul incohérent.");p.audio.duration=storedDuration;if(p.audio.duration<=0||p.audio.duration>1200)fail("Durée MIDI invalide (maximum 20 minutes).");}
    else p.audio=pcmInfo(p.container.data[0]);
    p.source.gmNames=names;
    bool mapsEmpty=p.container.data[1].empty()&&p.container.data[2].empty()&&p.container.data[3].empty();
    if(!nil(midi)){
        if(!nil(mu)||!nil(bp)||!nil(profileName)||frames||!cs.isEmpty()||!mapsEmpty)fail("Source MIDI incohérente.");midi::Source source;
        source.createdEmpty=midi.hasProperty("created_empty")?boolean(midi["created_empty"]):false;source.path=midi.hasProperty("source_path")?str(midi["source_path"]):"";source.name=str(midi["name"]);
        for(auto& byte:array(midi["bytes"],8000000))source.bytes.push_back(static_cast<std::uint8_t>(uint(byte,255)));auto parsed=source.parse(names);
        p.source.notes=parsed.notes;p.source.allNotes=parsed.notes;p.midi=std::move(source);p.kind=SourceKind::midi;
    }else if(!nil(bp)){
        if(!nil(mu)||!nil(profileName)||midiOnly||!cs.isEmpty()||!p.container.data[3].empty())fail("Source de projet Basic Pitch incohérente.");
        basicPitch::Cache cache;cache.samples=std::size_t(uint(bp["input_samples"],22050*420));cache.notes=basicPitch::unpack(p.container.data[1]);cache.onsets=basicPitch::unpack(p.container.data[2]);cache.validate();
        if(cache.frames()!=frames)fail("Durée du cache Basic Pitch invalide.");
        p.source.notes=basicPitch::restore(bp,names);p.source.allNotes=p.source.notes;p.source.sourceCount=p.source.notes.size();p.kind=SourceKind::basicPitch;
    }else if(!nil(mu)){
        if(!nil(profileName)||frames||!cs.isEmpty()||!mapsEmpty)fail("Analyse MuScriptor incohérente.");p.source.notes=muNotes(mu,names);p.source.allNotes=p.source.notes;p.kind=SourceKind::muscriptor;
    }else if(!nil(profileName)){
        if(!profile(profileName))fail("Profil inconnu.");if(frames<5)fail("Cartes absentes.");for(std::size_t i=1;i<4;++i)map(p.container.data[i],static_cast<std::size_t>(frames));
        candidates(p.source,cs,static_cast<std::size_t>(frames),threshold);p.kind=SourceKind::legacy;
    }else if(frames||!cs.isEmpty()||!mapsEmpty)fail("Analyse incohérente.");
    basicPitch::validateVoiceCaches(m["voice_transcriptions"]);
    if(auto* voices=m["voice_transcriptions"].getDynamicObject())for(auto& voice:voices->getProperties())if((bool)voice.value["primary"]&&p.kind!=SourceKind::basicPitch)fail("Cache Basic Pitch invalide.");
    p.editor=readEditor(m["editor"],p.source);if(p.kind==SourceKind::none&&p.editor.document.view(p.source).hasChanges)fail("Analyse incohérente.");
    double base=p.audio.duration;
    if(p.kind==SourceKind::midi||p.kind==SourceKind::basicPitch)for(auto& n:p.source.allNotes)base=std::max(base,static_cast<double>(n.end));
    if(p.kind==SourceKind::muscriptor)for(auto& n:array(mu["raw"]["notes"],1000000))base=std::max(base,real(n["offset"]));
    p.duration=p.editor.document.timelineDuration(base);
    checkUi(m["ui"],p.duration);if(real(m["ui"]["sensitivity"])!=sensitivity||(!nil(profileName)&&m["ui"]["profile"]!=profileName))fail("Profil ou sensibilité de projet incohérents.");return p;
}
}
