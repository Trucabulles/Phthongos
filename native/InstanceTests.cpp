#include "App.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

bool App::runInputQueueTests(){
#if JUCE_WINDOWS
  // Focus only our hidden test window: never inject desktop input or attach queues.
  HWND handle=CreateWindowExW(0,L"STATIC",L"Phthongos input regression",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(!handle)return false;
  auto previous=SetFocus(handle);
  struct Worker : juce::Thread {
    juce::WaitableEvent ready,release;DWORD id=0;
    Worker():Thread("Input queue regression"){}
    void run() override {id=GetCurrentThreadId();MSG msg{};PeekMessageW(&msg,nullptr,0,0,PM_NOREMOVE);ready.signal();release.wait(10000);}
    ~Worker() override {release.signal();stopThread(10000);}
  };
  bool isolated=false,focused=false;
  {
    Worker worker;worker.startThread();
    if(worker.ready.wait(5000)){
      GUITHREADINFO mainInfo{sizeof(GUITHREADINFO)},workerInfo{sizeof(GUITHREADINFO)};
      focused=GetGUIThreadInfo(GetCurrentThreadId(),&mainInfo)&&mainInfo.hwndFocus==handle;
      isolated=GetGUIThreadInfo(worker.id,&workerInfo)&&workerInfo.hwndFocus!=handle;
    }
  }
  SetFocus(previous);DestroyWindow(handle);
  auto out=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results");out.createDirectory();
  out.getChildFile("input-queues-latest.json").replaceWithText(juce::JSON::toString(object({{"passed",focused&&isolated},{"gui_focus",focused},{"worker_isolated",isolated}})));
  return focused&&isolated;
#else
  return true;
#endif
}
// Real Windows peers and audio devices, separate processes, isolated test caches.
void App::runInstanceTests(const juce::File& wav,const juce::File& midi,const juce::File& output,std::function<void(bool)> done){
  struct Run {int stage=0,cycle=0;double started=juce::Time::getMillisecondCounterHiRes(),entered=started;bool finished=false;juce::Component::SafePointer<Form> form;std::vector<var> checks;};
  auto run=std::make_shared<Run>();auto poll=std::make_shared<std::function<void()>>();
  auto check=[run](bool ok,const String& label){run->checks.push_back(object({{"name",label},{"passed",ok}}));if(!ok)throw std::runtime_error(label.toStdString());};
  auto advance=[run](int stage){run->stage=stage;run->entered=juce::Time::getMillisecondCounterHiRes();};
  auto finish=[this,run,poll,done,output](bool ok){if(run->finished)return;run->finished=true;dirty=false;output.createDirectory();output.getChildFile("result.json").replaceWithText(juce::JSON::toString(object({{"passed",ok},{"stage",run->stage},{"status",status.getText()},{"checks",array(run->checks)}}),false,17));phthongos::diagnostics::event("instance_test_finished",ok?"passed":"failed");
    if(run->form)run->form->close();if(helpPopup)helpPopup->dismiss();
    juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
  *poll=[this,run,poll,advance,check,finish]{try{
    if(run->finished)return;
    auto now=juce::Time::getMillisecondCounterHiRes();if(now-run->started>90000){check(false,"Instance scenario completes within 90 seconds");return;}
    auto* window=findParentComponentOfClass<juce::DocumentWindow>();
    if(run->stage==1&&!locked&&notesReady&&!result.isVoid()){
      check(window&&window->isShowing(),"Native main window is visible");check(!roll.notes.empty(),"Real audio and reference MIDI loaded in isolated editor");
      mode=2;advance(2);control("seek",std::min(20.,roll.duration()/2),[this,advance](var){control("play",0,[advance](var){advance(3);});});
    }else if(run->stage==3&&now-run->entered>500){
      check((bool)playback["ready"]&&(bool)playback["playing"],"Playback stays available alongside another process");advance(4);control("pause",0,[this,advance](var){settings();advance(5);});
    }else if(run->stage==5&&!Form::activeForms.empty()){
      run->form=*Form::activeForms.begin();check(run->form->isShowing(),"Settings/audio-device enumeration opens a visible modal form");advance(6);window->setMinimised(true);
    }else if(run->stage==6&&now-run->entered>350){
      check(window->isMinimised(),"Main window can minimise while a form is open");window->setMinimised(false);window->toFront(true);advance(7);
    }else if(run->stage==7&&now-run->entered>350){
      check(!window->isMinimised()&&window->isShowing(),"Main window restores while modal form is open");check(run->form&&run->form->isShowing(),"Modal form remains reachable after restore");run->form->close();advance(8);
    }else if(run->stage==8&&Form::activeForms.empty()&&juce::Component::getNumCurrentlyModalComponents()==0){
      check(!isCurrentlyBlockedByAnotherModalComponent(),"Closing form releases main-window input");buttonMap["help"]->onClick();check(helpPopup&&helpPopup->isVisible(),"Help popup is created visibly");advance(9);
    }else if(run->stage==9&&now-run->entered>150){
      check(!helpPopup||helpPopup->isShowing()||!helpPopup->isCurrentlyModal(),"Background help is visible or has released its modal state");window->setMinimised(true);advance(10);
    }else if(run->stage==10&&now-run->entered>250){window->setMinimised(false);window->toFront(true);advance(11);
    }else if(run->stage==11&&now-run->entered>350){
      check(!window->isMinimised()&&window->isShowing(),"Main window restores with a child callout");check(!helpPopup||helpPopup->isShowing()||!helpPopup->isCurrentlyModal(),"Restored callout is reachable or has closed without blocking input");if(helpPopup)helpPopup->dismiss();advance(12);
    }else if(run->stage==12&&juce::Component::getNumCurrentlyModalComponents()==0){
      check(!isCurrentlyBlockedByAnotherModalComponent(),"Dismissed help leaves no modal input blocker");advance(13);invoke("audio_output_select",object({{"id",var()}}),[advance](var){advance(14);});
    }else if(run->stage==14){
      if(run->cycle>=12){advance(16);control("stop",0,[advance](var){advance(17);});}
      else{int cycle=run->cycle++;advance(15);mode=cycle%3;control("seek",std::min(roll.duration()-1.,2.+cycle*3.),[this,advance](var){control("play",0,[advance](var){advance(14);});});}
    }else if(run->stage==17&&!transportPending){
      check(run->cycle==12,"Twelve seek/play/mix cycles complete without starving commands");check(!isCurrentlyBlockedByAnotherModalComponent(),"All dialogs and callouts have released modal input");check(roll.notes.size()==items(result["editing"]["notes"]).size(),"Audio/window stress leaves the MIDI document intact");finish(true);return;
    }
    juce::Timer::callAfterDelay(75,[poll]{if(*poll)(*poll)();});
  }catch(const std::exception& e){run->checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));finish(false);}};
  advance(0);setBusy(true,"Instance diagnostic test");invoke("import_midi_audio",object({{"midiPath",midi.getFullPathName()},{"audioPath",wav.getFullPathName()}}),[this,advance,poll](var opened){audio=opened["audio"];roll.audio=audio;updateResult(opened["result"]);prepare([this,advance,poll](var){setBusy(false);advance(1);if(*poll)(*poll)();});},true,[finish](String){finish(false);});
  juce::Timer::callAfterDelay(75,[poll]{if(*poll)(*poll)();});
}

bool App::runDiagnosticsTests(){
  namespace d=phthongos::diagnostics;
  auto output=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results");
  auto directory=output.getChildFile("diagnostics-"+juce::Uuid().toString());d::start(directory);
  d::guiHeartbeat("self_test: simulated modal blocker");
  {d::Span pending("test/blocked-command",false);pending.phase("queued");juce::Thread::sleep(5750);}
  d::suspendGuiHeartbeat();d::event("test_marker","completed");d::stop();
  bool queue=false,heartbeat=false,end=false,gui=false,finished=false;int lines=0;
  for(auto file:directory.findChildFiles(juce::File::findFiles,false,"*.jsonl")){
    juce::StringArray entries;entries.addLines(file.loadFileAsString());for(auto& line:entries){if(line.isEmpty())continue;auto value=juce::JSON::parse(line);if(!value.isObject())return false;++lines;auto event=value["event"].toString();
      queue|=event=="operation_pending"&&value["detail"].toString().contains("test/blocked-command / queued");heartbeat|=event=="gui_heartbeat_missing";end|=event=="end"&&value["detail"].toString()=="test/blocked-command";gui|=event=="gui_state";finished|=event=="session_end";
    }
  }
  bool passed=queue&&heartbeat&&end&&gui&&finished;output.getChildFile("diagnostics-latest.json").replaceWithText(juce::JSON::toString(object({{"passed",passed},{"checks",5},{"queue_detected",queue},{"heartbeat_detected",heartbeat},{"end_detected",end},{"gui_state_detected",gui},{"clean_shutdown_detected",finished},{"records",lines}})));return passed;
}
