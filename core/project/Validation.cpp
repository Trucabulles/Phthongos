#include "Validation.h"
#include "BasicPitch.h"
#include <cmath>
#include <stdexcept>
#include <set>
namespace phthongos::project {
namespace {
using juce::var;using juce::String;
[[noreturn]]void fail(const char* s){throw std::invalid_argument(s);}
bool nil(const var& v){return v.isVoid()||v.isUndefined();}
bool numeric(const var& v){return (v.isInt()||v.isInt64()||v.isDouble())&&std::isfinite(static_cast<double>(v));}
bool uint(const var& v){return (v.isInt()||v.isInt64())&&static_cast<juce::int64>(v)>=0;}
bool option(const var& v,std::initializer_list<const char*> options){if(!v.isString())return false;for(auto* o:options)if(v.toString()==o)return true;return false;}
bool range(const var& v,double low,double high){return numeric(v)&&static_cast<double>(v)>=low&&static_cast<double>(v)<=high;}
void ids(const var& v,int maximum,const char* malformed,const char* invalid){auto* a=v.getArray();if(!a)fail(malformed);if(a->size()>maximum)fail(invalid);for(auto& i:*a)if(!uint(i))fail(invalid);}
void mapping(const var& m){
    for(auto* key:{"level","contrast"})if(!numeric(m[key]))fail("Réglages de dynamique invalides.");
    for(auto* key:{"min","max"})if(!uint(m[key])||static_cast<juce::int64>(m[key])>255)fail("Réglages de dynamique invalides.");
    int min=m["min"],max=m["max"];
    if(min<1||min>max||max>127||!range(m["level"],min,max)||!range(m["contrast"],0,250))fail("Dynamique : niveau dans la plage min–max (1–127), contraste 0–250 %.");
}
}
void checkUi(const var& ui,double duration){
    auto transcription=ui["transcription"];
    if(!nil(transcription)){
        if(!transcription.isObject()||!option(transcription["engine"],{"basic_pitch","muscriptor"})||!option(transcription["model"],{"small","medium","large"}))fail("Moteur de transcription invalide.");
        basicPitch::Options::read(transcription["basic_pitch"]);
    }

    if(!ui.isObject()||juce::JSON::toString(ui,juce::JSON::FormatOptions().withSpacing(juce::JSON::Spacing::none)).getNumBytesAsUTF8()>1000000)fail("État de projet invalide.");
    if(!nil(ui["guided"])){auto state=ui["guided"];if(!state.isObject()||!state["use_mask"].isBool()||!state["visible"].isBool()||!state["curve"].isBool()||!option(state["zone"],{"mask","visible","all"})||!range(state["brush"],.1,12))fail("Réglages du masque invalides.");}
    if(!nil(ui["waveOpacity"])&&!range(ui["waveOpacity"],0,1))fail("Opacité du signal invalide.");
    if(!nil(ui["waveVisualGain"])&&!range(ui["waveVisualGain"],.25,8))fail("Gain visuel du signal invalide.");
    struct Field {const char* key;double low,high;};
    for(auto f:{Field{"position",0,duration},{"sensitivity",0,100},{"mode",0,2},{"audioDb",-60,0},{"midiDb",-60,12}}){
        if(!numeric(ui[f.key]))throw std::invalid_argument(std::string("Réglage absent : ")+f.key);
        if(!range(ui[f.key],f.low,f.high))throw std::invalid_argument(std::string("Réglage invalide : ")+f.key);
    }
    auto grid=ui["rhythmicGrid"];if(!nil(grid)){
        if(!grid.isObject()||!option(grid["mode"],{"musical","seconds","none"})||!option(grid["subdivision"],{"1/4","1/8","1/16","1/8T","1/16T"})||!range(grid["offset"],-86400,86400))fail("Grille de projet invalide.");
    }
    if(!nil(ui["linkOverlapMs"])&&!range(ui["linkOverlapMs"],0,500))fail("Chevauchement invalide dans le projet.");
    if(!nil(ui["dynamicsDraft"])){auto draft=ui["dynamicsDraft"];if(!draft["manualRange"].isBool())fail("Plage de dynamique invalide.");mapping(draft["mapping"]);}
    if(!nil(ui["referenceAudio"])&&!ui["referenceAudio"].isBool())fail("État de référence audio invalide.");
    if(!nil(ui["reverbEnabled"])&&!ui["reverbEnabled"].isBool())fail("État de réverbération invalide.");
    if(!nil(ui["reverbAmount"])&&!range(ui["reverbAmount"],0,100))fail("Niveau de réverbération invalide.");
    if(!nil(ui["piano"])&&!option(ui["piano"],{"control","basic"}))fail("Piano de projet invalide.");
    for(auto* key:{"follow","correctionOpen","tonalityOpen"})if(!ui[key].isBool())throw std::invalid_argument(std::string("Réglage absent : ")+key);
    auto tone=ui["tone"];if(nil(tone)||!tone["enabled"].isBool())fail("Gamme de projet absente.");
    if(!nil(tone["ids"]))ids(tone["ids"],100000,"Portée tonale invalide","Portée tonale invalide.");
    auto shortNotes=ui["short"];if(!nil(shortNotes)){
        if(!numeric(shortNotes["threshold_ms"]))fail("Seuil de durée absent");
        if(!range(shortNotes["threshold_ms"],1,1000)||!shortNotes["enabled"].isBool()||!option(shortNotes["scope"],{"all","selection"}))fail("Diagnostic de durée invalide.");
        if(!nil(shortNotes["ids"]))ids(shortNotes["ids"],100000,"Portée de durée invalide","Portée de durée invalide.");
    }
    auto selection=ui["selection"];if(!nil(selection)){
        ids(selection["ids"],1000000,"Sélection invalide","Sélection invalide.");
        for(auto f:{Field{"start",0,duration},{"end",0,duration},{"low",0,127},{"high",0,127}}){if(!numeric(selection[f.key]))fail("Sélection invalide");if(!range(selection[f.key],f.low,f.high))fail("Sélection hors limites.");}
        if(static_cast<double>(selection["start"])>static_cast<double>(selection["end"])||static_cast<double>(selection["low"])>static_cast<double>(selection["high"]))fail("Sélection inversée.");
    }
    if(!ui["profile"].isString())fail("Profil absent");if(!option(ui["profile"],{"versatile","guitar","piano"}))fail("Profil inconnu.");
    auto scale=tone["scale"];if(nil(scale))fail("Gamme de projet absente.");
    if(!uint(scale["tonic"])||static_cast<juce::int64>(scale["tonic"])>255)fail("Tonique de projet invalide.");
    if(!option(scale["mode"],{"major","minor","harmonic_minor","melodic_minor","dorian","phrygian","lydian","mixolydian","locrian","major_pentatonic","minor_pentatonic","chromatic"}))fail("Mode de gamme invalide.");
    if(static_cast<int>(scale["tonic"])>11)fail("Tonique invalide.");
    if(!option(tone["scope"],{"all","selection"}))fail("Portée tonale invalide.");
    auto view=ui["view"];if(!nil(view))for(auto f:{Field{"zoom",1,512},{"pan",0,1},{"pitchZoom",1,64},{"pitchPan",0,1},{"audioHeight",30,2000},{"keyHeight",20,1000}}){if(!numeric(view[f.key]))fail("Vue invalide");if(!range(view[f.key],f.low,f.high))fail("Vue hors limites.");}
}
void checkAudioOrigins(const var& origins){
    if(nil(origins))return;auto* object=origins.getDynamicObject();if(!object||object->getProperties().size()>4)fail("Empreinte de mesure audio invalide.");
    for(auto& field:object->getProperties()){
        auto key=field.name.toString();if(key!="dynamics"&&key!="verification"&&key!="cleanup"&&key!="transcription")fail("Empreinte de mesure audio invalide.");
        auto hash=field.value["sha256"],name=field.value["name"];
        if(!hash.isString()||hash.toString().length()!=64||hash.toString().containsOnly("0123456789abcdefABCDEF")==false||!name.isString()||name.toString().getNumBytesAsUTF8()>4096)fail("Empreinte de mesure audio invalide.");
    }
}
}
