#include "Inference.h"
#include "../audio/Pcm16.h"
#include "../project/BasicPitch.h"
#include <cmath>
#include <stdexcept>
namespace phthongos::services {
namespace {
using juce::var;using juce::String;
var obj(std::initializer_list<std::pair<const char*,var>> xs){auto* o=new juce::DynamicObject;for(auto& x:xs)o->setProperty(x.first,x.second);return var(o);}
template<class T>T symbol(juce::DynamicLibrary& dll,const char* name){auto* p=dll.getFunction(name);if(!p)throw std::runtime_error(std::string("Moteur natif absent/incompatible : ")+name);return reinterpret_cast<T>(p);}
const char* modelHash(const String& model){if(model=="small")return "925f55af65a20ebc4f8b45ceaf095a12b72493d436cb112623cd0041a1af23d4";if(model=="medium")return "3850cc9e5b436b17a09bd25b8f2615cb3366ab96a71e7b50f73a793a917fdf03";if(model=="large")return "35a750fb1ab1e77195cdc2c0b9b4aeea2f4d59f11f729f02af9920c4854ef72e";throw std::invalid_argument("Modèle de transcription invalide.");}
var decode(juce::DynamicLibrary& dll,char* pointer){if(!pointer)throw std::runtime_error("Allocation native impossible.");auto free=symbol<void(*)(char*)>(dll,"pht_ms_free");String data=String::fromUTF8(pointer);free(pointer);auto value=juce::JSON::parse(data);if(!(bool)value["ok"])throw std::runtime_error(value["error"].toString().toStdString());value.getDynamicObject()->removeProperty("ok");return value;}
std::vector<float> prepare(const juce::File& file,const juce::File& speex){audio::Pcm16 audio(file);auto mono=audio.mono(0,std::size_t(audio.frames()));if(audio.rate()==16000)return mono;juce::DynamicLibrary dll;if(!dll.open(speex.getFullPathName()))throw std::runtime_error("Chargement du rééchantillonneur impossible.");auto init=symbol<void*(*)(unsigned,unsigned,unsigned,int,int*)>(dll,"research_speex_resampler_init");auto process=symbol<int(*)(void*,unsigned,const float*,unsigned*,float*,unsigned*)>(dll,"research_speex_resampler_process_float");auto latency=symbol<unsigned(*)(void*)>(dll,"research_speex_resampler_get_output_latency");auto destroy=symbol<void(*)(void*)>(dll,"research_speex_resampler_destroy");int error=0;auto* raw=init(1,audio.rate(),16000,4,&error);if(!raw)throw std::runtime_error("Speex : "+std::to_string(error));std::unique_ptr<void,decltype(destroy)> state(raw,destroy);auto delay=latency(raw),count=unsigned(std::round(double(mono.size())*16000/audio.rate()));mono.resize(mono.size()+std::size_t(std::ceil(double(delay+16)*audio.rate()/16000)),0.f);std::vector<float> output(std::size_t(count)+delay+16,0.f);auto ni=unsigned(mono.size()),no=unsigned(output.size());auto result=process(raw,0,mono.data(),&ni,output.data(),&no);if(result||no<count+delay)throw std::runtime_error("Rééchantillonnage incomplet : "+std::to_string(result));return std::vector<float>(output.begin()+delay,output.begin()+delay+count);}
}
Inference::Inference(juce::File r,juce::File d):runtime(std::move(r)),data(std::move(d)){
 settings=juce::JSON::parse(data.getChildFile("muscriptor-settings.json").loadFileAsString());if(!settings.isObject()||!settings["model"].isString()||!settings["device"].isString()||!settings["models_dir"].isString()){
 settings=obj({{"model","small"},{"device","auto"},{"models_dir",""}});
 }else settings.getDynamicObject()->removeProperty("instrument_target");
 if(!settings.hasProperty("third_party"))settings.getDynamicObject()->setProperty("third_party",settings["models_dir"].toString().isNotEmpty());
 if(!settings["models"].isObject())settings.getDynamicObject()->setProperty("models",obj({}));
 if(!(bool)settings["third_party"])settings.getDynamicObject()->setProperty("engine","basic_pitch");
 if(!settings.hasProperty("engine"))settings.getDynamicObject()->setProperty("engine","basic_pitch");
 try{auto options=basicPitch::Options::read(settings["basic_pitch"]);settings.getDynamicObject()->setProperty("basic_pitch",options.json());}catch(const std::exception&){settings.getDynamicObject()->setProperty("basic_pitch",basicPitch::Options().json());}
 auto model=settings["model"].toString();if(model!="small"&&model!="medium"&&model!="large")settings.getDynamicObject()->setProperty("model","small");
 auto directory=settings["models_dir"].toString();if(directory.isNotEmpty()&&!juce::File::isAbsolutePath(directory))settings.getDynamicObject()->setProperty("models_dir","");
}
Inference::~Inference()=default;
void Inference::load(){if(dll.getFunction("pht_ms_abi_version"))return;if(!dll.open(runtime.getChildFile("phthongos_muscriptor.dll").getFullPathName()))throw std::runtime_error("Moteur natif absent/incompatible.");if(symbol<int(*)()>(dll,"pht_ms_abi_version")()!=1)throw std::runtime_error("Version ABI MuScriptor incompatible.");}
juce::var Inference::devices(){load();return decode(dll,symbol<char*(*)()>(dll,"pht_ms_devices")());}
int Inference::device(const juce::var& hardware,const juce::String& choice){auto* list=hardware["devices"].getArray();if(!list)throw std::runtime_error("Périphériques MuScriptor invalides.");if(choice=="auto"){int index=hardware["auto_index"];if(index>=0&&index<list->size())return index;}else for(int i=0;i<list->size();++i){auto d=(*list)[i];bool cpu=d["backend"].toString().equalsIgnoreCase("CPU"),integrated=d["integrated"];if((choice=="cpu"&&cpu)||(choice=="gpu"&&!cpu&&!integrated)||(choice=="integrated"&&!cpu&&integrated))return i;}
 throw std::invalid_argument("Périphérique "+choice.toStdString()+" indisponible. Choisissez un autre mode.");}
juce::File Inference::modelPath(const var& configured,const String& model)const{
 auto path=configured["models"][juce::Identifier(model)]["path"].toString();if(juce::File::isAbsolutePath(path))return juce::File(path);
 auto directory=configured["models_dir"].toString();return juce::File::isAbsolutePath(directory)?juce::File(directory).getChildFile("muscriptor-"+model+"-f16.gguf"):juce::File();
}
void Inference::declareModel(const String& model,const juce::File& file){
 auto hash=modelHash(model);if(!file.existsAsFile()||fileHash(file)!=hash)throw std::invalid_argument("Le SHA-256 du modèle ne correspond pas au manifeste NeuralNote 2.0.0.");
 std::lock_guard<std::mutex> lock(mutex);auto next=settings.clone();auto models=next["models"].clone();auto entry=models[juce::Identifier(model)].isObject()?models[juce::Identifier(model)].clone():obj({});entry.getDynamicObject()->setProperty("path",file.getFullPathName());models.getDynamicObject()->setProperty(model,entry);next.getDynamicObject()->setProperty("models",models);saveJson(data.getChildFile("muscriptor-settings.json"),next);settings=next;
}
void Inference::download(const String& model){
 auto interrupted=[this]{auto* job=juce::ThreadPoolJob::getCurrentThreadPoolJob();return downloadCancelled.load()||(job&&job->shouldExit());};
 modelHash(model);if(downloading.exchange(true))throw std::invalid_argument("Un téléchargement est déjà en cours.");struct Finish{std::atomic_bool& busy;~Finish(){busy=false;}} finish{downloading};downloadCancelled=false;
 auto update=[&](const char* phase,juce::int64 count,const String& error=String()){std::lock_guard<std::mutex> lock(mutex);downloadState=obj({{"model",model},{"phase",phase},{"received",var(count)},{"total",var(model=="small"?juce::int64(209425152):model=="medium"?juce::int64(618442496):juce::int64(2739142176))},{"error",error}});};
 const juce::int64 expected=model=="small"?209425152:model=="medium"?618442496:2739142176LL;auto folder=data.getChildFile("models/muscriptor");if(folder.createDirectory().failed())throw std::runtime_error("Dossier de modèles introuvable.");auto destination=folder.getChildFile("muscriptor-"+model+"-f16.gguf");
 try{if(destination.existsAsFile()&&destination.getSize()==expected&&fileHash(destination)==modelHash(model)){declareModel(model,destination);update("done",expected);return;}
  update("download",0);int status=0;auto url=juce::URL("https://huggingface.co/DamRsn/muscriptor-gguf/resolve/d7045f94e8b19427f4ff9542975035e66596e51c/v1/muscriptor-"+model+"-f16.gguf");auto input=url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(15000).withNumRedirectsToFollow(8).withStatusCode(&status));if(!input||status<200||status>=300)throw std::runtime_error("Téléchargement du modèle impossible.");
  juce::TemporaryFile temporary(destination);auto output=temporary.getFile().createOutputStream();if(!output)throw std::runtime_error("Écriture du modèle impossible.");std::array<char,65536> bytes;juce::int64 received=0;
  for(;;){if(interrupted())throw std::runtime_error("Téléchargement annulé.");int n=input->read(bytes.data(),int(bytes.size()));if(n<=0)break;received+=n;if(received>expected||!output->write(bytes.data(),std::size_t(n)))throw std::runtime_error("Taille ou écriture du modèle invalide.");update("download",received);}
  output->flush();output.reset();if(received!=expected)throw std::runtime_error("Téléchargement incomplet.");update("verify",received);if(fileHash(temporary.getFile())!=modelHash(model))throw std::runtime_error("Empreinte du modèle incorrecte.");if(interrupted())throw std::runtime_error("Téléchargement annulé.");if(!temporary.overwriteTargetFileWithTemporary())throw std::runtime_error("Publication du modèle impossible.");declareModel(model,destination);update("done",received);
 }catch(const std::exception& e){update("error",0,String::fromUTF8(e.what()));throw;}
}
juce::var Inference::status(){
 std::lock_guard<std::mutex> lock(mutex);juce::Array<var> models;
 models.add(obj({{"id",1},{"label","Basic Pitch"},{"model","basic_pitch"}}));
 auto directory=settings["models_dir"].toString();int id=2;bool selected=settings["engine"].toString()=="basic_pitch";
 if((bool)settings["third_party"])for(auto name:{"small","medium","large"}){
  if(modelPath(settings,name).existsAsFile()){
   models.add(obj({{"id",id},{"label",String::fromUTF8("MuScriptor · ")+String(name).substring(0,1).toUpperCase()+String(name).substring(1)},{"model",name}}));if(settings["model"].toString()==name)selected=true;
  }++id;
 }
 if(!(bool)settings["third_party"])selected=false;
 if(!selected)settings.getDynamicObject()->setProperty("engine","basic_pitch");
 var hardware=obj({{"devices",var(juce::Array<var>{})},{"auto_index",-1}});
 if((bool)settings["third_party"]&&models.size()>1)try{hardware=devices();}catch(const std::exception&){}
 juce::Array<var> catalog;for(auto name:{"small","medium","large"}){auto path=modelPath(settings,name);auto entry=settings["models"][juce::Identifier(name)];catalog.add(obj({{"model",name},{"path",path==juce::File()?String():path.getFullPathName()},{"available",path.existsAsFile()},{"device",settings["device"]}}));}
 return obj({{"third_models",var(catalog)},{"download",downloadState.clone()},{"downloading",downloading.load()},{"settings",settings.clone()},{"hardware",hardware},{"models",var(models)},{"basic_pitch_available",runtime.getChildFile("onnxruntime.dll").existsAsFile()&&runtime.getChildFile("basic-pitch/nmp.onnx").existsAsFile()}});
}
void Inference::configure(const juce::var& value){
 std::lock_guard<std::mutex> lock(mutex);auto engine=(bool)value["third_party"]?value["engine"].toString():String("basic_pitch");if(engine!="basic_pitch"&&engine!="muscriptor")throw std::invalid_argument("Moteur de transcription invalide.");
 modelHash(value["model"].toString());auto bp=basicPitch::Options::read(value["basic_pitch"]);
 auto directory=value["models_dir"].toString();if(directory.isNotEmpty()&&!juce::File(directory).isDirectory())throw std::invalid_argument("Dossier de modèles introuvable.");
 if(engine=="muscriptor"){
  if(!modelPath(value,value["model"].toString()).existsAsFile())throw std::invalid_argument("Modèle MuScriptor non configuré.");
  device(devices(),value["device"].toString());
 }
 auto mode=value["device"].toString();if(mode!="auto"&&mode!="cpu"&&mode!="gpu"&&mode!="integrated")throw std::invalid_argument("Mode MuScriptor invalide.");
 auto models=value["models"].isObject()?value["models"].clone():obj({});for(auto name:{"small","medium","large"}){auto entry=models[juce::Identifier(name)];if(entry.isVoid())continue;auto path=entry["path"].toString();if(!entry.isObject()||(path.isNotEmpty()&&!juce::File::isAbsolutePath(path)))throw std::invalid_argument("Déclaration de modèle tiers invalide.");models.getDynamicObject()->setProperty(name,obj({{"path",path}}));}
 auto checked=obj({{"third_party",(bool)value["third_party"]},{"models",models.clone()},{"engine",engine},{"basic_pitch",bp.json()},{"model",value["model"]},{"device",value["device"]},{"models_dir",directory}});
 saveJson(data.getChildFile("muscriptor-settings.json"),checked);settings=checked;
}
juce::var Inference::transcribe(const juce::File& audio,std::uint64_t audioId,const std::function<void(juce::var)>& emit){
 juce::var configured,hardware;{std::lock_guard<std::mutex> lock(mutex);configured=settings.clone();hardware=devices();}
 auto report=[&](const char* phase,float progress){emit(obj({{"event","muscriptor-progress"},{"payload",obj({{"audio_id",var(static_cast<juce::int64>(audioId))},{"phase",phase},{"progress",double(progress)}})}}));};report("verify",0);
 auto model=configured["model"].toString();auto path=modelPath(configured,model);if(!path.existsAsFile())throw std::runtime_error("Poids absent : "+path.getFullPathName().toStdString());if(fileHash(path)!=modelHash(model))throw std::invalid_argument("Le SHA-256 du modèle ne correspond pas au manifeste NeuralNote 2.0.0.");if(cancelled.load())throw std::invalid_argument("Transcription annulée.");
 auto pcm=prepare(audio,runtime.getChildFile("research_speex.dll"));struct Context{const std::function<void(juce::var)>* emit;std::atomic_bool* cancel;std::uint64_t id;} context{&emit,&cancelled,audioId};
 auto progress=+[](int phase,float amount,void* raw)noexcept{try{auto& c=*static_cast<Context*>(raw);(*c.emit)(obj({{"event","muscriptor-progress"},{"payload",obj({{"audio_id",var(static_cast<juce::int64>(c.id))},{"phase",phase==0?"load":"transcribe"},{"progress",double(amount)}})}}));}catch(...){}};
 auto cancel=+[](void* raw)noexcept->int{return static_cast<Context*>(raw)->cancel->load()?1:0;};
 using Transcribe=char*(*)(const char*,const float*,std::size_t,int,const int*,std::size_t,int,void(*)(int,float,void*),int(*)(void*),void*);
 auto raw=decode(dll,symbol<Transcribe>(dll,"pht_ms_transcribe")(path.getFullPathName().toRawUTF8(),pcm.data(),pcm.size(),device(hardware,configured["device"].toString()),nullptr,0,0,progress,cancel,&context));
 auto dir=data.getChildFile("analysis-cache");saveJson(dir.getChildFile("raw-"+String(static_cast<juce::int64>(audioId))+"-"+String(juce::Time::currentTimeMillis())+".json"),raw);
 return obj({{"instrument_target","all"},{"model",model},{"model_sha256",modelHash(model)},{"preprocessing","mono-f32 / SpeexDSP 1.2.1 Q4 / latency drained / 16000 Hz / no normalization / no prefix"},{"raw",raw}});
}
}
