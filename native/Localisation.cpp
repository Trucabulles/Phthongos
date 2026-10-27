#include "Localisation.h"
#include "../core/services/Resources.h"
#include <atomic>
namespace localisation {
namespace {
struct Entry {const char* source;const char* french;const char* english;};
#include "Translations.inc"
std::atomic<Language> active{Language::english};
const std::map<juce::String,const Entry*>& catalog(){static const auto table=[] {std::map<juce::String,const Entry*> out;for(const auto& entry:entries)out.emplace(juce::String::fromUTF8(entry.source),&entry);return out;}();return table;}
juce::String chosen(const Entry& entry){return juce::String::fromUTF8(language()==Language::english?entry.english:entry.french);}
}
Language language(){return active.load();}
void setLanguage(Language value){active.store(value);}
juce::String code(){return language()==Language::english?"en":"fr";}
juce::String savedChoice(const juce::File& folder){auto preferences=juce::JSON::parse(folder.getChildFile("data/language.json"));return preferences["language"].toString()=="fr"?"fr":"en";}
void initialise(const juce::File& folder){setLanguage(savedChoice(folder)=="en"?Language::english:Language::french);}
void saveChoice(const juce::File& folder,const juce::String& value){if(value!="fr"&&value!="en")throw std::invalid_argument("Unsupported language");auto* o=new juce::DynamicObject;o->setProperty("language",value);phthongos::services::saveJson(folder.getChildFile("data/language.json"),juce::var(o));}
juce::String text(const juce::String& source){auto found=catalog().find(source);return found==catalog().end()?source:chosen(*found->second);}
juce::String retranslate(const juce::String& displayed,Language previous){
  for(const auto& entry:entries){auto old=juce::String::fromUTF8(previous==Language::english?entry.english:entry.french);if(displayed==old)return chosen(entry);}
  return displayed;
}
juce::String error(const juce::String& source){
  auto reversed=retranslate(source,language()==Language::english?Language::french:Language::english);if(reversed!=source)return reversed;auto found=catalog().find(source);if(found!=catalog().end())return chosen(*found->second);
  // Only known error-message prefixes; the path/field supplied by the user stays untouched.
  if(language()==Language::english)for(const auto& pair:catalog())if(pair.first.endsWith(": ")&&source.startsWith(pair.first))return chosen(*pair.second)+source.substring(pair.first.length());return source;
}
juce::String format(const juce::String& source,std::initializer_list<juce::String> values){auto pattern=text(source);std::vector<juce::String> args(values);juce::String out;for(int i=0;i<pattern.length();){bool matched=false;for(int j=0;j<int(args.size());++j){auto marker="{"+juce::String(j)+"}";if(pattern.substring(i).startsWith(marker)){out+=args[j];i+=marker.length();matched=true;break;}}if(!matched)out+=juce::String::charToString(pattern[i++]);}return out;}
juce::String count(const juce::String& key,int n){return format(key+(n==1?".one":".other"),{juce::String(n)});}
juce::var report(juce::var value){
  // Copy only branches with translated presentation text. Numeric DSP arrays and
  // all project/note metadata stay shared and are never modified in the core.
  if(auto* items=value.getArray()) {
    if(items->isEmpty()||(!(*items)[0].isObject()&&!(*items)[0].isArray()))return value;
    juce::var changed;
    for(int i=0;i<items->size();++i){auto next=report((*items)[i]);if(next!=(*items)[i]){if(changed.isVoid())changed=juce::var(*items);changed.getArray()->set(i,next);}}
    return changed.isVoid()?value:changed;
  }
  if(auto* object=value.getDynamicObject()) {
    juce::var changed;
    for(const auto& property:object->getProperties()) {
      auto key=property.name.toString();auto next=property.value;
      if(key=="reasons"||key=="warnings") {
        if(auto* array=next.getArray()){juce::Array<juce::var> texts;bool different=false;for(const auto& item:*array){auto translated=item.isString()?juce::var(error(item.toString())):item;different|=translated!=item;texts.add(translated);}if(different)next=juce::var(texts);}
      } else if((key=="notice"||key=="confidence"||key=="error")&&next.isString())next=error(next.toString());
      else if(next.isArray()||next.isObject())next=report(next);
      if(next!=property.value) {
        if(changed.isVoid()){auto* copy=new juce::DynamicObject;for(const auto& p:object->getProperties())copy->setProperty(p.name,p.value);changed=juce::var(copy);}
        changed.getDynamicObject()->setProperty(property.name,next);
      }
    }
    return changed.isVoid()?value:changed;
  }
  return value;
}
int catalogSize(){return int(std::size(entries));}
}
