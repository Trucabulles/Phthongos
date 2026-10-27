#include "App.h"
#include <juce_audio_formats/juce_audio_formats.h>

bool App::isInterestedInFileDrag(const juce::StringArray& paths) {
  return !locked&&!closing&&!confirmation&&!midiRecording&&!midiRecordPending&&!midiCounting
      &&juce::Component::getNumCurrentlyModalComponents()==0&&fileTransfer::kind(paths)!=fileTransfer::Kind::none;
}
void App::fileDragEnter(const juce::StringArray& paths,int,int) {fileDropHover=isInterestedInFileDrag(paths);repaint();}
void App::fileDragExit(const juce::StringArray&) {fileDropHover=false;repaint();}
void App::paintOverChildren(juce::Graphics& g) {
  if(fileDropHover){g.setColour(theme.bronze);g.drawRect(getLocalBounds().reduced(2),3);}
}
void App::filesDropped(const juce::StringArray& paths,int,int) {
  fileDropHover=false;repaint();if(!isInterestedInFileDrag(paths))return;
  auto path=juce::File(paths[0]).getFullPathName();
  if(fileTransfer::kind(paths)==fileTransfer::Kind::midi) {
    if(!items(result["editing"]["notes"]).empty())appendMidi(path);
    else importMidi(path);
  } else if(!items(result["editing"]["notes"]).empty())associateAudio(path);
  else permitReplace([s=safe(),path]{if(s)s->openAudioPath(path);});
}
void App::prepareMidiDrag(MidiDragButton::Ready ready) {
  if(locked||closing||result.isVoid()||midiRecording||midiRecordPending||midiCounting){ready({});return;}
  auto folder=fileTransfer::stagingFolder();
  if(folder==juce::File()){status.setText(tr(String::fromUTF8("Export MIDI impossible.")),juce::dontSendNotification);ready({});return;}
  auto request=args().clone();request.getDynamicObject()->setProperty("path",folder.getFullPathName());
  request.getDynamicObject()->setProperty("transient",true);
  request.getDynamicObject()->setProperty("name",midiExportName());
  status.setText(tr(String::fromUTF8("Export MIDI…")),juce::dontSendNotification);
  invoke("export_midi_voices",request,[s=safe(),ready](var receipt){
    if(!s)return;
    s->status.setText(localisation::count("notes_exported",(int)receipt["notes"]),juce::dontSendNotification);
    juce::StringArray files;for(auto entry:items(receipt["files"]))files.add(entry["path"].toString());
    ready(files);
  },true,[ready](String){ready({});});
}

void App::runFileTransferTests(std::function<void(bool)> done) {
  stopTimer();setSize(1160,850);
  struct Run {int stage=0;bool finished=false;double started=juce::Time::getMillisecondCounterHiRes();juce::File folder,wav;var notes,locks;std::vector<var> checks;};
  auto run=std::make_shared<Run>();auto poll=std::make_shared<std::function<void()>>();
  run->folder=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/file-transfer-"+juce::Uuid().toString());run->folder.createDirectory();run->wav=run->folder.getChildFile("reference.wav");
  auto check=[run](bool ok,const String& label){run->checks.push_back(object({{"name",label},{"passed",ok}}));if(!ok)throw std::runtime_error(label.toStdString());};
  auto finish=[this,run,poll,done](bool ok){if(run->finished)return;run->finished=true;dirty=false;
    run->folder.getParentDirectory().getChildFile("file-transfer-latest.json").replaceWithText(juce::JSON::toString(object({{"passed",ok},{"stage",run->stage},{"directory",run->folder.getFullPathName()},{"checks",array(run->checks)}})));
    juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
  try {
    juce::AudioBuffer<float> signal(1,44100);signal.clear();juce::WavAudioFormat format;
    {std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(new juce::FileOutputStream(run->wav),44100,1,16,{},0));check(writer&&writer->writeFromAudioSampleBuffer(signal,0,signal.getNumSamples()),"Create isolated reference WAV");}
    auto created=bridge.call("create_empty_midi_project",object({}));check(bool(created["ok"]),"Create MIDI-only session");audio=created["value"]["audio"];updateResult(created["value"]["result"],false,false);
    auto action=object({{"kind","add"},{"pitch",60},{"start",.1},{"end",.7},{"velocity",85},{"part",result["midi_parts"][0]}});
    auto request=args().clone();request.getDynamicObject()->setProperty("action",action);auto added=bridge.call("edit_notes",request);check(bool(added["ok"]),"Add an edited note before associating audio");updateResult(added["value"],false,false);
    run->notes=result["editing"]["notes"].clone();run->locks=result["editing"]["locked_parts"].clone();projectName=String::fromUTF8("Étude clavier");dirty=true;
    prepare([run](var){run->stage=1;});
  } catch(const std::exception& e){run->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())}}));finish(false);return;}
  *poll=[this,run,check,finish]{try {
    if(run->finished)return;
    if(juce::Time::getMillisecondCounterHiRes()-run->started>45000)throw std::runtime_error("File transfer workflow timed out: "+status.getText().toStdString());
    if(run->stage==1&&!locked&&notesReady){
      check(isInterestedInFileDrag({run->wav.getFullPathName()}),"An edited MIDI session accepts a dropped WAV");run->stage=2;filesDropped({run->wav.getFullPathName()},10,10);
      check(!confirmation&&Form::activeForms.empty(),"Dropping reference audio does not open a replace-session or save confirmation");
    } else if(run->stage==2&&!locked&&notesReady){
      check(!(bool)audio["midi_only"]&&referenceAudio&&juce::JSON::toString(run->notes)==juce::JSON::toString(result["editing"]["notes"])&&juce::JSON::toString(run->locks)==juce::JSON::toString(result["editing"]["locked_parts"]),"Audio association preserves edited MIDI notes, voices and locks");
      selectPart(256);resized();auto* lock=buttonMap["partLock"];
      check(lock->getWidth()==29&&lock->getRight()<=roll.getRight()&&lock->getName().isNotEmpty(),"Compact padlock has a localized accessible name and stays within the narrow roll header");
      {juce::FileOutputStream image(run->folder.getChildFile("narrow-header.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);}
      run->stage=3;prepareMidiDrag([this,run,check,finish](juce::StringArray files){try{check(files.size()==1&&juce::File(files[0]).existsAsFile()&&juce::File(files[0]).getFileName()==String::fromUTF8("Étude clavier.mid"),"Single-voice drag uses the exact project name and keeps it after reference-audio association");check(juce::JSON::toString(run->notes)==juce::JSON::toString(result["editing"]["notes"]),"Preparing outgoing files preserves the current document");run->stage=4;}catch(const std::exception& e){run->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())}}));finish(false);}});
    } else if(run->stage==4){
      auto replacement=run->folder.getChildFile("replacement.wav");check(run->wav.copyFileTo(replacement),"Prepare replacement reference audio");run->stage=5;filesDropped({replacement.getFullPathName()},10,10);
    } else if(run->stage==5&&!locked&&notesReady){
      check(juce::JSON::toString(run->notes)==juce::JSON::toString(result["editing"]["notes"])&&!confirmation,"Replacing an associated WAV also preserves MIDI without replacing the session");finish(true);
    }
  }catch(const std::exception& e){run->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())}}));finish(false);}};
  struct Poll : juce::Timer {std::shared_ptr<std::function<void()>> callback;Poll(std::shared_ptr<std::function<void()>> p):callback(std::move(p)){startTimer(40);}void timerCallback()override{if(!*callback){delete this;return;}(*callback)();}};
  new Poll(poll);
}
