#include "Pht.h"
#include <filesystem>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <stdexcept>
namespace phthongos::project {
namespace {
constexpr std::uint64_t maximum=1100000000;
constexpr std::array<std::uint64_t,5> limits={256000000,700000000,20000000,20000000,20000000};
[[noreturn]] void fail(const char* s){throw std::invalid_argument(s);}
std::string hash(const void* b,std::size_t n){return juce::SHA256(b,n).toHexString().toStdString();}
void metadata(const Container& c){
    auto m=c.metadata;if(!m.isObject()||!m["version"].isInt()||(int)m["version"]!=8)fail("Fichier .pht invalide ou version non prise en charge (PHT 8 requis).");
    if(!m["pcm_sha256"].isString()||m["pcm_sha256"].toString().toStdString()!=hash(c.data[0].data(),c.data[0].size()))fail("Métadonnées incompatibles.");
    if(!m["editor"].isObject()||!m["ui"].isObject())fail("Métadonnées incompatibles.");
}
}
void validateEnvelope(const Container& c){metadata(c);}
Container decode(const std::vector<std::uint8_t>& bytes){
    static const std::string magic="PHTONG01";
    if(bytes.size()<112||bytes.size()>maximum||!std::equal(magic.begin(),magic.end(),bytes.begin()))fail("Fichier .pht invalide ou version non prise en charge.");
    auto end=bytes.size()-64;auto digest=hash(bytes.data(),end);if(!std::equal(digest.begin(),digest.end(),bytes.begin()+end))fail("Projet incomplet ou corrompu (SHA-256).");
    std::size_t at=8;Container c;
    for(std::size_t i=0;i<5;++i){if(end-at<8)fail("Projet tronqué.");std::uint64_t n=0;for(int j=0;j<8;++j)n|=std::uint64_t(bytes[at++])<<(j*8);
        if(n>limits[i]||n>end-at)fail("Section invalide.");
        if(i==0){auto result=juce::JSON::parse(juce::String::fromUTF8(reinterpret_cast<const char*>(bytes.data()+at),static_cast<int>(n)),c.metadata);if(result.failed())fail("Métadonnées invalides.");}
        else c.data[i-1].assign(bytes.begin()+at,bytes.begin()+at+static_cast<std::size_t>(n));at+=static_cast<std::size_t>(n);
    }
    if(at!=end)fail("Données inattendues dans le projet.");
    // The last hybrid release still selected versions 1..7 from the content.
    // Read that envelope under its original source/version rules, then apply
    // all current PHT8 integrity, document and source validation. Writers stay 8.
    auto m=c.metadata;
    if(!m.isObject()||!m["version"].isInt())fail("Métadonnées incompatibles.");
    const int version=m["version"];
    if(version<1||version>8)fail("Version de projet non prise en charge.");
    c.originVersion=unsigned(version);
    if(version<8){
        auto present=[](const juce::var& v){return !v.isVoid()&&!v.isUndefined();};
        const bool midi=present(m["imported_midi"]),mu=present(m["muscriptor"]);
        if((version==3&&!mu)||((bool)m["midi_only"]&&(version<6||!midi))||
           (midi&&(version<5||(bool)m["imported_midi"]["created_empty"]))||
           (!midi&&mu&&(version==1||version==5)))fail("Source incompatible avec la version du projet.");
        c.metadata.getDynamicObject()->setProperty("version",8);
    }
    metadata(c);return c;
}
std::vector<std::uint8_t> encode(const Container& c){
    metadata(c);auto json=juce::JSON::toString(c.metadata,true);auto utf8=json.toRawUTF8();auto size=json.getNumBytesAsUTF8();
    if(size>limits[0])fail("Projet trop volumineux.");std::uint64_t total=8+40+64+size;
    for(std::size_t i=0;i<4;++i){if(c.data[i].size()>limits[i+1])fail("Projet trop volumineux.");total+=c.data[i].size();}if(total>maximum)fail("Projet trop volumineux.");
    std::vector<std::uint8_t> out={'P','H','T','O','N','G','0','1'};out.reserve(static_cast<std::size_t>(total));
    auto section=[&](const void* ptr,std::size_t n){for(int j=0;j<8;++j)out.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(n)>>(j*8)));if(n){auto* b=static_cast<const std::uint8_t*>(ptr);out.insert(out.end(),b,b+n);}};
    section(utf8,size);for(auto& data:c.data)section(data.data(),data.size());auto digest=hash(out.data(),out.size());out.insert(out.end(),digest.begin(),digest.end());return out;
}
Container read(const juce::File& path){if(path.getSize()<0||static_cast<std::uint64_t>(path.getSize())>maximum)fail("Projet trop volumineux.");juce::MemoryBlock block;if(!path.loadFileAsData(block))fail("Lecture du projet impossible.");auto* b=static_cast<const std::uint8_t*>(block.getData());return decode({b,b+block.getSize()});}
void saveAtomic(const juce::File& path,const Container& c,bool createOnly){
    if(!path.hasFileExtension("pht"))fail("Choisissez un fichier .pht.");auto bytes=encode(c);juce::TemporaryFile temp(path);
    {juce::FileOutputStream stream(temp.getFile());if(stream.failedToOpen()||!stream.write(bytes.data(),bytes.size()))fail("Enregistrement impossible, ancien fichier conservé.");stream.flush();if(stream.getStatus().failed())fail("Enregistrement impossible, ancien fichier conservé.");}
    if(createOnly){
#if JUCE_WINDOWS
        if(!MoveFileExW(temp.getFile().getFullPathName().toWideCharPointer(),path.getFullPathName().toWideCharPointer(),MOVEFILE_WRITE_THROUGH))fail("Création de la version impossible, aucun fichier existant remplacé.");
#else
        std::error_code ec;std::filesystem::create_hard_link(std::filesystem::u8path(temp.getFile().getFullPathName().toStdString()),std::filesystem::u8path(path.getFullPathName().toStdString()),ec);if(ec)fail("Création de la version impossible, aucun fichier existant remplacé.");
#endif
        return;
    }
    if(!temp.overwriteTargetFileWithTemporary())fail("Enregistrement impossible, ancien fichier conservé.");
}
}
