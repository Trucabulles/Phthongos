#include "App.h"
#include "../core/project/Prepared.h"
#include "../core/project/Pht.h"
#include "../core/services/Resources.h"

namespace {
bool sameValue(const juce::var& a,const juce::var& b){
  if(auto* x=a.getDynamicObject()){
    auto* y=b.getDynamicObject();if(!y||x->getProperties().size()!=y->getProperties().size())return false;
    for(auto& p:x->getProperties())if(!y->hasProperty(p.name)||!sameValue(p.value,y->getProperty(p.name)))return false;
    return true;
  }
  if(a.isArray()){if(!b.isArray()||a.size()!=b.size())return false;for(int i=0;i<a.size();++i)if(!sameValue(a[i],b[i]))return false;return true;}
  if((a.isInt()||a.isInt64())&&(b.isInt()||b.isInt64()))return (juce::int64)a==(juce::int64)b;
  if((a.isDouble()||a.isInt()||a.isInt64())&&(b.isDouble()||b.isInt()||b.isInt64()))
    return double(a)==double(b)||float(double(a))==float(double(b));
  return a==b;
}
}

bool App::runProjectCompatibilityTest(const juce::File& path){
  using namespace phthongos::project;
  const auto executable=juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  auto directory=executable.getSiblingFile("test-results/compatibility-"+juce::Uuid().toString());directory.createDirectory();
  std::vector<var> checks;bool passed=false;
  auto check=[&](bool ok,const String& name){checks.push_back(object({{"name",name},{"passed",ok}}));if(!ok)throw std::runtime_error(name.toStdString());};
  auto call=[&](const String& command,const var& args){auto reply=bridge.call(command,args);if(!(bool)reply["ok"])throw std::runtime_error(reply["error"].toString().toStdString());return reply["value"];};
  try{
    const auto originalHash=phthongos::services::fileHash(path);
    auto original=read(path);
    const auto installedHash=phthongos::services::fileHash(executable.getSiblingFile("runtime/manifest.json"));
    check(original.metadata["runtime_sha256"].toString()!=installedHash,"Fixture uses a different runtime manifest");
    auto opened=call("open_project",object({{"path",path.getFullPathName()}}));
    auto restored=opened["result"]["editing"]["notes"].clone();
    check(restored.isArray()&&restored.size()>0,"Saved analysis opens without inference or original model weights");
    auto target=directory.getChildFile("compatible-copy.pht");
    auto parameters=object({{"audioId",opened["audio"]["id"]},{"analysisId",opened["result"]["analysis_id"]},
      {"revision",opened["result"]["editing"]["revision"]},{"sensitivity",opened["result"]["sensitivity"]},
      {"ui",opened["ui"]},{"path",target.getFullPathName()},{"create_only",true}});
    call("save_project",parameters);auto saved=read(target);
    check(saved.data==original.data,"Audio and analysis maps preserved byte for byte");
    check(saved.metadata["runtime_sha256"]==original.metadata["runtime_sha256"],"Analysis runtime provenance preserved across Save As");
    check(sameValue(saved.metadata["candidates"],original.metadata["candidates"]),"Raw candidate identities and times preserved");
    auto expectedEditor=original.metadata["editor"].clone();
    for(auto key:{"undo","redo"})if(auto* history=expectedEditor[key].getArray())
      if(history->size()>int(phthongos::Document::historyLimit))history->removeRange(0,history->size()-int(phthongos::Document::historyLimit));
    check(sameValue(saved.metadata["editor"],expectedEditor),"Edits, selection and supported Undo/Redo history preserved");
    check(sameValue(saved.metadata["ui"],original.metadata["ui"]),"Saved display and playback settings preserved");
    auto reopened=call("open_project",object({{"path",target.getFullPathName()}}));
    check(sameValue(restored,reopened["result"]["editing"]["notes"]),"Saved copy reopens with identical editable notes");
    if((bool)reopened["result"]["editing"]["can_undo"]){
      auto command=object({{"audioId",reopened["audio"]["id"]},{"analysisId",reopened["result"]["analysis_id"]},
        {"revision",reopened["result"]["editing"]["revision"]},{"sensitivity",reopened["result"]["sensitivity"]},
        {"action",object({{"kind","undo"}})}});
      auto undone=call("edit_notes",command);command.getDynamicObject()->setProperty("revision",undone["editing"]["revision"]);
      command.getDynamicObject()->setProperty("action",object({{"kind","redo"}}));
      check(sameValue(restored,call("edit_notes",command)["editing"]["notes"]),"Undo and Redo still restore the saved edits");
    }
    auto gm=juce::JSON::parse(executable.getSiblingFile("player-assets/gmPrograms.json").loadFileAsString());
    std::vector<std::string> names;for(auto& n:*gm.getArray())names.push_back(n.toString().toStdString());
    auto invalid=original;invalid.metadata=original.metadata.clone();
    invalid.metadata.getDynamicObject()->setProperty("frames",int(original.metadata["frames"])+1);
    bool rejected=false;try{phthongos::project::prepare(std::move(invalid),names,installedHash.toStdString());}catch(...){rejected=true;}
    check(rejected,"Incompatible map dimensions are still rejected");
    invalid=original;invalid.metadata=original.metadata.clone();invalid.metadata.getDynamicObject()->setProperty("version",99);
    rejected=false;try{phthongos::project::prepare(std::move(invalid),names,installedHash.toStdString());}catch(...){rejected=true;}
    check(rejected,"Unsupported project format is still rejected");
    auto bytes=encode(original);bytes[bytes.size()/2]^=1;
    rejected=false;try{decode(bytes);}catch(...){rejected=true;}
    check(rejected,"Corrupt project checksum is still rejected");
    check(phthongos::services::fileHash(path)==originalHash,"User original remains unchanged");
    passed=true;
  }catch(const std::exception& e){checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));}
  auto report=object({{"passed",passed},{"checks",array(checks)}});
  directory.getChildFile("report.json").replaceWithText(juce::JSON::toString(report,false,17));
  directory.getParentDirectory().getChildFile("compatibility-latest.json").replaceWithText(juce::JSON::toString(object({{"passed",passed},{"directory",directory.getFullPathName()},{"checks",array(checks)}}),false,17));
  return passed;
}
