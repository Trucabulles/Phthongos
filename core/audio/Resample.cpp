#include "Resample.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phthongos::audio {
namespace {
template<class T>T symbol(juce::DynamicLibrary& dll,const char* name){
    auto* address=dll.getFunction(name);if(!address)throw std::invalid_argument(std::string("Symbole Speex absent : ")+name);return reinterpret_cast<T>(address);
}
}
analysis::StereoPcm readStereo(const juce::File& path,unsigned rate,const juce::File& speex){
    if(!rate||rate>384000)throw std::invalid_argument("Fréquence audio invalide.");
    Pcm16 source(path);auto pcm=source.stereo();if(source.rate()==rate)return pcm;
    auto manifest=juce::JSON::parse(speex.getSiblingFile("manifest.json").loadFileAsString());
    const auto expected=manifest["files"]["research_speex.dll"].toString();
    if(expected.isEmpty())throw std::invalid_argument("Empreinte du rééchantillonneur absente.");
    if(juce::SHA256(speex).toHexString()!=expected)throw std::invalid_argument("Empreinte du rééchantillonneur incorrecte.");
    juce::DynamicLibrary dll;
    if(!dll.open(speex.getFullPathName()))throw std::invalid_argument("Chargement du rééchantillonneur impossible.");
    const auto init=symbol<void*(*)(unsigned,unsigned,unsigned,int,int*)>(dll,"research_speex_resampler_init");
    const auto latency=symbol<unsigned(*)(void*)>(dll,"research_speex_resampler_get_output_latency");
    const auto process=symbol<int(*)(void*,unsigned,const float*,unsigned*,float*,unsigned*)>(dll,"research_speex_resampler_process_float");
    const auto destroy=symbol<void(*)(void*)>(dll,"research_speex_resampler_destroy");
    const auto frames=static_cast<std::size_t>(std::round(static_cast<double>(pcm.size())*rate/source.rate()));
    analysis::StereoPcm result(frames);
    for(int channel=0;channel<2;++channel){
        int error=0;void* raw=init(1,source.rate(),rate,4,&error);
        if(!raw)throw std::invalid_argument("Speex player : "+std::to_string(error));
        const std::unique_ptr<void,decltype(destroy)> state(raw,destroy);
        const auto delay=latency(raw);
        const auto padding=static_cast<std::size_t>(std::ceil(static_cast<double>(delay+32)*source.rate()/rate))+32;
        std::vector<float> input(pcm.size()+padding,0.f),output(frames+delay+16,0.f);
        for(std::size_t i=0;i<pcm.size();++i)input[i]=pcm[i][channel];
        unsigned consumed=static_cast<unsigned>(input.size()),written=static_cast<unsigned>(output.size());
        const auto status=process(raw,0,input.data(),&consumed,output.data(),&written);
        if(status||written<frames+delay)throw std::invalid_argument("Speex player incomplet : "+std::to_string(status));
        for(std::size_t i=0;i<frames;++i)result[i][channel]=output[i+delay];
    }
    return result;
}
}
