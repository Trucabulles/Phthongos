// Basic Pitch note decoding adapted from Spotify basic_pitch/note_creation.py.
// Copyright 2022 Spotify AB. Apache-2.0; see models/basic-pitch/LICENSE and NOTICE.
#include "BasicPitch.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>
namespace phthongos::basicPitch {
namespace {
using juce::var;using juce::String;
var obj(std::initializer_list<std::pair<const char*,var>> values){auto* o=new juce::DynamicObject;for(auto& p:values)o->setProperty(p.first,p.second);return var(o);}
double number(const var& value,double low,double high){
 if(!value.isInt()&&!value.isInt64()&&!value.isDouble())throw std::invalid_argument("Métadonnées Basic Pitch invalides.");
 auto n=double(value);if(!std::isfinite(n)||n<low||n>high)throw std::invalid_argument("Métadonnées Basic Pitch invalides.");return n;
}
void checkCancel(const std::atomic_bool* flag){if(flag&&flag->load())throw std::runtime_error("Transcription annulée.");}
}
Options Options::read(const var& value){
 if(value.isVoid()||value.isUndefined())return {};
 Options options;options.noteSensitivity=number(value["noteSensitivity"],.05,.95);options.splitSensitivity=number(value["splitSensitivity"],.05,.95);options.minNoteMs=number(value["minNoteMs"],35,580);
 for(auto field:{"minPitch","maxPitch"})if(value.hasProperty(field)){auto n=number(value[field],21,108);if(n!=std::floor(n))throw std::invalid_argument("Métadonnées Basic Pitch invalides.");if(String(field)=="minPitch")options.minPitch=int(n);else options.maxPitch=int(n);}
 if(options.minPitch>options.maxPitch)throw std::invalid_argument("Métadonnées Basic Pitch invalides.");
 for(auto field:{"inferOnsets","melodiaTrick"})if(value.hasProperty(field)){if(!value[field].isBool())throw std::invalid_argument("Métadonnées Basic Pitch invalides.");if(String(field)=="inferOnsets")options.inferOnsets=bool(value[field]);else options.melodiaTrick=bool(value[field]);}
 if(value.hasProperty("energyTolerance")){auto n=number(value["energyTolerance"],1,25);if(n!=std::floor(n))throw std::invalid_argument("Métadonnées Basic Pitch invalides.");options.energyTolerance=int(n);}
 return options;
}
var Options::json()const{return obj({{"noteSensitivity",noteSensitivity},{"splitSensitivity",splitSensitivity},{"minNoteMs",minNoteMs},{"minPitch",minPitch},{"maxPitch",maxPitch},{"inferOnsets",inferOnsets},{"melodiaTrick",melodiaTrick},{"energyTolerance",energyTolerance}});}
void Cache::validate()const{
 if(!samples||samples>rate*420ull||notes.empty()||notes.size()%bins||notes.size()!=onsets.size()||frames()!=std::size_t(double(samples)*keptFrames/hop))throw std::invalid_argument("Dimensions du cache Basic Pitch invalides.");
 for(const auto* values:{&notes,&onsets})for(float p:*values)if(!std::isfinite(p)||p<0||p>1)throw std::invalid_argument("Probabilités Basic Pitch invalides.");
}
double frameTime(std::size_t frame){
 // Retained rows 15..156 of each 172-frame window. Anchor each window to
 // its actual input sample offset: no accumulated drift across long stems.
 return double((frame/keptFrames)*hop+(frame%keptFrames)*256)/rate;
}
var decode(const Cache& cache,const Options& options,const std::atomic_bool* cancelled){
 cache.validate();Options::read(options.json());checkCancel(cancelled);
 const int count=int(cache.frames()),last=count-1,minLength=int(std::round(options.minNoteMs*rate/256000.));
 const float frameThreshold=float(1-options.noteSensitivity),onsetThreshold=float(1-options.splitSensitivity);
 // Decode from private copies: range changes must never alter the posterior cache.
 auto frames=cache.notes,onsets=cache.onsets;
 for(int f=0;f<count;++f)for(int p=0;p<int(bins);++p)if(p+21<options.minPitch||p+21>options.maxPitch){frames[std::size_t(f)*bins+p]=0;onsets[std::size_t(f)*bins+p]=0;}
 auto remaining=frames;std::vector<float> diff(remaining.size(),0.f);
 if(options.inferOnsets){
 float maxDiff=0,maxOnset=*std::max_element(onsets.begin(),onsets.end());
 for(int f=2;f<count;++f)for(int p=0;p<int(bins);++p){auto i=f*bins+p;auto value=std::max(0.f,std::min(frames[i]-frames[i-bins],frames[i]-frames[i-2*bins]));diff[i]=value;maxDiff=std::max(maxDiff,value);}
 if(maxDiff>0)for(std::size_t i=0;i<onsets.size();++i)onsets[i]=std::max(onsets[i],maxOnset*diff[i]/maxDiff);
 }
 struct Event{int start,end,pitch;double amplitude;};std::vector<Event> events;
 auto erase=[&](int f,int p){for(int b=std::max(0,p-1);b<=std::min(87,p+1);++b)remaining[std::size_t(f)*bins+b]=0;};
 auto add=[&](int start,int end,int pitch){
   if(end-start<=minLength)return;
   double amplitude=0;for(int f=start;f<end;++f)amplitude+=cache.notes[std::size_t(f)*bins+pitch];
   events.push_back({start,end,pitch+21,amplitude/(end-start)});
   if(events.size()>100000)throw std::runtime_error("Trop de notes Basic Pitch : réduisez la sensibilité des notes.");
 };
 // Strict local onset peaks, backwards in time, matching Spotify's argrelmax.
 for(int f=last-1;f>0;--f){if((f&255)==0)checkCancel(cancelled);
  for(int p=87;p>=0;--p){auto i=std::size_t(f)*bins+p;float onset=onsets[i];
   if(onset<onsetThreshold||onset<=onsets[i-bins]||onset<=onsets[i+bins])continue;
   int end=f+1,below=0;while(end<last&&below<options.energyTolerance){below=remaining[std::size_t(end)*bins+p]<frameThreshold?below+1:0;++end;}end-=below;
   if(end-f<=minLength)continue;add(f,end,p);for(int t=f;t<end;++t)erase(t,p);
  }
 }
 // Remaining-energy (Melodia) pass. Values only decrease to zero, so a single
 // stable descending index replaces the quadratic repeated argmax scan.
 if(options.melodiaTrick){
 std::vector<std::size_t> order(remaining.size());std::iota(order.begin(),order.end(),0);
 std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return remaining[a]>remaining[b];});
 for(auto index:order){checkCancel(cancelled);float value=remaining[index];if(value<=frameThreshold)continue;
  int middle=int(index/bins),pitch=int(index%bins);remaining[index]=0;
  int t=middle+1,below=0;while(t<last&&below<options.energyTolerance){below=remaining[std::size_t(t)*bins+pitch]<frameThreshold?below+1:0;erase(t,pitch);++t;}int end=t-1-below;
  t=middle-1;below=0;while(t>0&&below<options.energyTolerance){below=remaining[std::size_t(t)*bins+pitch]<frameThreshold?below+1:0;erase(t,pitch);--t;}int start=t+1+below;
  if(start>=0&&end>start)add(start,end,pitch);
 }
 }
 std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b){return std::tie(a.start,a.pitch,a.end)<std::tie(b.start,b.pitch,b.end);});
 juce::Array<var> notes;double duration=double(cache.samples)/rate;
 for(const auto& e:events){double start=frameTime(std::size_t(e.start)),end=std::min(duration,frameTime(std::size_t(e.end)));if(end<=start||end-start<.01)continue;
  notes.add(obj({{"onset",start},{"offset",end},{"pitch",e.pitch},{"velocity",std::clamp(int(std::round(e.amplitude*127)),1,127)}}));}
 return obj({{"model_sha256",modelHash},{"decode_version",1},{"sample_rate",int(rate)},{"input_samples",var(juce::int64(cache.samples))},{"settings",options.json()},{"notes",var(notes)}});
}
std::vector<Note> restore(const var& metadata,const std::vector<std::string>& names){
 if(metadata["model_sha256"].toString()!=modelHash||number(metadata["decode_version"],1,1)!=1||number(metadata["sample_rate"],rate,rate)!=rate)throw std::invalid_argument("Provenance du modèle Basic Pitch invalide.");
 double duration=number(metadata["input_samples"],1,rate*420.)/rate;Options::read(metadata["settings"]);
 auto* raw=metadata["notes"].getArray();if(!raw||raw->size()>100000)throw std::invalid_argument("Notes Basic Pitch invalides.");
 std::vector<Note> notes;for(auto value:*raw){Note n;n.id=notes.size()+1;n.sourceIds={n.id};n.start=float(number(value["onset"],0,duration));n.end=float(number(value["offset"],0,duration));
  auto pitch=number(value["pitch"],21,108),velocity=number(value["velocity"],1,127);if(pitch!=std::floor(pitch)||velocity!=std::floor(velocity)||n.end<=n.start)throw std::invalid_argument("Note Basic Pitch invalide.");
  n.pitch=int(pitch);n.velocity=int(velocity);n.velocityNormalised=n.velocity/127.f;n.part=Part{{},0,false,names.empty()?"Piano":names[0]};notes.push_back(n);}
 return notes;
}
std::vector<std::uint8_t> pack(const std::vector<float>& values){std::vector<std::uint8_t> out(values.size()*4);for(std::size_t i=0;i<values.size();++i){std::uint32_t bits;std::memcpy(&bits,&values[i],4);for(int b=0;b<4;++b)out[i*4+b]=std::uint8_t(bits>>(8*b));}return out;}
std::vector<float> unpack(const std::vector<std::uint8_t>& bytes){if(bytes.size()%4)throw std::invalid_argument("Carte de probabilités Basic Pitch invalide.");std::vector<float> out(bytes.size()/4);for(std::size_t i=0;i<out.size();++i){std::uint32_t bits=0;for(int b=0;b<4;++b)bits|=std::uint32_t(bytes[i*4+b])<<(8*b);std::memcpy(&out[i],&bits,4);}return out;}

var encodeCache(const Cache& cache){
 cache.validate();juce::MemoryOutputStream output;
 {juce::GZIPCompressorOutputStream compressed(output,6);auto notes=pack(cache.notes),onsets=pack(cache.onsets);if(!compressed.write(notes.data(),notes.size())||!compressed.write(onsets.data(),onsets.size()))throw std::runtime_error("Cache Basic Pitch invalide.");}
 return obj({{"model_sha256",modelHash},{"samples",var(juce::int64(cache.samples))},{"compressed",juce::Base64::toBase64(output.getData(),output.getDataSize())}});
}
Cache decodeCache(const var& value){
 Cache cache;double samples=number(value["samples"],1,rate*420.);if(samples!=std::floor(samples))throw std::invalid_argument("Cache Basic Pitch invalide.");cache.samples=std::size_t(samples);
 std::size_t frames=std::size_t(double(cache.samples)*keptFrames/hop),half=frames*bins*4;
 auto text=value["compressed"].toString();juce::MemoryOutputStream block;
 if(value["model_sha256"].toString()!=modelHash||!value["compressed"].isString()||std::size_t(text.length())>((half*2+65536)*4/3+8)||!juce::Base64::convertFromBase64(block,text))throw std::invalid_argument("Cache Basic Pitch invalide.");
 std::vector<std::uint8_t> bytes(half*2);
 juce::MemoryInputStream input(block.getData(),block.getDataSize(),false);juce::GZIPDecompressorInputStream stream(input);
 if(stream.read(bytes.data(),int(bytes.size()))!=int(bytes.size()))throw std::invalid_argument("Cache Basic Pitch invalide.");char extra;if(stream.read(&extra,1)!=0)throw std::invalid_argument("Cache Basic Pitch invalide.");
 cache.notes=unpack({bytes.begin(),bytes.begin()+half});cache.onsets=unpack({bytes.begin()+half,bytes.end()});cache.validate();return cache;
}
void validateVoiceCaches(const var& value){
 if(value.isVoid()||value.isUndefined())return;auto* object=value.getDynamicObject();if(!object||object->getProperties().size()>512)throw std::invalid_argument("Cache Basic Pitch invalide.");
 std::size_t total=0;for(auto& entry:object->getProperties()){
  auto key=entry.name.toString();if(key.isEmpty()||key.containsAnyOf("-+. ")||key.length()>5||key.getIntValue()>32767||String(key.getIntValue())!=key||key.retainCharacters("0123456789")!=key)throw std::invalid_argument("Cache Basic Pitch invalide.");
  auto v=entry.value;Options::read(v["settings"]);if(!v["settings"].isObject()||v["engine"].toString()!="basic_pitch"||!v["signature"].isString()||v["signature"].toString().length()!=64||!v["source_path"].isString()||v["source_path"].toString().length()>32768||!v["primary"].isBool())throw std::invalid_argument("Cache Basic Pitch invalide.");
  if(!(bool)v["primary"]){total+=std::size_t(v["cache"]["compressed"].toString().getNumBytesAsUTF8());if(total>180000000)throw std::invalid_argument("Cache Basic Pitch invalide.");decodeCache(v["cache"]);}
 }
}
}
