#include "Import.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#endif
namespace phthongos::audio {
namespace {
using juce::var;using juce::String;
var obj(std::initializer_list<std::pair<const char*,var>> xs){auto* o=new juce::DynamicObject;for(auto& x:xs)o->setProperty(x.first,x.second);return var(o);}
struct Decoded {std::vector<float> samples;unsigned rate=0,channels=0;};
void validate(const Decoded& d){double duration=double(d.samples.size())/d.rate/d.channels;if(d.channels<1||d.channels>2||d.rate<8000||d.rate>192000)throw std::invalid_argument("Import : seuls les audios mono/stéréo entre 8 et 192 kHz sont acceptés.");if(d.samples.size()%d.channels||!std::isfinite(duration)||duration<.1||duration>419.95)throw std::invalid_argument("L’audio doit durer de 0,1 seconde à moins de 7 minutes.");if(std::any_of(d.samples.begin(),d.samples.end(),[](float x){return !std::isfinite(x);}))throw std::invalid_argument("L’audio contient des échantillons invalides.");}
#if JUCE_WINDOWS
void checked(HRESULT result,const char* message){if(FAILED(result))throw std::runtime_error(std::string(message)+" ("+std::to_string(static_cast<unsigned>(result))+")");}
Decoded alac(const juce::File& file){
 struct Com {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Com(){if(FAILED(hr)&&hr!=RPC_E_CHANGED_MODE)checked(hr,"Initialisation du décodeur audio impossible");}~Com(){if(SUCCEEDED(hr))CoUninitialize();}} com;
 struct Mf {Mf(){checked(MFStartup(MF_VERSION),"Initialisation Media Foundation impossible");}~Mf(){MFShutdown();}} mf;
 using Microsoft::WRL::ComPtr;ComPtr<IMFSourceReader> reader;checked(MFCreateSourceReaderFromURL(file.getFullPathName().toWideCharPointer(),nullptr,&reader),"Format non reconnu");
 checked(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE),"Sélection audio impossible");checked(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE),"Aucune piste audio lisible");
 ComPtr<IMFMediaType> native;checked(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,&native),"Codec audio non reconnu");GUID subtype{};checked(native->GetGUID(MF_MT_SUBTYPE,&subtype),"Codec audio non reconnu");
 if(subtype!=MFAudioFormat_ALAC&&subtype.Data1!=0x616c6163&&subtype.Data1!=0x63616c61)throw std::invalid_argument("M4A contenant de l’AAC non pris en charge dans cette version. Utilisez WAV, MP3, FLAC ou M4A/ALAC.");
 ComPtr<IMFMediaType> request;checked(MFCreateMediaType(&request),"Décodage ALAC impossible");request->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);request->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_Float);checked(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,request.Get()),"Codec ALAC indisponible");
 ComPtr<IMFMediaType> actual;checked(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&actual),"Format audio invalide");Decoded out;UINT32 rate=0,channels=0;checked(actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&rate),"Fréquence audio invalide");checked(actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&channels),"Canaux audio invalides");out.rate=rate;out.channels=channels;
 if(channels<1||channels>2||rate<8000||rate>192000)throw std::invalid_argument("Import : audio mono/stéréo entre 8 et 192 kHz uniquement.");
 while(true){DWORD flags=0;ComPtr<IMFSample> sample;checked(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,nullptr,&sample),"Fichier audio incomplet ou invalide");if(flags&MF_SOURCE_READERF_ERROR)throw std::invalid_argument("Décodage audio interrompu.");if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED){ComPtr<IMFMediaType> changed;reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,&changed);UINT32 r=0,c=0;changed->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,&r);changed->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS,&c);if(r!=rate||c!=channels)throw std::invalid_argument("Les changements de fréquence ou de canaux dans un même fichier ne sont pas pris en charge.");}
 if(sample){ComPtr<IMFMediaBuffer> buffer;checked(sample->ConvertToContiguousBuffer(&buffer),"Données audio invalides");BYTE* bytes=nullptr;DWORD count=0;checked(buffer->Lock(&bytes,nullptr,&count),"Données audio invalides");if(count%sizeof(float)){buffer->Unlock();throw std::invalid_argument("Trame audio incomplète.");}auto* samples=reinterpret_cast<const float*>(bytes);out.samples.insert(out.samples.end(),samples,samples+count/sizeof(float));buffer->Unlock();if(double(out.samples.size())>419.95*rate*channels)throw std::invalid_argument("Ce fichier dépasse la limite de moins de 7 minutes.");}
 if(flags&MF_SOURCE_READERF_ENDOFSTREAM)break;
 }validate(out);return out;
}
#endif
// JUCE's layer-III decoder retains the 529-sample synthesis delay. Read the
// Xing/LAME gapless fields rather than shifting untagged MP3s heuristically.
void trimMp3(const juce::File& file,Decoded& out){
 auto input=file.createInputStream();if(!input)return;std::array<std::uint8_t,65536> b{};auto size=input->read(b.data(),int(b.size()));if(size<16)return;std::size_t first=0;
 if(b[0]=='I'&&b[1]=='D'&&b[2]=='3'){first=10+((std::size_t(b[6]&127)<<21)|(std::size_t(b[7]&127)<<14)|(std::size_t(b[8]&127)<<7)|(b[9]&127));if(b[5]&16)first+=10;}
 for(;first+40<std::size_t(size);++first){if(b[first]!=255||(b[first+1]&224)!=224||((b[first+1]>>1)&3)!=1)continue;
 int version=(b[first+1]>>3)&3;if(version==1)continue;bool mono=(b[first+3]>>6)==3;std::size_t at=first+4+((b[first+1]&1)?0:2)+(version==3?(mono?17:32):(mono?9:17));if(at+8>=std::size_t(size))return;
 if(std::memcmp(b.data()+at,"Xing",4)&&std::memcmp(b.data()+at,"Info",4))return;unsigned flags=0;for(int j=4;j<8;++j)flags=(flags<<8)|b[at+j];at+=8;
 for(auto field:std::array<std::pair<unsigned,unsigned>,4>{{{1,4},{2,4},{4,100},{8,4}}})if(flags&field.first)at+=field.second;
 if(at+24>std::size_t(size))return;if(std::memcmp(b.data()+at,"LAME",4)&&std::memcmp(b.data()+at,"Lavc",4)&&std::memcmp(b.data()+at,"Lavf",4))return;
 unsigned packed=(unsigned(b[at+21])<<16)|(unsigned(b[at+22])<<8)|b[at+23],delay=packed>>12,padding=packed&4095;if(!delay&&!padding)return;
 auto begin=std::size_t(delay+529)*out.channels,end=std::size_t(padding>=529?padding-529:0)*out.channels;if(begin+end>out.samples.size())throw std::invalid_argument("Métadonnées gapless MP3 invalides.");out.samples=std::vector<float>(out.samples.begin()+begin,out.samples.end()-end);return;
 }
}
Decoded decode(const juce::File& file){
 if(file.hasFileExtension("m4a")){
#if JUCE_WINDOWS
 return alac(file);
#else
 throw std::invalid_argument("Décodeur ALAC indisponible sur cette plateforme.");
#endif
 }
 juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));if(!reader)throw std::invalid_argument("Format non reconnu ou fichier audio incomplet.");Decoded out;out.rate=unsigned(reader->sampleRate);out.channels=reader->numChannels;
 double duration=double(reader->lengthInSamples)/reader->sampleRate;if(out.channels<1||out.channels>2||out.rate<8000||out.rate>192000||duration<.1||duration>(file.hasFileExtension("mp3")?420.15:419.95))throw std::invalid_argument("Import : mono/stéréo, 8–192 kHz, de 0,1 seconde à moins de 7 minutes.");out.samples.reserve(std::size_t(reader->lengthInSamples)*out.channels);juce::AudioBuffer<float> block(out.channels,8192);
 for(juce::int64 at=0;at<reader->lengthInSamples;){auto count=int(std::min<juce::int64>(8192,reader->lengthInSamples-at));if(!reader->read(&block,0,count,at,true,true))throw std::invalid_argument("Décodage audio interrompu.");for(int i=0;i<count;++i)for(unsigned c=0;c<out.channels;++c)out.samples.push_back(block.getSample(int(c),i));at+=count;}if(file.hasFileExtension("mp3"))trimMp3(file,out);validate(out);return out;
}
double even(double x){double a=std::floor(x),f=x-a;return f>.5||(f==.5&&std::fmod(a,2.)!=0)?a+1:a;}
}
std::shared_ptr<juce::File> ownCachedPcm(const juce::File& path){return std::shared_ptr<juce::File>(new juce::File(path),[](juce::File* file){auto directory=file->getParentDirectory();file->deleteFile();directory.deleteFile();delete file;});}
ImportedAudio importAudio(const juce::File& original,const juce::File& cache){if(!original.hasFileExtension("wav;mp3;flac;m4a"))throw std::invalid_argument("Formats acceptés : WAV, MP3, M4A (ALAC uniquement), FLAC.");ImportedAudio out;out.original=original;
 if(original.hasFileExtension("wav")){try{Pcm16 pcm(original);double duration=double(pcm.frames())/pcm.rate();if(duration<.1||duration>419.95)throw std::invalid_argument("Ce prototype est limité à un bloc de moins de 420 secondes.");out.pcm=original;out.rate=pcm.rate();out.channels=pcm.channels();out.duration=duration;out.format="WAV PCM16";return out;}catch(const std::exception&){} }
 auto decoded=decode(original);auto folder=cache.getChildFile("import-"+juce::Uuid().toString());if(folder.createDirectory().failed())throw std::runtime_error("Cache audio indisponible.");out.pcm=folder.getChildFile("audio.wav");out.cacheOwner=ownCachedPcm(out.pcm);auto stream=out.pcm.createOutputStream();if(!stream)throw std::runtime_error("Cache audio indisponible.");auto size=std::uint32_t(decoded.samples.size()*2);stream->write("RIFF",4);stream->writeInt(int(size+36));stream->write("WAVEfmt ",8);stream->writeInt(16);stream->writeShort(1);stream->writeShort(short(decoded.channels));stream->writeInt(int(decoded.rate));stream->writeInt(int(decoded.rate*decoded.channels*2));stream->writeShort(short(decoded.channels*2));stream->writeShort(16);stream->write("data",4);stream->writeInt(int(size));std::uint64_t clipped=0;for(float sample:decoded.samples){double scaled=even(double(sample)*32768.);clipped+=scaled< -32768||scaled>32767;stream->writeShort(short(std::clamp(scaled,-32768.,32767.)));}stream->flush();if(stream->getStatus().failed()){stream.reset();out.pcm.deleteFile();throw std::runtime_error("Enregistrement du cache audio impossible.");}out.rate=decoded.rate;out.channels=decoded.channels;out.duration=double(decoded.samples.size())/decoded.channels/decoded.rate;out.format=original.hasFileExtension("m4a")?String::fromUTF8("M4A · ALAC"):original.hasFileExtension("mp3")?"MP3":original.hasFileExtension("flac")?"FLAC":"WAV";if(clipped)out.notice=String(static_cast<juce::int64>(clipped))+String::fromUTF8(" échantillons hors plage ont été limités pendant l’import.");return out;
}
juce::var waveform(const juce::File& path,double start,double end,std::size_t bins){Pcm16 reader(path);double duration=double(reader.frames())/reader.rate();if(!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start||end>duration+.001||!bins||bins>100000)throw std::invalid_argument("Intervalle de forme d’onde invalide.");auto first=std::uint64_t(std::floor(start*reader.rate())),last=std::min(reader.frames(),std::uint64_t(std::ceil(end*reader.rate())));auto mono=reader.mono(first,std::size_t(last-first));bins=std::min(bins,mono.size());std::vector<std::array<float,2>> peaks(bins,{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()});for(std::size_t i=0;i<mono.size();++i){auto bin=std::min(bins-1,i*bins/mono.size());peaks[bin][0]=std::min(peaks[bin][0],mono[i]);peaks[bin][1]=std::max(peaks[bin][1],mono[i]);}juce::Array<var> result;for(auto& p:peaks)result.add(var(juce::Array<var>{double(p[0]),double(p[1])}));return var(result);}
juce::var audioView(const ImportedAudio& audio,std::uint64_t id){return obj({{"midi_only",false},{"id",var(static_cast<juce::int64>(id))},{"name",audio.original.getFileName()},{"path",audio.original.getFullPathName()},{"duration",audio.duration},{"sample_rate",int(audio.rate)},{"channels",int(audio.channels)},{"peaks",waveform(audio.pcm,0,audio.duration,6000)},{"format",audio.format},{"notice",audio.notice}});}
void publishNew(const juce::File& temporary,const juce::File& destination){
#if JUCE_WINDOWS
 if(!MoveFileExW(temporary.getFullPathName().toWideCharPointer(),destination.getFullPathName().toWideCharPointer(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error(destination.exists()?"Ce fichier existe déjà. Choisissez un autre nom pour préserver l’export précédent.":"Impossible de publier le fichier exporté.");
#else
 std::error_code ec;std::filesystem::create_hard_link(temporary.getFullPathName().toStdString(),destination.getFullPathName().toStdString(),ec);if(ec)throw std::runtime_error("Impossible de publier le fichier sans remplacer un export existant.");temporary.deleteFile();
#endif
}
}
