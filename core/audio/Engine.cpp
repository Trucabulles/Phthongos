#include "Engine.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <mutex>
#include <stdexcept>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace phthongos::audio {
namespace {
std::mutex fluidInit;
using P=void*;
void verify(const juce::File& file,const char* expected,const char* error){auto in=file.createInputStream();if(!in)throw std::invalid_argument(error);juce::BufferedInputStream buffered(*in,65536);if(juce::SHA256(buffered).toHexString()!=expected)throw std::invalid_argument(error);}
}
struct Sampler::Impl {
#if JUCE_WINDOWS
 HMODULE dll=nullptr;
#else
 juce::DynamicLibrary dll;
#endif
 P settings=nullptr,synth=nullptr;std::array<P,129> presets{};std::vector<P> customPresets;std::map<std::string,unsigned> customPrograms;std::map<std::string,P> fonts;juce::File piano;
 P(*newSettings)();void(*deleteSettings)(P);int(*setInt)(P,const char*,int);int(*setNum)(P,const char*,double);P(*newSynth)(P);void(*deleteSynth)(P);
 int(*load)(P,const char*,int);P(*font)(P,int);P(*preset)(P,int,int);int(*start)(P,unsigned,P,int,int,int,int);int(*stop)(P,unsigned);
 int(*setGenerator)(P,int,int,float);int(*reset)(P);int(*kill)(P,int);int(*cc)(P,int,int,int);int(*reverb)(P,int,int);int(*chorus)(P,int,int);int(*render)(P,int,P,int,int,P,int,int);
 void* symbol(const char* name){
#if JUCE_WINDOWS
 auto p=reinterpret_cast<void*>(GetProcAddress(dll,name));
#else
 auto p=dll.getFunction(name);
#endif
 if(!p)throw std::runtime_error(std::string("FluidSynth : fonction absente ")+name);return p;}
 ~Impl(){if(synth)deleteSynth(synth);if(settings)deleteSettings(settings);
#if JUCE_WINDOWS
 if(dll)FreeLibrary(dll);
#else
 dll.close();
#endif
 }
};
Sampler::Sampler(const juce::File& piano,const juce::File& bank,unsigned rate):impl(std::make_unique<Impl>()){
 verify(piano,"cf2a98eb38a32c4954b4b6e2caae4112d62dd8e892eceefdd7942b0e7d01ac2f","Empreinte du piano droit incorrecte.");
 bool gm=bank!=juce::File();if(gm)verify(bank,"59287cbb0f424ec76d1bc269eee40dfe0b02252588d72eb016b5d2382e710a8f","Le fichier SF2 ne correspond pas à MuseScore General HQ 0.2.1.");
 impl->piano=piano;
 auto dll=piano.getSiblingFile("fluidsynth/libfluidsynth-3.dll");auto& a=*impl;
#if JUCE_WINDOWS
 a.dll=LoadLibraryExW(dll.getFullPathName().toWideCharPointer(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!a.dll)throw std::runtime_error("Bibliothèque FluidSynth absente.");
#else
 if(!a.dll.open(dll.getFullPathName()))throw std::runtime_error("Bibliothèque FluidSynth absente.");
#endif
#define LOAD(member,name) a.member=reinterpret_cast<decltype(a.member)>(a.symbol(name))
 LOAD(setGenerator,"fluid_synth_set_gen");LOAD(newSettings,"new_fluid_settings");LOAD(deleteSettings,"delete_fluid_settings");LOAD(setInt,"fluid_settings_setint");LOAD(setNum,"fluid_settings_setnum");LOAD(newSynth,"new_fluid_synth");LOAD(deleteSynth,"delete_fluid_synth");LOAD(load,"fluid_synth_sfload");LOAD(font,"fluid_synth_get_sfont_by_id");LOAD(preset,"fluid_sfont_get_preset");LOAD(start,"fluid_synth_start");LOAD(stop,"fluid_synth_stop");LOAD(reset,"fluid_synth_system_reset");LOAD(kill,"fluid_synth_all_sounds_off");LOAD(cc,"fluid_synth_cc");LOAD(reverb,"fluid_synth_reverb_on");LOAD(chorus,"fluid_synth_chorus_on");LOAD(render,"fluid_synth_write_float");
#undef LOAD
 std::lock_guard<std::mutex> lock(fluidInit);a.settings=a.newSettings();if(!a.settings)throw std::runtime_error("FluidSynth : paramètres indisponibles.");
 for(auto x:std::vector<std::pair<const char*,double>>{{"synth.sample-rate",double(rate)},{"synth.gain",1.},{"synth.reverb.room-size",.6},{"synth.reverb.level",.5}})if(a.setNum(a.settings,x.first,x.second)<0)throw std::runtime_error("Réglage FluidSynth refusé.");
 for(auto x:std::vector<std::pair<const char*,int>>{{"synth.midi-channels",128},{"synth.polyphony",512},{"synth.cpu-cores",1},{"synth.threadsafe-api",0},{"synth.dynamic-sample-loading",0},{"synth.chorus.active",0},{"synth.reverb.active",0}})if(a.setInt(a.settings,x.first,x.second)<0)throw std::runtime_error("Réglage FluidSynth refusé.");
 a.synth=a.newSynth(a.settings);if(!a.synth)throw std::runtime_error("Création du lecteur FluidSynth impossible.");
 auto id=a.load(a.synth,(gm?bank:piano).getFullPathName().toRawUTF8(),1);if(id<0)throw std::runtime_error("Chargement de MuseScore General HQ impossible.");auto font=a.font(a.synth,id);if(!font)throw std::runtime_error("Banque HQ introuvable après chargement.");
 for(unsigned p=0;p<129;++p){a.presets[p]=a.preset(font,gm&&p==128?128:0,!gm||p==128?0:int(p));if(!a.presets[p])throw std::runtime_error("Instrument absent : "+std::to_string(p));}
 if(gm){id=a.load(a.synth,piano.getFullPathName().toRawUTF8(),0);if(id<0)throw std::runtime_error("Chargement du piano d’origine impossible.");font=a.font(a.synth,id);if(!font)throw std::runtime_error("Piano d’origine absent après chargement.");a.presets[0]=a.preset(font,0,0);if(!a.presets[0])throw std::runtime_error("Preset du piano d’origine absent.");}
 effects(0);std::array<float,512> l{},r{};render(l.data(),r.data(),512);reset();effects(0);
}
juce::var Sampler::inspectSoundfont(const juce::File& piano,const juce::File& file){
 if(!file.hasFileExtension("sf2")||!file.existsAsFile()||file.getSize()<12||file.getSize()>2147483647)throw std::invalid_argument("Fichier SF2 invalide ou introuvable.");
 auto input=file.createInputStream();char magic[12];if(!input||input->read(magic,12)!=12||std::memcmp(magic,"RIFF",4)||std::memcmp(magic+8,"sfbk",4))throw std::invalid_argument("Fichier SF2 invalide ou introuvable.");
 Sampler probe(piano,{},22050);auto& a=*probe.impl;int id=a.load(a.synth,file.getFullPathName().toRawUTF8(),0);if(id<0)throw std::invalid_argument("Chargement SF2 impossible.");auto font=a.font(a.synth,id);
 auto begin=reinterpret_cast<void(*)(P)>(a.symbol("fluid_sfont_iteration_start"));auto next=reinterpret_cast<P(*)(P)>(a.symbol("fluid_sfont_iteration_next"));auto name=reinterpret_cast<const char*(*)(P)>(a.symbol("fluid_preset_get_name"));auto bank=reinterpret_cast<int(*)(P)>(a.symbol("fluid_preset_get_banknum"));auto preset=reinterpret_cast<int(*)(P)>(a.symbol("fluid_preset_get_num"));juce::Array<juce::var> items;
 begin(font);while(auto p=next(font)){int b=bank(p),n=preset(p);if(b<0||b>16383||n<0||n>127||items.size()>=4096)throw std::invalid_argument("Presets SF2 invalides.");auto* v=new juce::DynamicObject;v->setProperty("bank",b);v->setProperty("preset",n);v->setProperty("name",juce::String::fromUTF8(name(p)));items.add(juce::var(v));}
 if(items.isEmpty())throw std::invalid_argument("Aucun preset dans la banque SF2.");auto in=file.createInputStream();auto* out=new juce::DynamicObject;out->setProperty("path",file.getFullPathName());out->setProperty("name",file.getFileNameWithoutExtension());out->setProperty("sha256",juce::SHA256(*in).toHexString());out->setProperty("presets",juce::var(items));return juce::var(out);
}
unsigned Sampler::program(const Part& part){
 if(part.soundfontPath.empty())return part.program;auto& a=*impl;auto key=part.soundfontPath+"|"+part.soundfontHash+"|"+std::to_string(part.soundfontBank)+"|"+std::to_string(part.soundfontPreset);auto found=a.customPrograms.find(key);if(found!=a.customPrograms.end())return found->second;
 auto fontKey=part.soundfontPath+"|"+part.soundfontHash;P font=nullptr;auto at=a.fonts.find(fontKey);if(at!=a.fonts.end())font=at->second;else{
  juce::File path(juce::String::fromUTF8(part.soundfontPath.c_str()));verify(path,part.soundfontHash.c_str(),"Banque SF2 absente ou modifiée : redéclarez-la dans Paramètres.");int id=a.load(a.synth,path.getFullPathName().toRawUTF8(),0);if(id<0)throw std::runtime_error("Chargement SF2 impossible.");font=a.font(a.synth,id);a.fonts.emplace(fontKey,font);
 }P p=a.preset(font,int(part.soundfontBank),int(part.soundfontPreset));if(!p)throw std::runtime_error("Preset SF2 introuvable.");unsigned token=unsigned(129+a.customPresets.size());a.customPresets.push_back(p);a.customPrograms[key]=token;return token;
}
Sampler::~Sampler()=default;
void Sampler::start(unsigned program,int pitch,int velocity,std::uint64_t occurrence,unsigned channel){auto& a=*impl;a.start(a.synth,unsigned(occurrence),(program<129?a.presets[program]:a.customPresets.at(program-129)),0,int(channel),pitch,velocity);}
void Sampler::level(unsigned channel,float db){if(channel>=128||!std::isfinite(db)||db< -60||db>0)throw std::invalid_argument("Volume de voix invalide.");if(impl->setGenerator(impl->synth,int(channel),48,-10.f*db)<0)throw std::runtime_error("Volume de voix indisponible.");}
void Sampler::stop(std::uint64_t occurrence){impl->stop(impl->synth,unsigned(occurrence));}
void Sampler::reset(){auto& a=*impl;a.reset(a.synth);a.kill(a.synth,-1);std::array<float,128> l{},r{};render(l.data(),r.data(),128);}
void Sampler::effects(unsigned amount){auto& a=*impl;a.reverb(a.synth,-1,amount>0?1:0);a.chorus(a.synth,-1,0);for(int ch=0;ch<128;++ch){a.cc(a.synth,ch,91,int(amount*127+50)/100);a.cc(a.synth,ch,93,0);}}
void Sampler::render(float* l,float* r,int count){auto& a=*impl;if(a.render(a.synth,count,l,0,1,r,0,1)<0){std::fill(l,l+count,0.f);std::fill(r,r+count,0.f);}}
Engine::Engine(unsigned r,std::unique_ptr<Sampler> s,std::unique_ptr<Sampler> a):rate(r),pcm(std::make_shared<const analysis::StereoPcm>()),synth(std::move(s)),auditionSynth(std::move(a)){}
void Engine::setReverb(unsigned amount){reverb=std::min(amount,100u);synth->effects(reverb);auditionSynth->effects(reverb);}
void Engine::cancelAudition(){auditionHeld=false;if(liveNotes.empty()&&!liveTail){transitionLeft=0;auditionLast={};auditionSynth->reset();setReverb(reverb);auditionOffset=block;}else if(auditionLeft)auditionSynth->stop(auditionOccurrence);auditionLeft=0;}
void Engine::silence(){endLeft=endTotal=endQuiet=0;cancelAudition();synth->reset();setReverb(reverb);applyLevels();active.fill(0);offset=block;}
void Engine::seek(std::uint64_t frame){silence();position=std::min(frame,recording?std::uint64_t(rate)*1200:std::uint64_t(pcm->size()));cursor=std::upper_bound(events.begin(),events.end(),position,[](auto p,const Event& e){return p<e.frame;})-events.begin();if(playing&&mode!=0)for(std::size_t i=0;i<spans.size();++i){auto& n=spans[i];if(n.start<=position&&position<n.end){synth->start(n.program,n.pitch,n.velocity,i,n.channel);++active[n.pitch];}}}
void Engine::pause(){playing=false;countRemaining=countTotal=0;click.reset();silence();}
void Engine::play(){if(!recording&&position>=pcm->size())position=0;playing=true;seek(position);}
void Engine::applyLevels(){for(const auto& entry:voiceChannels){auto it=voiceLevels.find(entry.first);synth->level(entry.second,it==voiceLevels.end()?0.f:it->second);}}
void Engine::levels(const std::map<int,float>& values){voiceLevels=values;applyLevels();}
void Engine::notes(const std::vector<Note>& input){std::map<int,unsigned> programs;for(const auto& n:input)if(!programs.count(n.key()))programs[n.key()]=n.part?synth->program(*n.part):0u;spans.clear();events.clear();voiceChannels.clear();for(const auto& n:input)voiceChannels.emplace(n.key(),0);if(voiceChannels.size()>128)throw std::invalid_argument("Trop de voix pour la lecture (maximum 128).");unsigned channel=0;for(auto& entry:voiceChannels)entry.second=channel++;for(auto& n:input){if(n.pitch<0||n.pitch>127||!std::isfinite(n.start)||!std::isfinite(n.end)||n.end<=n.start)continue;auto a=std::uint64_t(std::round(double(std::max(n.start,0.f))*rate)),b=std::uint64_t(std::round(double(std::max(n.end,0.f))*rate));spans.push_back({programs.at(n.key()),voiceChannels.at(n.key()),a,std::max(b,a+1),n.pitch,n.velocity});}for(std::size_t i=0;i<spans.size();++i){events.push_back({spans[i].start,i,true});events.push_back({spans[i].end,i,false});}std::sort(events.begin(),events.end(),[](auto a,auto b){return std::tie(a.frame,a.on,a.index)<std::tie(b.frame,b.on,b.index);});seek(position);}
void Engine::beginAudition(unsigned program,int pitch,int velocity,float db,bool held){
 if(auditionLeft){auditionSynth->stop(auditionOccurrence);transitionFrom=auditionLast;transitionFrames=std::max(rate/200,1u);transitionLeft=transitionFrames;}
 else cancelAudition();
 auditionPitch=pitch;auditionHeld=held;auditionReleaseFrames=reverb>0?std::uint64_t(rate)*2:rate/5;auditionLeft=held?auditionReleaseFrames+1:std::uint64_t(rate)*3/10+auditionReleaseFrames;
 auditionSynth->level(0,db);auditionSynth->start(program,pitch,velocity,++auditionOccurrence);
}
void Engine::audition(unsigned program,int pitch,int velocity){if(playing||pitch<0||pitch>127||velocity<1||velocity>127)return;beginAudition(program,pitch,velocity,0,false);}
void Engine::keyDown(unsigned program,int pitch,int velocity,float db){beginAudition(program,pitch,velocity,db,true);}
void Engine::keyUp(){if(!auditionHeld)return;auditionHeld=false;auditionSynth->stop(auditionOccurrence);auditionLeft=auditionReleaseFrames;}
void Engine::midiNote(unsigned channel,int pitch,int velocity,unsigned program,float db){
 if(auditionLeft||!liveNotes.empty()||liveTail){transitionFrom=auditionLast;transitionFrames=std::max(rate/500,1u);transitionLeft=transitionFrames;}
 const unsigned key=channel*128+unsigned(pitch);auto found=liveNotes.find(key);
 if(found!=liveNotes.end()){if(!velocity&&livePedal[channel]){found->second.down=false;return;}auditionSynth->stop(found->second.occurrence);liveNotes.erase(found);}
 if(velocity){auto occurrence=0x80000000u|unsigned(++liveOccurrence);auditionSynth->level(channel+1,db);auditionSynth->start(program,pitch,velocity,occurrence,channel+1);liveNotes[key]={occurrence,pitch,channel,true};}
 liveTail=reverb?std::uint64_t(rate)*2:rate/5;
}
void Engine::midiSustain(unsigned channel,bool down){livePedal[channel]=down;if(!down){for(auto i=liveNotes.begin();i!=liveNotes.end();)if(i->second.channel==channel&&!i->second.down){auditionSynth->stop(i->second.occurrence);i=liveNotes.erase(i);}else ++i;liveTail=reverb?std::uint64_t(rate)*2:rate/5;}}
void Engine::midiPanic(){if(liveNotes.empty()&&!liveTail)return;for(auto& entry:liveNotes)auditionSynth->stop(entry.second.occurrence);liveNotes.clear();livePedal.fill(false);liveTail=std::max(rate/100,1u);}
std::array<bool,128> Engine::keys()const{std::array<bool,128> out{};for(std::size_t i=0;i<128;++i)out[i]=active[i]>0;if(auditionLeft>auditionReleaseFrames)out[auditionPitch]=true;for(auto& n:liveNotes)out[n.second.pitch]=true;return out;}
std::array<float,2> Engine::nextAudition(){if(!auditionLeft&&liveNotes.empty()&&!liveTail)return {};if(auditionLeft&&!auditionHeld&&auditionLeft==auditionReleaseFrames){auditionSynth->stop(auditionOccurrence);}if(auditionOffset==block){std::array<float,block> l{},r{};auditionSynth->render(l.data(),r.data(),block);for(unsigned i=0;i<block;++i)auditionCache[i]={l[i],r[i]};auditionOffset=0;}float fade=(auditionHeld||!liveNotes.empty())?1.f:std::min(float(std::max(auditionLeft,liveTail))/(float(rate)*.01f),1.f);auto out=auditionCache[auditionOffset];for(auto& x:out){x=x*midiGain*fade;if(std::abs(x)>1)++clipped;x=std::clamp(x,-1.f,1.f);}if(transitionLeft){float mix=1.f-float(transitionLeft)/transitionFrames;for(int c=0;c<2;++c)out[c]=transitionFrom[c]*(1.f-mix)+out[c]*mix;--transitionLeft;}auditionLast=out;if(auditionLeft&&!auditionHeld)--auditionLeft;if(liveTail&&liveNotes.empty())--liveTail;++auditionOffset;if(!auditionLeft&&liveNotes.empty()&&!liveTail)cancelAudition();return out;}
std::array<float,2> Engine::nextEnd(){
 if(!endTotal){for(std::size_t i=0;i<spans.size();++i)if(spans[i].start<position&&spans[i].end>=(position>block-1?position-(block-1):0))synth->stop(i);active.fill(0);endTotal=mode==0?std::max(rate/200,1u):reverb>0?std::uint64_t(rate)*4:rate/2;endLeft=endTotal;}
 if(offset==block){std::array<float,block> l{},r{};synth->render(l.data(),r.data(),block);for(unsigned i=0;i<block;++i)cache[i]={l[i],r[i]};offset=0;}
 auto elapsed=endTotal-endLeft;float audioFade=std::max(1.f-float(elapsed+1)/std::max(float(rate)*.005f,1.f),0.f),midiFade=std::min(float(endLeft?endLeft-1:0)/std::max(float(rate)*.05f,1.f),1.f);auto last=pcm->empty()?std::array<float,2>{}:pcm->back();std::array<float,2> out{};
 for(int c=0;c<2;++c){float v=(mode!=1?last[c]*audioGain*audioFade:0.f)+(mode!=0?cache[offset][c]*midiGain*midiFade:0.f);if(std::abs(v)>1)++clipped;out[c]=std::clamp(v,-1.f,1.f);}++offset;--endLeft;if(std::abs(out[0])<1e-6f&&std::abs(out[1])<1e-6f)++endQuiet;else endQuiet=0;
 if(!endLeft||(elapsed>=rate/5&&endQuiet>=rate/50))pause();return out;
}
std::array<float,2> Engine::nextPerformance(){if(!playing)return {};if(recording&&position>=std::uint64_t(rate)*1200){playing=false;recording=false;return {};}if(!recording&&position>=pcm->size())return nextEnd();
 if(offset==block){while(cursor<events.size()&&events[cursor].frame<=position){auto e=events[cursor];auto& n=spans[e.index];if(mode!=0){if(e.on){synth->start(n.program,n.pitch,n.velocity,e.index,n.channel);++active[n.pitch];}else{if(active[n.pitch])--active[n.pitch];synth->stop(e.index);}}++cursor;}
 std::array<float,block> l{},r{};synth->render(l.data(),r.data(),block);for(unsigned i=0;i<block;++i)cache[i]={l[i],r[i]};offset=0;}
 auto a=position<pcm->size()?(*pcm)[position]:std::array<float,2>{},m=cache[offset];++position;++offset;std::array<float,2> out{};for(int c=0;c<2;++c){float v=(mode!=1?a[c]*audioGain:0.f)+(mode!=0?m[c]*midiGain:0.f);if(std::abs(v)>1)++clipped;out[c]=std::clamp(v,-1.f,1.f);}return out;
}
void Engine::preCount(unsigned bars){
 if(bars>2)throw std::invalid_argument("Invalid MIDI pre-count");
 double seconds=double(position)/rate,beat=click.grid.beatAt(seconds);
 auto meter=click.grid.meters.front();for(auto m:click.grid.meters)if(m.beat<=beat)meter=m;
 auto tempo=click.grid.tempos.front();for(auto t:click.grid.tempos)if(t.seconds+click.grid.origin<=seconds)tempo=t;
 countNumerator=meter.numerator;countPeriod=tempo.quarter*4./meter.denominator*rate;
 countRemaining=countTotal=std::uint64_t(std::llround(bars*countNumerator*countPeriod));
}
std::array<float,2> Engine::next(){
 float tick=0;std::array<float,2> out{};
 if(playing&&countRemaining){auto elapsed=countTotal-countRemaining;auto pulse=std::uint64_t(std::floor(double(elapsed)/countPeriod));auto start=std::uint64_t(std::llround(pulse*countPeriod));tick=click.pulse(elapsed>=start?elapsed-start:0,pulse%countNumerator==0);--countRemaining;}
 else {if(playing&&(recording||position<pcm->size()))tick=click.sample(position);out=nextPerformance();}
 for(auto& sample:out)sample+=tick;
 auto preview=nextAudition();for(int c=0;c<2;++c){auto sum=out[c]+preview[c];if(std::abs(sum)>1)++clipped;out[c]=std::clamp(sum,-1.f,1.f);}return out;}
}
