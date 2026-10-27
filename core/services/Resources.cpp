#include "../audio/Engine.h"
#include "Resources.h"
#include "Diagnostics.h"
#include "../audio/Import.h"
#include <juce_cryptography/juce_cryptography.h>
#include <lzma.h>
#include <array>
#include <functional>
#include <stdexcept>
namespace phthongos::services {
namespace {
using juce::var;using juce::String;
constexpr std::uint64_t bankBytes=489519900,maxPackage=512ull*1024*1024;
constexpr const char* bankSha="59287cbb0f424ec76d1bc269eee40dfe0b02252588d72eb016b5d2382e710a8f";
var obj(std::initializer_list<std::pair<const char*,var>> xs){auto* o=new juce::DynamicObject;for(auto& x:xs)o->setProperty(x.first,x.second);return var(o);}
var integer(std::uint64_t x){return var(static_cast<juce::int64>(x));}
std::uint64_t number(const char* bytes,int count,int radix){String s=String::fromUTF8(bytes,count).trimCharactersAtEnd("\0 ").trim();std::size_t consumed=0;auto result=std::stoull(s.toStdString(),&consumed,radix);if(consumed!=std::size_t(s.length()))throw std::invalid_argument("Taille d’archive invalide.");return result;}
void readExact(juce::InputStream& in,void* bytes,int count){if(in.read(bytes,count)!=count)throw std::invalid_argument("Paquet incomplet.");}
class Xz {
 juce::InputStream& source;std::uint64_t left;lzma_stream stream=LZMA_STREAM_INIT;bool ended=false;std::array<std::uint8_t,65536> input{};
public:
 Xz(juce::InputStream& in,std::uint64_t bytes):source(in),left(bytes){if(lzma_stream_decoder(&stream,512ull*1024*1024,0)!=LZMA_OK)throw std::runtime_error("Décodeur XZ indisponible.");}
 ~Xz(){lzma_end(&stream);}
 int read(void* bytes,int count){if(ended)return 0;stream.next_out=static_cast<std::uint8_t*>(bytes);stream.avail_out=std::size_t(count);while(stream.avail_out){if(!stream.avail_in&&left){auto n=int(std::min<std::uint64_t>(left,input.size()));readExact(source,input.data(),n);left-=std::uint64_t(n);stream.next_in=input.data();stream.avail_in=std::size_t(n);}auto result=lzma_code(&stream,left||stream.avail_in?LZMA_RUN:LZMA_FINISH);if(result==LZMA_STREAM_END){ended=true;break;}if(result!=LZMA_OK)throw std::invalid_argument("Archive XZ incorrecte.");}return count-int(stream.avail_out);}
 void exact(void* bytes,int count){if(read(bytes,count)!=count)throw std::invalid_argument("Paquet incomplet.");}
 void skip(std::uint64_t count,const std::function<void()>& check){std::array<char,65536> buffer;while(count){check();auto n=int(std::min<std::uint64_t>(count,buffer.size()));exact(buffer.data(),n);count-=std::uint64_t(n);}}
};
}
juce::String fileHash(const juce::File& file){auto in=file.createInputStream();if(!in)throw std::runtime_error("Fichier introuvable : "+file.getFullPathName().toStdString());juce::BufferedInputStream buffered(*in,65536);return juce::SHA256(buffered).toHexString();}
void saveJson(const juce::File& path,const juce::var& value){if(path.getParentDirectory().createDirectory().failed())throw std::runtime_error("Dossier de paramètres indisponible.");juce::TemporaryFile temporary(path);auto stream=temporary.getFile().createOutputStream();if(!stream)throw std::runtime_error("Enregistrement impossible.");auto bytes=juce::JSON::toString(value,false,17);if(!stream->write(bytes.toRawUTF8(),bytes.getNumBytesAsUTF8()))throw std::runtime_error("Enregistrement impossible.");stream->flush();if(stream->getStatus().failed())throw std::runtime_error("Enregistrement impossible.");stream.reset();if(!temporary.overwriteTargetFileWithTemporary())throw std::runtime_error("Enregistrement impossible, ancien fichier conservé.");}
Banks::Banks(juce::File p,juce::File d,juce::File shared,juce::File documentsBank):piano(std::move(p)),data(std::move(d)),sharedSettings(std::move(shared)),storage(sharedSettings==juce::File()?data:sharedSettings.getParentDirectory()),settings(juce::JSON::parse(data.getChildFile("soundbank-settings.json"))),progress(obj({{"phase",""},{"received",integer(0)},{"total",integer(0)},{"error",var()}})){
 if(sharedSettings==juce::File())return; // Explicitly isolated service/test sessions.
 auto common=juce::JSON::parse(sharedSettings);auto local=settings;
 std::vector<juce::File> candidates;
 for(auto preference:{common,local}){auto path=preference["path"].toString();if(path.isNotEmpty())candidates.emplace_back(path);}
 candidates.push_back(storage.getChildFile("soundbanks/msghq-0.2.1/MuseScore_General_HQ.sf2"));
 candidates.push_back(data.getChildFile("soundbanks/msghq-0.2.1/MuseScore_General_HQ.sf2"));
 candidates.push_back(piano.getSiblingFile("MuseScore_General_HQ.sf2"));
 if(documentsBank!=juce::File())candidates.push_back(documentsBank);
 settings=var();
 for(const auto& file:candidates){if(!file.existsAsFile())continue;
   try{verify(file);}catch(const std::exception&){continue;}
   settings=obj({{"use_hq",true},{"path",file.getFullPathName()}});
   if(common["path"].toString()!=file.getFullPathName())try{saveJson(sharedSettings,settings);}catch(const std::exception& e){diagnostics::event("soundbank_preferences_failed",String::fromUTF8(e.what()));}
   break;
 }
}
juce::File Banks::selected()const{std::lock_guard<std::mutex> l(mutex);auto remembered=settings["path"].toString();if(remembered.isNotEmpty()){juce::File file(remembered);if(file.existsAsFile())return file;}if(sharedSettings!=juce::File())return {};for(auto file:{storage.getChildFile("soundbanks/msghq-0.2.1/MuseScore_General_HQ.sf2"),data.getChildFile("soundbanks/msghq-0.2.1/MuseScore_General_HQ.sf2"),piano.getSiblingFile("MuseScore_General_HQ.sf2")})if(file.existsAsFile())return file;return {};}
juce::var Banks::status()const{auto file=selected();std::lock_guard<std::mutex> l(mutex);bool available=file!=juce::File();auto custom=juce::JSON::parse(storage.getChildFile("custom-soundfonts.json"));return obj({{"custom",custom.isArray()?custom:var(juce::Array<var>{})},{"active",available},{"available",available},{"path",available?var(file.getFullPathName()):var()},{"name",String::fromUTF8("MuseScore General HQ 0.2.1 — SF2")},{"busy",busy.load()},{"progress",progress.clone()}});}
juce::var Banks::customBanks()const{std::lock_guard<std::mutex> lock(mutex);auto banks=juce::JSON::parse(storage.getChildFile("custom-soundfonts.json"));return banks.isArray()?banks:var(juce::Array<var>{});}
juce::var Banks::addCustom(const juce::File& file){auto entry=audio::Sampler::inspectSoundfont(piano,file);std::lock_guard<std::mutex> lock(mutex);auto existing=juce::JSON::parse(storage.getChildFile("custom-soundfonts.json"));juce::Array<var> list;if(auto* a=existing.getArray())for(auto item:*a)if(item["path"]!=entry["path"])list.add(item);if(list.size()>=64)throw std::invalid_argument("Maximum de 64 banques SF2 personnelles.");list.add(entry);saveJson(storage.getChildFile("custom-soundfonts.json"),var(list));return var(list);}
void Banks::removeCustom(const String& path){std::lock_guard<std::mutex> lock(mutex);auto existing=juce::JSON::parse(storage.getChildFile("custom-soundfonts.json"));juce::Array<var> list;if(auto* a=existing.getArray())for(auto item:*a)if(item["path"].toString()!=path)list.add(item);saveJson(storage.getChildFile("custom-soundfonts.json"),var(list));}
void Banks::check()const{if(cancelled.load())throw std::invalid_argument("Téléchargement annulé.");}
void Banks::stage(const char* phase,std::uint64_t received,std::uint64_t total){std::lock_guard<std::mutex> l(mutex);progress=obj({{"phase",phase},{"received",integer(received)},{"total",integer(total)},{"error",var()}});}
void Banks::verify(const juce::File& file,const std::function<void()>& check){if(!file.existsAsFile())throw std::invalid_argument("Fichier MuseScore General HQ introuvable.");if(std::uint64_t(file.getSize())!=bankBytes)throw std::invalid_argument("Choisissez MuseScore General HQ 0.2.1 au format SF2 ; ce fichier ne correspond pas à la banque attendue.");check();if(fileHash(file)!=bankSha)throw std::invalid_argument("Le fichier SF2 ne correspond pas à MuseScore General HQ 0.2.1.");check();}
void Banks::save(const juce::File& file){auto next=obj({{"use_hq",true},{"path",file.getFullPathName()}});saveJson(sharedSettings==juce::File()?data.getChildFile("soundbank-settings.json"):sharedSettings,next);std::lock_guard<std::mutex> l(mutex);settings=next;}
void Banks::select(const juce::File& path){std::unique_lock<std::mutex> operationGuard(operation,std::try_to_lock);if(!operationGuard.owns_lock())throw std::invalid_argument("Une opération sur la banque est déjà en cours.");if(busy.load())throw std::invalid_argument("Une opération sur la banque est déjà en cours.");auto chosen=path==juce::File()?selected():path;verify(chosen);save(chosen);}
void Banks::download(){std::unique_lock<std::mutex> operationGuard(operation,std::try_to_lock);if(!operationGuard.owns_lock())throw std::invalid_argument("Une opération sur la banque est déjà en cours.");if(busy.exchange(true))throw std::invalid_argument("Une opération sur la banque est déjà en cours.");struct Working{std::atomic_bool& b;~Working(){b=false;}} guard{busy};cancelled=false;
 try{
 auto existing=selected();if(existing!=juce::File()){try{verify(existing,[&]{check();});save(existing);stage("done",bankBytes,bankBytes);return;}catch(const std::exception&){check();}}
 auto dir=storage.getChildFile("soundbanks/msghq-0.2.1");if(dir.createDirectory().failed())throw std::runtime_error("Dossier de banque indisponible.");auto destination=dir.getChildFile("MuseScore_General_HQ.sf2");
 if(destination.existsAsFile()){try{verify(destination,[&]{check();});save(destination);stage("done",bankBytes,bankBytes);return;}catch(const std::exception&){check();}}
 auto source=juce::JSON::parse(piano.getSiblingFile("msghq.json").loadFileAsString());auto url=source["url"].toString();if(!url.startsWith("https://")||source["sha256"].toString()!=bankSha||source["package_sha256"].toString().length()!=64)throw std::invalid_argument("Source de téléchargement invalide.");
 stage("download");int status=0;auto input=juce::URL(url).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(15000).withNumRedirectsToFollow(5).withStatusCode(&status).withExtraHeaders("User-Agent: Phthongos SoundBank Installer\r\n"));if(!input||status<200||status>=300)throw std::runtime_error("Téléchargement de la banque impossible.");auto total=input->getTotalLength();if(total>juce::int64(maxPackage))throw std::invalid_argument("Paquet de banque trop volumineux.");
 juce::TemporaryFile package(dir.getChildFile("package.deb"));auto output=package.getFile().createOutputStream();if(!output)throw std::runtime_error("Écriture du paquet impossible.");std::array<char,65536> bytes;std::uint64_t received=0;
 while(true){check();auto count=input->read(bytes.data(),int(bytes.size()));if(count<=0)break;received+=std::uint64_t(count);if(received>maxPackage)throw std::invalid_argument("Paquet de banque trop volumineux.");if(!output->write(bytes.data(),std::size_t(count)))throw std::runtime_error("Écriture du paquet impossible.");stage("download",received,total>0?std::uint64_t(total):0);}
 output->flush();output.reset();check();if(total>0&&received!=std::uint64_t(total))throw std::invalid_argument("Téléchargement incomplet.");stage("verify_package",received,received);if(fileHash(package.getFile())!=source["package_sha256"].toString())throw std::invalid_argument("Le paquet téléchargé ne correspond pas à la version attendue.");
 auto archive=package.getFile().createInputStream();std::array<char,8> magic;readExact(*archive,magic.data(),8);if(std::string(magic.data(),8)!="!<arch>\n")throw std::invalid_argument("Format Debian invalide.");bool foundData=false;juce::TemporaryFile bank(destination);
 for(int member=0;member<16;++member){check();std::array<char,60> header;readExact(*archive,header.data(),60);if(std::string(header.data()+58,2)!="`\n")throw std::invalid_argument("En-tête Debian invalide.");auto size=number(header.data()+48,10,10);if(size>maxPackage)throw std::invalid_argument("Archive Debian trop volumineuse.");auto name=String::fromUTF8(header.data(),16).trim().trimCharactersAtEnd("/");if(name!="data.tar.xz"){if(!archive->setPosition(archive->getPosition()+juce::int64(size+size%2)))throw std::invalid_argument("Paquet incomplet.");continue;}
 Xz xz(*archive,size);auto sf=bank.getFile().createOutputStream();if(!sf)throw std::runtime_error("Écriture de la banque impossible.");bool found=false;std::uint64_t consumed=0;stage("extract",0,bankBytes);
 while(true){check();std::array<char,512> tar;xz.exact(tar.data(),512);if(std::all_of(tar.begin(),tar.end(),[](char c){return c==0;}))break;auto expected=number(tar.data()+148,8,8);std::uint64_t sum=0;for(int i=0;i<512;++i)sum+=i>=148&&i<156?32:static_cast<unsigned char>(tar[i]);if(sum!=expected)throw std::invalid_argument("En-tête TAR incorrect.");auto length=number(tar.data()+124,12,8);consumed+=length+512;if(consumed>1024ull*1024*1024)throw std::invalid_argument("Archive trop volumineuse.");auto entry=String::fromUTF8(tar.data(),100);while(entry.startsWith("./"))entry=entry.substring(2);
 if(entry=="usr/share/sounds/sf2/MuseScore_General_Full.sf2"){if(found||length!=bankBytes||(tar[156]!=0&&tar[156]!='0'))throw std::invalid_argument("Entrée de banque invalide.");std::uint64_t left=length;while(left){check();auto count=int(std::min<std::uint64_t>(left,bytes.size()));xz.exact(bytes.data(),count);if(!sf->write(bytes.data(),std::size_t(count)))throw std::runtime_error("Écriture de la banque impossible.");left-=std::uint64_t(count);stage("extract",length-left,bankBytes);}found=true;}else xz.skip(length,[&]{check();});xz.skip((512-length%512)%512,[&]{check();});}
 while(xz.read(bytes.data(),int(bytes.size()))>0)check();if(!found)throw std::invalid_argument("MuseScore_General_Full.sf2 absent du paquet Debian.");sf->flush();sf.reset();stage("verify_bank",0,bankBytes);verify(bank.getFile(),[&]{check();});check();if(!bank.overwriteTargetFileWithTemporary())throw std::runtime_error("Installation de la banque impossible.");save(destination);stage("done",bankBytes,bankBytes);foundData=true;break;
 }if(!foundData)throw std::invalid_argument("Données de la banque absentes du paquet Debian.");
 }catch(const std::exception& e){std::lock_guard<std::mutex> l(mutex);progress.getDynamicObject()->setProperty("phase",cancelled.load()?"cancelled":"failed");progress.getDynamicObject()->setProperty("error",String::fromUTF8(e.what()));throw;}
}
}
