#include "BasicPitchInference.h"
#include "../audio/Resample.h"
#include "Resources.h"
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <stdexcept>
namespace phthongos::services {
struct BasicPitchInference::State {
 juce::DynamicLibrary dll;
 std::unique_ptr<Ort::Env> env;
 std::unique_ptr<Ort::Session> session;
 explicit State(const juce::File& runtime){
  auto manifest=juce::JSON::parse(runtime.getChildFile("basic-pitch/manifest.json").loadFileAsString());
  auto binary=runtime.getChildFile("onnxruntime.dll"),model=runtime.getChildFile("basic-pitch/nmp.onnx");
  if(manifest["model_sha256"].toString()!=basicPitch::modelHash||fileHash(model)!=basicPitch::modelHash||fileHash(binary)!=manifest["runtime_sha256"].toString())throw std::runtime_error("Empreinte du modèle ou du moteur Basic Pitch incorrecte.");
  if(!dll.open(binary.getFullPathName()))throw std::runtime_error("Chargement d’ONNX Runtime CPU impossible.");
  auto getApi=reinterpret_cast<const OrtApiBase*(ORT_API_CALL*)()>(dll.getFunction("OrtGetApiBase"));
  if(!getApi)throw std::runtime_error("Version d’ONNX Runtime incompatible.");
  auto* api=getApi()->GetApi(ORT_API_VERSION);if(!api)throw std::runtime_error("API d’ONNX Runtime incompatible.");Ort::InitApi(api);
  env=std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING,"Phthongos Basic Pitch");env->DisableTelemetryEvents();
  Ort::SessionOptions options;options.SetIntraOpNumThreads(std::max(1,std::min(4,juce::SystemStats::getNumCpus())));options.SetInterOpNumThreads(1);options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  // No execution provider is appended: only the bundled CPU provider is used.
  session=std::make_unique<Ort::Session>(*env,model.getFullPathName().toWideCharPointer(),options);
 }
};
BasicPitchInference::BasicPitchInference(juce::File r):runtime(std::move(r)){}
BasicPitchInference::~BasicPitchInference()=default;
bool BasicPitchInference::available()const{return runtime.getChildFile("onnxruntime.dll").existsAsFile()&&runtime.getChildFile("basic-pitch/nmp.onnx").existsAsFile();}
basicPitch::Cache BasicPitchInference::run(const juce::File& file,std::atomic_bool& cancelled,const std::function<void(float)>& progress){
 auto check=[&]{if(cancelled.load())throw std::runtime_error("Transcription annulée.");};check();
 if(!state)state=std::make_unique<State>(runtime);check();
 auto stereo=audio::readStereo(file,basicPitch::rate,runtime.getChildFile("research_speex.dll"));
 basicPitch::Cache cache;cache.samples=stereo.size();std::vector<float> mono(stereo.size());for(std::size_t i=0;i<mono.size();++i)mono[i]=(stereo[i][0]+stereo[i][1])*.5f;stereo.clear();
 auto expected=std::size_t(double(cache.samples)*basicPitch::keptFrames/basicPitch::hop);cache.notes.reserve(expected*88);cache.onsets.reserve(expected*88);
 constexpr std::size_t prefix=15*256;std::vector<float> input(basicPitch::window,0.f);std::array<int64_t,3> shape{1,basicPitch::window,1};
 auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
 const char* inputName="serving_default_input_2:0";const char* outputs[]={"StatefulPartitionedCall:1","StatefulPartitionedCall:2"};
 for(std::size_t offset=0;offset<mono.size()+prefix&&cache.frames()<expected;offset+=basicPitch::hop){check();
  std::fill(input.begin(),input.end(),0.f);for(std::size_t i=0;i<input.size();++i){auto at=std::int64_t(offset+i)-std::int64_t(prefix);if(at>=0&&std::size_t(at)<mono.size())input[i]=mono[std::size_t(at)];}
  auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),shape.data(),shape.size());Ort::RunOptions options;
  auto result=state->session->Run(options,&inputName,&tensor,1,outputs,2);check();
  auto take=std::min<std::size_t>(basicPitch::keptFrames,expected-cache.frames());
  for(int out=0;out<2;++out){auto dimensions=result[out].GetTensorTypeAndShapeInfo().GetShape();if(dimensions!=std::vector<int64_t>{1,172,88})throw std::runtime_error("Dimensions de sortie ONNX Basic Pitch incorrectes.");
   auto* values=result[out].GetTensorData<float>()+prefix/256*88;auto& target=out==0?cache.notes:cache.onsets;target.insert(target.end(),values,values+take*88);}
  progress(float(cache.frames())/float(std::max<std::size_t>(1,expected)));
 }
 cache.validate();return cache;
}
}
