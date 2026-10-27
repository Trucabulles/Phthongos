#include "App.h"
#include "../core/midi/Midi.h"

namespace {
class InitialisationPopup final:public juce::Component,private juce::Timer {
 juce::Image logo;juce::Colour paper,ink,border;int dots=0;
 void timerCallback()override{dots=(dots+1)%3;repaint();}
 void visibilityChanged()override{if(isVisible()){dots=0;startTimer(350);}else stopTimer();}
public:
 InitialisationPopup(juce::Image image,juce::Colour background,juce::Colour text,juce::Colour outline):logo(std::move(image)),paper(background),ink(text),border(outline){setName(tr(String::fromUTF8("Initialisation…")));setInterceptsMouseClicks(false,false);setAlwaysOnTop(true);startTimer(350);}
 void paint(juce::Graphics& g)override{
  auto box=getLocalBounds().toFloat().reduced(.5f);g.setColour(paper);g.fillRoundedRectangle(box,22.4f);g.setColour(border);g.drawRoundedRectangle(box,22.4f,1.f);
  if(logo.isValid()){g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);g.drawImageWithin(logo,49,28,252,238,juce::RectanglePlacement::centred);}
  auto caption=tr(String::fromUTF8("Initialisation…")).trimCharactersAtEnd(String::fromUTF8("…."));juce::Font font(juce::FontOptions(23.8f));g.setFont(font);g.setColour(ink);
  juce::GlyphArrangement label,tail;label.addLineOfText(font,caption,0,0);tail.addLineOfText(font,"...",0,0);int width=int(std::ceil(label.getBoundingBox(0,-1,true).getWidth()))+4,extra=int(std::ceil(tail.getBoundingBox(0,-1,true).getWidth()))+3,x=(getWidth()-width-extra)/2;
  g.drawText(caption,x,290,width,35,juce::Justification::centredLeft,false);g.drawText(String::repeatedString(".",dots+1),x+width,290,extra,35,juce::Justification::centredLeft,false);
 }
};
}
void App::syncInitialisationPopup(){
 bool visible=initialisingProject&&locked;
 if(visible&&!initialisationPopup){initialisationPopup=std::make_unique<InitialisationPopup>(logo,theme.paper,theme.ink,theme.border);addChildComponent(*initialisationPopup);}
 if(initialisationPopup){initialisationPopup->setBounds(getLocalBounds().withSizeKeepingCentre(350,350));initialisationPopup->setVisible(visible);if(visible)initialisationPopup->toFront(false);}
}

void App::startEmptyProject(std::function<void(bool)> done) {
  if(locked||!audio.isVoid()){if(done)done(false);return;}
  initialisingProject=true;
  setBusy(true,tr(String::fromUTF8("Initialisation…")));
  invoke("create_empty_midi_project",object({}),[this,done](var value){
    audio=value["audio"];roll.audio=audio;referenceAudio=false;mode=1;partId=256;
    projectPath={};projectSaveBase={};projectSaveFolder={};projectName={};projectIdentity=juce::Uuid().toString();
    hiddenParts.clear();legacyVisiblePart=-1;voiceLevels=object({});
    roll.clearSelection();roll.clearDetail();roll.view=View();roll.view.pitchZoom=4;roll.view.pitchPan=.5;
    roll.displayMode=0;displayMode.setSelectedId(1,juce::dontSendNotification);audioLayer.setVisible(false);opacity.setVisible(false);visualGain.setVisible(false);
    updateResult(value["result"],false);refreshTempo();
    prepare([this,done](var){
      invoke("player_mix",object({{"audioId",audio["id"]},{"mode",1},{"audioDb",audioDb},{"midiDb",midiDb}}),[this,done](var){
        invoke("player_snapshot",object({}),[this,done](var value){
          playback=value;roll.updatePlayback(value);dirty=false;
          setBusy(false,tr(String::fromUTF8("Prêt")));
          resized();if(done)done(true);
        });
      });
    });
  },false,[this,done](String error){setBusy(false,error);if(done)done(false);});
}

void App::runEmptyMidiTests(std::function<void(bool)> done){
  stopTimer();setSize(1400,900);setVisible(true);
  struct Run{int stage=0;bool finished=false;double started=juce::Time::getMillisecondCounterHiRes(),oldDuration=0,oldSpan=0;juce::File folder;juce::Image firstDots;double dotsAt=0;var notes,extended;std::vector<var> checks;std::unique_ptr<App> replacement;};
  auto r=std::make_shared<Run>();auto poll=std::make_shared<std::function<void()>>();r->folder=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/empty-"+juce::Uuid().toString());r->folder.createDirectory();
  auto finish=[this,r,poll,done](bool ok){if(r->finished)return;r->finished=true;auto report=object({{"passed",ok},{"stage",r->stage},{"language",localisation::code()},{"checks",array(r->checks)},{"directory",r->folder.getFullPathName()}});r->folder.getParentDirectory().getChildFile("empty-latest.json").replaceWithText(juce::JSON::toString(report));juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
  auto check=[r](bool ok,const String& text){r->checks.push_back(object({{"name",text},{"passed",ok}}));if(!ok)throw std::runtime_error(text.toStdString());};
  startEmptyProject([r,finish](bool ok){if(ok)r->stage=1;else finish(false);});
  check(initialisationPopup&&initialisationPopup->isVisible()&&initialisationPopup->getWidth()==350&&initialisationPopup->getHeight()==350&&initialisationPopup->getBounds().getCentre()==getLocalBounds().getCentre(),"Startup immediately shows the centred 350-square initialization popup");{juce::FileOutputStream image(r->folder.getChildFile("initialisation-popup.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);}
  *poll=[this,r,poll,finish,check]{try{
    if(r->finished)return;if(juce::Time::getMillisecondCounterHiRes()-r->started>60000)throw std::runtime_error("Empty-project workflow timed out: "+status.getText().toStdString());
    if(r->stage==1&&!locked&&notesReady){
      check(initialisationPopup&&!initialisationPopup->isVisible()&&!initialisingProject,"The popup disappears as soon as the initial piano and audio engine are ready");
      check((bool)audio["midi_only"]&&parts.size()==1&&partId==256&&roll.notes.empty()&&!dirty&&!projectPath.isNotEmpty(),"Startup creates a clean empty piano project without importing any file");
      check(!audioLayer.isVisible()&&!opacity.isVisible()&&!visualGain.isVisible(),"MIDI-only startup hides audio display controls without overlapping the zoom tools");
      check(roll.editable&&buttonMap["add"]->isEnabled()&&buttonMap["partAdd"]->isEnabled()&&buttonMap["tempo"]->isEnabled(),"Drawing, adding voices and BPM are available immediately");
      check(!(bool)result["editing"]["can_undo"]&&editorBadge.getText()==tr(String::fromUTF8("Édition MIDI")),"Initial piano is baseline state, with a clear MIDI editing badge");
      r->stage=-1;saveProjectTo(r->folder.getChildFile("empty.pht").getFullPathName(),[r,finish](bool ok){if(ok)r->stage=2;else finish(false);});
    }else if(r->stage==2&&!locked){
      check(r->folder.getChildFile("empty.pht").existsAsFile(),"An empty MIDI project can already be saved");
      roll.keyAudition(60);r->stage=3;
    }else if(r->stage==3){
      auto value=bridge.call("player_snapshot",object({}))["value"];bool sounding=false;for(const auto& key:items(value["active_keys"]))sounding|=(int)key==60;
      if(sounding){check(true,"Bottom keyboard produces a held piano note before any import or note entry");roll.keyAudition(-1);r->stage=4;}
    }else if(r->stage==4){
      auto value=bridge.call("player_snapshot",object({}))["value"];bool sounding=false;for(const auto& key:items(value["active_keys"]))sounding|=(int)key==60;
      if(!sounding){check(roll.notes.empty()&&!dirty,"Keyboard audition and release never create notes or dirty the empty project");
        buttonMap["add"]->onClick();auto at=juce::Point<float>(float(roll.x(.347)),float(roll.y(60)+roll.row()*.5));auto now=juce::Time::getCurrentTime();juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&roll,&roll,now,at,now,1,false);roll.mouseDown(event);roll.mouseUp(event);r->stage=5;}
    }else if(r->stage==5&&!locked&&notesReady&&roll.notes.size()==1){
      auto n=roll.notes.front();check(n.pitch==60&&std::abs(n.start-.347)<.01&&std::abs(n.end-n.start-.5)<.001&&dirty,"Pencil gesture creates a real MIDI note at its free timing without an audio source");r->stage=6;command(object({{"kind","undo"}}));
    }else if(r->stage==6&&!locked&&notesReady){
      check(roll.notes.empty()&&parts.size()==1,"Undo removes the first note while retaining the initial piano voice");r->stage=7;command(object({{"kind","redo"}}));
    }else if(r->stage==7&&!locked&&notesReady){
      check(roll.notes.size()==1,"Redo restores the manually entered note");r->stage=8;addNote(roll.duration()-1,64);
    }else if(r->stage==8&&!locked&&notesReady&&roll.notes.size()==2){
      check(number(result,"duration")>8&&roll.duration()>13,"Entering a note in the final margin extends the editable timeline");
      r->notes=result["editing"]["notes"].clone();auto response=bridge.call("set_project_tempo",object({{"audioId",audio["id"]},{"bpm",65}}));check((bool)response["ok"],"A BPM can be declared without importing audio or MIDI");refreshTempo();r->stage=9;buttonMap["partAdd"]->onClick();
    }else if(r->stage==9){
      auto* dialog=dynamic_cast<juce::DialogWindow*>(juce::Component::getCurrentlyModalComponent());auto* f=dialog?dynamic_cast<Form*>(dialog->getContentComponent()):nullptr;
      if(f){check(juce::JSON::toString(r->notes)==juce::JSON::toString(result["editing"]["notes"]),"Changing BPM preserves the manually entered timings");f->setValue("name","Apports");f->setValue("program",40);r->stage=10;f->onApply(f->state());}
    }else if(r->stage==10&&!locked&&notesReady&&!juce::Component::getCurrentlyModalComponent()&&parts.size()==2){
      check(roll.notes.size()==2&&partId!=256,"A second empty voice can be added without losing the notes already entered");r->stage=-1;saveProjectTo(r->folder.getChildFile("composed.pht").getFullPathName(),[r,finish](bool ok){if(ok)r->stage=11;else finish(false);});
    }else if(r->stage==11&&!locked){
      openProject(projectPath);r->stage=12;
    }else if(r->stage==12&&!locked&&notesReady){
      check(parts.size()==2&&juce::JSON::toString(r->notes)==juce::JSON::toString(result["editing"]["notes"]),"Reopening the composed PHT preserves both voices and all manually created notes");r->stage=13;exportMidiTo(r->folder.getChildFile("composed.mid").getFullPathName(),var());
    }else if(r->stage==13&&!locked){
      auto file=r->folder.getChildFile("composed.mid");check(file.existsAsFile(),"A project created from nothing exports MIDI normally");
      juce::MemoryBlock bytes;file.loadFileAsData(bytes);phthongos::midi::Source source;source.name="composed.mid";auto* data=static_cast<const std::uint8_t*>(bytes.getData());source.bytes.assign(data,data+bytes.getSize());auto parsed=source.parse({});bool tempo=false;for(const auto& track:parsed.tracks)for(const auto& e:track)if(e.isMeta(0x51)&&e.data.size()==3)tempo|=std::abs(double((unsigned(e.data[0])<<16)|(unsigned(e.data[1])<<8)|e.data[2])-60000000./65)<2;check(parsed.notes.size()==2&&tempo,"Export contains both notes and the declared 65 BPM");
      newProjectRequested=[this,r]{r->replacement=std::make_unique<App>(savePreferencesFile.getParentDirectory().getChildFile("replacement/audio"));r->replacement->stopTimer();r->replacement->startEmptyProject([r](bool ok){r->stage=ok?15:99;});if(!r->replacement->initialisationPopup||!r->replacement->initialisationPopup->isVisible())r->stage=99;};requestClose(newProjectRequested);r->stage=14;
    }else if(r->stage==15){
      auto& app=*r->replacement;check(app.initialisationPopup&&!app.initialisationPopup->isVisible(),"New project also dismisses its popup when ready");check(app.notesReady&&app.parts.size()==1&&app.roll.notes.empty()&&!app.dirty,"New project returns to a fresh playable piano session in the existing application");
      juce::FileOutputStream image(r->folder.getChildFile("empty-editor.png"));juce::PNGImageFormat().writeImageToStream(app.createComponentSnapshot(app.getLocalBounds()),image);r->stage=16;selectPart(-1);
    }else if(r->stage==16&&!locked&&notesReady){
      roll.selected.clear();for(const auto& n:roll.notes)roll.selected.insert(n.id);copyNotes();r->oldDuration=roll.duration();r->oldSpan=roll.span();
      playback=playback.clone();playback.getDynamicObject()->setProperty("position",r->oldDuration-.1);r->stage=17;pasteNotes();
    }else if(r->stage==17&&!locked&&notesReady&&roll.notes.size()==4){
      bool exact=true;for(const auto& original:items(r->notes)){auto n=Note::parse(original);bool found=false;for(const auto& pasted:roll.notes)if(roll.selected.count(pasted.id)&&pasted.pitch==n.pitch&&pasted.velocity==n.velocity&&std::abs((pasted.end-pasted.start)-(n.end-n.start))<3e-6)found=true;exact&=found;}
      check(exact&&roll.duration()>r->oldDuration&&std::abs(roll.span()-r->oldSpan)<1e-6,"Clipboard paste beyond the old end extends the score and preserves note durations, pitches, velocities and viewport scale");
      r->extended=result["editing"]["notes"].clone();r->stage=18;command(object({{"kind","undo"}}));
    }else if(r->stage==18&&!locked&&notesReady){check(juce::JSON::toString(r->notes)==juce::JSON::toString(result["editing"]["notes"]),"One Undo removes the extended paste and restores all original notes");r->stage=19;command(object({{"kind","redo"}}));
    }else if(r->stage==19&&!locked&&notesReady){check(juce::JSON::toString(r->extended)==juce::JSON::toString(result["editing"]["notes"]),"Redo restores the extended paste exactly");r->stage=-1;saveProjectTo(r->folder.getChildFile("extended-paste.pht").getFullPathName(),[r,finish](bool ok){if(ok)r->stage=20;else finish(false);});
    }else if(r->stage==20&&!locked){r->stage=21;openProject(projectPath);
    }else if(r->stage==21&&!locked&&notesReady){check(juce::JSON::toString(r->extended)==juce::JSON::toString(result["editing"]["notes"])&&roll.duration()>r->oldDuration,"Saving and reopening preserves the complete extended timeline and pasted notes");r->stage=22;initialisingProject=true;setBusy(true,tr(String::fromUTF8("Initialisation…")));r->firstDots=initialisationPopup->createComponentSnapshot(initialisationPopup->getLocalBounds());r->dotsAt=juce::Time::getMillisecondCounterHiRes();setSize(1280,850);check(initialisationPopup->getBounds().getCentre()==getLocalBounds().getCentre()&&initialisationPopup->getWidth()==350,"Resizing the application keeps the popup centred and square");
    }else if(r->stage==22&&juce::Time::getMillisecondCounterHiRes()-r->dotsAt>450){
      auto next=initialisationPopup->createComponentSnapshot(initialisationPopup->getLocalBounds());bool changed=false;for(int y=280;y<336;++y)for(int x=21;x<329;++x)changed|=r->firstDots.getPixelAt(x,y)!=next.getPixelAt(x,y);check(changed,"The initialization dots animate while the operation remains active");r->stage=23;invoke("player_prepare",object({{"audioId",-1}}),[finish](var){finish(false);},false,[this,r,check,finish](String){try{check(!locked&&!initialisingProject&&initialisationPopup&&!initialisationPopup->isVisible(),"An initialization failure removes the popup and unlocks the interface");finish(true);}catch(...){finish(false);}});return;
    }
    juce::Timer::callAfterDelay(30,[poll]{if(*poll)(*poll)();});
  }catch(const std::exception& e){r->checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));finish(false);}};(*poll)();
}
