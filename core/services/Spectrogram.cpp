#include "Spectrogram.h"
#include "../audio/Pcm16.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phthongos::services {
juce::var spectrogram(const juce::File& path,double start,double end,std::size_t columns){
    constexpr int size=16384,rows=384;
    if(!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start||end>86400||columns<1||columns>1024)
        throw std::invalid_argument("Intervalle de spectrogramme invalide.");
    audio::Pcm16 pcm(path);const double rate=pcm.rate();juce::dsp::FFT fft(14);
    std::vector<float> buffer(size*2),window(size);
    constexpr float tau=6.283185307179586476925286766559f;
    for(int i=0;i<size;++i)window[i]=.5f-.5f*std::cos(tau*static_cast<float>(i)/static_cast<float>(size-1));
    juce::Array<juce::var> values;values.ensureStorageAllocated(static_cast<int>(columns)*rows);
    for(std::size_t x=0;x<columns;++x){
        std::fill(buffer.begin(),buffer.end(),0.f);
        const auto centre=(start+(x+.5)/columns*(end-start))*rate;
        const auto first=static_cast<std::int64_t>(std::floor(centre))-size/2;
        const auto offset=static_cast<std::size_t>(std::max(-first,std::int64_t{0}));
        const auto from=static_cast<std::uint64_t>(std::max(first,std::int64_t{0}));
        if(from<pcm.frames()&&offset<size){
            auto samples=pcm.mono(from,static_cast<std::size_t>(std::min<std::uint64_t>(size-offset,pcm.frames()-from)));
            for(std::size_t i=0;i<samples.size();++i)buffer[offset+i]=samples[i]*window[offset+i];
        }
        fft.performRealOnlyForwardTransform(buffer.data(),true);
        for(int row=0;row<rows;++row){
            const auto pitch=127.5-(row+.5)/3.,hz=440.*std::pow(2.,(pitch-69.)/12.);
            if(hz>rate/2.){values.add(0);continue;}
            const auto bin=hz*size/rate;const auto a=std::min(static_cast<int>(std::floor(bin)),size/2),b=std::min(a+1,size/2);
            const auto fraction=static_cast<float>(bin-std::floor(bin));
            const auto magnitude=std::hypot(buffer[a*2],buffer[a*2+1])*(1.f-fraction)+std::hypot(buffer[b*2],buffer[b*2+1])*fraction;
            const auto db=20.f*std::log10(std::max(magnitude/(size/4.f),1e-6f));
            values.add(static_cast<int>(std::clamp((db+90.f)/90.f,0.f,1.f)*255.f));
        }
    }
    auto* out=new juce::DynamicObject;out->setProperty("columns",static_cast<int>(columns));out->setProperty("rows",rows);out->setProperty("values",juce::var(values));return juce::var(out);
}
}
