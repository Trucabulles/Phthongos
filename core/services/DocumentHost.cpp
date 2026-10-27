#include "DocumentHost.h"
#include "../project/DocumentJson.h"
#include "Cleanup.h"
#include <map>
#include <mutex>
#include <cstring>
#include <stdexcept>
namespace phthongos::services {
namespace {
using juce::var;
struct Owned { project::Editor editor; };
std::mutex mutex;
std::map<std::uint64_t,Owned> documents;
std::uint64_t next=0;
var obj(std::initializer_list<std::pair<const char*,var>> fields){auto* o=new juce::DynamicObject;for(auto& f:fields)o->setProperty(f.first,f.second);return var(o);}
std::uint64_t integer(const var& v){if((!v.isInt()&&!v.isInt64())||(juce::int64)v<0)throw std::invalid_argument("Identifiant invalide.");return static_cast<std::uint64_t>((juce::int64)v);}
SourceSnapshot source(const var& v){SourceSnapshot s;
    auto* all=v["all"].getArray();auto* current=v["current"].getArray();auto* names=v["names"].getArray();
    if(!all||!current||!names)throw std::invalid_argument("Source documentaire invalide.");
    for(auto& n:*all)s.allNotes.push_back(project::readNote(n));for(auto& n:*current)s.notes.push_back(project::readNote(n));
    for(auto& n:*names)s.gmNames.push_back(n.toString().toStdString());s.sourceCount=integer(v["count"]);return s;
}
var dispatch(const juce::String& command,const var& args){
    std::lock_guard<std::mutex> lock(mutex);
    if(command=="create"){
        Owned o;if(args["state"].isObject())o.editor=project::readEditor(args["state"],source(args["source"]));
        const auto handle=++next;documents.emplace(handle,std::move(o));return var(static_cast<juce::int64>(handle));
    }
    const auto handle=integer(args["handle"]);auto it=documents.find(handle);
    if(it==documents.end())throw std::invalid_argument("Document C++ absent.");
    if(command=="drop"){documents.erase(it);return var();}
    auto& editor=it->second.editor;auto& doc=editor.document;
    if(command=="save")return project::writeEditor(editor);
    if(command=="duration")return var(doc.timelineDuration((double)args["base"]));
    if(command=="flags"){
        bool parts=false,muted=false,mutedParts=false,dynamics=false;
        auto scan=[&](const Layer& l){parts=parts||!l.parts.empty()||!l.deletedParts.empty();muted=muted||!l.muted.empty()||!l.mutedParts.empty();mutedParts=mutedParts||!l.mutedParts.empty();dynamics=dynamics||!l.dynamics.empty();};
        scan(doc.layer());for(auto& l:doc.undoLayers())scan(*l);for(auto& l:doc.redoLayers())scan(*l);
        auto& l=doc.layer();bool changes=!l.deletedParts.empty()||!l.parts.empty()||!l.muted.empty()||!l.mutedParts.empty()||!l.automaticMerges.empty()||!l.manual.empty()||!l.dynamics.empty();
        return obj({{"parts",parts},{"muted",muted},{"muted_parts",mutedParts},{"dynamics",dynamics},{"changes",changes}});
    }
    const auto s=source(args["source"]);
    if(command=="preview"){
        std::optional<LegacySupport> support;
        if(args["maps"].isObject()){LegacySupport m;m.frames=integer(args["maps"]["frames"]);auto* values=args["maps"]["note"].getArray();if(!values)throw std::invalid_argument("Cartes absentes.");
            for(auto& v:*values)m.notes.push_back((float)(double)v);support=std::move(m);}
        return cleanupPreview(doc.view(s).notes,doc.revision(),(float)(double)args["threshold"],args["scope"],args["settings"],support);
    }
    if(command=="validate"){auto checked=Document::restore(doc.state(),s);(void)checked;return var();}
    if(command=="view")return project::writeView(doc.view(s));
    if(command=="parts"){
        juce::Array<var> list;for(auto& p:doc.view(s).parts){auto v=obj({{"program",(int)p.program},{"is_drum",p.drum},{"label",juce::String::fromUTF8(p.label.c_str())}});
            if(p.identity)v.getDynamicObject()->setProperty("identity",(int)*p.identity);list.add(v);}return var(list);
    }
    if(command=="action")doc.act(s,integer(args["revision"]),project::readAction(args["action"]));
    else if(command=="dynamics"){
        auto cache=project::readDynamicsCache(args["cache"]);std::vector<NoteId> ids;for(auto& id:*args["ids"].getArray())ids.push_back(integer(id));
        Mapping m;auto v=args["mapping"];m.level=(double)v["level"];m.contrast=(double)v["contrast"];m.min=(int)v["min"];m.max=(int)v["max"];
        doc.applyDynamics(s,integer(args["revision"]),ids,m,cache);
    }else if(command=="clean"){
        auto preview=project::readCleanPreview(args["preview"]);std::vector<NoteId> ids;for(auto& id:*args["ids"].getArray())ids.push_back(integer(id));
        doc.applyCleanRuns(s,(float)(double)args["threshold"],integer(args["revision"]),ids,preview);
        auto value=args["preview"].clone();
        if(!preview)value=var();else{
            auto* o=value.getDynamicObject();o->setProperty("revision",(juce::int64)preview->revision);o->setProperty("before",(juce::int64)preview->before);o->setProperty("after",(juce::int64)preview->after);
            std::set<NoteId> chosen(ids.begin(),ids.end());juce::Array<var> runs,edges;
            for(auto& r:*value["runs"].getArray())if(!chosen.count(integer(r["ids"][0])))runs.add(r);
            for(auto& e:*value["proposals"].getArray())if(!chosen.count(integer(e["left"])))edges.add(e);
            o->setProperty("runs",var(runs));o->setProperty("proposals",var(edges));
        }
        return obj({{"view",project::writeView(doc.view(s))},{"preview",value}});
    }else throw std::invalid_argument("Commande documentaire inconnue.");
    return project::writeView(doc.view(s));
}
}
char* documentRequest(const char* command,const char* json) noexcept {
    try{
        var response;try{var args;auto error=juce::JSON::parse(juce::String::fromUTF8(json),args);if(error.failed())throw std::invalid_argument(error.getErrorMessage().toStdString());
            response=obj({{"ok",true},{"value",dispatch(juce::String::fromUTF8(command),args)}});
        }catch(const std::exception& e){response=obj({{"ok",false},{"error",juce::String::fromUTF8(e.what())}});}
        auto text=juce::JSON::toString(response,true,17);const auto bytes=text.getNumBytesAsUTF8();auto* output=new char[bytes+1];std::memcpy(output,text.toRawUTF8(),bytes+1);return output;
    }catch(...){return nullptr;}
}
void documentFree(char* value) noexcept {delete[] value;}
}
