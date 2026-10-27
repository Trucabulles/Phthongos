#include "Diagnostics.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#if JUCE_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace phthongos::diagnostics {
namespace {
double now(){return juce::Time::getMillisecondCounterHiRes();}
struct Entry {juce::String operation,phase="running";double started=now(),changed=started,lastWarning=0;bool verbose=true;};
struct Session {
  std::mutex mutex,outputMutex;std::condition_variable wake;std::thread watchdog;
  std::map<std::uint64_t,Entry> active;std::uint64_t next=0;std::atomic_bool outputAvailable{false};bool enabled=false,stopping=false,gui=false;
  double heartbeat=0,lastGuiWarning=0;juce::String lastState;juce::File file;int rotation=0;std::deque<juce::String> records;
  ~Session(){finish();}
  void write(const juce::String& kind,const juce::String& detail,std::uint64_t id=0,double elapsed=0){
    std::lock_guard<std::mutex> lock(outputMutex);if(!outputAvailable.load())return;
    auto* data=new juce::DynamicObject;
    data->setProperty("time",juce::Time::getCurrentTime().toISO8601(true));data->setProperty("monotonic_ms",now());
    data->setProperty("thread",juce::String::toHexString((juce::int64)(std::uintptr_t)juce::Thread::getCurrentThreadId()));
    data->setProperty("event",kind);data->setProperty("detail",detail);data->setProperty("id",(juce::int64)id);data->setProperty("elapsed_ms",elapsed);
    if(records.size()>=4096)records.pop_front();
    records.push_back(juce::JSON::toString(juce::var(data),true));
  }
  void flush(){
    std::deque<juce::String> pending;{std::lock_guard<std::mutex> lock(outputMutex);pending.swap(records);}
    if(pending.empty())return;
    if(file.getSize()>4*1024*1024){rotation=1-rotation;file=file.getSiblingFile(file.getFileNameWithoutExtension().upToLastOccurrenceOf("-",true,false)+juce::String(rotation)+".jsonl");file.replaceWithText("");}
    if(auto output=file.createOutputStream()){for(auto& line:pending)output->writeText(line+"\n",false,false,nullptr);output->flush();}
  }
  void finish(){ {std::lock_guard<std::mutex> lock(mutex);stopping=true;enabled=false;}wake.notify_all();if(watchdog.joinable())watchdog.join(); }
  void run(){std::unique_lock<std::mutex> lock(mutex);while(!stopping){wake.wait_for(lock,std::chrono::milliseconds(250));if(stopping)break;auto t=now();std::vector<std::tuple<juce::String,std::uint64_t,double>> warnings;
    for(auto& item:active)if(t-item.second.changed>3000&&t-item.second.lastWarning>5000){item.second.lastWarning=t;warnings.emplace_back(item.second.operation+" / "+item.second.phase,item.first,t-item.second.started);}
    bool stalled=gui&&t-heartbeat>5000&&t-lastGuiWarning>5000;if(stalled)lastGuiWarning=t;auto stale=t-heartbeat;
    lock.unlock();for(auto& w:warnings)write("operation_pending",std::get<0>(w),std::get<1>(w),std::get<2>(w));if(stalled)write("gui_heartbeat_missing",{},0,stale);flush();lock.lock();
  }lock.unlock();flush();}
};
Session& session(){static Session value;return value;}
}
void start(const juce::File& directory){auto& s=session();s.finish();if(directory.createDirectory().failed())return;
#if JUCE_WINDOWS
 auto pid=GetCurrentProcessId();
#else
 auto pid=getpid();
#endif
 {std::lock_guard<std::mutex> output(s.outputMutex);s.file=directory.getChildFile("session-"+juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S")+"-"+juce::String((int)pid)+"-"+juce::Uuid().toString().substring(0,8)+"-0.jsonl");}
 {std::lock_guard<std::mutex> lock(s.mutex);s.stopping=false;s.enabled=true;s.gui=false;s.active.clear();}
 s.outputAvailable=true;
 s.write("session_start","pid="+juce::String((int)pid));s.watchdog=std::thread([&s]{s.run();});
}
void stop(){auto& s=session();s.write("session_end",{});s.finish();s.outputAvailable=false;}
void event(const juce::String& kind,const juce::String& detail){session().write(kind,detail);}
void guiHeartbeat(const juce::String& state){auto& s=session();bool changed=false;{std::lock_guard<std::mutex> lock(s.mutex);if(!s.enabled)return;s.gui=true;s.heartbeat=now();changed=state!=s.lastState;s.lastState=state;}if(changed)s.write("gui_state",state);}
void suspendGuiHeartbeat(){auto& s=session();std::lock_guard<std::mutex> lock(s.mutex);s.gui=false;}
Span::Span(const juce::String& operation,bool verbose){auto& s=session();{std::lock_guard<std::mutex> lock(s.mutex);if(!s.enabled)return;id=++s.next;auto& e=s.active[id];e.operation=operation;e.verbose=verbose;}if(verbose)s.write("begin",operation,id);}
void Span::phase(const juce::String& phase){if(!id)return;auto& s=session();bool verbose=false;{std::lock_guard<std::mutex> lock(s.mutex);auto i=s.active.find(id);if(i==s.active.end())return;i->second.phase=phase;i->second.changed=now();verbose=i->second.verbose;}if(verbose)s.write("phase",phase,id);}
Span::~Span(){if(!id)return;auto& s=session();Entry e;{std::lock_guard<std::mutex> lock(s.mutex);auto i=s.active.find(id);if(i==s.active.end())return;e=i->second;s.active.erase(i);}auto elapsed=now()-e.started;if(e.verbose||elapsed>1000)s.write("end",e.operation,id,elapsed);}
}
