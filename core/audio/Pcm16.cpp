#include "Pcm16.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace phthongos::audio {
namespace {
[[noreturn]] void bad(){throw std::invalid_argument("Cache audio PCM invalide.");}
std::uint32_t le(const std::uint8_t* b,int count){std::uint32_t out=0;for(int i=0;i<count;++i)out|=std::uint32_t(b[i])<<(8*i);return out;}
float sample(const std::uint8_t* p){const auto x=le(p,2);const auto signedValue=x>=32768?static_cast<int>(x)-65536:static_cast<int>(x);return static_cast<float>(signedValue)/32768.f;}
}
Pcm16::Pcm16(const juce::File& file):input_(file){
    if(!input_.openedOk())throw std::invalid_argument(input_.getStatus().getErrorMessage().toStdString());
    const auto length=input_.getTotalLength();std::uint8_t header[12];
    if(length<12||input_.read(header,12)!=12||std::memcmp(header,"RIFF",4)||std::memcmp(header+8,"WAVE",4))bad();
    bool format=false,data=false;
    for(std::uint64_t at=12;at+8<=static_cast<std::uint64_t>(length);){
        if(!input_.setPosition(static_cast<juce::int64>(at)))bad();std::uint8_t chunk[8];if(input_.read(chunk,8)!=8)bad();
        const auto size=le(chunk+4,4);const auto payload=at+8;
        if(size>static_cast<std::uint64_t>(length)-payload)bad();
        if(!std::memcmp(chunk,"fmt ",4)){
            if(size<16)bad();std::uint8_t spec[40]{};const auto count=std::min(size,40u);
            if(input_.read(spec,count)!=static_cast<int>(count))bad();
            auto encoding=le(spec,2);channels_=le(spec+2,2);rate_=le(spec+4,4);
            if(encoding==65534){
                const std::uint8_t guidTail[]={0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if(size<40||le(spec+16,2)<22||le(spec+18,2)!=16||std::memcmp(spec+28,guidTail,12))bad();
                encoding=le(spec+24,4);
            }
            if(encoding!=1||le(spec+14,2)!=16||!channels_||!rate_||le(spec+12,2)!=channels_*2)bad();
            format=true;
        }else if(!std::memcmp(chunk,"data",4)&&!data){
            if(!format||size%(channels_*2))bad();offset_=payload;frames_=size/(channels_*2);data=true;
        }
        at=payload+size+(size&1u);
    }
    if(!format||!data)bad();
}
std::vector<std::uint8_t> Pcm16::read(std::uint64_t first,std::size_t count){
    if(first>frames_||count>frames_-first||count>static_cast<std::size_t>(INT_MAX)/channels_/2)bad();
    std::vector<std::uint8_t> bytes(count*channels_*2);
    if(!input_.setPosition(static_cast<juce::int64>(offset_+first*channels_*2))||input_.read(bytes.data(),static_cast<int>(bytes.size()))!=static_cast<int>(bytes.size()))bad();
    return bytes;
}
std::vector<float> Pcm16::mono(std::uint64_t first,std::size_t count){
    const auto bytes=read(first,count);std::vector<float> out(count,0.f);
    for(std::size_t i=0;i<count;++i)for(unsigned c=0;c<channels_;++c)
        out[i]+=sample(bytes.data()+(i*channels_+c)*2)/static_cast<float>(channels_);
    return out;
}
analysis::StereoPcm Pcm16::stereo(){
    if(channels_>2)throw std::invalid_argument("Le player accepte les WAV PCM16 mono ou stéréo.");
    // Existing audio imports are bounded to 420 seconds. Guard allocations even
    // if an internal caller supplies an unrelated cache file.
    if(frames_>static_cast<std::uint64_t>(rate_)*1200)bad();
    const auto bytes=read(0,static_cast<std::size_t>(frames_));analysis::StereoPcm out(static_cast<std::size_t>(frames_));
    for(std::size_t i=0;i<out.size();++i)out[i]={sample(bytes.data()+i*channels_*2),sample(bytes.data()+(i*channels_+channels_-1)*2)};
    return out;
}
}
