#include "App.h"
#include "../core/analysis/Dynamics.h"
#include <juce_cryptography/juce_cryptography.h>
#include "../core/audio/Engine.h"
#include "../core/midi/Tempo.h"
#include "../core/midi/TempoEstimate.h"
#include "../migration/data/TempoReference.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "../core/project/Pht.h"
#include "../core/project/Validation.h"
#include "../core/services/Resources.h"
bool App::runSelfTests() {
  auto directory =
      juce::File::getSpecialLocation(juce::File::currentExecutableFile)
          .getSiblingFile("test-results")
          .getChildFile(juce::Uuid().toString());
  directory.createDirectory();
  std::vector<var> checks;
  auto check = [&](bool ok, const String &name) {
    checks.push_back(object({{"name", name}, {"passed", ok}}));
    if (!ok)
      throw std::runtime_error(name.toStdString());
  };
  auto call = [&](const String &name, const var &a) {
    auto reply = bridge.call(name, a);
    check((bool)reply["ok"],
          name + juce::String::fromUTF8(" · ") + reply["error"].toString());
    return reply["value"];
  };
  std::function<bool(const var &, const var &)> equalJson;
  equalJson = [&](const var &a, const var &b) {
    if (auto *o = a.getDynamicObject()) {
      auto *p = b.getDynamicObject();
      if (!p || o->getProperties().size() != p->getProperties().size())
        return false;
      for (auto &entry : o->getProperties())
        if (!p->hasProperty(entry.name) ||
            !equalJson(entry.value, p->getProperty(entry.name)))
          return false;
      return true;
    }
    if (a.isArray()) {
      auto aa = items(a), bb = items(b);
      if (aa.size() != bb.size())
        return false;
      for (size_t i = 0; i < aa.size(); ++i)
        if (!equalJson(aa[i], bb[i]))
          return false;
      return true;
    }
    if ((a.isInt() || a.isInt64() || a.isDouble()) &&
        (b.isInt() || b.isInt64() || b.isDouble()))
      return (double)a == (double)b;
    return a == b;
  };
  bool passed = false;
  try {
  {
    namespace core=phthongos;core::SourceSnapshot source;core::Document doc;core::Action add;add.kind=core::Action::Kind::add;add.pitch=60;add.start=.1;add.end=.8;add.velocity=42;doc.act(source,doc.revision(),add);
    auto n=doc.view(source).notes.front();core::analysis::DynamicsCache cache;auto fp=core::analysis::fingerprint(n);cache.fingerprints.push_back(fp);cache.estimates.push_back({fp,.1,.1,1.,.5,true});
    core::Mapping mapping;mapping.level=90;mapping.contrast=0;mapping.min=1;mapping.max=127;
    doc.applyDynamics(source,doc.revision(),{n.id},mapping,cache);check(doc.view(source).notes.front().velocity==90&&doc.view(source).notes.front().protectedNote,"Explicit audio dynamics can replace a manually created velocity without unprotecting geometry");
    core::project::Editor stored;stored.document=doc;stored.dynamicsCache=core::project::writeDynamicsCache(cache);auto encoded=core::project::writeEditor(stored);auto reopened=core::project::readEditor(encoded,source).document;check(reopened.view(source).notes.front().velocity==90,"Manual-note audio dynamics survives document serialisation");
    auto legacy=doc.state();legacy.layer.dynamics[n.id].overrideManual=false;stored.document=core::Document::restore(legacy,source);auto old=core::project::readEditor(core::project::writeEditor(stored),source).document;check(old.view(source).notes.front().velocity==42,"Legacy protected velocities remain unchanged until explicit application");
    core::Action undo;undo.kind=core::Action::Kind::undo;doc.act(source,doc.revision(),undo);check(doc.view(source).notes.front().velocity==42,"One undo restores the entered velocity");undo.kind=core::Action::Kind::redo;doc.act(source,doc.revision(),undo);check(doc.view(source).notes.front().velocity==90,"Redo restores the explicitly applied audio velocity");
    core::Action modify=add;modify.kind=core::Action::Kind::modify;modify.id=n.id;modify.velocity=35;doc.act(source,doc.revision(),modify);check(doc.view(source).notes.front().velocity==35,"A later manual velocity edit supersedes audio dynamics");
    doc.applyDynamics(source,doc.revision(),{n.id},mapping,cache);check(doc.view(source).notes.front().velocity==90,"Audio dynamics can explicitly replace that later manual velocity again");
    core::Action reset;reset.kind=core::Action::Kind::resetDynamics;doc.act(source,doc.revision(),reset);check(doc.view(source).notes.front().velocity==35,"Removing audio dynamics restores the last entered baseline velocity");
    auto lockedState=doc.state();lockedState.layer.lockedParts.insert(doc.view(source).notes.front().key());auto lockedDoc=core::Document::restore(lockedState,source);auto revision=lockedDoc.revision();bool refused=false;try{lockedDoc.applyDynamics(source,revision,{n.id},mapping,cache);}catch(...){refused=true;}check(refused&&lockedDoc.revision()==revision&&lockedDoc.view(source).notes.front().velocity==35,"A locked voice rejects audio replacement without adding undo history");
  }
    {
      auto midi=directory.getChildFile("drag-source.MID"),wav=directory.getChildFile("drop-source.WAV"),other=directory.getChildFile("drop-source.txt");
      midi.replaceWithText("fixture");wav.replaceWithText("fixture");other.replaceWithText("fixture");
      check(fileTransfer::kind({midi.getFullPathName()})==fileTransfer::Kind::midi&&fileTransfer::kind({wav.getFullPathName()})==fileTransfer::Kind::audio,"Windows file drop recognises MIDI and existing audio formats case-insensitively");
      check(fileTransfer::kind({other.getFullPathName()})==fileTransfer::Kind::none&&fileTransfer::kind({midi.getFullPathName(),wav.getFullPathName()})==fileTransfer::Kind::none&&fileTransfer::kind({directory.getFullPathName()})==fileTransfer::Kind::none,"Unsupported files, directories and multiple-file drops are rejected without changing the session");
      check(dynamic_cast<MidiDragButton*>(buttonMap["export"])!=nullptr,"The existing export button supports native file dragging");
      MidiDragButton fixture("MIDI");fixture.setSize(130,38);int clicks=0,preparations=0,drags=0;std::function<void()> dragDone;
      fixture.onClick=[&]{++clicks;};fixture.prepare=[&](MidiDragButton::Ready ready){++preparations;ready({midi.getFullPathName()});};
      fixture.externalDrag=[&](const juce::StringArray& files,juce::Component* source,std::function<void()> done){check(files==juce::StringArray{midi.getFullPathName()}&&source==&fixture,"Native drag offers the prepared MIDI from the export button");++drags;dragDone=done;return true;};
      auto now=juce::Time::getCurrentTime();juce::Point<float> origin(10,15);
      auto event=[&](juce::Point<float> at,bool dragged){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&fixture,&fixture,now,origin,now,1,dragged);};
      // Offscreen components have no native hover state; model the pressed visual
      // state explicitly at release, including after an external drag comes back.
      auto release=[&](juce::Point<float> at,bool dragged){fixture.setState(juce::Button::buttonDown);fixture.mouseUp(event(at,dragged));};
      fixture.mouseDown(event(origin,false));release(origin,false);
      check(clicks==1&&preparations==0,"A normal export click keeps the existing dialog action without preparing a drag file");
      fixture.mouseDown(event(origin,false));fixture.mouseDrag(event({12,16},true));release({12,16},true);
      check(clicks==2&&preparations==0,"Small pointer movement remains a click rather than an accidental export drag");
      fixture.mouseDown(event(origin,false));fixture.mouseDrag(event({30,15},true));fixture.mouseDrag(event({35,15},true));release({35,15},true);if(dragDone)dragDone();
      check(drags==1&&preparations==1&&clicks==2,"Dragging exports once and release never opens the ordinary export dialog");
      MidiDragButton::Ready pending;fixture.prepare=[&](MidiDragButton::Ready ready){++preparations;pending=ready;};
      fixture.mouseDown(event(origin,false));fixture.mouseDrag(event({30,15},true));release({30,15},true);pending({midi.getFullPathName()});
      fixture.mouseDown(event(origin,false));fixture.mouseDrag(event({12,16},true));release({12,16},true);
      check(drags==1&&clicks==3,"Releasing before MIDI preparation completes cancels the drag and stale callbacks cannot start a later gesture");
      fixture.mouseDown(event(origin,false));fixture.mouseDrag(event({30,15},true));
      check(fixture.cancelPendingDrag(),"Escape can cancel MIDI preparation before the native drag starts");pending({midi.getFullPathName()});fixture.mouseDrag(event({35,15},true));release({35,15},true);
      check(drags==1&&clicks==3,"Cancelled preparation never starts a native drag or opens the export dialog");
      auto staged=fileTransfer::stagingFile("Session : piano");check(staged.hasFileExtension("mid")&&staged.getFileName().contains("piano"),"Drag export uses a legal, readable MIDI filename in a unique temporary directory");
      staged.getParentDirectory().deleteFile();
      auto batch=fileTransfer::stagingFolder();check(batch!=juce::File()&&!batch.exists()&&batch.getParentDirectory().isDirectory(),"Batch drag reserves an unpublished unique directory for atomic export");
      check(fileTransfer::nativeDragTests(directory),"Windows Shell copies both Unicode MIDI files with the native data object and thumbnail; drag continuation handles release and Escape");
      for(bool multiple:{false,true}){juce::FileOutputStream out(directory.getChildFile(multiple?"drag-batch.png":"drag-single.png"));juce::PNGImageFormat().writeImageToStream(fileTransfer::dragImage(multiple?juce::StringArray{midi.getFullPathName(),wav.getFullPathName()}:juce::StringArray{midi.getFullPathName()}),out);}
    }
    {
      ListeningVolumeSlider volume;volume.setRange(-60,12,0);
      check(ListeningVolumeSlider::percent(-60)==0&&ListeningVolumeSlider::percent(0)==100,"Listening percentage endpoints are silence and nominal gain");
      check(std::abs(ListeningVolumeSlider::decibels(50)+12.0411998266)<1e-8,"Half listening volume uses a perceptual taper");
      for(double db:{-59.2,-30.,-12.,-9.345,0.,6.,12.}){
        volume.setValue(db,juce::dontSendNotification);
        check(volume.getValue()==db&&std::abs(volume.proportionOfLengthToValue(volume.valueToProportionOfLength(db))-db)<1e-8,"Percentage controls preserve exact legacy dB values and boosted MIDI levels");
      }
      check(volume.getTextFromValue(12)=="100 %"&&volume.getTextFromValue(0)=="50 %"&&volume.getValueFromText("100 %")==12,"Global MIDI percentage reaches 100 at the right end without changing its audio range");
      volume.setRange(-60,0,0);
      check(volume.getValueFromText("50 %")==ListeningVolumeSlider::decibels(50)&&volume.getTextFromValue(0)=="100 %"&&volume.getTextFromValue(-18)=="35 %","Listening text entry and display use percentages");
      auto oldClick=clickEnabled;clickEnabled=false;updateClickVisuals();check(!beatIndicator.isVisible(),"Beat dots are hidden when the metronome is off");clickEnabled=true;updateClickVisuals();check(beatIndicator.isVisible(),"Enabling the metronome reveals beat dots");clickEnabled=oldClick;updateClickVisuals();
      ClickOptionsPanel panel(-12,2);panel.counts[0].onClick();check(panel.count==0&&panel.volume.getDoubleClickReturnValue()==-12,"Count-in buttons choose their own value and click volume retains its original default");
    }

    {
      {
        using pointerSafety::Recovery;pointerSafety::State pointer;
        pointer.foregroundOwned=true;pointer.overOwned=true;pointer.visible=false;
        check(pointerSafety::recoveryFor(pointer,false)==Recovery::refresh,"A hidden pointer is recovered even without an unbounded drag, including maximised windows");
        pointer.visible=true;check(pointerSafety::recoveryFor(pointer,false)==Recovery::none&&pointerSafety::recoveryFor(pointer,true)==Recovery::refresh,"Normal polling leaves a visible pointer alone; activation or fullscreen transition explicitly refreshes it");
        pointer.unbounded=true;check(pointerSafety::recoveryFor(pointer,false)==Recovery::endUnbounded,"A stale unbounded drag is released after the mouse button is up");
        pointer.buttonsDown=true;check(pointerSafety::recoveryFor(pointer,true)==Recovery::none,"Fullscreen activation never interrupts a drag whose button is still held");pointer.buttonsDown=false;
        pointer.foregroundOwned=false;check(pointerSafety::recoveryFor(pointer,true)==Recovery::none,"Alt-Tab away never alters another application's pointer");pointer.foregroundOwned=true;
        pointer.overOwned=false;check(pointerSafety::recoveryFor(pointer,true)==Recovery::none,"A pointer over another application's window is never repositioned or refreshed");pointer.overOwned=true;
        pointer.suppressed=true;check(pointerSafety::recoveryFor(pointer,true)==Recovery::none,"Intentional touch or pen cursor suppression is respected");pointer.suppressed=false;
        VisibleCursorSlider slider;slider.setSize(240,30);slider.setRange(0,100,1);slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);slider.setSliderStyle(juce::Slider::LinearHorizontal);
        check(!slider.getVelocityBasedMode()&&!slider.getVelocityModeIsSwappable(),"Sliders keep their pointer visible with Ctrl and Alt instead of enabling hidden velocity mode");
        double normal=-1;for(int modifier:{0,int(juce::ModifierKeys::ctrlModifier),int(juce::ModifierKeys::altModifier),int(juce::ModifierKeys::ctrlModifier|juce::ModifierKeys::altModifier)}){
          slider.setValue(20,juce::dontSendNotification);auto source=juce::Desktop::getInstance().getMainMouseSource();auto now=juce::Time::getCurrentTime();juce::Point<float> start(40,15),end(170,15);
          auto event=[&](juce::Point<float> at,bool dragged){return juce::MouseEvent(source,at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|modifier),1,0,0,0,0,&slider,&slider,now,start,now,1,dragged);};
          slider.mouseDown(event(start,false));slider.mouseDrag(event(end,true));slider.mouseUp(event(end,true));if(normal<0)normal=slider.getValue();
          check(slider.getValue()>20&&slider.getValue()==normal&&!source.isUnboundedMouseMovementEnabled(),"Normal, Ctrl, Alt and Ctrl+Alt slider drags retain the same value adjustment and visible-pointer mode");
        }
        auto value=slider.getValue();restorePointerAfterInterruption(true);check(slider.getValue()==value,"Pointer recovery after a window switch never changes slider values");
      }
      {
        using namespace phthongos;SourceSnapshot source;Document doc;
        Action add;add.kind=Action::Kind::addMarker;add.name="history";
        for(int i=0;i<100;++i){add.at=i;doc.act(source,doc.revision(),add);}
        auto before=project::writeEditor(project::Editor{doc,var()});
        check(doc.undoLayers().size()==25&&doc.view(source).markers.size()==100,"One hundred edits retain only 25 undo states without losing current markers");
        Action undo;undo.kind=Action::Kind::undo;for(int i=0;i<25;++i)doc.act(source,doc.revision(),undo);
        check(!doc.view(source).canUndo&&doc.view(source).markers.size()==75&&doc.redoLayers().size()==25,"The retained undo states are precisely the 25 most recent edits");
        Action redo;redo.kind=Action::Kind::redo;for(int i=0;i<25;++i)doc.act(source,doc.revision(),redo);
        auto after=project::writeEditor(project::Editor{doc,var()});
        check(juce::JSON::toString(before["layer"])==juce::JSON::toString(after["layer"]),"Redo restores the exact current layer after all 25 retained undos");
        auto legacy=before.clone();auto* history=legacy["undo"].getArray();while(history->size()<100)history->insert(0,(*history)[0].clone());
        auto restored=project::readEditor(legacy,source);auto saved=project::writeEditor(restored);
        check(saved["undo"].size()==25&&juce::JSON::toString(saved["layer"])==juce::JSON::toString(before["layer"]),"Old projects with 100 states open and save with 25 states while preserving the current layer");
      }
      {
        using namespace phthongos;SourceSnapshot source;Document doc;
        Action add;add.kind=Action::Kind::add;add.pitch=60;add.start=0;add.end=.5;
        doc.actBatch(source,0,{add,add});
        auto initial=doc.view(source).notes;auto state=doc.state();
        check(initial.size()==2&&doc.revision()==1&&doc.undoLayers().size()==1,"Batch additions commit one revision and one undo state");
        Action undo;undo.kind=Action::Kind::undo;doc.act(source,doc.revision(),undo);
        check(doc.view(source).notes.empty()&&doc.redoLayers().size()==1,"One undo removes every note added by a batch");
        Action redo;redo.kind=Action::Kind::redo;doc.act(source,doc.revision(),redo);
        check(doc.view(source).notes.size()==2&&doc.view(source).notes[0].id==initial[0].id,"One redo restores the complete batch and its identifiers");
        auto before=project::writeEditor(project::Editor{doc,var()});auto revision=doc.revision(),manual=doc.nextManual();
        Action invalid=add;invalid.end=-1;bool rejected=false;
        try{doc.actBatch(source,revision,{add,invalid});}catch(...){rejected=true;}
        check(rejected&&doc.revision()==revision&&doc.nextManual()==manual&&equalJson(before,project::writeEditor(project::Editor{doc,var()})),"Invalid second batch action rolls back notes, manual IDs, undo and redo history");
        rejected=false;try{doc.actBatch(source,revision-1,{add});}catch(...){rejected=true;}
        check(rejected&&doc.revision()==revision,"Batch rejects an obsolete revision");
        rejected=false;try{doc.actBatch(source,revision,{undo});}catch(...){rejected=true;}
        check(rejected&&doc.revision()==revision,"Batch cannot contain undo or other non-correction actions");
        source.notes=doc.view(source).notes;
        Action lock;lock.kind=Action::Kind::togglePartLock;lock.partKey=0;doc.act(source,doc.revision(),lock);
        auto lockedRevision=doc.revision();rejected=false;
        Action remove;remove.kind=Action::Kind::remove;remove.ids={initial[0].id};
        try{doc.actBatch(source,lockedRevision,{remove});}catch(...){rejected=true;}
        check(rejected&&doc.revision()==lockedRevision&&doc.view(source).notes.size()==2,"Locked voice rejects the complete batch without changing a note");
      }
      {
        using namespace phthongos;SourceSnapshot source;Document doc;Action add;add.kind=Action::Kind::add;add.start=.2;add.end=.4;
        std::vector<Action> actions(5000,add);doc.actBatch(source,0,actions,std::string("Spectral test"));auto view=doc.view(source);
        check(view.notes.size()==5000&&view.parts.size()==1&&view.parts.front().label=="Spectral test"&&view.notes.back().key()==view.parts.front().key(),"Batch above 4096 notes creates the complete destination without truncation");
        Action undo;undo.kind=Action::Kind::undo;doc.act(source,doc.revision(),undo);
        check(doc.view(source).notes.empty()&&doc.view(source).parts.empty(),"Undo removes the spectral voice and all 5000 notes together");
        auto before=project::writeEditor(project::Editor{doc,var()});add.end=-1;bool rejected=false;
        try{doc.actBatch(source,doc.revision(),{add},std::string("Never published"));}catch(...){rejected=true;}
        check(rejected&&equalJson(before,project::writeEditor(project::Editor{doc,var()})),"Invalid proposals roll back voice creation and the whole batch atomically");
        add.end=.4;doc.act(source,doc.revision(),add);check(doc.view(source).parts.size()==1,"A manually drawn note exposes its implicit destination voice");
      }
      {
        using namespace phthongos;project::Container large;large.metadata=object({{"version",8},{"pcm_sha256",juce::SHA256(nullptr,0).toHexString()},{"editor",object({})},{"ui",object({})},{"large_metadata",String(std::string(65000000,'x'))}});
        auto encoded=project::encode(large);auto decoded=project::decode(encoded);
        check(decoded.metadata["large_metadata"].toString().length()==65000000,"PHT metadata above the former 64 MB limit saves and reopens without truncation");
      }
      using phthongos::rhythm::Grid;
      Grid grid;grid.tempos={{0,0,60./65}};grid.subdivision=2;
      auto at=[&](const std::vector<phthongos::rhythm::Line>& lines,double time,int level){return std::any_of(lines.begin(),lines.end(),[&](auto& line){return std::abs(line.seconds-time)<1e-7&&line.level==level;});};
      auto lines=grid.lines(0,4,1600);
      check(at(lines,0,0)&&at(lines,60./65,1)&&at(lines,30./65,2)&&at(lines,15./65,3)&&at(lines,240./65,0),"65 BPM grid distinguishes bars, quarters, eighths and sixteenths");
      grid.tempos={{0,0,.5}};grid.subdivision=1;lines=grid.lines(0,2,1600);
      check(at(lines,.25,2)&&!at(lines,.125,3),"120 BPM eighth grid has 250 ms spacing and no sixteenths");
      grid.subdivision=3;lines=grid.lines(0,2,1600);
      check(at(lines,1./6,2)&&!at(lines,.25,2),"Eighth-triplet grid divides each quarter into three, without binary half-beats");
      grid.subdivision=4;lines=grid.lines(0,2,1600);
      check(at(lines,1./12,3)&&at(lines,1./6,2),"Sixteenth-triplet grid retains its ternary hierarchy");
      grid.origin=1.327;grid.subdivision=1;lines=grid.lines(1,2,1600);
      check(at(lines,1.327,0)&&at(lines,1.577,2),"Grid origin offsets only the coordinate transform");
      grid.origin=0;grid.tempos={{0,0,.5},{4,2,1.}};grid.meters={{0,4,4},{8,3,4}};
      check(std::abs(grid.beatAt(3)-5)<1e-9&&std::abs(grid.secondsAt(5)-3)<1e-9,"Musical coordinates follow source tempo changes in both directions");
      lines=grid.lines(0,10,2200);
      check(at(lines,2,0)&&at(lines,3,1)&&at(lines,6,0)&&at(lines,9,0),"Grid follows actual 4/4 to 3/4 meter changes and continuous tempo");
      auto wide=grid.lines(0,10,2000),panned=grid.lines(3.1,3,600);bool stable=true;
      for(auto& line:panned)if(!std::any_of(wide.begin(),wide.end(),[&](auto& other){return std::abs(line.seconds-other.seconds)<1e-8&&line.level==other.level&&line.bar==other.bar;}))stable=false;
      check(stable,"Panning preserves line times, hierarchy and bar numbering");
      grid.tempos={{0,0,.1}};grid.meters={{0,4,4}};grid.subdivision=4;lines=grid.lines(0,1200,1000);
      check(lines.size()<50&&std::all_of(lines.begin(),lines.end(),[](auto& line){return line.level==0;}),"Wide zoom decimates bars instead of producing a forest of lines");
      grid.mode=1;lines=grid.lines(0,8,1000);
      check(at(lines,1,0)&&at(lines,.5,2)&&at(lines,.25,2),"Seconds grid uses second boundaries with 500/250 ms subdivisions");
      grid.mode=2;check(grid.lines(0,8,1000).empty(),"None hides vertical grid lines");
      {
        auto fixture=phthongos::midi::Source::blank("tempo-and-meter.mid");fixture.bytes=phthongos::midi::encode(0,480,{{{0,255,81,{7,161,32}},{0,255,88,{4,2,24,8}},{0,144,0,{60,90}},{1920,255,81,{15,66,64}},{3840,255,88,{3,2,24,8}},{4800,128,0,{60,0}},{4800,255,47,{}}}});
        auto file=directory.getChildFile("tempo-and-meter.mid");check(file.replaceWithData(fixture.bytes.data(),fixture.bytes.size()),"Grid MIDI fixture written");
        Bridge mapBridge(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("runtime"),directory.getChildFile("grid-map-cache"));
        auto invoke=[&](const String& command,var parameters){auto reply=mapBridge.call(command,parameters);check((bool)reply["ok"],command+" grid-map service");return reply["value"];};
        auto opened=invoke("import_midi_audio",object({{"midiPath",file.getFullPathName()},{"audioPath",var()}}));auto id=opened["audio"]["id"];
        auto info=invoke("project_tempo",object({{"audioId",id}}));
        check((bool)info["variable"]&&info["tempo_map"].size()==2&&number(info["tempo_map"][1],"beat")==4&&number(info["tempo_map"][1],"seconds")==2&&number(info["tempo_map"][1],"quarter_seconds")==1,"Service exposes the real imported tempo coordinate map");
        check(info["meter_map"].size()==2&&number(info["meter_map"][1],"beat")==8&&(int)info["meter_map"][1]["numerator"]==3,"Service exposes imported 4/4 and 3/4 boundaries");
        info=invoke("set_project_tempo",object({{"audioId",id},{"bpm",65}}));
        check(info["tempo_map"].size()==1&&std::abs(number(info["tempo_map"][0],"quarter_seconds")-60./65)<1e-9&&std::abs(number(info["meter_map"][1],"beat")-6.5)<1e-9,"Fixed BPM recalculates the visual map without relocating signature events in seconds");
        info=invoke("set_project_tempo",object({{"audioId",id},{"bpm",0}}));
        check(info["tempo_map"].size()==2&&number(info["meter_map"][1],"beat")==8,"Source mode restores its tempo and meter map");
      }

      Roll fixture;fixture.setSize(1100,500);fixture.scoreDuration=4;fixture.displayMode=0;
      Note note;note.id=1;note.start=.347;note.end=1.123;note.pitch=60;note.velocity=88;fixture.notes={note};auto before=fixture.rect(note);
      for(int mode=0;mode<3;++mode){fixture.rhythmicGrid.mode=mode;fixture.rhythmicGrid.tempos={{0,0,60./65}};auto image=fixture.createComponentSnapshot(fixture.getLocalBounds());
        check(fixture.rect(fixture.notes[0])==before&&fixture.notes[0].start==note.start&&fixture.notes[0].end==note.end,"Grid rendering preserves real note times and exact note rectangles");
        juce::FileOutputStream output(directory.getChildFile("grid-mode-"+String(mode)+".png"));juce::PNGImageFormat().writeImageToStream(image,output);
      }
    }
    {
      using namespace phthongos::midi;
      for(const auto& fixture:tempoFixtures()){
        phthongos::midi::bpm::Config config;config.center=fixture.center;config.range=fixture.range;
        auto actual=phthongos::midi::bpm::detect(fixture.notes,config);float peak=0,error=0;
        for(auto weight:fixture.histogram)peak=std::max(peak,weight);
        bool compatible=std::abs(actual.bpm-fixture.bpm)<.001;
        if(!fixture.histogram.empty()){
          compatible&=actual.bins.size()==fixture.histogram.size();
          if(actual.bins.size()==fixture.histogram.size())for(std::size_t i=0;i<actual.bins.size();++i)error=std::max(error,std::abs(actual.bins[i]-fixture.histogram[i])/std::max(1.f,peak));
          compatible&=error<.0001f;
        }else for(auto weight:actual.bins)compatible&=weight==0;
        check(compatible,String("C++ detector reproduces upstream BPM and every histogram bin: ")+fixture.name);
      }
      auto containsTempo=[](const TempoEstimate& estimate,double bpm){for(auto candidate:estimate.candidates){double ratio=candidate.bpm/bpm;if(std::min({std::abs(ratio-1),std::abs(ratio-2),std::abs(ratio-.5)})<.025)return true;}return false;};
      for(double bpm:{65.,90.,120.,137.}){
        std::vector<double> attacks;for(int i=0;i<40;++i){double t=3+i*60/bpm+(i%3-1)*.003;attacks.push_back(t);attacks.push_back(t+.008);attacks.push_back(t+.017);}
        auto original=attacks;auto estimate=estimateTempo(attacks);
        check(containsTempo(estimate,bpm)&&estimate.attacks==40&&attacks==original,"Tempo candidates retain an octave-equivalent pulse from jittered chords without mutating captured onsets");
      }
      auto sparse=estimateTempo(std::vector<double>{0,0,0});
      check(sparse.bpm==0&&String(sparse.reason)=="few_attacks","Too few attacks give a precise reason rather than judging performance regularity");
      auto brief=estimateTempo(std::vector<double>{0,.04});
      check(brief.bpm==0&&String(brief.reason)=="short_span","An insufficient analysis span is distinguished from too few onsets");
      auto separated=estimateTempo({0,4,8,12,16,20});
      check(separated.bpm>0&&!separated.candidates.empty(),"Widely separated attacks still produce approximate candidates rather than being rejected");
      for(double bpm:{90.,120.,137.}){
        std::vector<double> shortTake;for(int i=0;i<15;++i)shortTake.push_back(60/bpm*.5*i+(i%3-1)*.006);
        auto estimate=estimateTempo(shortTake);
        check(containsTempo(estimate,bpm)&&estimate.seconds<6,"A short fifteen-note phrase can propose a tempo before six seconds");
      }
      std::vector<double> mixed{0};for(double beats:{.5,1.,1.5,.5,.5,1.,1.5,1.,.5,1.5,.5,1.,.5,1.})mixed.push_back(mixed.back()+beats*.5);
      auto mixedOriginal=mixed;auto mixedEstimate=estimateTempo(mixed);
      check(containsTempo(mixedEstimate,120)&&mixed==mixedOriginal,"Mixed eighth, quarter and dotted durations retain the intended pulse among candidates without modifying onsets");
      std::vector<double> human;for(int i=0;i<15;++i)human.push_back(i*.5+(i%2? .03:0));
      check(containsTempo(estimateTempo(human),120),"Small human timing deviations do not suppress a clear pulse");
      std::vector<double> shortChords;for(int i=0;i<8;++i)for(double offset:{0.,.008,.017})shortChords.push_back(i*.5+offset);
      auto chordEstimate=estimateTempo(shortChords);
      check(chordEstimate.attacks==8&&containsTempo(chordEstimate,120),"Eight short chord attacks are sufficient without counting chord tones as extra evidence");
      std::vector<double> triplets{0};for(double beats:{1.,2./3,1./3,1.,1.5,.5,2./3,1./3,1.,2.,.5,.5,1.,1.})triplets.push_back(triplets.back()+beats*.5);
      check(containsTempo(estimateTempo(triplets),120),"Ternary and straight durations can support the same pulse");
      std::vector<double> irregular;double cursor=0;unsigned random=17;for(int i=0;i<70;++i){random=random*1664525u+1013904223u;cursor+=.11+double(random%1700)/1000;irregular.push_back(cursor);}
      auto uncertain=estimateTempo(irregular);check(uncertain.bpm>0&&!uncertain.candidates.empty(),"Uncertain timing keeps approximate scored alternatives instead of suppressing all proposals");
      std::vector<double> sixteenths;for(int i=0;i<80;++i)sixteenths.push_back(i*.125);
      check(std::abs(estimateTempo(sixteenths).bpm-120)<.5,"Sixteenth-note attacks produce a plausible pulse independent of visual grid or project BPM");
      std::vector<TempoOnset> dense;for(int i=0;i<12000;++i)dense.push_back({i*.01,48+i%36,30+i%98});
      auto begin=juce::Time::getMillisecondCounterHiRes();auto longEstimate=estimateTempo(dense);
      check(longEstimate.bpm>0&&longEstimate.histogram.size()<=1000&&juce::Time::getMillisecondCounterHiRes()-begin<5000,"Dense takes use bounded analysis windows and finish within five seconds");
      bool invalid=false;try{phthongos::midi::bpm::detect({{0,60,90},{-1,60,90}});}catch(const std::invalid_argument&){invalid=true;}
      check(invalid,"The independent detector rejects invalid or unordered event times");
      auto cleaned=estimateTempo(std::vector<double>{std::numeric_limits<double>::quiet_NaN(),-1,0,.5,1,1.5,2,2.5});
      check(cleaned.bpm>0&&cleaned.attacks==6,"Invalid external timestamps are excluded without corrupting valid tempo evidence");

      check(std::abs(rhythmMilliseconds(120,2,0)-500)<1e-9 && std::abs(rhythmMilliseconds(120,0,0)-2000)<1e-9,"120 BPM calculator: quarter 500 ms, whole 2000 ms");
      check(std::abs(rhythmMilliseconds(65,2,0)-60000./65)<1e-9 && std::abs(rhythmMilliseconds(65,3,1)-45000./65)<1e-9,"65 BPM calculator: straight and dotted durations");
      auto match=nearestRhythm(120,335);
      check(match.value==2&&match.variant==2&&std::abs(match.difference-(335-1000./3))<1e-9,"Reverse calculator identifies triplet and signed deviation");
      for(double bad:{0.,9.,601.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){bool rejected=false;try{nearestRhythm(bad,500);}catch(...){rejected=true;}check(rejected,"Invalid calculator BPM rejected");}
      std::vector<std::string> names(129,"Piano");
      auto input=Source::blank("variable.mid");input.createdEmpty=false;
      input.bytes=encode(0,480,{{{0,255,81,{7,161,32}},{240,144,0,{60,90}},{480,255,81,{15,66,64}},{720,176,0,{64,127}},{960,128,0,{60,0}},{960,255,47,{}}}});
      auto original=input.parse(names);auto preserved=input;preserved.bytes=input.exportNotes(original.notes,names);auto preservedParsed=preserved.parse(names);
      check(preservedParsed.noteTicks==original.noteTicks&&preservedParsed.clock.points.size()==original.clock.points.size(),"Source mode preserves MIDI ticks and tempo map");
      auto changed=input;changed.bytes=input.exportNotes(original.notes,names,65);auto parsed=changed.parse(names);
      int tempoEvents=0;double controllerTime=-1;
      for(auto& track:parsed.tracks)for(auto& e:track){if(e.isMeta(81)){++tempoEvents;check(e.tick==0&&e.data==std::vector<std::uint8_t>{14,21,197},"65 BPM tempo event is explicit at tick zero");}if(e.channel()&&(e.status&240)==176)controllerTime=parsed.clock.seconds(e.tick);}
      check(tempoEvents==1&&parsed.notes.size()==1&&std::abs(parsed.notes[0].start-.25)<.001&&std::abs(parsed.notes[0].end-1.5)<.001,"Fixed export preserves note times across a source tempo change");
      check(std::abs(controllerTime-1.)<.001,"Fixed export retimes sustain controller without changing its real time");
      auto edited=original.notes;edited[0].start=.4f;edited[0].end=1.7f;changed.bytes=input.exportNotes(edited,names,65);parsed=changed.parse(names);
      check(std::abs(parsed.notes[0].start-.4)<.001&&std::abs(parsed.notes[0].end-1.7)<.001,"Edited note durations survive fixed-tempo export");
      changed=Source::blank("new.mid");changed.bytes=exportParts(edited,names,65);parsed=changed.parse(names);
      check(parsed.division==9600&&std::abs(parsed.notes[0].start-.4)<.0001&&std::abs(parsed.notes[0].end-1.7)<.0001,"Audio transcription export uses 65 BPM and preserves high-resolution timings");
      auto smpte=input;smpte.bytes=encode(0,0xe728,{{{0,144,0,{60,90}},{1000,128,0,{60,0}},{1000,255,47,{}}}});auto timed=smpte.parse(names);changed=smpte;changed.bytes=smpte.exportNotes(timed.notes,names,65);parsed=changed.parse(names);
      check(!(parsed.division&32768)&&std::abs(parsed.notes[0].end-1.)<.001,"SMPTE source converts to musical time without changing elapsed seconds");
    }
    {
      auto before=ui();
      tonalityOpen=true;toneExpanded=true;toneOptions();
      check(roll.getY()>toneHelp.getBottom() && buttonMap["select"]->getY()>toneHelp.getBottom(),"Tonality expanded reserves space above tools and timeline");
      toneExpanded=false;toneOptions();
      check(buttonMap["select"]->getY()>toneHelp.getBottom(),"Tonality compact reserves space above tools");
      auto closed=ui();closed.getDynamicObject()->setProperty("tonalityOpen",false);loadUi(closed);
      check(!toneCaption.isVisible()&&!toneHelp.isVisible()&&buttonMap["select"]->getY()==232+pitchBandHeight(),"Restoring closed tonality hides controls and restores toolbar");
      loadUi(before);
    }
    {
      Roll fixture;fixture.setSize(1100,540);fixture.displayMode=0;fixture.editable=true;
      fixture.scoreDuration=4;fixture.view.pitchZoom=8;fixture.view.pitchPan=.5;fixture.tool=4;fixture.eraserDiameter=12;
      Note n;n.id=1;n.pitch=60;n.start=.3;n.end=1.3;n.velocity=79;n.editable=true;
      fixture.notes={n};n.id=2;n.editable=false;fixture.notes.push_back(n);
      n.id=3;n.editable=true;n.start=2;n.end=3;fixture.notes.push_back(n);
      check(fixture.noteInformation(fixture.notes[0]).contains("1000 ms"),"Note information includes duration in milliseconds");
      auto at=fixture.rect(fixture.notes[0]).getCentre(),to=fixture.rect(fixture.notes[2]).getCentre();
      auto event=[&](juce::Point<float> p){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),p,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&fixture,&fixture,juce::Time::getCurrentTime(),at,juce::Time::getCurrentTime(),1,true);};
      int calls=0;std::set<std::int64_t> removed;
      fixture.remove=[&](auto ids){++calls;removed=ids;};
      fixture.mouseDown(event(at));fixture.mouseDrag(event(to));
      check(calls==0&&fixture.erasedIds==std::set<std::int64_t>{1,3},"Eraser previews swept rectangles and protects other parts");
      fixture.mouseUp(event(to));
      check(calls==1&&removed==std::set<std::int64_t>{1,3},"Eraser commits the whole stroke as one deletion");
      fixture.mouseDown(event(at));fixture.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));fixture.mouseUp(event(at));
      check(calls==1,"Escape cancels pending eraser stroke");
      check(!Roll::eraserTouches({0,0,10,10},{14,14},{15,15},5),"Round eraser does not erase beyond its circular corner");
      fixture.tool=0;
      auto edge=fixture.rect(fixture.notes[0]).getRight()-1;
      auto edgePoint=juce::Point<float>(edge,at.y);
      fixture.mouseDown(event(edgePoint));fixture.mouseDrag(event(edgePoint+juce::Point<float>(50,0)));
      check(fixture.getTooltip().contains(" ms")&&!fixture.getTooltip().contains("1000 ms"),"Duration updates live while resizing");
    }
    {
      Roll fixture;fixture.setSize(1100,600);fixture.editable=true;fixture.displayMode=0;fixture.scoreDuration=4;fixture.view.pitchZoom=8;fixture.view.pitchPan=.5;
      Note a;a.id=1;a.start=6;a.end=7;a.pitch=60;a.velocity=83;Note b=a;b.id=2;b.start=7;b.end=8;b.pitch=64;fixture.notes={a,b};fixture.selected={1,2};
      auto event=[&](juce::Point<float> at,bool copy){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|(copy?juce::ModifierKeys::ctrlModifier:0)),1,0,0,0,0,&fixture,&fixture,juce::Time::getCurrentTime(),at,juce::Time::getCurrentTime(),1,true);};
      double shift=0;int commits=0;std::set<std::int64_t> copied;fixture.duplicateGroup=[&](auto ids,double seconds,int){copied=ids;shift=seconds;++commits;};
      auto centre=fixture.rect(a).getCentre();auto far=centre+juce::Point<float>(float(fixture.layout().width*1.2),0);const double originalSpan=fixture.span(),originalDuration=fixture.duration();auto originalView=fixture.view.json();
      fixture.mouseDown(event(centre,true));fixture.mouseDrag(event(far,true));
      check(fixture.duration()>originalDuration&&std::abs(fixture.span()-originalSpan)<1e-9,"Group copy extends the timeline without changing seconds per pixel");
      check(fixture.start()>0,"Dragging at the right edge scrolls the viewport into newly created timeline space");double firstStart=fixture.start();for(int i=0;i<12;++i)fixture.mouseDrag(event(far,true));
      check(fixture.start()>firstStart,"Repeated stationary drag callbacks keep edge scrolling active");
      fixture.mouseUp(event(far,true));check(commits==1&&copied==std::set<std::int64_t>{1,2}&&shift>originalDuration-8,"Copying a group beyond the former timeline emits one complete untruncated duplication");
      check(fixture.notes[0].start==a.start&&fixture.notes[1].end==b.end&&fixture.notes[0].velocity==83,"Timeline extension never mutates the original group during preview");
      fixture.setScoreDuration(8+shift);check(std::abs(fixture.span()-originalSpan)<1e-9,"Committing an extended timeline preserves the visible time scale");
      fixture.scoreDuration=4;fixture.view.load(originalView);fixture.selected={1,2};centre=fixture.rect(a).getCentre();far=centre+juce::Point<float>(float(fixture.layout().width*1.2),0);
      fixture.mouseDown(event(centre,true));fixture.mouseDrag(event(far,true));fixture.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));fixture.mouseUp(event(far,true));
      check(commits==1&&fixture.duration()==originalDuration&&juce::JSON::toString(fixture.view.json())==juce::JSON::toString(originalView),"Escape removes provisional extension, restores the viewport and creates no copied notes");
      fixture.selected={1};double movedEnd=0;fixture.modify=[&](const Note& n){movedEnd=n.end;++commits;};centre=fixture.rect(a).getCentre();far=centre+juce::Point<float>(float(fixture.layout().width*1.2),0);
      fixture.mouseDown(event(centre,false));fixture.mouseDrag(event(far,false));fixture.mouseUp(event(far,false));check(movedEnd>originalDuration&&commits==2,"A single-note move can extend past the previous project end");fixture.cancelTimelinePreview();
      auto edge=fixture.rect(b).getCentre();edge.x=fixture.rect(b).getRight()-1;fixture.mouseDown(event(edge,false));fixture.mouseDrag(event(edge+juce::Point<float>(float(fixture.layout().width),0),false));fixture.mouseUp(event(edge,false));check(movedEnd>originalDuration&&commits==3,"Resizing a note past the old project end extends rather than truncates it");fixture.cancelTimelinePreview();
      fixture.selected={1,2};centre=fixture.rect(a).getCentre();far=centre+juce::Point<float>(float(fixture.layout().width*1000),0);fixture.mouseDown(event(centre,true));fixture.mouseDrag(event(far,true));fixture.mouseUp(event(far,true));check(fixture.duration()==1200&&std::abs(shift+8-1200)<1e-6,"Extended group duplication respects the twenty-minute limit atomically");
    }
    {
      Roll fixture;fixture.setSize(1100,600);fixture.scoreDuration=120;fixture.displayMode=2;fixture.maskMode=1;
      auto event=[&](juce::Point<float> at){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&fixture,&fixture,juce::Time::getCurrentTime(),at,juce::Time::getCurrentTime(),1,false);};
      double small=0;
      for(double zoom:{1.,4.,16.})for(double brush:{.25,1.,6.}){
        fixture.view.pitchZoom=zoom;fixture.view.pitchPan=.5;fixture.brushSemitones=brush;
        auto point=juce::Point<float>(550,float(fixture.layout().rollTop+8+(fixture.layout().dividerKeys-fixture.layout().rollTop-8)*.5));
        fixture.mouseDown(event(point));
        double rx=fixture.maskDraft.timeRadius*fixture.layout().width/fixture.span(),ry=fixture.maskDraft.pitchRadius*fixture.row();
        check(fixture.maskPainting&&std::abs(rx-ry)<1e-6&&std::abs(rx-fixture.maskBrushRadius())<1e-6,"Mask painted radii match the circular preview at every brush size and pitch zoom");
        phthongos::validateMaskStroke(fixture.maskDraft);
        auto centre=fixture.maskDraft.points.front();phthongos::SpectralMask mask{std::make_shared<const phthongos::MaskStroke>(fixture.maskDraft)};
        check(phthongos::maskContains(mask,centre.time+.7*fixture.maskDraft.timeRadius,centre.pitch+.7*fixture.maskDraft.pitchRadius)&&!phthongos::maskContains(mask,centre.time+.8*fixture.maskDraft.timeRadius,centre.pitch+.8*fixture.maskDraft.pitchRadius),"Mask selection follows the circular boundary, including diagonal corners");
        if(zoom==4&&brush==.25)small=rx;
        if(zoom==4&&brush==6)check(rx>small*20,"Large brush expands horizontally as well as vertically");
        fixture.mouseUp(event(point));
      }
      fixture.view.pitchZoom=4;fixture.brushSemitones=6;fixture.brushInside=true;fixture.brushPosition={550,300};
      auto image=fixture.createComponentSnapshot(fixture.getLocalBounds());juce::FileOutputStream imageFile(directory.getChildFile("circular-mask-brush.png"));juce::PNGImageFormat().writeImageToStream(image,imageFile);
      fixture.maskMode=2;fixture.mouseDown(event({550,300}));
      check(fixture.maskDraft.erase&&std::abs(fixture.maskDraft.timeRadius*fixture.layout().width/fixture.span()-fixture.maskDraft.pitchRadius*fixture.row())<1e-6,"Mask eraser uses the same circular footprint");fixture.mouseUp(event({550,300}));
      fixture.setSize(150,600);fixture.scoreDuration=1200;fixture.view.pitchZoom=64;
      check(fixture.maskBrushRadius()==0,"Impossible musical-coordinate bounds reject a stroke instead of showing a false circular footprint");
    }
    {
      Roll fixture;fixture.setSize(1200,600);fixture.scoreDuration=10;fixture.editable=true;fixture.displayMode=0;fixture.view.pitchZoom=8;fixture.view.pitchPan=.5;
      Note n;n.id=1;n.start=double(float(1.123456));n.end=double(float(2.654321));n.pitch=60;n.velocity=83;n.editable=true;fixture.notes={n};
      auto centre=fixture.rect(n).getCentre();Note changed;int modifications=0;
      fixture.modify=[&](const Note& value){changed=value;++modifications;};
      auto event=[&](juce::Point<float> at,bool shift){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|(shift?juce::ModifierKeys::shiftModifier:0)),1,0,0,0,0,&fixture,&fixture,juce::Time::getCurrentTime(),centre,juce::Time::getCurrentTime(),1,true);};
      fixture.mouseDown(event(centre,true));fixture.mouseDrag(event(centre+juce::Point<float>(2,float(fixture.row()*3)),true));fixture.mouseUp(event(centre,true));
      check(modifications==1&&changed.pitch==57&&changed.start==n.start&&changed.end==n.end,"Shift vertical move preserves exact note timestamps and changes pitch");
      fixture.mouseDown(event(centre,true));fixture.mouseDrag(event(centre+juce::Point<float>(40,2),true));fixture.mouseDrag(event(centre+juce::Point<float>(60,float(fixture.row()*3)),true));fixture.mouseUp(event(centre,true));
      check(modifications==2&&changed.pitch==60&&changed.start>n.start,"Shift horizontal move preserves pitch and retains its initially chosen axis");
      fixture.mouseDown(event(centre,false));fixture.mouseDrag(event(centre+juce::Point<float>(40,float(fixture.row()*2)),false));fixture.mouseUp(event(centre,false));
      check(modifications==3&&changed.pitch==58&&changed.start>n.start,"Unmodified dragging still moves freely on both axes");
      n.id=2;n.start=3;n.end=4;n.pitch=64;fixture.notes.push_back(n);fixture.selected={1,2};double seconds=99;int semitones=99;
      fixture.moveGroup=[&](const auto&,double dt,int dp){seconds=dt;semitones=dp;};
      fixture.mouseDown(event(centre,true));fixture.mouseDrag(event(centre+juce::Point<float>(2,float(fixture.row()*2)),true));fixture.mouseUp(event(centre,true));
      check(seconds==0&&semitones==-2,"Shift vertical group move preserves timing with zero time displacement");
      int duplications=0;std::set<std::int64_t> copied;
      fixture.duplicateGroup=[&](const auto& ids,double dt,int dp){++duplications;copied=ids;seconds=dt;semitones=dp;};
      auto duplicateEvent=[&](juce::Point<float> at,bool shift=true){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|(shift?juce::ModifierKeys::shiftModifier:0)|juce::ModifierKeys::ctrlModifier),1,0,0,0,0,&fixture,&fixture,juce::Time::getCurrentTime(),centre,juce::Time::getCurrentTime(),1,true);};
      fixture.mouseDown(duplicateEvent(centre));fixture.mouseDrag(duplicateEvent(centre+juce::Point<float>(2,float(fixture.row()*3))));
      check(duplications==0&&fixture.notes[0].pitch==60&&fixture.notes.size()==2,"Duplicate drag previews without modifying original notes or committing early");
      auto preview=fixture.createComponentSnapshot(fixture.getLocalBounds());juce::FileOutputStream previewFile(directory.getChildFile("duplicate-drag.png"));juce::PNGImageFormat().writeImageToStream(preview,previewFile);
      fixture.mouseUp(duplicateEvent(centre));check(duplications==1&&copied==std::set<std::int64_t>{1,2}&&seconds==0&&semitones==-3&&modifications==3,"Ctrl+Shift vertical drag duplicates the whole group with zero timing offset");
      fixture.selected.clear();fixture.mouseDown(duplicateEvent(centre));fixture.mouseDrag(duplicateEvent(centre+juce::Point<float>(40,2)));fixture.mouseUp(duplicateEvent(centre));
      check(duplications==2&&copied==std::set<std::int64_t>{1}&&seconds>0&&semitones==0,"Ctrl+Shift horizontal drag duplicates only the unselected grabbed note without transposing it");
      fixture.mouseDown(duplicateEvent(centre));fixture.mouseDrag(duplicateEvent(centre+juce::Point<float>(1,float(fixture.row()*2))));fixture.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));fixture.mouseUp(duplicateEvent(centre));
      check(duplications==2,"Escape cancels a duplicate drag without creating notes");
      fixture.mouseDown(duplicateEvent(centre));fixture.mouseUp(duplicateEvent(centre));check(duplications==2,"Ctrl+Shift click without movement creates no duplicate");
      fixture.selected={1,2};fixture.mouseDown(duplicateEvent(centre,false));fixture.mouseUp(duplicateEvent(centre,false));
      check(fixture.selected==std::set<std::int64_t>{2}&&duplications==2,"Ctrl click removes a selected note without copying");
      fixture.mouseDown(duplicateEvent(centre,false));fixture.mouseUp(duplicateEvent(centre,false));
      check(fixture.selected==std::set<std::int64_t>{1,2}&&duplications==2,"Ctrl click adds a note without copying");
      auto freeTo=centre+juce::Point<float>(40,float(fixture.row()*2));
      fixture.mouseDown(duplicateEvent(centre,false));fixture.mouseDrag(duplicateEvent(freeTo,false));fixture.mouseUp(duplicateEvent(freeTo,false));
      check(duplications==3&&copied==std::set<std::int64_t>{1,2}&&seconds>0&&semitones==-2&&modifications==3,"Ctrl drag duplicates the selected group freely across both axes, preserving originals");
      fixture.selected.clear();auto edge=juce::Point<float>(fixture.rect(fixture.notes[0]).getX()+1,centre.y);
      fixture.mouseDown(duplicateEvent(edge,false));fixture.mouseDrag(duplicateEvent(edge+juce::Point<float>(40,float(fixture.row()*2)),false));fixture.mouseUp(duplicateEvent(edge,false));
      check(duplications==4&&copied==std::set<std::int64_t>{1}&&seconds>0&&semitones==-2&&modifications==3,"Ctrl drag from a note edge duplicates instead of resizing");
      fixture.mouseDown(duplicateEvent(centre,false));fixture.mouseDrag(duplicateEvent(freeTo,false));fixture.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));fixture.mouseUp(duplicateEvent(freeTo,false));
      check(duplications==4,"Escape cancels free duplication");
      {
        for(bool grouped:{false,true}){
          Roll editor;editor.setSize(1200,600);editor.scoreDuration=12;editor.editable=true;editor.displayMode=0;editor.view.pitchZoom=8;editor.view.pitchPan=.5;
          Note first;first.id=101;first.start=1.237891;first.end=2.643217;first.pitch=60;first.velocity=83;first.editable=true;Note second=first;second.id=102;second.start=3.121;second.end=3.818;second.pitch=64;editor.notes={first,second};editor.selected=grouped?std::set<std::int64_t>{101,102}:std::set<std::int64_t>{101};
          auto origin=editor.rect(first).getCentre();auto stamp=juce::Time::getCurrentTime();auto copyEvent=[&](juce::Point<float> at,bool shift){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::ctrlModifier|(shift?int(juce::ModifierKeys::shiftModifier):0)),1,0,0,0,0,&editor,&editor,stamp,origin,stamp,1,true);};
          int copies=0,dp=99;double dt=99;std::set<std::int64_t> ids;editor.duplicateGroup=[&](const auto& values,double seconds,int pitches){++copies;ids=values;dt=seconds;dp=pitches;};
          auto originalView=juce::JSON::toString(editor.view.json());editor.mouseDown(copyEvent(origin,true));editor.mouseDrag(copyEvent(origin+juce::Point<float>(4,3),true));
          check(editor.moveAxis<=0&&copies==0,"A small diagonal twitch does not prematurely choose a constrained duplication axis");
          editor.mouseDrag(copyEvent(origin+juce::Point<float>(8,7),false));check(editor.moveAxis<=0&&copies==0,"Releasing Shift while direction remains ambiguous does not unlock or commit the copy");
          editor.mouseDrag(copyEvent(origin+juce::Point<float>(2,-float(editor.row()*2)),false));check(editor.moveAxis==2,"A subsequent clear vertical movement selects the vertical axis after initial jitter");
          auto end=juce::Point<float>(float(editor.getWidth()+100),origin.y-float(editor.row()*3));editor.mouseDrag(copyEvent(end,false));editor.mouseUp(copyEvent(end,false));
          check(copies==1&&dt==0&&dp==3&&ids.size()==(grouped?2:1)&&juce::JSON::toString(editor.view.json())==originalView,"Vertical duplication keeps exact timing and prevents edge autoscroll even after Shift is released, for a note or a group");
          check(editor.notes[0].start==first.start&&editor.notes[0].end==first.end&&editor.notes[0].pitch==first.pitch,"Constrained duplication leaves the originals untouched");
          editor.mouseDown(copyEvent(origin,true));editor.mouseDrag(copyEvent(origin+juce::Point<float>(3,4),true));editor.mouseDrag(copyEvent(origin+juce::Point<float>(80,2),true));
          auto horizontal=origin+juce::Point<float>(95,-float(editor.row()*3));editor.mouseDrag(copyEvent(horizontal,false));editor.mouseUp(copyEvent(horizontal,false));
          check(copies==2&&dt>0&&dp==0,"Horizontal duplication keeps original pitches after initial jitter and early Shift release");
          auto selectedBefore=editor.selected;editor.mouseDown(copyEvent(origin,true));editor.mouseDrag(copyEvent(origin+juce::Point<float>(10,10),true));editor.mouseUp(copyEvent(origin+juce::Point<float>(10,10),false));
          check(copies==2&&editor.selected==selectedBefore,"An entirely ambiguous diagonal gesture creates no accidental copy or selection change");
          editor.mouseDown(copyEvent(origin,false));auto free=origin+juce::Point<float>(40,-float(editor.row()*2));editor.mouseDrag(copyEvent(free,false));editor.mouseUp(copyEvent(free,false));
          check(copies==3&&dt>0&&dp==2,"The next unmodified Ctrl drag starts fresh and still duplicates freely on both axes");
        }
      }
      fixture.markers={object({{"id",1},{"time",2.},{"name","Check attack"}})};double sought=-1;fixture.seek=[&](double t){sought=t;};var markerAction,target;
      fixture.markerChanged=[&](const var& a){markerAction=a;};fixture.context=[&](auto,const var& t){target=t;};
      auto flag=juce::Point<float>(float(fixture.x(2)),4);fixture.mouseDown(event(flag,false));fixture.mouseUp(event(flag,false));
      check(sought==2&&markerAction.isVoid(),"Clicking a marker seeks without editing it");
      fixture.mouseDown(event(flag,false));fixture.mouseDrag(event(flag+juce::Point<float>(60,0),false));fixture.mouseUp(event(flag,false));
      check(markerAction["kind"].toString()=="modify_marker"&&number(markerAction,"at")>2&&markerAction["name"].toString()=="Check attack","Marker drag emits one final time change and preserves its name");
      markerAction=var();fixture.mouseDown(event(flag,false));fixture.mouseDrag(event(flag+juce::Point<float>(60,0),false));fixture.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));fixture.mouseUp(event(flag,false));
      check(markerAction.isVoid(),"Escape cancels marker dragging");
      fixture.mouseMove(event(flag,false));check(fixture.getTooltip().contains("Check attack"),"Marker hover reveals its personal label");
      auto popup=juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),flag,juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier),1,0,0,0,0,&fixture,&fixture,juce::Time::getCurrentTime(),flag,juce::Time::getCurrentTime(),1,false);fixture.mouseDown(popup);
      check(target["kind"].toString()=="timeline"&&int(target["marker"]["id"])==1,"Timeline right-click exposes marker actions before note context");
      check(int(fixture.markerAt(flag+juce::Point<float>(14,15))["id"])==1&&int(fixture.markerAt(flag+juce::Point<float>(-14,15))["id"])==1,"Marker hit area tolerates 14-pixel offsets on either side of the flag");
      fixture.mouseDown(popup.withNewPosition(flag+juce::Point<float>(14,15)));check(int(target["marker"]["id"])==1,"Right-click near a flag exposes deletion and rename for that marker");
      fixture.markers.push_back(object({{"id",2},{"time",fixture.timeAt(flag.x+20)},{"name","Neighbour"}}));
      check(int(fixture.markerAt(flag+juce::Point<float>(9,0))["id"])==1&&int(fixture.markerAt(flag+juce::Point<float>(12,0))["id"])==2,"Overlapping hit areas choose the nearest marker rather than the last marker in the list");
      check(fixture.markerAt(flag+juce::Point<float>(40,0)).isVoid()&&fixture.markerAt({flag.x,31}).isVoid(),"Empty ruler space and the note area remain outside marker hit areas");fixture.markers.pop_back();
      markerAction=var();auto near=flag+juce::Point<float>(14,0);fixture.mouseDown(event(near,false));fixture.mouseDrag(event(near+juce::Point<float>(6,0),false));fixture.mouseUp(event(near,false));
      check(markerAction["kind"].toString()=="modify_marker"&&std::abs(number(markerAction,"at")-(2+6/fixture.layout().width*fixture.span()))<1e-6,"Dragging from a flag's enlarged hit area preserves the grab offset without a time jump");
      fixture.view.zoom=2;fixture.view.pan=.1;auto movedFlag=juce::Point<float>(float(fixture.x(2)),4);check(int(fixture.markerAt(movedFlag)["id"])==1,"Marker hit testing follows time coordinates across zoom and pan");
      fixture.view.zoom=1;fixture.view.pan=0;auto picture=fixture.createComponentSnapshot(fixture.getLocalBounds());juce::FileOutputStream imageFile(directory.getChildFile("timeline-marker.png"));juce::PNGImageFormat().writeImageToStream(picture,imageFile);
    }
    {
      Roll palette;palette.setSize(1100,600);palette.editable=true;palette.displayMode=2;palette.spectral=true;palette.overlayOpacity=.65f;palette.audio=object({{"duration",4.},{"midi_only",false}});palette.scoreDuration=4;palette.view.pitchZoom=8;palette.view.pitchPan=.5;
      palette.spectrum=juce::Image(juce::Image::ARGB,256,128,true);palette.spectrumStart=0;palette.spectrumEnd=4;
      {juce::Graphics g(palette.spectrum);g.fillAll(juce::Colour(0xffa26591));g.setColour(juce::Colour(0xffedd55a));for(int y=0;y<128;y+=11)g.fillRect(0,y,256,3);g.setColour(juce::Colour(0xffdd805a));for(int x=0;x<256;x+=21)g.fillRect(x,0,3,128);}
      for(unsigned i=0;i<12;++i){Note n;n.id=i+1;n.part=256+i;n.program=0;n.pitch=58+i;n.start=.5;n.end=2.3;n.velocity=96;n.editable=true;palette.notes.push_back(n);palette.partColours[n.part]=i;}
      {juce::FileOutputStream out(directory.getChildFile("voice-palette-spectrogram.png"));juce::PNGImageFormat().writeImageToStream(palette.createComponentSnapshot(palette.getLocalBounds()),out);}
    }
    {
      Roll fixture;fixture.setSize(1100,600);fixture.editable=false;fixture.tool=4;fixture.lockedParts={256};int changes=0;std::vector<int> played;
      fixture.add=[&](double,int){++changes;};fixture.remove=[&](auto){++changes;};fixture.keyAudition=[&](int pitch){played.push_back(pitch);};
      auto l=fixture.layout();int whites=0,before=0;for(int p=0;p<128;++p)if(p%12!=1&&p%12!=3&&p%12!=6&&p%12!=8&&p%12!=10){++whites;if(p<60)++before;}auto kw=l.width/whites;
      auto white=juce::Point<float>(float(58+(before+.5)*kw),float(l.keyTop+l.keys*.85));auto black=juce::Point<float>(float(58+(before+1)*kw),float(l.keyTop+l.keys*.25));
      check(fixture.keyAt(white)==60&&fixture.keyAt(black)==61,"Bottom piano hit testing distinguishes white and overlaid black keys");
      auto event=[&](juce::Point<float> at){auto now=juce::Time::getCurrentTime();return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&fixture,&fixture,now,at,now,1,false);};
      fixture.mouseDown(event(white));fixture.mouseDrag(event(black));fixture.mouseDrag(event(black));fixture.mouseUp(event({0,0}));
      check(played==std::vector<int>{60,61,-1}&&fixture.keyboardPitch==-1&&changes==0,"Bottom keyboard auditions and releases across keys without creating or deleting notes on a locked voice");
      fixture.mouseDown(event(white));fixture.focusLost(juce::Component::focusChangedDirectly);check(played.back()==-1&&!fixture.keyboardDragging,"Losing keyboard focus releases the audition note");
    }
    {
      using namespace phthongos;auto piano=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("player-assets/piano.sf2");unsigned rate=16000;
      audio::Engine sound(rate,std::make_unique<audio::Sampler>(piano,juce::File(),rate),std::make_unique<audio::Sampler>(piano,juce::File(),rate));sound.mode=1;sound.midiGain=.1f;sound.pcm=std::make_shared<analysis::StereoPcm>(rate*2,std::array<float,2>{});
      phthongos::Note low,high;low.id=1;low.part=Part{PartId(256),0,false,"Low"};low.pitch=60;low.velocity=90;low.start=0;low.end=.2f;high=low;high.id=2;high.part->identity=257;high.pitch=72;high.start=.7f;high.end=.9f;
      sound.notes({low,high});
      auto energy=[&]{sound.play();std::array<double,2> sum{};for(unsigned i=0;i<rate;++i){auto x=sound.next();if(i>=rate/40&&i<rate*3/20)sum[0]+=double(x[0])*x[0];if(i>=rate*29/40&&i<rate*17/20)sum[1]+=double(x[0])*x[0];}sound.pause();sound.seek(0);return sum;};
      auto baseline=energy();sound.levels({{256,-12}});auto lowered=energy();check(baseline[0]>1e-8&&baseline[1]>1e-8&&lowered[0]<baseline[0]*.2&&std::abs(lowered[1]/baseline[1]-1)<.03,"Voice level lowers only its own sound even when both voices use the same piano");
      sound.notes({});sound.levels({});sound.keyDown(0,67,90,-6);double heard=0;for(unsigned i=0;i<rate;++i){auto x=sound.next();heard+=double(x[0])*x[0];}check(heard>1e-8&&sound.keys()[67],"Keyboard audition sustains beyond the normal one-shot preview");sound.keyUp();for(unsigned i=0;i<rate*3;++i)sound.next();check(!sound.keys()[67],"Keyboard Note Off releases the held note");
      sound.keyDown(0,60,90,0);std::array<float,2> previous{};for(unsigned i=0;i<rate/8;++i)previous=sound.next();
      bool continuous=true,finite=true;for(int pitch=61;pitch<85;++pitch){sound.keyDown(0,pitch,90,0);auto first=sound.next();continuous&=first==previous;for(unsigned i=0;i<rate/50;++i){previous=sound.next();finite&=std::isfinite(previous[0])&&std::isfinite(previous[1])&&std::abs(previous[0])<=1&&std::abs(previous[1])<=1;}}
      check(continuous&&finite,"Keyboard pitch slides preserve waveform continuity and bounded audio");sound.keyUp();for(unsigned i=0;i<rate*3;++i)sound.next();check(sound.next()==std::array<float,2>{},"Sliding keyboard notes release to silence without stuck tails");
      sound.audition(0,60,90);for(unsigned i=0;i<rate/8;++i)previous=sound.next();sound.audition(0,64,90);check(sound.next()==previous,"Moving a MIDI note between pitches preserves audition waveform continuity");sound.pause();
      sound.midiNote(0,60,90,0,0);sound.midiNote(0,64,75,0,0);for(unsigned i=0;i<rate/50;++i)sound.next();check(sound.keys()[60]&&sound.keys()[64],"External MIDI auditions independent notes as a chord");
      sound.midiNote(0,60,0,0,0);check(!sound.keys()[60]&&sound.keys()[64],"External MIDI Note Off releases only the specified pitch");
      sound.midiSustain(1,true);sound.midiNote(1,67,100,0,0);sound.midiNote(1,67,0,0,0);sound.midiNote(0,64,0,0,0);check(sound.keys()[67]&&!sound.keys()[64],"Sustain holds released notes on its own MIDI channel only");
      sound.midiSustain(1,false);check(!sound.keys()[67],"Releasing the sustain pedal releases sustained keys");
      sound.midiNote(0,60,90,0,-6);sound.midiNote(0,64,90,0,-6);sound.notes({});check(sound.keys()[60]&&sound.keys()[64],"Synchronising edited notes does not cut the held external MIDI chord");sound.midiPanic();for(unsigned i=0;i<rate;++i)sound.next();check(!sound.keys()[60]&&!sound.keys()[64]&&sound.next()==std::array<float,2>{},"Disconnect panic releases all external MIDI notes and reaches silence");
      previous=sound.next();continuous=true;finite=true;
      for(int pitch=48;pitch<96;++pitch){sound.midiNote(0,pitch,95,0,-9);continuous&=sound.next()==previous;for(unsigned i=0;i<rate/200;++i){previous=sound.next();finite&=std::isfinite(previous[0])&&std::isfinite(previous[1]);}sound.midiNote(0,pitch,0,0,-9);continuous&=sound.next()==previous;for(unsigned i=0;i<rate/200;++i)previous=sound.next();}
      check(continuous&&finite,"Rapid external MIDI attacks and releases preserve waveform continuity without reloading the bank");sound.midiPanic();for(unsigned i=0;i<rate;++i)sound.next();
      sound.recording=true;sound.seek(std::uint64_t(rate)*10);sound.play();auto beforeRecording=sound.position;for(unsigned i=0;i<rate/100;++i)sound.next();check(sound.position>beforeRecording&&sound.playing,"Recording transport advances beyond the original PCM duration");sound.recording=false;sound.pause();
      sound.play();sound.keyDown(0,72,90,0);heard=0;for(unsigned i=0;i<rate/4;++i){auto x=sound.next();heard+=double(x[0])*x[0];}check(sound.playing&&heard>1e-8&&sound.keys()[72],"Keyboard audition mixes with ongoing playback without stopping it");sound.keyUp();sound.pause();for(unsigned i=0;i<rate*2;++i)sound.next();
      rhythm::Grid countGrid;sound.click.configure(rate,false,-12,countGrid);sound.recording=true;sound.seek(rate/2);sound.play();sound.preCount(1);
      double clickEnergy=0;bool heldPosition=true;for(unsigned i=0;i<rate*2;++i){auto x=sound.next();clickEnergy+=x[0]*x[0];heldPosition&=sound.position==rate/2;}
      check(heldPosition&&sound.countRemaining==0&&clickEnergy>1e-6,"One bar of pre-count sounds with the normal click disabled while the recording position stays fixed");
      sound.next();check(sound.position==rate/2+1,"Recording starts on the exact sample immediately after the count-in");
      sound.preCount(2);for(unsigned i=0;i<100;++i)sound.next();sound.pause();check(sound.countRemaining==0,"Pausing cancels a two-bar audio count-in immediately");sound.recording=false;
    }
    {
      using namespace phthongos;rhythm::Grid grid;
      for(unsigned rate:{44100u,48000u}){
        audio::Metronome click;grid.tempos={{0,0,60/79.4}};grid.meters={{0,4,4}};click.configure(rate,true,-12,grid);
        bool exact=true;for(std::size_t i=0;i<std::min<std::size_t>(1000,click.ticks.size());++i)exact&=std::abs(double(click.ticks[i].frame)-double(i)*60/79.4*rate)<=.50001;
        check(exact,"Fractional-BPM click scheduling has no cumulative drift at 44.1 and 48 kHz");
        check(click.ticks[0].accent&&!click.ticks[1].accent&&click.ticks[4].accent,"The metronome accents the first beat of each 4/4 bar");
      }
      audio::Metronome click;grid.tempos={{0,0,.5},{4,2,1.}};grid.meters={{0,3,4},{6,6,8}};click.configure(48000,true,0,grid);
      check(click.ticks[3].accent&&click.ticks[4].frame==96000&&click.ticks[5].frame==144000,"Click scheduling respects tempo changes and 3/4 accents");
      check(click.ticks[6].accent&&click.ticks[7].frame-click.ticks[6].frame==24000&&click.ticks[12].accent,"A 6/8 bar has six eighth-note pulses and a first-beat accent");
      double full=0,quiet=0;for(unsigned i=0;i<1200;++i){auto x=click.pulse(i,true);full+=x*x;}
      click.configure(48000,true,-12,grid);for(unsigned i=0;i<1200;++i){auto x=click.pulse(i,true);quiet+=x*x;}
      check(full>0&&std::abs(quiet/full-std::pow(10.,-12./10))<1e-6,"Metronome volume changes click energy independently from instrument velocities");
      click.configure(48000,true,0,grid);
      double oldEnergy=0,newEnergy=0,normalEnergy=0,peak=0;
      for(unsigned i=0;i<2400;++i){double t=double(i)/48000;double old=t<.025?std::sin(2*3.141592653589793*1800*t)*std::min(t/.001,1.)*std::exp(-t*220)*(1-t/.025)*.34:0;double x=click.pulse(i,true),normal=click.pulse(i,false);oldEnergy+=old*old;newEnergy+=x*x;normalEnergy+=normal*normal;peak=std::max(peak,std::abs(x));}
      check(newEnergy>oldEnergy*8&&newEnergy>normalEnergy&&peak<1&&click.pulse(0,true)==0&&click.pulse(2400,true)==0,"Fuller metronome click has substantially more energy, a distinct accent and smooth bounded endpoints");
      click.configure(48000,true,-60,grid);check(click.pulse(50,true)==0,"The metronome volume can be completely muted");
    }
    auto initialUi=ui();
    {
      juce::DocumentWindow owner("Dialog owner fixture",juce::Colours::white,juce::DocumentWindow::allButtons);
      owner.setBounds(100,100,900,700);owner.setVisible(true);
      std::unique_ptr<juce::DialogWindow> dialog((new Form)->createDialog("Owned settings",&owner));
      check(dialogHasOwner(*dialog,owner),"Settings window has a native owner and no independent taskbar entry");
      check(owner.getBounds().contains(dialog->getBounds().getCentre()),"Settings dialog opens centred on its owner");
      check(!dialog->isAlwaysOnTop(),"Owned settings never become globally always on top");
      auto* content=dynamic_cast<Form*>(dialog->getContentComponent());
      content->toggle("Increment saves","increment",false);auto* toggle=content->toggleFields["increment"];
      auto click=[&](juce::Point<float> at){auto source=juce::Desktop::getInstance().getMainMouseSource();auto now=juce::Time::getCurrentTime();
        juce::MouseEvent down(source,at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,toggle,toggle,now,at,now,1,false);
        static_cast<juce::Component*>(toggle)->mouseDown(down);static_cast<juce::Component*>(toggle)->mouseUp(down.withNewPosition(at));};
      click({10,15});check(toggle->getToggleState()&&(bool)content->state()["increment"],"Clicking checkbox square enables the option");
      click({90,15});check(!toggle->getToggleState()&&!(bool)content->state()["increment"],"Clicking checkbox label disables the option");
      for(float scale:{1.f,1.25f,1.5f,2.f}){
        auto image=toggle->createComponentSnapshot(toggle->getLocalBounds(),true,scale);
        juce::FileOutputStream file(directory.getChildFile("checkbox-"+String(scale,2)+".png"));juce::PNGImageFormat().writeImageToStream(image,file);
      }
      dialog->setVisible(true);dialog->enterModalState(false);check(static_cast<juce::Component*>(dialog.get())->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))&&!dialog->isVisible(),"Escape closes the owned settings dialog");
    }

    {
      Form form;form.setSize(540,220);
      juce::DialogWindow frame("Rounded sheet test",juce::Colours::transparentBlack,true,false);
      frame.setTitleBarHeight(0);frame.setContentNonOwned(&form,true);
      auto image=frame.createComponentSnapshot(frame.getLocalBounds());
      check(image.getPixelAt(0,0).getAlpha()==0&&image.getPixelAt(4,4).getAlpha()<255,"Dialog corners remain transparent around the unified rounded sheet");
      check(image.getPixelAt(image.getWidth()/2,5).getAlpha()==255,"Dialog sheet remains filled inside its rounded contour");
      juce::FileOutputStream imageFile(directory.getChildFile("rounded-sheet.png"));juce::PNGImageFormat().writeImageToStream(image,imageFile);
      frame.clearContentComponent();
      std::unique_ptr<juce::DialogWindow> native((new Form)->createDialog("Native rounded sheet"));
      check(!native->isOpaque()&&native->getPeer()&&(native->getPeer()->getStyleFlags()&juce::ComponentPeer::windowHasDropShadow)==0,"Production dialog peer is transparent and does not add a conflicting Windows corner/shadow");
      auto sheet=native->createComponentSnapshot(native->getLocalBounds());check(sheet.getPixelAt(0,0).getAlpha()==0,"Production form factory preserves transparent outer corners");

    }
    {
      Roll fixture;fixture.setSize(1100,540);fixture.scoreDuration=4;fixture.editable=false;
      fixture.playback=object({{"ready",true},{"position",1.}});fixture.selected={42};
      double sought=-1;int calls=0;fixture.seek=[&](double p){sought=p;++calls;};
      auto right=juce::KeyPress(juce::KeyPress::rightKey),left=juce::KeyPress(juce::KeyPress::leftKey);
      auto fineLeft=juce::KeyPress(juce::KeyPress::leftKey,juce::ModifierKeys::shiftModifier,0);
      check(fixture.keyPressed(right)&&sought==1.5,"Right arrow advances the audio-only playhead by 0.5 seconds");
      fixture.updatePlayback(object({{"ready",true},{"position",1.}}));fixture.keyPressed(right);
      check(sought==2,"Repeated arrows accumulate despite a delayed playback status");
      fixture.updatePlayback(object({{"ready",true},{"position",2.}}));fixture.keyPressed(fineLeft);
      check(std::abs(sought-1.95)<1e-9&&fixture.selected==std::set<std::int64_t>{42},"Shift+arrow seeks 50 ms without moving or deselecting notes");
      fixture.keyboardSeekTarget=-1;fixture.playback=object({{"ready",true},{"position",.01}});fixture.keyPressed(left);
      check(sought==0,"Arrow seek stops at the beginning");
      fixture.keyboardSeekTarget=-1;fixture.playback=object({{"ready",true},{"position",3.9}});fixture.keyPressed(right);
      check(sought==4,"Arrow seek stops at the end");
      fixture.busy=true;int before=calls;fixture.keyPressed(left);check(calls==before,"Arrow navigation is blocked during an analysis");fixture.busy=false;
      check(!fixture.keyPressed(juce::KeyPress(juce::KeyPress::leftKey,juce::ModifierKeys::ctrlModifier,0))&&calls==before,"Ctrl+arrow is not consumed by playhead navigation");
      juce::TextEditor field;check(!keyPressed(right,&field)&&!keyPressed(right,&model),"Text fields and combo controls retain their arrow keys");
      fixture.keyboardSeekTarget=-1;fixture.playback=object({{"ready",true},{"position",2.}});fixture.view.zoom=4;fixture.view.pan=0;fixture.keyPressed(right);
      check(sought>=fixture.start()&&sought<=fixture.start()+fixture.span(),"Keyboard navigation keeps the playhead visible when zoomed");
      auto savedSize=getBounds();auto savedSections=openSections;openSections.insert("guided");correctionOpen=true;setSize(1400,900);displayMode.setSelectedId(3,juce::sendNotificationSync);
      check(opacity.getY()==visualGain.getY()&&visualGain.getX()>opacity.getRight(),"Wide toolbar places opacity and gain on the same line");
      check(buttonMap["tempo"]->getY()==gridMode.getY()&&buttonMap["tempo"]->getX()>gridSubdivision.getRight()&&zoom.getY()==displayMode.getY(),"BPM follows both grid selectors; zoom and panels share the tools row");
      check(toolbarBounds.getHeight()==(roll.spectral?110:76),"Compact toolbar saves one row at normal width");
      {juce::FileOutputStream out(directory.getChildFile("toolbar-wide.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),out);}
      setSize(1160,900);
      check(gridMode.getY()==buttonMap["tempo"]->getY()&&visualGain.getY()==opacity.getY(),"Minimum supported width keeps BPM beside grid and both gain controls together");
      {juce::FileOutputStream out(directory.getChildFile("toolbar-minimum.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),out);}
      setSize(1100,900);
      check(!opacity.getBounds().intersects(visualGain.getBounds())&&visualGain.getRight()<=toolbarBounds.getRight()-12&&gainCaptionBounds.getRight()+6==visualGain.getX(),"Narrow toolbar wraps groups without separating slider captions");
      openSections=savedSections;setBounds(savedSize);loadUi(initialUi);
    }
    {
      Roll fixture;fixture.setSize(1100,540);fixture.displayMode=0;fixture.scoreDuration=4;fixture.view.pitchZoom=8;fixture.view.pitchPan=.5;
      Note n;n.id=1;n.pitch=60;n.start=.3;n.end=1.3;n.velocity=80;n.editable=true;fixture.notes.push_back(n);
      n.id=2;n.start=.7;n.end=1.1;n.part=2;n.editable=false;fixture.notes.push_back(n);
      n.id=3;n.start=1.3;n.end=1.6;fixture.notes.push_back(n);
      n.id=4;n.pitch=61;n.start=.3;n.end=1.3;fixture.notes.push_back(n);
      auto plain=fixture.createComponentSnapshot(fixture.getLocalBounds());fixture.refreshOverlaps();
      check(fixture.overlaps.size()==1&&fixture.overlaps[0].ids==std::set<std::int64_t>{1,2},"Overlaps: nested notes and cross-part overlap detected; touching and other pitches excluded");
      check(fixture.overlaps[0].spans==std::vector<std::pair<double,double>>{{.7,1.1}},"Overlap hatching limited to the exact shared duration");
      auto hatched=fixture.createComponentSnapshot(fixture.getLocalBounds());
      bool marked=false;auto common=juce::Rectangle<int>(int(fixture.x(.72)),int(fixture.y(60)),int(fixture.x(1.08)-fixture.x(.72)),int(fixture.row()-1));
      for(int y=common.getY();y<common.getBottom();++y)for(int x=common.getX();x<common.getRight();++x)if(plain.getPixelAt(x,y)!=hatched.getPixelAt(x,y))marked=true;
      auto outside=juce::Point<int>(int(fixture.x(.5)),int(fixture.pitchY(60)));
      check(marked&&plain.getPixelAt(outside.x,outside.y)==hatched.getPixelAt(outside.x,outside.y),"Overlap hatching visible without selection and absent outside common portion");
      var target;fixture.context=[&](juce::Point<int>,var item){target=item;};auto point=juce::Point<float>(float(fixture.x(.9)),float(fixture.pitchY(60)));
      fixture.mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier),1,0,0,0,0,&fixture,&fixture,juce::Time::getCurrentTime(),point,juce::Time::getCurrentTime(),1,false));
      check(target["kind"].toString()=="overlap"&&idSet(target["ids"])==std::set<std::int64_t>{1,2},"Right-click accesses every stacked note, including a completely hidden rectangle");
      fixture.notes[1]=fixture.notes[0];fixture.notes[1].id=2;fixture.refreshOverlaps();
      check(fixture.overlaps[0].spans==std::vector<std::pair<double,double>>{{.3,1.3}},"Exact duplicate notes are detected");
      fixture.notes.erase(fixture.notes.begin()+1);fixture.refreshOverlaps();
      check(fixture.overlaps.empty(),"Overlap cache clears after removal of duplicate");
    }
    opacity.setValue(37,juce::sendNotificationSync);
    visualGain.setValue(2.35,juce::sendNotificationSync);
    audioLayer.setSelectedId(2,juce::sendNotificationSync);
    check(roll.spectral&&opacity.getValue()==100&&visualGain.getValue()==8,"Spectrogram defaults: opacity 100%, visual gain 8");
    opacity.setValue(63,juce::sendNotificationSync);visualGain.setValue(6.4,juce::sendNotificationSync);
    auto savedSpectro=ui();
    audioLayer.setSelectedId(1,juce::sendNotificationSync);
    check(std::abs(opacity.getValue()-37)<.01&&std::abs(visualGain.getValue()-2.35)<.001,"Waveform recovers its independent opacity and gain");
    loadUi(savedSpectro);
    check(roll.spectral&&opacity.getValue()==63&&std::abs(visualGain.getValue()-6.4)<.001,"Opening saved spectrogram preserves adjusted settings");
    audioLayer.setSelectedId(1,juce::sendNotificationSync);
    check(std::abs(opacity.getValue()-37)<.01&&std::abs(visualGain.getValue()-2.35)<.001,"Saved project also preserves independent waveform settings");
    audioLayer.setSelectedId(2,juce::sendNotificationSync);
    check(opacity.getValue()==100&&visualGain.getValue()==8,"Selecting spectrogram again reapplies readable defaults");
    loadUi(initialUi);
    displayMode.setSelectedId(2,juce::sendNotificationSync);
    check(!visualGain.isVisible(),"Vue séparée : réglage du gain masqué");
    displayMode.setSelectedId(3,juce::sendNotificationSync);
    check(visualGain.isVisible(),"Superposition : réglage du gain disponible");
    displayMode.setSelectedId(2,juce::sendNotificationSync);
    for (double z : {1., 2., 8., 64., 512.})
      for (double pz : {1., 2., 4., 16., 64.})
        for (double factor : {.01, .5, 1., 2., 100.}) {
          View v;
          v.zoom = z;
          v.pan = .4;
          v.pitchZoom = pz;
          v.pitchPan = .5;
          double beforeTime = .4 * (280 - 280 / z) + .4 * 280 / z,
                 beforePitch = 127 - .5 * (128 - 128 / pz) - .5 * 128 / pz;
          v.proportional(280, factor, .4, .5);
          check(
              std::abs(v.zoom / z - v.pitchZoom / pz) < 1e-12,
              juce::String::fromUTF8("Zoom homogène : proportions identiques"));
          check(v.zoom >= 1 && v.zoom <= 512 && v.pitchZoom >= 1 &&
                    v.pitchZoom <= 64,
                "Zoom : limites conjointes");
          check(std::abs(beforeTime - (v.pan * (280 - 280 / v.zoom) +
                                       .4 * 280 / v.zoom)) < 1e-9,
                "Zoom : ancrage temporel");
          check(std::abs(beforePitch -
                         (127 - v.pitchPan * (128 - 128 / v.pitchZoom) -
                          .5 * 128 / v.pitchZoom)) < 1e-9,
                "Zoom : ancrage hauteur");
        }
    {
      Roll fixture;
      fixture.setSize(1100, 540);
      fixture.audio = object({{"duration", 2}, {"midi_only", true}});
      fixture.scoreDuration = 2;
      fixture.editable = true;
      fixture.view.pitchZoom = 8;
      fixture.view.pitchPan = .5;
      Note first;
      first.id = 1;
      first.pitch = 60;
      first.start = .3;
      first.end = 1.3;
      first.velocity = 80;
      Note second = first;
      second.id = 2;
      second.start = 1.1;
      second.end = 1.6;
      fixture.notes = {first, second};
      fixture.selectZone(1.2, 1.25, 60, 60, false, false);
      check(fixture.selected.size() == 2,
            juce::String::fromUTF8(
                "Sélection : notes traversées, même sans leurs attaques"));
      fixture.selectZone(.5, .8, 0, 127, true, false);
      check((bool)fixture.selection["temporal"] &&
                number(fixture.selection, "start") == .5 &&
                number(fixture.selection, "end") == .8,
            juce::String::fromUTF8(
                "Plage temporelle : bornes exactes conservées"));
      auto event = [&](juce::Point<float> p, juce::Point<float> down,
                       bool dragged) {
        return juce::MouseEvent(
            juce::Desktop::getInstance().getMainMouseSource(), p,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0,
            0, 0, &fixture, &fixture, juce::Time::getCurrentTime(), down,
            juce::Time::getCurrentTime(), 1, dragged);
      };
      Note changed;
      bool committed = false;
      int auditions = 0;
      fixture.modify = [&](const Note &n) {
        changed = n;
        committed = true;
      };
      fixture.beginEdit = [] {};
      fixture.audition = [&](const Note &) { ++auditions; };
      auto r = fixture.rect(first);
      juce::Point<float> down((float)fixture.x(.7), r.getCentreY());
      juce::Point<float> to =
          down + juce::Point<float>(10, (float)fixture.row() / 2);
      fixture.mouseDown(event(down, down, false));
      fixture.mouseDrag(event(to, down, true));
      fixture.mouseUp(event(to, down, true));
      check(committed && changed.pitch == 60,
            juce::String::fromUTF8(
                "Déplacement : arrondi du demi-pas identique à JavaScript"));
      check(
          auditions == 0,
          juce::String::fromUTF8(
              "Déplacement temporel : pas de réattaque de préécoute inutile"));
      committed = false;
      down = {r.getCentreX(), r.getY() + 1};
      to = down + juce::Point<float>(0, 5);
      fixture.mouseDown(event(down, down, false));
      fixture.mouseDrag(event(to, down, true));
      fixture.mouseUp(event(to, down, true));
      check(committed && changed.velocity == 78,
            juce::String::fromUTF8(
                "Vélocité : poignée et arrondi négatif identiques"));
      fixture.selectIds({1,2});
      bool groupCommitted=false; double groupDelta=0; int groupTranspose=0;
      fixture.moveGroup=[&](const auto& ids,double delta,int semitones){
        groupCommitted=ids==std::set<std::int64_t>{1,2};groupDelta=delta;groupTranspose=semitones;
      };
      down={(float)fixture.x(.7),r.getCentreY()};
      to=down+juce::Point<float>(-2000,-(float)fixture.row()*2);
      fixture.mouseMove(event(down,down,false));
      check(fixture.getMouseCursor()==juce::MouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor),"Groupe : curseur quatre flèches au survol du corps");
      fixture.mouseDown(event(down,down,false));fixture.mouseDrag(event(to,down,true));
      check(fixture.getMouseCursor()==juce::MouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor),"Groupe : curseur conservé pendant le déplacement");
      fixture.mouseUp(event(to,down,true));
      auto edge=juce::Point<float>(r.getX()+1,r.getCentreY());fixture.mouseMove(event(edge,edge,false));
      check(fixture.getMouseCursor()==juce::MouseCursor(juce::MouseCursor::LeftRightResizeCursor),"Groupe : curseur de durée préservé au bord");
      check(groupCommitted && std::abs(groupDelta+.3)<1e-6 && groupTranspose==2 && fixture.selected.size()==2,
        String::fromUTF8("Groupe : déplacement commun, début borné à zéro, transposition et sélection conservées"));
      fixture.displayMode=0; auto midiLayout=fixture.layout();
      fixture.displayMode=1; auto splitLayout=fixture.layout();
      fixture.displayMode=2; auto overlayLayout=fixture.layout();
      check(midiLayout.audio==0 && overlayLayout.audio==0 && splitLayout.audio>0 && midiLayout.roll>splitLayout.roll,
        String::fromUTF8("Vues : espace audio rendu au roll en MIDI et superposition"));
      check(std::abs(fixture.timeAt((float)fixture.x(1.2))-1.2)<1e-6,
        "Superposition : coordonnées temporelles partagées");
      fixture.waveReferenceZoom=fixture.view.pitchZoom;fixture.waveCalibrated=true;fixture.visualGain=4.9;
      double ratio=fixture.waveformScale()/fixture.row();
      for(double zoom:{1.,4.,16.,64.}) {
        fixture.view.pitchZoom=zoom;
        check(std::abs(fixture.waveformScale()/fixture.row()-ratio)<1e-8,
          String::fromUTF8("Signal superposé : proportion signal/notes conservée au zoom et dézoom"));
      }
      fixture.tool = 1;
      fixture.tool=0;fixture.view.pitchZoom=8;fixture.view.pitchPan=.5;fixture.view.zoom=1;fixture.view.pan=0;
      fixture.cutHeld=true;double cut=-1;std::int64_t cutId=0;
      fixture.split=[&](std::int64_t id,double t){cutId=id;cut=t;};
      down={(float)fixture.x(.7),fixture.rect(first).getCentreY()};
      fixture.mouseDown(event(down,down,false));fixture.mouseUp(event(down,down,false));
      check(cutId==1 && std::abs(cut-.7)<1e-6,"C + clic : coupure au temps du clic");
      fixture.cutHeld=false;fixture.tool=3;cut=-1;
      fixture.mouseDown(event(down,down,false));fixture.mouseUp(event(down,down,false));
      check(cutId==1 && std::abs(cut-.7)<1e-6,"Scissors tool splits at click without holding C");
      fixture.tool=0;fixture.cutHeld=true;
      cut=-1;down={(float)fixture.x(.301),fixture.rect(first).getCentreY()};
      fixture.mouseDown(event(down,down,false));check(cut==-1,"C + clic : pas de fragment minuscule au bord");
      fixture.cutHeld=false;fixture.spectral=true;fixture.bandEnabled=true;fixture.bandLow=58;fixture.bandHigh=64;
      bool bandChanged=false;fixture.bandChanged=[&]{bandChanged=true;};
      down={(float)fixture.x(.7),(float)fixture.bandY(true)};to=down+juce::Point<float>(0,(float)fixture.row()*2);
      fixture.mouseDown(event(down,down,false));fixture.mouseDrag(event(to,down,true));fixture.mouseUp(event(to,down,true));
      check(bandChanged && fixture.bandHigh==62 && fixture.bandLow==58,"Registre : borne haute déplaçable, snap au demi-ton");
      fixture.bandEnabled=false;fixture.tool=1;
      double seeked=-1;fixture.seek=[&](double t){seeked=t;};
      for(int mode=0;mode<3;++mode) {
        fixture.displayMode=mode;fixture.view.pan=.6;fixture.view.zoom=4;
        double oldPitchPan=fixture.view.pitchPan,oldPitchZoom=fixture.view.pitchZoom;
        auto point=juce::Point<float>(10,(float)fixture.layout().dividerAudio);
        fixture.mouseDoubleClick(event(point,point,false));
        check(seeked==0 && fixture.view.pan==0 && fixture.view.zoom==4 && fixture.view.pitchZoom==oldPitchZoom && fixture.view.pitchPan==oldPitchPan,
          String::fromUTF8("Double-clic marge gauche : début dans les trois vues, zoom conservé"));
      }
      fixture.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
      check(fixture.tool == 0 && fixture.selected.empty(),
            juce::String::fromUTF8(
                "Échap : quitter l’insertion et effacer la sélection"));
      check(std::abs(decimal(var("12,345")) - 12.345) < 1e-9 &&
                std::abs(decimal(var(".5")) - .5) < 1e-9 &&
                std::isnan(decimal(var("1.2.3"))),
            juce::String::fromUTF8("Saisies décimales : virgule, point et "
                                   "rejet des valeurs incomplètes"));
    }
    {
      Roll fixture;fixture.setSize(1100,540);fixture.displayMode=1;
      fixture.audio=object({{"duration",2.},{"peaks",array({array({-.8,.8}),array({-1.,1.})})}});
      fixture.visualGain=1;auto normal=fixture.createComponentSnapshot(fixture.getLocalBounds());
      fixture.visualGain=8;auto amplified=fixture.createComponentSnapshot(fixture.getLocalBounds());
      auto layout=fixture.layout();bool contained=true;
      for(int py=0;py<fixture.getHeight();++py)
        for(int px=0;px<fixture.getWidth();++px) if(normal.getPixelAt(px,py)!=amplified.getPixelAt(px,py))contained=false;
      check(contained,"Vue séparée : gain de superposition sans effet sur le signal, règle et MIDI inchangés");
      juce::FileOutputStream stream(directory.getChildFile("waveform-gain8.png"));
      juce::PNGImageFormat().writeImageToStream(amplified,stream);
      fixture.editable=true;fixture.scoreDuration=8;fixture.view.zoom=2;fixture.view.pan=0;
      fixture.audio.getDynamicObject()->setProperty("peaks",array({array({0.,0.})}));
      auto silent=fixture.createComponentSnapshot(fixture.getLocalBounds());
      fixture.setDetail(array({array({-.8,.8}),array({-.8,.8})}),0,2);
      auto clipped=fixture.createComponentSnapshot(fixture.getLocalBounds());bool visible=false,tailClean=true;
      for(int px=59;px<(int)(58+fixture.layout().width);++px)for(int py=35;py<(int)(31+fixture.layout().audio);++py){
        bool differs=silent.getPixelAt(px,py)!=clipped.getPixelAt(px,py);
        if(px<fixture.x(2)-2)visible|=differs;
        if(px>fixture.x(2)+2)tailClean&=!differs;
      }
      check(visible&&tailClean,"Clipped waveform remains precise and leaves the MIDI extension empty");
    }
    {
      Roll fixture;
      fixture.setSize(1100,540);fixture.editable=true;fixture.displayMode=0;
      fixture.scoreDuration=4;fixture.view.pitchZoom=8;fixture.view.pitchPan=.5;
      Note n;n.id=1;n.pitch=60;n.start=.3;n.end=1.3;n.velocity=80;
      fixture.notes.push_back(n);n.id=2;n.start=2;n.end=3;fixture.notes.push_back(n);
      auto beforeFocus=fixture.createComponentSnapshot(fixture.getLocalBounds());
      fixture.focused={1};
      auto afterFocus=fixture.createComponentSnapshot(fixture.getLocalBounds());
      auto noteRect=fixture.rect(fixture.notes[0]);
      auto centre=noteRect.getCentre().toInt();
      check(beforeFocus.getPixelAt(centre.x,centre.y)==afterFocus.getPixelAt(centre.x,centre.y),
        "Diagnostic : le repérage conserve la couleur intérieure de la voix");
      bool outlineChanged=false;
      for(int y=(int)noteRect.getY()-3;y<=(int)noteRect.getBottom()+3;++y)
        for(int x=(int)noteRect.getX()-3;x<=(int)noteRect.getRight()+3;++x)
          if(x>=0&&y>=0&&x<fixture.getWidth()&&y<fixture.getHeight()
            &&beforeFocus.getPixelAt(x,y)!=afterFocus.getPixelAt(x,y))outlineChanged=true;
      check(outlineChanged,"Diagnostic : contour distinct visible autour de la note repérée");
      fixture.focused.clear();
      fixture.selected={2};fixture.extra.insert(1);
      var target;
      fixture.context=[&](juce::Point<int>,var value){target=value;};
      auto rightClick=[&](double time) {
        juce::Point<float> point((float)fixture.x(time),fixture.rect(fixture.notes[0]).getCentreY());
        fixture.mouseDown(juce::MouseEvent(
          juce::Desktop::getInstance().getMainMouseSource(),point,
          juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier),1,0,0,0,0,
          &fixture,&fixture,juce::Time::getCurrentTime(),point,juce::Time::getCurrentTime(),1,false));
      };
      rightClick(.7);
      check(target["kind"].toString()=="extra" && fixture.selected==std::set<std::int64_t>{1},
        "Clic droit : note en trop visée malgré une ancienne sélection différente");
      fixture.ghosts={object({{"id","ghost"},{"pitch",60},{"start",.4},{"end",.8}})};
      rightClick(.6);
      check(target["kind"].toString()=="missing","Clic droit : actions accessibles sur la note fantôme");
      fixture.ghosts.clear();
      fixture.holds={object({{"id","hold"},{"pitch",60},{"start",.3},{"end",3.},{"ids",array({1,2})}})};
      rightClick(.6);
      check(target["kind"].toString()=="hold" && fixture.selected==std::set<std::int64_t>{1,2},
        "Clic droit : tous les fragments sélectionnés pour écoute, mute et fusion");
      fixture.holds.clear();
      fixture.timing={object({{"id","timing-split"},{"kind","split"},{"note_id",1},{"pitch",60},{"start",.3},{"end",1.3},{"at",.7},{"proposed_end",1.3}})};
      rightClick(.7);
      check(target["kind"].toString()=="split" && fixture.selected==std::set<std::int64_t>{1},
        "Clic droit : suggestion de scission cible la note source");
      fixture.timing={object({{"id","timing-extend"},{"kind","extend"},{"note_id",1},{"pitch",60},{"start",.3},{"end",1.3},{"at",1.3},{"proposed_end",1.6}})};
      rightClick(1.5);
      check(target["kind"].toString()=="extend" && fixture.selected==std::set<std::int64_t>{1},
        "Clic droit : prolongation proposée accessible sur la fin fantôme");
      check(Roll::noteLabel(36).contains(localisation::language()==localisation::Language::english?"C2":"Do2 / C2") && Roll::noteLabel(74).contains("D5"),
        "Registre : notes nommées avec octave, sans numéro MIDI à saisir");
    }
    const unsigned char data[] = {
        0x4d, 0x54, 0x68, 0x64, 0,    0,    0,    6,    0,    0,  0,
        1,    1,    0xe0, 0x4d, 0x54, 0x72, 0x6b, 0,    0,    0,  30,
        0,    0xff, 0x51, 3,    7,    0xa1, 0x20, 0,    0x90, 60, 100,
        0x81, 0x70, 0x90, 60,   80,   0x81, 0x70, 0x80, 60,   0,  0x81,
        0x70, 0x80, 60,   0,    0,    0xff, 0x2f, 0};
    auto midi = directory.getChildFile("source.mid");
    midi.replaceWithData(data, sizeof(data));
    auto pair = call(
        "import_midi_audio",
        object({{"midiPath", midi.getFullPathName()}, {"audioPath", var()}}));
    audio = pair["audio"];
    roll.audio = audio;
    result = pair["result"];
    mode = 1;
    roll.scoreDuration = number(result, "duration");
    roll.editable = true;
    {
      Roll fixture;fixture.setSize(900,500);fixture.scoreDuration=4;fixture.displayMode=0;fixture.view.pitchZoom=8;fixture.view.pitchPan=.5;fixture.editable=true;
      Note gray;gray.id=1;gray.part=256;gray.pitch=60;gray.start=1;gray.end=2;gray.velocity=100;gray.editable=false;
      Note blue=gray;blue.id=2;blue.part=257;blue.pitch=64;blue.editable=true;fixture.notes={gray,blue};fixture.lockedParts={256};
      auto image=fixture.createComponentSnapshot(fixture.getLocalBounds());auto r=fixture.rect(gray);auto pixel=image.getPixelAt(int(r.getX()+5),int(r.getCentreY()));
      check(std::abs(int(pixel.getRed())-int(pixel.getGreen()))<18&&std::abs(int(pixel.getGreen())-int(pixel.getBlue()))<18,"Locked voice fill is gray");
      auto s=fixture.rect(blue);auto active=image.getPixelAt(int(s.getX()+5),int(s.getCentreY()));check(active.getRed()!=active.getBlue(),"Editable voice retains its color");
      {juce::FileOutputStream out(directory.getChildFile("voice-lock-colors.png"));juce::PNGImageFormat().writeImageToStream(image,out);}

      fixture.tonal={1,2};fixture.selected={2};auto marked=fixture.createComponentSnapshot(fixture.getLocalBounds());
      for(const auto& note:fixture.notes){auto box=fixture.rect(note);bool magenta=false;for(int y=int(box.getY());y<int(box.getBottom());++y)for(int x=int(box.getX());x<std::min(int(box.getRight()),int(box.getX())+4);++x){auto c=marked.getPixelAt(x,y);magenta|=c.getRed()>220&&c.getBlue()>120&&c.getGreen()<75;}check(magenta,"Dedicated magenta outline remains visible on selected and locked notes");}
      {juce::FileOutputStream out(directory.getChildFile("magenta-outline.png"));juce::PNGImageFormat().writeImageToStream(marked,out);}
      fixture.tool=4;fixture.eraserDiameter=512;fixture.eraserPrevious=r.getCentre();fixture.eraseTo(s.getCentre());
      check(fixture.erasedIds==std::set<std::int64_t>{2},"Large eraser crossing both voices touches only editable notes");
    }
    auto original = items(result["editing"]["notes"]);
    check(
        original.size() == 2,
        juce::String::fromUTF8("Import MIDI : deux occurrences indépendantes"));
    check(std::abs(number(original[0], "end_time_s") - .5) < 1e-6 &&
              std::abs(number(original[1], "end_time_s") - .75) < 1e-6,
          juce::String::fromUTF8("Fins indépendantes de même hauteur"));
    {
      auto savedAudio=audio;auto before=result["editing"]["notes"].clone();
      auto with=[](var value,const String& key,var entry){value=value.clone();value.getDynamicObject()->setProperty(key,entry);return value;};
      auto edit=[&](var action){auto request=args();request.getDynamicObject()->setProperty("action",action);result=call("edit_notes",request);};
      edit(object({{"kind","set_instrument"},{"part",Note::parse(before[0]).part},{"program",52}}));
      auto preview=call("preview_midi_addition",with(args(),"path",midi.getFullPathName()));std::vector<var> mappings;
      for(const auto& source:items(preview["parts"]))mappings.push_back(object({{"source",source["source"]},{"target",var()},{"program",52},{"name","Second voice"}}));
      auto request=with(with(with(args(),"token",preview["token"]),"offset",2.25),"mappings",array(mappings));
      result=call("apply_midi_addition",request);audio=var();updateResult(result,false);
      check(parts.size()==2&&parts[0].first!=parts[1].first,"MIDI addition after instrument change gives each menu entry a unique ID");
      for(const auto& entry:parts){part.setSelectedId(entry.first+2,juce::sendNotificationSync);
        check(partId==entry.first&&roll.notes.size()==4,"Selecting a voice preserves visibility of other voices");
        others.onClick();check(hiddenParts.count(entry.first)&&roll.notes.size()==2&&std::none_of(roll.notes.begin(),roll.notes.end(),[&](const Note& n){return n.part==entry.first;}),"Eye hides the active voice only");
        auto other=parts[0].first==entry.first?parts[1].first:parts[0].first;selectPart(other);check(hiddenParts.count(entry.first)&&roll.notes.size()==2,"Switching voices retains independent visibility");
        selectPart(entry.first);others.onClick();check(roll.notes.size()==4&&!hiddenParts.count(entry.first),"Eye restores the selected hidden voice");}
      selectPart(-1);auto beforeVisibility=juce::JSON::toString(result["editing"]);others.onClick();check(roll.notes.empty()&&hiddenParts.size()==2,"Global eye hides all voices");
      auto visibility=ui();hiddenParts.clear();loadUi(visibility);rebuildNotes();check(roll.notes.empty()&&hiddenParts.size()==2,"Project UI restores each voice visibility");
      others.onClick();check(roll.notes.size()==4&&hiddenParts.empty()&&beforeVisibility==juce::JSON::toString(result["editing"]),"Global eye restores voices without changing notes, mute or history");
      audio=savedAudio;
      auto firstKey=parts[0].first,secondKey=parts[1].first;
      edit(object({{"kind","toggle_part_lock"},{"part",firstKey}}));
      audio=var();updateResult(result,false);selectPart(secondKey);
      check(roll.notes.size()==4&&std::count_if(roll.notes.begin(),roll.notes.end(),[](auto& n){return n.editable;})==2,"Locked voice stays visible as context while only the other voice is editable");
      std::set<std::int64_t> all;for(auto& n:roll.notes)all.insert(n.id);roll.selectIds(all);
      check(roll.selected.size()==2&&roll.lockedParts.count(firstKey),"Selections exclude every locked note");
      {juce::FileOutputStream image(directory.getChildFile("voice-locks.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);}
      {
        auto oldTone=tone.clone(),oldReport=toneReport.clone();bool oldOpen=tonalityOpen,oldExpanded=toneExpanded;
        partId=-1;hiddenParts.clear();rebuildNotes();audio=savedAudio;
        auto scale=object({{"tonic",2},{"mode","major"}});tone=object({{"enabled",true},{"scope","all"},{"scale",scale}});
        toneReport=call("tonal_analysis",with(with(args(),"ids",var()),"scale",scale));roll.tonal=idSet(toneReport["out_ids"]);tonalityOpen=true;toneOptions();
        auto groups=toneVoices();check(groups.size()==2&&groups[0].key==firstKey&&groups[1].key==secondKey&&groups[0].notes.size()==2&&groups[1].notes.size()==2,"Global out-of-scale diagnostic groups all notes by stable voice identity");
        check(toneCaption.getText().contains(groups[0].name+" : 2")&&toneCaption.getText().contains(groups[1].name+" : 2"),"Global tonality caption names both voices and their counts");
        std::unique_ptr<Form> form(toneDetailsForm());form->setSize(540,720);
        std::function<SidePanel*(juce::Component*)> findPanel=[&](juce::Component* c)->SidePanel*{if(auto* p=dynamic_cast<SidePanel*>(c))return p;for(auto* child:c->getChildren())if(auto* p=findPanel(child))return p;return nullptr;};
        auto* panel=findPanel(form.get());check(panel!=nullptr,"Voice details contain a scrollable grouped note list");
        int rows=0,selectors=0;SidePanel::DiagnosticRow* lockedRow=nullptr;juce::TextButton* selectVoice=nullptr;
        for(auto* child:panel->widgets){if(auto* row=dynamic_cast<SidePanel::DiagnosticRow*>(child)){if(!lockedRow)lockedRow=row;++rows;check(row->detail.contains(" ms")&&row->detail.contains(" s"),"Each out-of-scale note reports its time and duration");}
          if(auto* button=dynamic_cast<juce::TextButton*>(child)){++selectors;if(button->isEnabled())selectVoice=button;}}
        check(rows==4&&selectors==2&&selectVoice,"Every affected voice has its own selection action; locked voice is disabled");
        selectVoice->onClick();check(roll.selected.size()==2&&std::none_of(groups[0].notes.begin(),groups[0].notes.end(),[&](auto& n){return roll.selected.count(n.id);}),"Per-voice selection excludes locked voice notes");
        audio=var();lockedRow->onClick();check(roll.selected.empty()&&roll.focused==std::set<std::int64_t>{groups[0].notes.front().id},"Clicking a locked diagnostic note locates it without selecting it for editing");
        {juce::FileOutputStream image(directory.getChildFile("out-of-scale-voices.png"));juce::PNGImageFormat().writeImageToStream(form->createComponentSnapshot(form->getLocalBounds()),image);}
        audio=savedAudio;partId=secondKey;rebuildNotes();toneReport=call("tonal_analysis",with(with(args(),"ids",var()),"scale",scale));toneOptions();
        check(toneVoices().size()==1&&toneVoices().front().key==secondKey&&toneCaption.getText().contains(groups[1].name),"Single-voice diagnostic names the voice and only counts its notes");
        partId=-1;rebuildNotes();auto chosen=std::set<std::int64_t>{groups[0].notes[0].id,groups[1].notes[0].id};tone.getDynamicObject()->setProperty("scope","selection");
        toneReport=call("tonal_analysis",with(with(args(),"ids",idsVar(chosen)),"scale",scale));auto selectedGroups=toneVoices();
        check(selectedGroups.size()==2&&selectedGroups[0].notes.size()==1&&selectedGroups[1].notes.size()==1,"Per-voice counts respect the original selected-note scope");
        auto cleanScale=object({{"tonic",0},{"mode","chromatic"}});toneReport=call("tonal_analysis",with(with(args(),"ids",var()),"scale",cleanScale));check(toneVoices().empty(),"No out-of-scale notes produces an empty detail list");
        auto realResult=result,realReport=toneReport;result=result.clone();std::vector<var> many,ids;
        for(int i=0;i<60;++i){auto n=result["editing"]["notes"][0].clone();n.getDynamicObject()->setProperty("id",2001+i);n.getDynamicObject()->setProperty("part",result["editing"]["notes"][i<30?0:2]["part"]);many.push_back(n);ids.push_back(2001+i);}
        result["editing"].getDynamicObject()->setProperty("notes",array(many));toneReport=object({{"out_ids",array(ids)}});
        auto countRows=[&](Form* f){int count=0;auto* p=findPanel(f);for(auto* child:p->widgets)if(dynamic_cast<SidePanel::DiagnosticRow*>(child))++count;return count;};
        {std::unique_ptr<Form> first(toneDetailsForm()),next(toneDetailsForm(1));check(countRows(first.get())==50&&countRows(next.get())==10,"Large voice details are paginated without losing notes between voices");}
        result=realResult;toneReport=realReport;
        ++toneVersion;tone=oldTone;toneReport=oldReport;tonalityOpen=oldOpen;toneExpanded=oldExpanded;roll.tonal.clear();roll.focused.clear();roll.clearSelection();partId=secondKey;rebuildNotes();toneOptions();
      }
      audio=savedAudio;auto prior=result["editing"].clone();auto blocked=with(args(),"action",object({{"kind","delete"},{"ids",idsVar(all)}}));
      check(!(bool)bridge.call("edit_notes",blocked)["ok"],"Mixed bulk deletion refuses locked voices atomically");
      check(equalJson(call("select_notes",args())["editing"]["notes"],prior["notes"]),"Refused deletion preserves every voice");
      call("set_project_tempo",object({{"audioId",audio["id"]},{"bpm",65}}));
      auto validateExport=[&](const juce::File& file,int key){phthongos::midi::Source source;juce::MemoryBlock bytes;file.loadFileAsData(bytes);auto* data=static_cast<const std::uint8_t*>(bytes.getData());source.bytes.assign(data,data+bytes.getSize());
        auto parsed=source.parse({});std::vector<Note> expected;for(auto& value:items(result["editing"]["notes"])){auto n=Note::parse(value);if(n.part==key)expected.push_back(n);}
        check(parsed.notes.size()==expected.size(),"Separate MIDI contains exactly its voice, including locked notes");
        for(size_t i=0;i<expected.size();++i)check(std::abs(parsed.notes[i].start-expected[i].start)<.003&&std::abs(parsed.notes[i].end-expected[i].end)<.003&&parsed.notes[i].pitch==expected[i].pitch&&parsed.notes[i].velocity==expected[i].velocity&&parsed.notes[i].part->program==expected[i].program,"Voice export preserves absolute timing, pitch, velocity and instrument");
        check(!parsed.clock.points.empty()&&std::abs(parsed.clock.points.back().step*parsed.division-60./65)<.00001,"Separate voice export declares 65 BPM");};
      auto one=directory.getChildFile("one-voice.mid");call("export_midi",with(with(args(),"path",one.getFullPathName()),"part",firstKey));validateExport(one,firstKey);
      auto suggestedBefore=call("suggest_export_path",with(args(),"stamp","same"));auto transient=directory.getChildFile("temporary-drag");transient.createDirectory();
      call("export_midi",with(with(with(args(),"path",transient.getChildFile("all-voices.mid").getFullPathName()),"part",var()),"transient",true));
      check(call("suggest_export_path",with(args(),"stamp","same"))==suggestedBefore,"A drag export never replaces the normal MIDI export destination with a temporary folder");
      auto dragFolder=transient.getChildFile("separate-drag");
      auto dragReceipt=call("export_midi_voices",with(with(args(),"path",dragFolder.getFullPathName()),"transient",true));
      check(items(dragReceipt["files"]).size()==2&&call("suggest_export_path",with(args(),"stamp","same"))==suggestedBefore,"Multi-file drag keeps separate voices and never changes the normal export destination");
      for(const auto& file:items(dragReceipt["files"]))validateExport(juce::File(file["path"].toString()),(int)file["part"]);
      auto folder=directory.getChildFile("separate-voices");auto receipt=call("export_midi_voices",with(args(),"path",folder.getFullPathName()));
      check(items(receipt["files"]).size()==2&&(int)receipt["notes"]==4,"Batch MIDI exports every voice to a separate file");
      for(const auto& file:items(receipt["files"]))validateExport(juce::File(file["path"].toString()),(int)file["part"]);
      check(!(bool)bridge.call("export_midi_voices",with(args(),"path",folder.getFullPathName()))["ok"],"Batch MIDI export refuses replacing an existing folder");
      auto oldName=projectName;auto oldPath=projectPath;auto oldDirty=dirty;
      projectName={};projectPath={};auto initialName=juce::File(audio["name"].toString()).getFileNameWithoutExtension();
      check(projectDisplayName()==(projectSaveBase.isNotEmpty()?projectSaveBase:initialName),"A project without an explicit name falls back to its existing file or source name");
      projectName=String::fromUTF8("Étude du soir");projectPath=directory.getChildFile("existing-file.pht").getFullPathName();dirty=false;
      {std::unique_ptr<Form> rename(projectNameForm());
       {juce::FileOutputStream image(directory.getChildFile("rename-project.png"));juce::PNGImageFormat().writeImageToStream(rename->createComponentSnapshot(rename->getLocalBounds()),image);}
       auto draft=rename->state().clone();draft.getDynamicObject()->setProperty("name","CON");rename->onApply(draft);
       check(projectName==String::fromUTF8("Étude du soir")&&!dirty,"Renaming rejects Windows reserved filenames without changing the project");
       draft.getDynamicObject()->setProperty("name",String::fromUTF8("  Étude finale  "));auto frozen=result["editing"].clone();rename->onApply(draft);
       check(projectName==String::fromUTF8("Étude finale")&&dirty&&projectPath==directory.getChildFile("existing-file.pht").getFullPathName()&&equalJson(frozen,result["editing"]),"Renaming trims the title, marks the project modified, and preserves its saved path and every note");}
      check(title.getText()==String::fromUTF8("Étude finale *"),"The window title displays the chosen name and unsaved marker");
      check(juce::File(call("suggest_export_path",with(args(),"name",midiExportName())).toString()).getFileName()==String::fromUTF8("Étude finale.mid"),"Click export suggests the exact project name without a timestamp");
      auto named=call("export_midi_voices",with(with(args(),"path",directory.getChildFile("named-voices").getFullPathName()),"name",midiExportName()));
      for(auto file:items(named["files"]))check(juce::File(file["path"].toString()).getFileName().startsWith(String::fromUTF8("Étude finale - ")),"Every separate voice file carries the project name and its voice label");
      auto savedLocks=directory.getChildFile("locked-voices.pht");call("save_project",with(with(args(),"path",savedLocks.getFullPathName()),"ui",ui()));
      {Bridge reopen(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("runtime"),directory.getChildFile("lock-reopen-cache"));auto reply=reopen.call("open_project",object({{"path",savedLocks.getFullPathName()}}));
        check(reply["value"]["ui"]["projectName"].toString()==String::fromUTF8("Étude finale"),"PHT8 preserves the custom project name independently of its filename");
        check((bool)reply["ok"]&&idSet(reply["value"]["result"]["editing"]["locked_parts"])==std::set<std::int64_t>{firstKey}&&equalJson(reply["value"]["result"]["editing"]["notes"],result["editing"]["notes"]),"PHT8 reopens voice locks without changing any notes");}
      projectName=oldName;projectPath=oldPath;dirty=oldDirty;
      edit(object({{"kind","toggle_part_mute"},{"part",secondKey}}));
      auto audible=call("export_midi_voices",with(args(),"path",directory.getChildFile("audible-voices").getFullPathName()));
      check(items(audible["files"]).size()==1&&(int)audible["files"][0]["part"]==firstKey,"Batch MIDI excludes muted voices while retaining locked voices");
      {auto request=with(args(),"part",secondKey);auto tempo=call("estimate_notes_tempo",request);check(number(tempo,"attacks")>0&&int(tempo["part"])==secondKey,"Imported MIDI tempo estimation includes a selected voice even when playback-muted");
       std::vector<var> voiceIds;for(auto n:items(result["editing"]["notes"]))if(Note::parse(n).part==secondKey)voiceIds.push_back(n["id"]);
       edit(object({{"kind","toggle_mute"},{"ids",array(voiceIds)}}));auto silent=call("estimate_notes_tempo",with(args(),"part",secondKey));check(number(silent,"bpm")==0&&number(silent,"attacks")==0&&silent["reason"].toString()=="few_attacks","Individually excluded MIDI notes produce factual insufficient-data feedback");edit(object({{"kind","undo"}}));
      }
      edit(object({{"kind","undo"}}));
      std::unique_ptr<Form> exportForm(midiExportForm());check(exportForm->comboFields["scope"]->getNumItems()==2&&exportForm->comboFields["scope"]->getSelectedId()==1,"Multi-voice MIDI export defaults to separate files and offers selected voice without a merged option");
      call("set_project_tempo",object({{"audioId",audio["id"]},{"bpm",0}}));
      edit(object({{"kind","undo"}})); // unlock by undoing the lock transaction
      audio=savedAudio;edit(object({{"kind","undo"}}));edit(object({{"kind","undo"}}));
      audio=var();partId=-1;hiddenParts.clear();roll.clearSelection();updateResult(result,false);audio=savedAudio;
      check(equalJson(before,result["editing"]["notes"]),"Undo of MIDI addition and instrument change restores every original note");
    }
    {
      auto savedUi=ui();auto savedSections=openSections;auto savedAudio=audio;
      rebuildNotes();openSections={"overlaps"};buildSide();
      SidePanel::DiagnosticRow* row=nullptr;int rows=0;
      for(auto* child:side.widgets)if(auto* entry=dynamic_cast<SidePanel::DiagnosticRow*>(child)){row=entry;++rows;}
      check(rows==1&&row,"Overlap diagnostic is reachable as a clickable list without acoustic analysis");
      auto before=result["editing"].clone();audio=var();row->onClick();audio=savedAudio;
      check(roll.selected==std::set<std::int64_t>{(juce::int64)original[0]["id"],(juce::int64)original[1]["id"]}&&equalJson(before,result["editing"]),"Clicking an overlap locates and selects its notes without editing the document");
      rebuildSide();
      {juce::FileOutputStream imageFile(directory.getChildFile("overlap-panel.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),imageFile);}
      auto request=args();request.getDynamicObject()->setProperty("action",object({{"kind","delete"},{"ids",array({original[1]["id"]})}}));result=call("edit_notes",request);rebuildNotes();
      check(items(result["editing"]["notes"]).size()==1&&result["editing"]["notes"][0]["id"]==original[0]["id"]&&roll.overlaps.empty(),"Deleting one overlapping note preserves the other and clears the diagnostic");
      request=args();request.getDynamicObject()->setProperty("action",object({{"kind","undo"}}));result=call("edit_notes",request);rebuildNotes();
      check(equalJson(result["editing"]["notes"],before["notes"])&&roll.overlaps.size()==1,"Ctrl+Z restores the deleted overlap and its diagnostic");
      openSections=savedSections;loadUi(savedUi);diagnosticActive.clear();roll.focused.clear();
    }
    {
      auto readerArgs=args();
      readerArgs.getDynamicObject()->setProperty("ids",var());
      readerArgs.getDynamicObject()->setProperty("part",var());
      readerArgs.getDynamicObject()->setProperty("scale",object({{"tonic",0},{"mode","major"}}));
      readerArgs.getDynamicObject()->setProperty("thresholdMs",500.);
      auto tonal=call("tonal_analysis",readerArgs);
      check(tonal["examined_ids"].size()==2 && tonal["out_ids"].size()==0,
        "Service C++ tonal : les deux occurrences du MIDI sont examinées sans correction");
      auto durations=call("short_note_analysis",readerArgs);
      check(durations["examined_ids"].size()==2 && durations["notes"].size()==2,
        "Service C++ durée : seuil inclusif appliqué aux occurrences finales");
      readerArgs.getDynamicObject()->setProperty("revision",(juce::int64)result["editing"]["revision"]+1);
      check(!(bool)bridge.call("tonal_analysis",readerArgs)["ok"] && !(bool)bridge.call("short_note_analysis",readerArgs)["ok"],
        "Services C++ : une révision périmée est refusée");
      auto after=call("select_notes",args());
      check(equalJson(after["editing"],result["editing"]),
        "Services C++ : notes, mutes, protections et historique inchangés après lecture");
    }
    roll.displayMode=2;roll.spectral=true;roll.overlayOpacity=.5f;roll.visualGain=1.5;
    spectralPalette=1;spectralStyle=1;spectralContrast=63;
    roll.bandEnabled=true;roll.bandLow=52;roll.bandHigh=76;saveSpectralBand();
    partId=256;roll.bandLow=60;roll.bandHigh=80;saveSpectralBand();
    partId=-1;loadSpectralBand();
    check(roll.bandLow==52 && roll.bandHigh==76,"Registre : plages indépendantes par partie");
    spectrumData=object({{"columns",1},{"rows",3},{"values",array({0,128,255})}});
    std::set<juce::uint32> palettePeaks;
    for(int palette=0;palette<3;++palette) {
      spectralPalette=palette;recolourSpectrum();
      check(roll.spectrum.isValid() && roll.spectrum.getPixelAt(0,0).getAlpha()==0,"Palette : silence transparent");
      palettePeaks.insert(roll.spectrum.getPixelAt(0,2).getARGB());
    }
    check(palettePeaks.size()==3,"Palettes : Inferno, Magma et Viridis distinctes");
    spectralPalette=1;
    {
      auto before=result["editing"].clone();
      gridMode.setSelectedId(2,juce::sendNotificationSync);gridSubdivision.setSelectedId(5,juce::sendNotificationSync);
      check(roll.rhythmicGrid.mode==1&&!gridSubdivision.isEnabled(),"Seconds mode disables rhythmic subdivision without forgetting its selection");
      gridMode.setSelectedId(1,juce::sendNotificationSync);
      check(roll.rhythmicGrid.subdivision==4&&gridSubdivision.isEnabled(),"Returning to musical mode restores the selected subdivision");
      gridMode.setSelectedId(3,juce::sendNotificationSync);
      check(roll.gridLines().empty(),"None mode also reaches the actual piano roll");
      gridMode.setSelectedId(2,juce::sendNotificationSync);
      check(equalJson(before,call("select_notes",args())["editing"]),"Switching grid modes leaves note data and editing history unchanged");
      check(!gridMode.getBounds().intersects(buttonMap["guidedOptions"]->getBounds())&&!gridMode.getBounds().intersects(buttonMap["tempo"]->getBounds()),"Grid selectors do not overlap spectral or tempo controls");
    }
    {
      auto oldMapping=dynMapping.clone();bool oldDraft=hasDynDraft,oldManual=manualRange;auto notesBefore=juce::JSON::toString(result["editing"]);
      dynMapping=object({{"level",80.},{"contrast",100.},{"min",35.0},{"max",120.0}});hasDynDraft=true;manualRange=true;
      auto state=ui();phthongos::project::checkUi(state,roll.scoreDuration);
      check(state["dynamicsDraft"]["mapping"]["min"].isInt()&&state["dynamicsDraft"]["mapping"]["max"].isInt(),"Whole-valued decimal Min/Max from an existing session are serialised as valid integers");
      check(dynMapping["min"].isDouble()&&juce::JSON::toString(result["editing"])==notesBefore,"Preparing dynamics settings for saving changes neither the draft values nor notes, velocities or undo history");
      for(auto invalid:std::vector<var>{35.5,0.,128.,String("35"),true}){dynMapping.getDynamicObject()->setProperty("min",invalid);bool refused=false;try{phthongos::project::checkUi(ui(),roll.scoreDuration);}catch(...){refused=true;}check(refused,"Invalid Min values are still rejected rather than silently rounded or replaced");}
      dynMapping=oldMapping;hasDynDraft=oldDraft;manualRange=oldManual;
    }
    auto originalUi = ui();
    {
      auto previewArgs=args();previewArgs.getDynamicObject()->setProperty("path",midi.getFullPathName());
      auto preview=call("preview_midi_addition",previewArgs);
      std::unique_ptr<Form> dialog(midiAdditionForm(preview,args()));dialog->setSize(540,720);
      check(dialog->state()["target0"].toString()=="new" && decimal(dialog->state()["offset"])==0,
        String::fromUTF8("Ajout MIDI : nouvelle voix et décalage nul par défaut"));
      check(dialog->compactFields.count("target0") && dialog->compactFields.count("program0"),
        String::fromUTF8("Ajout MIDI : sélecteurs de voix et d’instrument compacts"));
      juce::FileOutputStream imageFile(directory.getChildFile("ajout-midi.png"));
      juce::PNGImageFormat().writeImageToStream(dialog->createComponentSnapshot(dialog->getLocalBounds()),imageFile);
    }
    {
      auto before=result["editing"].clone();
      auto info=call("project_tempo",object({{"audioId",audio["id"]}}));
      check(!(bool)info["fixed"]&&std::abs((double)info["bpm"]-120)<.001,"Imported MIDI initially retains source tempo");
      std::unique_ptr<Form> form(tempoForm(info));form->setSize(540,720);
      check(form->state()["mode"].toString()=="source","Tempo dialog preserves imported map by default");
      form->textFields["bpm"]->setText("65",false);
      form->textFields["bpm"]->onTextChange();
      check(form->state()["mode"].toString()=="fixed"&&std::abs(decimal(form->state()["bpm"])-65)<1e-9,"Typing a BPM explicitly selects fixed export mode");
      {juce::FileOutputStream imageFile(directory.getChildFile("tempo-calculator.png"));juce::PNGImageFormat().writeImageToStream(form->createComponentSnapshot(form->getLocalBounds()),imageFile);}
      auto malformed=ui();malformed.getDynamicObject()->setProperty("rhythmicGrid",object({{"mode","snap"},{"subdivision","1/8"},{"offset",0}}));
      bool rejectedGrid=false;try{phthongos::project::checkUi(malformed,roll.scoreDuration);}catch(...){rejectedGrid=true;}check(rejectedGrid,"Malformed project grid settings are rejected before save");
      auto updated=call("set_project_tempo",object({{"audioId",audio["id"]},{"bpm",65}}));
      check((bool)updated["fixed"]&&std::abs((double)updated["bpm"]-65)<1e-9,"Project BPM is set independently of note editing");
      check(equalJson(call("select_notes",args())["editing"],before),"Setting BPM leaves notes and undo history unchanged");
      check(!(bool)bridge.call("set_project_tempo",object({{"audioId",audio["id"]},{"bpm",601}}))["ok"],"Service rejects invalid project BPM");
    }
    auto pht = directory.getChildFile("before.pht");
    call("save_project", object({{"path", pht.getFullPathName()},
                                 {"audioId", audio["id"]},
                                 {"analysisId", result["analysis_id"]},
                                 {"revision", result["editing"]["revision"]},
                                 {"ui", originalUi}}));
    auto a = args();
    a.getDynamicObject()->setProperty("action",
                                      object({{"kind", "add"},
                                              {"pitch", 64},
                                              {"start", .1},
                                              {"end", 2.},
                                              {"velocity", 83},
                                              {"part", original[0]["part"]}}));
    result = call("edit_notes", a);
    check(number(result, "duration") >= 2,
          juce::String::fromUTF8("Extension de fin sans déplacer le début"));
    a = args();
    a.getDynamicObject()->setProperty("action", object({{"kind", "undo"}}));
    result = call("edit_notes", a);
    check(items(result["editing"]["notes"]).size() == 2,
          juce::String::fromUTF8("Annuler l’ajout"));
    a = args();
    a.getDynamicObject()->setProperty("action", object({{"kind", "redo"}}));
    result = call("edit_notes", a);
    check(items(result["editing"]["notes"]).size() == 3,
          juce::String::fromUTF8("Rétablir l’ajout"));
    auto exported = directory.getChildFile("final.mid");
    a = args();
    a.getDynamicObject()->setProperty("path", exported.getFullPathName());
    check((int)call("export_midi", a) == 3, "Export des trois notes");
    auto restored =
        call("open_project", object({{"path", pht.getFullPathName()}}));
    auto restoredTempo=call("project_tempo",object({{"audioId",restored["audio"]["id"]}}));
    check((bool)restoredTempo["fixed"]&&std::abs((double)restoredTempo["bpm"]-65)<1e-9,"PHT8 restores project BPM");
    check(items(restored["result"]["editing"]["notes"]).size() == 2,
          "PHT : restauration du MIDI source");
    check(
        equalJson(restored["ui"], originalUi),
        juce::String::fromUTF8("PHT : conservation des réglages et de la vue"));
    auto rejected = bridge.call("analyze_muscriptor",
                                object({{"audioId", restored["audio"]["id"]}}));
    check(!(bool)rejected["ok"] && rejected["error"].toString().contains(
                                       tr(juce::String::fromUTF8("mode Édition"))),
          juce::String::fromUTF8(
              "Mode éditeur : transcription protégée sans charger les poids"));
    audio=restored["audio"];result=restored["result"];loadUi(restored["ui"]); rebuildNotes();
    check(roll.rhythmicGrid.mode==1&&roll.rhythmicGrid.subdivision==4&&gridSubdivision.getSelectedId()==5,"PHT8 restores grid mode and subdivision independently of BPM");
    check(spectralPalette==1 && spectralStyle==1 && spectralContrast==63 && roll.bandEnabled && roll.bandLow==52 && roll.bandHigh==76,
      "PHT : palette, contraste et registre par partie conservés");
    {
      auto edit=[&](var action) {auto p=args();p.getDynamicObject()->setProperty("action",action);result=call("edit_notes",p);};
      edit(object({{"kind","modify"},{"id",original[0]["id"]},{"pitch",original[0]["pitch_midi"]},
        {"start",original[0]["start_time_s"]},{"end",.1},{"velocity",original[0]["velocity_midi"]}}));
      auto before=result["editing"]["notes"].clone();
      auto ids=array({original[0]["id"],original[1]["id"]});
      edit(object({{"kind","link"},{"ids",ids},{"overlap_ms",20.}}));
      auto linked=result["editing"]["notes"].clone();
      check(std::abs(number(items(linked)[0],"end_time_s")-.27)<1e-5,
        "Lier via ABI : fin à la prochaine attaque + 20 ms");
      edit(object({{"kind","undo"}}));
      check(equalJson(before,result["editing"]["notes"]),"Lier : une seule annulation");
      edit(object({{"kind","redo"}}));
      check(equalJson(linked,result["editing"]["notes"]),"Lier : rétablissement exact");
      edit(object({{"kind","unlink"},{"ids",ids}}));
      check(std::abs(number(items(result["editing"]["notes"])[0],"end_time_s")-.25)<1e-5,
        "Délier via ABI : fin à la prochaine attaque sans déplacer les débuts");
      edit(object({{"kind","undo"}}));
      check(equalJson(linked,result["editing"]["notes"]),"Délier : une seule annulation");
      auto opened=call("open_project",object({{"path",pht.getFullPathName()}}));
      audio=opened["audio"];result=opened["result"];loadUi(opened["ui"]);rebuildNotes();
      check(linkOverlapMs==20,"PHT : chevauchement configuré conservé");
      juce::TextEditor editor;
      check(!keyPressed(juce::KeyPress('L'),&editor)&&!keyPressed(juce::KeyPress('L'),&side),
        "L inactif pendant une saisie et hors du roll");
    }
    auto groupArgs=args(); groupArgs.getDynamicObject()->setProperty("action",object({{"kind","move_group"},
      {"ids",array({original[0]["id"],original[1]["id"]})},{"seconds",.25},{"semitones",3}}));
    result=call("edit_notes",groupArgs);
    auto moved=items(result["editing"]["notes"]);
    check(moved.size()==2 && (int)moved[0]["pitch_midi"]==63 && std::abs(number(moved[0],"start_time_s")-.25)<1e-6 &&
      (int)moved[1]["velocity_midi"]==(int)original[1]["velocity_midi"],"Groupe backend : hauteurs, temps et vélocités conservés");
    auto undoArgs=args();undoArgs.getDynamicObject()->setProperty("action",object({{"kind","undo"}}));
    result=call("edit_notes",undoArgs);
    check(equalJson(result["editing"]["notes"],restored["result"]["editing"]["notes"]),"Groupe : une seule annulation restaure toutes les notes");
    check(roll.displayMode==2 && roll.spectral && std::abs(roll.overlayOpacity-.5)<1e-6 && roll.visualGain==1.5,
      String::fromUTF8("PHT : restitution du mode, du fond, de l’opacité et du gain visuel"));
    {
      auto savedAudio=audio.clone();
      auto beforeNotes=result["editing"]["notes"].clone();
      auto revision=result["editing"]["revision"];
      auto firstId=original[0]["id"],secondId=original[1]["id"];
      auto h=object({{"id","held-test"},{"pitch",60},{"start",0.},{"end",.75},{"ids",array({firstId,secondId})}});
      auto ghost=object({{"id","missing-test"},{"pitch",67},{"start",.2},{"end",.4}});
      auto extra=object({{"id",firstId},{"pitch",60},{"start",0.},{"end",.5}});
      audio.getDynamicObject()->setProperty("midi_only",false);
      verification=object({{"existing",array({extra})}});
      verificationStale=false;locked=false;
      roll.holds={h};roll.ghosts={ghost};roll.extra={(juce::int64)firstId};
      openSections={"verification","holds","missing","extra"};
      buildSide();
      std::vector<SidePanel::DiagnosticRow*> rows;
      for(auto* w:side.widgets) if(auto* row=dynamic_cast<SidePanel::DiagnosticRow*>(w)) rows.push_back(row);
      check(rows.size()==3,"Analyse : une ligne cliquable par tenue, manque et note en trop");
      for(auto* w:side.widgets)
        if(auto* b=dynamic_cast<juce::TextButton*>(w))
          check(!b->getButtonText().contains("couter") &&
                !b->getButtonText().contains("Pourquoi") &&
                !b->getButtonText().contains("Voir /"),
                "Analyse : boutons redondants retirés des listes");
      rows[0]->onClick();
      check(roll.selected==idSet(h["ids"]) && roll.focused==idSet(h["ids"]),
        "Ligne tenue : sélection et mise en évidence des fragments");
      rows[1]->onClick();
      check(roll.selected.empty() && roll.diagnosticGhost=="missing-test",
        "Ligne manque : fantôme actif sans sélectionner une autre note");
      rows[2]->onClick();
      check(roll.selected==std::set<std::int64_t>{(juce::int64)firstId} &&
            roll.diagnosticGhost.isEmpty(),
        "Ligne note en trop : bonne note sélectionnée");
      check(equalJson(beforeNotes,result["editing"]["notes"]) && revision==result["editing"]["revision"],
        "Navigation : aucune modification des notes ni de la révision");
      buildSide();
      int activeRows=0;
      for(auto* w:side.widgets) if(auto* row=dynamic_cast<SidePanel::DiagnosticRow*>(w)) activeRows+=row->active?1:0;
      check(activeRows==1,"Liste : une seule ligne active surlignée");
      setSize(1400,900);refresh();buildSide();
      juce::FileOutputStream stream(directory.getChildFile("diagnostic-list.png"));
      juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),stream);
      auto split=object({{"id","split-ui"},{"kind","split"},{"note_id",firstId},{"pitch",60},{"start",0.},{"end",.5},{"at",.2},{"proposed_end",.5}});
      auto extend=object({{"id","extend-ui"},{"kind","extend"},{"note_id",firstId},{"pitch",60},{"start",0.},{"end",.5},{"at",.5},{"proposed_end",.7}});
      verification.getDynamicObject()->setProperty("timing",array({split,extend}));
      roll.timing={split,extend};openSections.insert("timing");buildSide();
      int timingRows=0;
      for(auto* w:side.widgets)if(dynamic_cast<SidePanel::DiagnosticRow*>(w))++timingRows;
      check(timingRows==5,"Durées et réattaques : deux propositions cliquables ajoutées aux trois catégories");
      navigateDiagnostic("split",split);
      check(roll.diagnosticGhost=="split-ui" && roll.selected==std::set<std::int64_t>{(juce::int64)firstId},
        "Scission suggérée : note ciblée et marqueur actif");
      navigateDiagnostic("extend",extend);
      check(roll.diagnosticGhost=="extend-ui" && equalJson(beforeNotes,result["editing"]["notes"]),
        "Prolongation suggérée : navigation seule sans correction");
      buildSide();sideView.setViewPosition(0,350);
      {
        juce::FileOutputStream image(directory.getChildFile("timing-suggestions.png"));
        juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);
      }
      auto batch=diagnosticBatchPlan("all");
      check((int)batch["holds"]==1&&(int)batch["missing"]==1&&(int)batch["timing"]==0&&(int)batch["extra"]==0&&(int)batch["skipped"]==3,
            "Global diagnostic plan adds missing notes and merges holds, excluding conflicting timing and deletion proposals");
      auto declared=result["midi_parts"].clone();result.getDynamicObject()->setProperty("midi_parts",array({}));
      check(!items(diagnosticDestinations()).empty()&&int(diagnosticBatchPlan("missing")["missing"])==1,"Missing declarations never discard proposals in a manual MIDI voice");
      result.getDynamicObject()->setProperty("midi_parts",declared);
      auto missingBatch=diagnosticBatchPlan("missing");
      check((int)missingBatch["missing"]==1&&missingBatch["actions"].size()==1,"Missing-note section plans only its own additions");
      applyDiagnosticBatch("missing");Form* confirmation=nullptr;
      for(auto* f:Form::activeForms)if(f->getComponentID()=="diagnosticBatchConfirmation")confirmation=f;
      check(confirmation&&confirmation->comboFields.count("destination"),"Batch confirmation exposes the destination voice before adding missing notes");
      if(confirmation){
        juce::FileOutputStream image(directory.getChildFile("diagnostic-batch-confirmation.png"));
        juce::PNGImageFormat().writeImageToStream(confirmation->createComponentSnapshot(confirmation->getLocalBounds()),image);
        confirmation->close();
      }
      check(equalJson(beforeNotes,result["editing"]["notes"])&&revision==result["editing"]["revision"],"Dismissing batch confirmation leaves all notes unchanged");
      auto batchArgs=args();batchArgs.getDynamicObject()->setProperty("actions",array({
        object({{"kind","delete"},{"ids",array({firstId})}}),
        object({{"kind","add"},{"pitch",67},{"start",.2},{"end",.4},{"velocity",80},{"part",original[0]["part"]}})}));
      auto batchResult=call("edit_notes_batch",batchArgs);
      check((juce::int64)batchResult["editing"]["revision"]==(juce::int64)revision+1&&items(batchResult["editing"]["notes"]).size()==items(beforeNotes).size(),"Batch service commits additions and deletions together in one revision");
      auto batchUndo=args();batchUndo.getDynamicObject()->setProperty("revision",batchResult["editing"]["revision"]);batchUndo.getDynamicObject()->setProperty("action",object({{"kind","undo"}}));
      result=call("edit_notes",batchUndo);
      check(equalJson(beforeNotes,result["editing"]["notes"]),"Batch service undo restores the exact notes before the entire batch");
      roll.timing.clear();
      audio=savedAudio;verification=var();diagnosticActive.clear();roll.diagnosticGhost.clear();
      roll.holds.clear();roll.ghosts.clear();roll.extra.clear();roll.focused.clear();roll.clearSelection();
      openSections.clear();
    }
    for(auto size : {juce::Point<int>(1160,800),juce::Point<int>(1400,900)}) {
      setSize(size.x,size.y);refresh();
      check(roll.getHeight()>=300 && !buttonMap["help"]->getBounds().intersects(roll.getBounds()),"Disposition : roll lisible et aide hors de la zone de travail");
      for(auto* b:buttons)check(b->getTooltip().isEmpty(),"Interface : pas d’infobulle redondante sur les boutons");
      auto image=createComponentSnapshot(getLocalBounds());
      juce::FileOutputStream stream(directory.getChildFile("interface-"+String(size.x)+".png"));
      juce::PNGImageFormat().writeImageToStream(image,stream);
    }
    {
      juce::TextEditor helpContent;
      helpContent.setMultiLine(true); helpContent.setReadOnly(true);helpContent.setSize(630,560);
      juce::CallOutBox popup(helpContent,buttonMap["help"]->getBounds(),this);
      helpPopup=&popup;
      for(auto size:{juce::Point<int>(1400,900),juce::Point<int>(1160,800),juce::Point<int>(1024,700)}) {
        setSize(size.x,size.y);positionHelp();
        check(getLocalBounds().contains(popup.getBounds()),"Aide : popup intégralement contenu dans le cadre après redimensionnement");
      }
      helpPopup=nullptr;
    }
    {
      // Synthetic stem only: exercises GUI gestures, the real service and PHT8 without user files.
      Bridge guidedBridge(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("runtime"),directory.getChildFile("guided-cache"));
      auto guidedCall=[&](const String& name,const var& parameters){auto response=guidedBridge.call(name,parameters);check((bool)response["ok"],name+" : "+response["error"].toString());return response["value"];};
      constexpr int rate=22050;juce::AudioBuffer<float> signal(2,rate*4);signal.clear();
      std::array<int,3> pitches={60,64,67};std::array<double,3> beginnings={.3,1.5,2.6},endings={1.2,2.4,3.6};
      for(int n=0;n<3;++n)for(int i=int(beginnings[n]*rate);i<int(endings[n]*rate);++i){double t=double(i)/rate,f=440*std::exp2((pitches[n]-69.)/12.);float value=float(std::min({1.,(t-beginnings[n])/.015,(endings[n]-t)/.025})*(.14*std::sin(juce::MathConstants<double>::twoPi*f*t)+.07*std::sin(juce::MathConstants<double>::twoPi*f*2*t)+.04*std::sin(juce::MathConstants<double>::twoPi*f*3*t)));for(int c=0;c<2;++c)signal.setSample(c,i,value);}
      auto wav=directory.getChildFile("guided-source.wav");juce::WavAudioFormat format;
      {std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(new juce::FileOutputStream(wav),rate,2,16,{},0));check(writer&&writer->writeFromAudioSampleBuffer(signal,0,signal.getNumSamples()),"Guided synthetic WAV written");}
      auto guidedAudio=guidedCall("open_audio",object({{"path",wav.getFullPathName()}}));
      auto defaultTempo=guidedCall("project_tempo",object({{"audioId",guidedAudio["id"]}}));
      check(!(bool)defaultTempo["fixed"]&&(double)defaultTempo["bpm"]==120,"New audio resets previous project BPM");
      guidedCall("set_project_tempo",object({{"audioId",guidedAudio["id"]},{"bpm",65}}));
      auto guidedResult=guidedCall("create_blank_midi",object({{"audioId",guidedAudio["id"]}}));
      check((double)guidedCall("project_tempo",object({{"audioId",guidedAudio["id"]}}))["bpm"]==65,"BPM declared before editing survives blank MIDI creation");
      auto parameters=[&]{return object({{"audioId",guidedAudio["id"]},{"analysisId",guidedResult["analysis_id"]},{"revision",guidedResult["editing"]["revision"]},{"sensitivity",100},{"part",var()}});};
      auto edit=[&](var action){auto request=parameters();request.getDynamicObject()->setProperty("action",action);guidedResult=guidedCall("edit_notes",request);};
      Roll fixture;fixture.setSize(1000,600);fixture.audio=guidedAudio;fixture.displayMode=2;fixture.spectral=true;fixture.view.pitchZoom=4;fixture.view.pitchPan=.5;fixture.maskMode=1;
      std::vector<phthongos::MaskStroke> strokes;fixture.maskCommitted=[&](const phthongos::MaskStroke& stroke){strokes.push_back(stroke);edit(object({{"kind","mask_stroke"},{"part",int(phthongos::allMaskParts)},{"stroke",phthongos::project::writeMaskStroke(stroke)}}));};
      auto event=[&](juce::Point<float> at,juce::Point<float> down,bool dragged){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&fixture,&fixture,juce::Time::getCurrentTime(),down,juce::Time::getCurrentTime(),1,dragged);};
      for(int n=0;n<3;++n){juce::Point<float> a(float(fixture.x(beginnings[n])),float(fixture.pitchY(pitches[n]))),b(float(fixture.x(endings[n])),a.y);fixture.mouseDown(event(a,a,false));fixture.mouseDrag(event(b,a,true));fixture.mouseUp(event(b,a,true));}
      check(strokes.size()==3&&std::abs(strokes[0].points.front().pitch-60)<1e-5&&std::abs(strokes[0].points.front().time-.3)<1e-5,"Brush drag stores seconds and pitch, once per gesture");
      check(guidedResult["editing"]["notes"].size()==0&&guidedResult["editing"]["spectral_masks"]["65535"].size()==3,"Painting creates a blank editing document without adding MIDI notes");
      edit(object({{"kind","undo"}}));check(guidedResult["editing"]["spectral_masks"]["65535"].size()==2,"One brush gesture is one undo");edit(object({{"kind","redo"}}));
      for(auto& stroke:strokes)fixture.spectralMask.push_back(std::make_shared<const phthongos::MaskStroke>(stroke));++fixture.maskRevision;
      fixture.maskMode=2;auto erased=juce::Point<float>(float(fixture.x(.7)),float(fixture.pitchY(60)));fixture.mouseDown(event(erased,erased,false));fixture.mouseUp(event(erased,erased,false));
      check(strokes.back().erase,"Eraser emits an erasing stroke");edit(object({{"kind","undo"}}));
      auto image=fixture.createComponentSnapshot(fixture.getLocalBounds());fixture.view.pitchZoom=8;fixture.view.pitchPan=.5;fixture.view.zoom=2;fixture.view.pan=0;auto zoomed=fixture.createComponentSnapshot(fixture.getLocalBounds());
      check(image.isValid()&&zoomed.isValid()&&phthongos::maskContains(fixture.spectralMask,.7,60),"Mask remains in musical coordinates across horizontal and vertical zoom");
      juce::PNGImageFormat png;{juce::FileOutputStream stream(directory.getChildFile("guided-mask.png"));png.writeImageToStream(image,stream);}
      auto analyze=[&](bool useMask,var target){auto request=parameters();auto* o=request.getDynamicObject();o->setProperty("low",48);o->setProperty("high",84);o->setProperty("start",0.);o->setProperty("end",4.);o->setProperty("useMask",useMask);o->setProperty("maskExtent",false);o->setProperty("part",target);return guidedCall("guided_analyze",request);};
      auto report=analyze(true,var());check(report["notes"].size()==3&&guidedResult["editing"]["notes"].size()==0,"Masked analysis proposes three ghost notes and changes no MIDI");
      auto choose=[&](const String& command,const var& ids){auto request=parameters();request.getDynamicObject()->setProperty("token",report["token"]);request.getDynamicObject()->setProperty("ids",ids);auto response=guidedCall(command,request);guidedResult=response["result"];report=response["report"];return response;};
      choose("guided_accept",array({report["notes"][0]["id"]}));check(guidedResult["editing"]["notes"].size()==1&&report["notes"].size()==2,"Accepting one proposal adds only that note");
      choose("guided_accept",array({report["notes"][0]["id"],report["notes"][1]["id"]}));auto accepted=items(guidedResult["editing"]["notes"]);check(accepted.size()==3&&Note::parse(accepted[0]).part==Note::parse(accepted[1]).part&&Note::parse(accepted[1]).part==Note::parse(accepted[2]).part,"Accepted batches share their newly created voice");
      edit(object({{"kind","undo"}}));check(guidedResult["editing"]["notes"].size()==1,"One undo removes the entire accepted group");
      auto stale=parameters();stale.getDynamicObject()->setProperty("token",report["token"]);stale.getDynamicObject()->setProperty("ids",array({"guided-1"}));check(!(bool)guidedBridge.call("guided_accept",stale)["ok"],"Changed document rejects stale proposals");edit(object({{"kind","redo"}}));
      report=analyze(false,Note::parse(accepted[0]).part);auto duplicate=choose("guided_accept",array({report["notes"][0]["id"],report["notes"][1]["id"],report["notes"][2]["id"]}));check((int)duplicate["duplicates"]==3&&guidedResult["editing"]["notes"].size()==3,"Existing matching notes are not duplicated in the selected voice");
      report=analyze(false,Note::parse(accepted[0]).part);auto unchangedRevision=guidedResult["editing"]["revision"];choose("guided_ignore",array({report["notes"][0]["id"]}));check(report["notes"].size()==2&&guidedResult["editing"]["revision"]==unchangedRevision,"Ignoring a proposal changes no MIDI or undo history");
      auto saved=parameters();saved.getDynamicObject()->setProperty("path",directory.getChildFile("guided.pht").getFullPathName());saved.getDynamicObject()->setProperty("ui",ui());guidedCall("save_project",saved);
      auto reopened=guidedCall("open_project",object({{"path",directory.getChildFile("guided.pht").getFullPathName()}}));check(reopened["result"]["editing"]["notes"].size()==3&&reopened["result"]["editing"]["spectral_masks"]["65535"].size()==3,"PHT8 restores accepted notes, mask and undo history");
      guidedAudio=reopened["audio"];guidedResult=reopened["result"];auto untouched=juce::JSON::toString(guidedResult["editing"]["notes"]);
      edit(object({{"kind","add"},{"pitch",60},{"start",4.5},{"end",7.},{"velocity",83}}));auto lateId=guidedResult["editing"]["notes"][3]["id"];
      auto extended=parameters();auto extendedUi=ui();extendedUi.getDynamicObject()->setProperty("position",6.5);extended.getDynamicObject()->setProperty("ui",extendedUi);extended.getDynamicObject()->setProperty("path",directory.getChildFile("extended-midi.pht").getFullPathName());guidedCall("save_project",extended);
      auto extendedFile=phthongos::project::read(directory.getChildFile("extended-midi.pht"));
      check(number(extendedFile.metadata["ui"],"position")==6.5&&number(guidedResult,"duration")==7.,"Notes after the audio extend the MIDI duration and preserve a playhead inside that extension when saving");
      edit(object({{"kind","delete"},{"ids",array({lateId})}}));

      auto beforeCopies=guidedResult["editing"]["notes"].clone();std::set<std::int64_t> originalIds;for(auto& n:items(beforeCopies))originalIds.insert((juce::int64)n["id"]);
      edit(object({{"kind","duplicate_notes"},{"ids",idsVar(originalIds)},{"seconds",0.},{"semitones",-12}}));std::set<std::int64_t> addedIds;int preserved=0;
      for(auto& n:items(guidedResult["editing"]["notes"]))if(originalIds.count((juce::int64)n["id"])){for(auto& old:items(beforeCopies))if(n["id"]==old["id"]&&juce::JSON::toString(n)==juce::JSON::toString(old))++preserved;}else{
        addedIds.insert((juce::int64)n["id"]);bool matches=false;for(auto& old:items(beforeCopies))if(int(n["pitch_midi"])==int(old["pitch_midi"])-12&&n["start_time_s"]==old["start_time_s"]&&n["end_time_s"]==old["end_time_s"]&&n["velocity_midi"]==old["velocity_midi"]&&Note::parse(n).part==Note::parse(old).part)matches=true;
        check(matches,"Vertical duplication preserves exact timing, velocity and original part");}
      check(preserved==3&&addedIds.size()==3,"Batch duplication keeps original note data and assigns fresh copy IDs");
      edit(object({{"kind","undo"}}));check(juce::JSON::toString(guidedResult["editing"]["notes"])==juce::JSON::toString(beforeCopies),"One undo removes all copies and leaves originals unchanged");
      edit(object({{"kind","redo"}}));int restoredIds=0;for(auto& n:items(guidedResult["editing"]["notes"]))restoredIds+=addedIds.count((juce::int64)n["id"]);check(restoredIds==3,"Redo restores all copies with stable IDs");
      edit(object({{"kind","undo"}}));auto invalidCopy=parameters();invalidCopy.getDynamicObject()->setProperty("action",object({{"kind","duplicate_notes"},{"ids",idsVar(originalIds)},{"seconds",0.},{"semitones",-100}}));
      check(!(bool)guidedBridge.call("edit_notes",invalidCopy)["ok"],"Out-of-range duplicate is rejected atomically");check(juce::JSON::toString(guidedCall("select_notes",parameters())["editing"]["notes"])==untouched,"Failed duplicate preserves original notes");
      edit(object({{"kind","add_marker"},{"at",1.234567},{"name",String::fromUTF8("Vérifier attaque")}}));auto marker=guidedResult["editing"]["markers"][0];
      check(guidedResult["editing"]["markers"].size()==1&&juce::JSON::toString(guidedResult["editing"]["notes"])==untouched,"Adding a timeline marker preserves all MIDI notes");
      edit(object({{"kind","modify_marker"},{"id",marker["id"]},{"at",2.345678},{"name","New name"}}));
      edit(object({{"kind","undo"}}));check(number(guidedResult["editing"]["markers"][0],"time")==1.234567,"Marker movement and rename undo together");
      edit(object({{"kind","redo"}}));edit(object({{"kind","delete_marker"},{"id",marker["id"]}}));check(guidedResult["editing"]["markers"].size()==0,"Marker deletion removes only the marker");
      edit(object({{"kind","undo"}}));check(guidedResult["editing"]["markers"][0]["name"].toString()=="New name","Marker deletion undo restores its name and position");
      auto malformed=parameters();malformed.getDynamicObject()->setProperty("action",object({{"kind","add_marker"},{"at",-1.},{"name","bad"}}));check(!(bool)guidedBridge.call("edit_notes",malformed)["ok"],"Out-of-range marker is rejected without mutation");
      auto saveAtMargin=parameters();auto marginUi=ui();marginUi.getDynamicObject()->setProperty("position",9.);saveAtMargin.getDynamicObject()->setProperty("ui",marginUi);saveAtMargin.getDynamicObject()->setProperty("path",directory.getChildFile("marker-and-margin.pht").getFullPathName());guidedCall("save_project",saveAtMargin);
      auto marginProject=guidedCall("open_project",object({{"path",directory.getChildFile("marker-and-margin.pht").getFullPathName()}}));
      check(number(marginProject["ui"],"position")==4.,"Saving from beyond the real end clamps only the stored playhead and reopens successfully");
      check(number(marginProject["result"]["editing"]["markers"][0],"time")==2.345678&&marginProject["result"]["editing"]["markers"][0]["name"].toString()=="New name"&&juce::JSON::toString(marginProject["result"]["editing"]["notes"])==untouched,"PHT8 round trip preserves marker precision and exact notes");
      auto undoReopened=parameters();undoReopened.getDynamicObject()->setProperty("audioId",marginProject["audio"]["id"]);undoReopened.getDynamicObject()->setProperty("analysisId",marginProject["result"]["analysis_id"]);undoReopened.getDynamicObject()->setProperty("revision",marginProject["result"]["editing"]["revision"]);undoReopened.getDynamicObject()->setProperty("action",object({{"kind","undo"}}));
      check(number(guidedCall("edit_notes",undoReopened)["editing"]["markers"][0],"time")==1.234567,"PHT8 preserves marker undo history");

    }
    passed = true;
  } catch (const std::exception &e) {
    checks.push_back(
        object({{"error", String::fromUTF8(e.what())}, {"passed", false}}));
  }
  auto report = object({{"passed", passed}, {"checks", array(checks)}});
  directory.getChildFile("results.json")
      .replaceWithText(juce::JSON::toString(report));
  directory.getParentDirectory()
      .getChildFile("latest.json")
      .replaceWithText(juce::JSON::toString(
          object({{"passed", passed},{"language",localisation::code()},
                  {"directory", directory.getFullPathName()},
                  {"checks", (int)checks.size()}})));
  return passed;
}

void App::runFlowTests(std::function<void(bool)> done) {
  struct Run {
    int stage = 0;
    double started = juce::Time::getMillisecondCounterHiRes();
    bool completed = false;
    std::vector<var> checks;
    int initialNotes=0;
    var beforeLink,beforeUnlink,clipboardBefore;
    std::set<std::int64_t> clipboardOriginal,clipboardAdded;
    int manualPass=0;
    var manualAudioId,manualView;
    String manualAudioPath;
    double manualStart=0;
    juce::File additionFile;
    double zoom=1,pitchZoom=1;
  };
  setSize(1400,900);
  auto run = std::make_shared<Run>();
  auto poll = std::make_shared<std::function<void()>>();
  auto finish = [s = safe(), run, poll, done](bool ok) {
    if (run->completed)
      return;
    run->completed = true;
    auto out = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                   .getSiblingFile("test-results/flow-latest.json");
    out.replaceWithText(juce::JSON::toString(
        object({{"passed", ok},
                {"stage", run->stage},
                {"status", s ? s->status.getText() : String("App destroyed")},
                {"checks", array(run->checks)}})));
    if (s && ok) {
      s->buildSide();
      s->resized();
      auto preview =
          out.getSiblingFile("flow-preview.png").createOutputStream();
      if (preview) {
        preview->setPosition(0); preview->truncate();
        juce::PNGImageFormat().writeImageToStream(
            s->createComponentSnapshot(s->getLocalBounds()), *preview);
      }
    }
    juce::MessageManager::callAsync([poll,done,ok] { *poll = {}; done(ok); });
  };
  auto click = [this](const String &label) {
    for (auto *c : side.widgets)
      if (auto *b = dynamic_cast<juce::TextButton *>(c))
        if (b->getButtonText() == tr(label) && b->isEnabled()) {
          b->onClick();
          return true;
        }
    return false;
  };
  *poll = [this, run, poll, finish, click] {
    if (juce::Time::getMillisecondCounterHiRes() - run->started > 120000) {
      finish(false);
      return;
    }
    if (run->stage == 0 && !locked && !audio.isVoid() && notesReady) {
      if(run->manualPass<2) {
        if(!roll.editable || !buttonMap["add"]->isEnabled()){finish(false);return;}
        run->manualAudioId=audio["id"];run->manualAudioPath=audio["path"].toString();
        roll.view.zoom=2;roll.view.pan=.2;roll.view.pitchZoom=4;roll.view.pitchPan=.5;
        roll.displayMode=2;displayMode.setSelectedId(3,juce::dontSendNotification);
        run->manualView=roll.view.json();run->manualStart=roll.start()+.3;
        refresh();run->stage=100;
        auto point=juce::Point<float>((float)roll.x(run->manualStart),(float)(roll.y(60)+roll.row()/2));
        auto click=juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,
          juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&roll,&roll,
          juce::Time::getCurrentTime(),point,juce::Time::getCurrentTime(),run->manualPass==0?1:2,false);
        if(run->manualPass==0) {buttonMap["add"]->onClick();roll.mouseDown(click);roll.mouseUp(click);}
        else {roll.tool=0;roll.mouseDoubleClick(click);}
        juce::Timer::callAfterDelay(100,[poll]{if(*poll)(*poll)();});return;
      }
      run->checks.push_back(
          object({{"action", "open_audio + player_prepare + initialise"},
                  {"passed", true}}));
      if (!buttonMap["analyze"]->isEnabled()) {
        finish(false);
        return;
      }
      run->stage = 1;
      buttonMap["analyze"]->onClick();
    } else if(run->stage==100 && !locked && notesReady && !result.isVoid()) {
      auto notes=items(result["editing"]["notes"]);
      bool ok=notes.size()==1 && result["engine"].toString()=="midi" &&
        audio["id"]==run->manualAudioId && (int)notes[0]["pitch_midi"]==60 &&
        std::abs(number(notes[0],"start_time_s")-run->manualStart)<1e-5 &&
        juce::JSON::toString(roll.view.json())==juce::JSON::toString(run->manualView) &&
        !buttonMap["analyze"]->isVisible() && roll.displayMode==2;
      run->checks.push_back(object({{"action",run->manualPass==0?"Audio only: pencil creates first MIDI note, protected editor and unchanged view":"Audio only: double-click creates first MIDI note without inference"},{"passed",ok}}));
      if(!ok){finish(false);return;}run->stage=101;
      command(object({{"kind","undo"}}));
    } else if(run->stage==101 && !locked && notesReady) {
      bool ok=items(result["editing"]["notes"]).empty() && result["engine"].toString()=="midi" &&
        (bool)result["editing"]["can_redo"] && !buttonMap["analyze"]->isVisible();
      if(!ok){finish(false);return;}run->stage=102;
      command(object({{"kind","redo"}}));
    } else if(run->stage==102 && !locked && notesReady) {
      bool ok=items(result["editing"]["notes"]).size()==1;
      run->checks.push_back(object({{"action","Manual audio project: undo to zero notes and redo keep editing mode"},{"passed",ok}}));
      if(!ok){finish(false);return;}
      ++run->manualPass;run->stage=0;openAudioPath(run->manualAudioPath);
    } else if (run->stage == 1 && !locked && !result.isVoid()) {
      if (items(result["editing"]["notes"]).empty()) {
        finish(false);
        return;
      }
      run->checks.push_back(
          object({{"action", "Analyser button callback -> GGUF -> notes"},
                  {"notes", (int)items(result["editing"]["notes"]).size()},
                  {"passed", true}}));
      verifyScope = "all";
      openSections.insert("verification");
      buildSide();
      run->stage = 2;
      if (!click(String::fromUTF8(
              "Vérifier les notes et rechercher les manques"))) {
        finish(false);
        return;
      }
    } else if (run->stage == 2 && !locked && !verification.isVoid()) {
      run->checks.push_back(
          object({{"action", "Verification button callback -> acoustic report"},
                  {"passed", true}}));
      openSections.insert("dynamics");
      buildSide();
      run->stage = 3;
      if (!click(String::fromUTF8("Estimer depuis l’audio"))) {
        finish(false);
        return;
      }
    } else if (run->stage == 3 && !locked && !dynamics.isVoid()) {
      run->checks.push_back(
          object({{"action", "Dynamics button callback -> measured velocities"},
                  {"passed", true}}));
      {
        auto options=args();
        options.getDynamicObject()->setProperty("scope",object({{"kind","all"}}));
        options.getDynamicObject()->setProperty("settings",object({{"gap_ms",30.},{"sustain_min",.3},{"velocity_delta",18}}));
        options.getDynamicObject()->setProperty("acoustic",true);
        auto continuity=bridge.call("preview_clean",options);
        if(!(bool)continuity["ok"]){finish(false);return;}
        auto selected=bridge.call("select_notes",args());
        if(!(bool)selected["ok"] || selected["value"]["editing"]["revision"]!=result["editing"]["revision"]){finish(false);return;}
        run->checks.push_back(object({{"action","C++ continuity service: audio search commits without changing document revision"},{"passed",true}}));
      }
      run->stage=4;
      displayMode.setSelectedId(3,juce::sendNotificationSync);
      audioLayer.setSelectedId(2,juce::sendNotificationSync);
    } else if(run->stage==4 && roll.spectrum.isValid() && spectrumKey==spectrumWanted) {
      run->checks.push_back(object({{"action","Spectrogram: real audio, asynchronous cache and MIDI overlay"},{"passed",true}}));
      roll.bandEnabled=true;roll.bandLow=60;roll.bandHigh=76;saveSpectralBand();
      for(int palette=0;palette<3;++palette) {
        spectralPalette=palette;recolourSpectrum();
        auto out=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/palette-"+String(palette)+".png");
        juce::FileOutputStream stream(out);stream.setPosition(0);stream.truncate();
        juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),stream);
      }
      spectralPalette=0;recolourSpectrum();
      run->stage=5; roll.view.proportional(roll.duration(),4,.5,.5);roll.repaint();
    } else if(run->stage==5 && roll.spectrum.isValid() && spectrumKey==spectrumWanted && std::abs(roll.spectrumStart-roll.start())<1e-6 && std::abs(roll.spectrumEnd-roll.start()-roll.span())<1e-6) {
      run->checks.push_back(object({{"action","Spectrogram: viewport regenerated after proportional zoom"},{"passed",true}}));
      run->stage=6;run->initialNotes=(int)items(result["editing"]["notes"]).size();
      const unsigned char midi[]={0x4d,0x54,0x68,0x64,0,0,0,6,0,0,0,1,1,0xe0,0x4d,0x54,0x72,0x6b,0,0,0,13,0,0x90,72,90,0x83,0x60,0x80,72,0,0,0xff,0x2f,0};
      run->additionFile=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("phthongos-flow-addition",".mid");
      run->additionFile.replaceWithData(midi,sizeof(midi));
      auto params=args();params.getDynamicObject()->setProperty("path",run->additionFile.getFullPathName());
      invoke("preview_midi_addition",params,[this,run,finish](var preview){
        auto* form=midiAdditionForm(preview,args());form->setValue("offset",.25);
        auto state=form->state();form->onApply(state);delete form;
      },true,[finish](String){finish(false);});
    } else if(run->stage==6 && !locked && notesReady && (int)items(result["editing"]["notes"]).size()==run->initialNotes+1) {
      run->checks.push_back(object({{"action","Add MIDI form callback: new voice and notes appended to transcribed project"},{"passed",true}}));
      int added=-1;for(auto& p:items(result["midi_parts"]))if(!p["identity"].isVoid())added=(int)p["identity"];
      if(added<0){finish(false);return;}selectPart(added);run->stage=7;
    } else if(run->stage==7 && notesReady && !locked) {
      buttonMap["partMute"]->onClick();run->stage=8;
    } else if(run->stage==8 && notesReady && !locked && !items(result["editing"]["muted_parts"]).empty()) {
      run->checks.push_back(object({{"action","Part mute button: engine refreshed with part muted"},{"passed",true}}));
      buttonMap["partMute"]->onClick();run->stage=9;
    } else if(run->stage==9 && notesReady && !locked && items(result["editing"]["muted_parts"]).empty()) {
      run->zoom=roll.view.zoom;run->pitchZoom=roll.view.pitchZoom;
      buttonMap["end"]->onClick();run->stage=10;
    } else if(run->stage==10 && !seekInFlight && std::abs(number(playback,"position")-std::max(number(result,"duration"),number(audio,"duration")))<.02) {
      if(roll.view.zoom!=run->zoom || roll.view.pitchZoom!=run->pitchZoom){finish(false);return;}
      run->checks.push_back(object({{"action","End button: actual transport positioned at project end without zoom change"},{"passed",true}}));
      buttonMap["begin"]->onClick();run->stage=11;
    } else if(run->stage==11 && !seekInFlight && number(playback,"position")<.02) {
      if(roll.view.pan!=0 || roll.view.zoom!=run->zoom || roll.view.pitchZoom!=run->pitchZoom){finish(false);return;}
      run->checks.push_back(object({{"action","Beginning button: transport and viewport reset without zoom change"},{"passed",true}}));
      run->stage=12;
      auto params=args();auto* p=params.getDynamicObject();p->setProperty("start",0.);p->setProperty("end",number(audio,"duration"));p->setProperty("ids",var());p->setProperty("part",var());p->setProperty("pitchRange",array({60,72}));
      invoke("verify_midi_audio",params,[this,run,finish](var report){
        if(!(bool)report["cached"] || (int)report["pitch_low"]!=60 || (int)report["pitch_high"]!=72){finish(false);return;}
        for(auto& item:items(report["existing"]))if((int)item["pitch"]<60 || (int)item["pitch"]>72){finish(false);return;}
        for(auto& item:items(report["missing"]))if((int)item["pitch"]<60 || (int)item["pitch"]>72){finish(false);return;}
        for(auto& item:items(report["timing"]))if((int)item["pitch"]<60 || (int)item["pitch"]>72 ||
            item["confidence"].toString()!=String::fromUTF8("à vérifier") ||
            number(item,"proposed_end")>number(audio,"duration")){finish(false);return;}
        run->checks.push_back(object({{"action","Register: constrained diagnostics reuse cached spectrum after note edits"},{"passed",true}}));
        if(roll.notes.empty()){finish(false);return;}auto note=roll.notes.front();
        run->stage=13;roll.split(note.id,(note.start+note.end)/2);
      },true,[finish](String){finish(false);});
    } else if(run->stage==13 && !locked && notesReady && (int)items(result["editing"]["notes"]).size()==run->initialNotes+2) {
      run->stage=14;
      command(object({{"kind","undo"}}),[this,run,finish](var view){
        bool ok=(int)items(view["editing"]["notes"]).size()==run->initialNotes+1;
        run->checks.push_back(object({{"action","Split callback and Ctrl+Z-equivalent command: both fragments restored atomically"},{"passed",ok}}));
        if(!ok){finish(false);return;}
        selectPart(-1);
        std::set<std::int64_t> ids;for(auto& n:roll.notes)ids.insert(n.id);
        roll.selectIds(ids);run->beforeUnlink=view["editing"]["notes"].clone();
        run->stage=15;linkNotes(ids,true);
      });
    } else if(run->stage==15 && !locked && notesReady) {
      auto notes=items(result["editing"]["notes"]);
      bool ok=true;
      for(auto& n:notes)for(auto& before:items(run->beforeUnlink))
        if(n["id"]==before["id"] && number(n,"end_time_s")>number(before,"end_time_s")+1e-6)ok=false;
      run->checks.push_back(object({{"action",String::fromUTF8("Délier callback: no attack moved and no note extended")},{"passed",ok}}));
      if(!ok){finish(false);return;}
      run->beforeLink=result["editing"]["notes"].clone();run->stage=16;
      pressedKeys.erase('L');keyPressed(juce::KeyPress('L'),&roll);
    } else if(run->stage==16 && !locked && notesReady) {
      bool ok=true,changed=false;
      for(auto& n:items(result["editing"]["notes"]))for(auto& before:items(run->beforeLink))
        if(n["id"]==before["id"]) {
          if(n["pitch_midi"]!=before["pitch_midi"] || n["velocity_midi"]!=before["velocity_midi"] ||
             n["start_time_s"]!=before["start_time_s"] || number(n,"end_time_s")<number(before,"end_time_s")-1e-6)ok=false;
          if(number(n,"end_time_s")>number(before,"end_time_s")+1e-6)changed=true;
        }
      ok=ok&&changed;
      run->checks.push_back(object({{"action","L shortcut: selected notes linked, attacks and velocities unchanged"},{"passed",ok}}));
      if(!ok){finish(false);return;}run->stage=17;
      command(object({{"kind","undo"}}),[this,run,finish](var v){
        bool same=juce::JSON::toString(v["editing"]["notes"])==juce::JSON::toString(run->beforeLink);
        run->checks.push_back(object({{"action","L shortcut: one undo restores the whole selection"},{"passed",same}}));
        if(!same || roll.notes.size()<2){finish(false);return;}
        selectPart(-1);run->clipboardOriginal={roll.notes[0].id,roll.notes[1].id};run->stage=18;
        command(object({{"kind","toggle_mute"},{"ids",idsVar({roll.notes[0].id})}}));
      });
    } else if(run->stage==18 && !locked && notesReady) {
      roll.selectIds(run->clipboardOriginal);pressedKeys.erase('C');keyPressed(juce::KeyPress('C',juce::ModifierKeys::ctrlModifier,0),&roll);
      bool ok=noteClipboard.size()==2 && !roll.cutHeld;
      run->checks.push_back(object({{"action","Ctrl+C copies a stable two-note snapshot without activating scissors"},{"passed",ok}}));
      if(!ok){finish(false);return;}run->clipboardBefore=result["editing"]["notes"].clone();
      run->stage=19;control("seek",2.);
    } else if(run->stage==19 && !locked && notesReady && !seekInFlight && std::abs(number(playback,"position")-2.)<.02) {
      run->stage=20;pressedKeys.erase('V');keyPressed(juce::KeyPress('V',juce::ModifierKeys::ctrlModifier,0),&roll);
    } else if(run->stage==20 && !locked && notesReady) {
      auto all=items(result["editing"]["notes"]);bool ok=all.size()==(size_t)run->clipboardBefore.size()+2&&roll.selected.size()==2;
      double first=1200;for(auto& n:items(noteClipboard))first=std::min(first,number(n,"start_time_s"));
      int found=0;
      for(auto& n:all)if(roll.selected.count((juce::int64)n["id"])) {
        run->clipboardAdded.insert((juce::int64)n["id"]);bool matched=false;
        for(auto& old:items(noteClipboard))if(n["pitch_midi"]==old["pitch_midi"]&&n["velocity_midi"]==old["velocity_midi"]&&Note::parse(n).part==Note::parse(old).part&&Note::parse(n).program==Note::parse(old).program&&
          std::abs(number(n,"start_time_s")-(2+number(old,"start_time_s")-first))<1e-5 &&
          std::abs(number(n,"end_time_s")-(2+number(old,"end_time_s")-first))<1e-5 && (bool)n["muted"]==(bool)old["muted"])matched=true;
        ok&=matched&&!run->clipboardOriginal.count((juce::int64)n["id"]);++found;
      }
      ok&=found==2;run->checks.push_back(object({{"action","Ctrl+V at playhead preserves notes, parts, geometry, velocities and individual mute; selects new IDs"},{"passed",ok}}));
      if(!ok){finish(false);return;}run->stage=21;pressedKeys.erase('Z');keyPressed(juce::KeyPress('Z',juce::ModifierKeys::ctrlModifier,0),&roll);
    } else if(run->stage==21 && !locked && notesReady) {
      bool ok=juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(run->clipboardBefore);
      run->checks.push_back(object({{"action","One Ctrl+Z removes the entire paste without changing original notes"},{"passed",ok}}));
      if(!ok){finish(false);return;}run->stage=22;pressedKeys.erase('Y');keyPressed(juce::KeyPress('Y',juce::ModifierKeys::ctrlModifier,0),&roll);
    } else if(run->stage==22 && !locked && notesReady) {
      bool ok=result["editing"]["notes"].size()==run->clipboardBefore.size()+2;
      for(auto id:run->clipboardAdded){bool found=false;for(auto& n:items(result["editing"]["notes"]))found|=(juce::int64)n["id"]==id;ok&=found;}
      run->checks.push_back(object({{"action","Ctrl+Y restores the entire paste with stable IDs"},{"passed",ok}}));
      if(!ok||parts.empty()){finish(false);return;}selectPart(parts.front().first);run->stage=23;
      pressedKeys.erase('V');keyPressed(juce::KeyPress('V',juce::ModifierKeys::ctrlModifier,0),&roll);
    } else if(run->stage==23 && !locked && notesReady) {
      bool ok=result["editing"]["notes"].size()==run->clipboardBefore.size()+4&&roll.selected.size()==2;
      for(auto& n:items(result["editing"]["notes"]))if(roll.selected.count((juce::int64)n["id"]))ok&=Note::parse(n).part==partId;
      run->checks.push_back(object({{"action","Paste routes all copied notes into the explicitly selected destination part"},{"passed",ok}}));
      if(!ok){finish(false);return;}
      auto directory=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results");
      auto saved=directory.getNonexistentChildFile("native-session",".pht"),wave=directory.getNonexistentChildFile("native-render",".wav");
      auto save=args();save.getDynamicObject()->setProperty("path",saved.getFullPathName());save.getDynamicObject()->setProperty("ui",ui());
      auto stored=bridge.call("save_project",save);
      run->checks.push_back(object({{"action","Native MuScriptor session with audio, diagnostics and edit history saves as PHT8"},{"passed",(bool)stored["ok"]},{"error",stored["error"]}}));
      if(!(bool)stored["ok"]){finish(false);return;}
      auto render=args();render.getDynamicObject()->setProperty("path",wave.getFullPathName());render.getDynamicObject()->setProperty("options",object({{"part",var()},{"midiDb",-12.},{"reverb",25.}}));
      auto rendered=bridge.call("export_wav",render);bool wav=(bool)rendered["ok"]&&wave.getSize()>44&&number(rendered["value"],"frames")>0;
      run->checks.push_back(object({{"action","Native 24-bit WAV render completes and refuses replacing its previous export"},{"passed",wav&&!(bool)bridge.call("export_wav",render)["ok"]},{"error",rendered["error"]}}));
      if(!wav){finish(false);return;}
      auto loaded=bridge.call("open_project",object({{"path",saved.getFullPathName()}}));bool restored=(bool)loaded["ok"]&&juce::JSON::toString(loaded["value"]["result"]["editing"]["notes"],true,17)==juce::JSON::toString(result["editing"]["notes"],true,17);
      run->checks.push_back(object({{"action","Native PHT8 restores transcribed, imported and pasted notes with their exact editing state"},{"passed",restored},{"error",loaded["error"]}}));finish(restored);
    }
    juce::Timer::callAfterDelay(100, [poll] {
      if (*poll)
        (*poll)();
    });
  };
  invoke(
      "muscriptor_status", object({}),
      [this, poll, finish](var engineStatus) {
        auto settings = engineStatus["settings"].clone();
        settings.getDynamicObject()->setProperty("model", "small");
        settings.getDynamicObject()->setProperty("device", "cpu");
        invoke(
            "muscriptor_settings", object({{"settings", settings}}),
            [this, poll](var) {
              auto path =
                  juce::File::getSpecialLocation(
                      juce::File::currentExecutableFile)
                      .getSiblingFile("test-results/verification-source.wav");
              openAudioPath(path.getFullPathName());
              (*poll)();
            },
            false, [finish](String) { finish(false); });
      },
      false, [finish](String) { finish(false); });
}

bool App::runProjectTransitionTest(const juce::File& path){
  using namespace phthongos::project;
  auto directory=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/transition-"+juce::Uuid().toString());directory.createDirectory();
  std::vector<var> checks;bool passed=false;
  auto check=[&](bool ok,const String& name){checks.push_back(object({{"name",name},{"passed",ok}}));if(!ok)throw std::runtime_error(name.toStdString());};
  auto call=[&](const String& command,const var& args){auto reply=bridge.call(command,args);if(!(bool)reply["ok"])throw std::runtime_error(reply["error"].toString().toStdString());return reply["value"];};
  std::function<bool(const var&,const var&)> equal;
  equal=[&](const var& a,const var& b){if(auto* x=a.getDynamicObject()){auto* y=b.getDynamicObject();if(!y||x->getProperties().size()!=y->getProperties().size())return false;for(auto& p:x->getProperties())if(!y->hasProperty(p.name)||!equal(p.value,y->getProperty(p.name)))return false;return true;}if(a.isArray()){if(!b.isArray()||a.size()!=b.size())return false;for(int i=0;i<a.size();++i)if(!equal(a[i],b[i]))return false;return true;}if((a.isInt()||a.isInt64())&&(b.isInt()||b.isInt64()))return (juce::int64)a==(juce::int64)b;if((a.isDouble()||a.isInt()||a.isInt64())&&(b.isDouble()||b.isInt()||b.isInt64()))return (double)a==(double)b||float(double(a))==float(double(b));return a==b;};
  try{
    const auto originalHash=phthongos::services::fileHash(path);
    auto original=read(path);auto opened=call("open_project",object({{"path",path.getFullPathName()}}));
    check((int)opened["upgraded_from"]==int(original.originVersion),"Original container version recognised");
    check(opened["result"]["editing"]["notes"].size()>0,"Project restores editable notes");
    auto restored=opened["result"]["editing"]["notes"].clone();
    auto parameters=object({{"audioId",opened["audio"]["id"]},{"analysisId",opened["result"]["analysis_id"]},{"revision",opened["result"]["editing"]["revision"]},{"sensitivity",opened["result"]["sensitivity"]},{"ui",opened["ui"]},{"path",path.getFullPathName()}});
    check(!(bool)bridge.call("save_project",parameters)["ok"],"Accidental replacement of original project refused");
    auto target=directory.getChildFile("recovered-PHT8.pht");parameters.getDynamicObject()->setProperty("path",target.getFullPathName());call("save_project",parameters);
    auto saved=read(target);check(saved.originVersion==8&&(int)saved.metadata["version"]==8,"Saved project is PHT8");
    auto expectedEditor=original.metadata["editor"].clone();for(auto key:{"undo","redo"})if(auto* history=expectedEditor[key].getArray())if(history->size()>int(phthongos::Document::historyLimit))history->removeRange(0,history->size()-int(phthongos::Document::historyLimit));
    check(equal(saved.metadata["editor"],expectedEditor),"Current editor state is preserved with the 25 most recent Undo/Redo layers");
    check(equal(saved.metadata["ui"],original.metadata["ui"]),"Display, register, selection and playback settings preserved");
    check(saved.data==original.data,"Embedded audio and analysis maps preserved byte for byte");
    auto reopened=call("open_project",object({{"path",target.getFullPathName()}}));check(equal(restored,reopened["result"]["editing"]["notes"]),"PHT8 reopens with identical notes and individual metadata");
    auto command=object({{"audioId",reopened["audio"]["id"]},{"analysisId",reopened["result"]["analysis_id"]},{"revision",reopened["result"]["editing"]["revision"]},{"sensitivity",reopened["result"]["sensitivity"]},{"action",object({{"kind","undo"}})}});
    if((bool)reopened["result"]["editing"]["can_undo"]){auto undone=call("edit_notes",command);command.getDynamicObject()->setProperty("revision",undone["editing"]["revision"]);command.getDynamicObject()->setProperty("action",object({{"kind","redo"}}));auto redone=call("edit_notes",command);check(equal(restored,redone["editing"]["notes"]),"Undo and Redo remain operational after conversion");}
    check(phthongos::services::fileHash(path)==originalHash,"Original file remains unchanged");passed=true;
  }catch(const std::exception& e){checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));}
  auto report=object({{"passed",passed},{"checks",array(checks)}});directory.getChildFile("report.json").replaceWithText(juce::JSON::toString(report,false,17));directory.getParentDirectory().getChildFile("transition-latest.json").replaceWithText(juce::JSON::toString(object({{"passed",passed},{"directory",directory.getFullPathName()},{"checks",array(checks)}}),false,17));return passed;
}

void App::runSaveCloseTests(const juce::File& original,std::function<void(bool)> done){
  struct Run{int stage=0,closed=0,choices=0;double started=juce::Time::getMillisecondCounterHiRes();bool finished=false;juce::File directory,legacy,target;String originalHash;std::vector<var> checks;};
  auto run=std::make_shared<Run>();auto poll=std::make_shared<std::function<void()>>();
  run->directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-save-close-"+juce::Uuid().toString());run->directory.createDirectory();run->legacy=run->directory.getChildFile("legacy.pht");run->target=run->directory.getChildFile("saved-PHT8.pht");
  auto check=[run](bool ok,const String& label){run->checks.push_back(object({{"name",label},{"passed",ok}}));if(!ok)throw std::runtime_error(label.toStdString());};
  auto finish=[this,run,poll,done](bool ok){if(run->finished)return;run->finished=true;stopTimer();testSaveDestination={};auto out=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/save-close-latest.json");out.getParentDirectory().createDirectory();out.replaceWithText(juce::JSON::toString(object({{"passed",ok},{"language",localisation::code()},{"stage",run->stage},{"checks",array(run->checks)}}),false,17));juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
  auto button=[this](const String& label){if(!confirmation)return false;for(auto* child:confirmation->getChildren())if(auto* b=dynamic_cast<juce::TextButton*>(child))if(b->getButtonText()==tr(label))return true;return false;};
  *poll=[this,run,poll,finish,check,button,original]{try{
    if(juce::Time::getMillisecondCounterHiRes()-run->started>60000)throw std::runtime_error("Save/close test timeout");
    if(run->stage==0&&notesReady&&!locked){
      dirty=true;projectPath={};testSaveDestination=[run](const String& initial){++run->choices;if(!initial.contains("PHT8"))throw std::runtime_error("Missing PHT8 save suggestion");return run->choices==1?run->legacy:run->target;};
      requestClose([run]{++run->closed;});run->stage=1;
    }else if(run->stage==1&&button("Enregistrer")){check(closing,"Closing stays pending during confirmation");confirmation->answer(1);run->stage=2;
    }else if(run->stage==2&&button(juce::String::fromUTF8("Enregistrer sous…"))){check(run->closed==0&&dirty,"Attempt to replace legacy file explains conversion and keeps session open");confirmation->answer(1);run->stage=3;
    }else if(run->stage==3&&run->closed==1){
      check(run->choices==2&&!dirty&&!closing&&run->target.existsAsFile(),"Save As retry completes one save and one close continuation");check(phthongos::project::read(run->target).originVersion==8,"Save As produces PHT8");
      control("play",0,[this,run](var){control("pause",0,[this,run](var){control("stop",0,[run](var){run->stage=4;});});});run->stage=30;
    }else if(run->stage==4){check(!dirty,"Play, pause and stop leave saved project clean");requestClose([run]{++run->closed;});check(run->closed==2&&!confirmation,"Clean project closes without asking to save again");dirty=true;requestClose([run]{++run->closed;});run->stage=5;
    }else if(run->stage==5&&button("Enregistrer")){auto* same=confirmation.get();requestClose([run]{run->closed+=100;});check(confirmation.get()==same&&run->closed==2,"Repeated close requests do not duplicate pending save/close");confirmation->answer(0);run->stage=6;
    }else if(run->stage==6&&!confirmation&&!closing){check(dirty&&run->closed==2,"Cancelling close preserves unsaved session");projectPath={};testSaveDestination=[](const String&){return juce::File();};requestClose([run]{++run->closed;});run->stage=7;
    }else if(run->stage==7&&button("Enregistrer")){confirmation->answer(1);run->stage=8;
    }else if(run->stage==8&&!confirmation&&!closing&&!locked){check(dirty&&run->closed==2,"Cancelling Save As releases close state without closing or clearing dirty flag");projectPath=run->directory.getChildFile("invalid.mid").getFullPathName();requestClose([run]{++run->closed;});run->stage=9;
    }else if(run->stage==9&&button("Enregistrer")){confirmation->answer(1);run->stage=10;
    }else if(run->stage==10&&!confirmation&&!closing&&!locked){check(dirty&&run->closed==2&&status.getText().contains(".pht"),"Failed save remains unsaved and displays its actual error");auto* alert=dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());check(alert!=nullptr,"Failed save is shown in a visible warning dialog");if(alert)alert->exitModalState(0);projectPath=run->target.getFullPathName();requestClose([run]{++run->closed;});run->stage=11;
    }else if(run->stage==11&&button("Enregistrer")){confirmation->answer(1);run->stage=12;
    }else if(run->stage==12&&run->closed==3){check(!dirty&&!closing,"Retry after failed save successfully saves and closes exactly once");check(phthongos::services::fileHash(original)==run->originalHash,"Actual user project remains unchanged throughout the test");finish(true);return;}
    juce::Timer::callAfterDelay(75,[poll]{if(*poll)(*poll)();});
  }catch(const std::exception& e){run->checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));finish(false);}};
  try{run->originalHash=phthongos::services::fileHash(original);if(!original.copyFileTo(run->legacy))throw std::runtime_error("Cannot create isolated test copy");check(phthongos::project::read(run->legacy).originVersion<8,"Test uses an isolated legacy project copy");auto reply=bridge.call("open_project",object({{"path",run->legacy.getFullPathName()}}));if(!(bool)reply["ok"])throw std::runtime_error(reply["error"].toString().toStdString());auto opened=reply["value"];audio=opened["audio"];roll.audio=audio;loadUi(opened["ui"]);updateResult(opened["result"]);setSize(1400,900);prepare([poll](var){if(*poll)(*poll)();});}
  catch(const std::exception& e){run->checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));finish(false);}
}

void App::runGuidedTests(const juce::File& source,std::function<void(bool)> done){
  struct Run{int stage=0;double started=juce::Time::getMillisecondCounterHiRes();bool finished=false;std::vector<var> checks;};
  auto run=std::make_shared<Run>();auto poll=std::make_shared<std::function<void()>>();
  auto check=[run](bool ok,const String& label){run->checks.push_back(object({{"name",label},{"passed",ok}}));if(!ok)throw std::runtime_error(label.toStdString());};
  auto finish=[this,run,poll,done](bool ok){if(run->finished)return;run->finished=true;stopTimer();auto folder=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results");folder.createDirectory();folder.getChildFile("guided-flow-latest.json").replaceWithText(juce::JSON::toString(object({{"passed",ok},{"language",localisation::code()},{"stage",run->stage},{"checks",array(run->checks)}}),false,17));juce::FileOutputStream picture(folder.getChildFile("guided-flow.png"));picture.setPosition(0);picture.truncate();juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),picture);juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
  auto paint=[this](int i){std::array<int,3> pitch={60,64,67};std::array<double,3> a={.3,1.5,2.6},b={1.2,2.4,3.6};auto from=juce::Point<float>(float(roll.x(a[i])),float(roll.pitchY(pitch[i]))),to=juce::Point<float>(float(roll.x(b[i])),from.y);auto event=[this,from](juce::Point<float> at,bool drag){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&roll,&roll,juce::Time::getCurrentTime(),from,juce::Time::getCurrentTime(),1,drag);};roll.mouseDown(event(from,false));roll.mouseDrag(event(to,true));roll.mouseUp(event(to,true));};
  *poll=[this,run,poll,check,finish,paint]{try{
    if(run->finished)return;if(juce::Time::getMillisecondCounterHiRes()-run->started>45000)throw std::runtime_error("Guided UI flow timeout");
    if(!locked&&notesReady){auto count=roll.spectralMask.size();
      if(run->stage==0&&!audio.isVoid()){
        check(result.isVoid(),"Audio-only project starts without MIDI");check(buttonMap["open"]->getButtonText()==(localisation::language()==localisation::Language::english?"Open audio":"Ouvrir audio"),"Requested UI language is active before editing");buttonMap["guidedOptions"]->onClick();roll.view.zoom=1;roll.view.pan=0;roll.view.pitchZoom=4;roll.view.pitchPan=.5;setMaskTool(1);check(roll.displayMode==2&&roll.spectral&&correctionOpen,"Guided toolbar opens spectrogram overlay and panel");run->stage=1;paint(0);
      }else if(run->stage>=1&&run->stage<=3&&count==std::size_t(run->stage)){
        check(items(result["editing"]["notes"]).empty(),"Brush leaves MIDI empty");if(run->stage<3){int next=run->stage++;paint(next);}else{run->stage=4;analyzeGuided();}
      }else if(run->stage==4&&!guidedReport.isVoid()){
        check(roll.guidedGhosts.size()==3&&roll.guidedCurve.isArray()&&items(result["editing"]["notes"]).empty(),"Async DSP displays curve and ghost proposals without MIDI changes");
        auto before=juce::JSON::toString(result["editing"]);auto categorized=guidedReport.clone();categorized["notes"][2].getDynamicObject()->setProperty("category","unlikely");installGuided(categorized);
        check(visibleGuidedNotes().size()==2&&roll.guidedGhosts.size()==2&&guidedReport["notes"].size()==3&&juce::JSON::toString(result["editing"])==before,"Default category filter hides unlikely proposals without deleting candidates or touching MIDI");
        auto savedUi=ui();guidedCategories.insert("unlikely");guidedUseScale=true;guidedShowProposals=false;loadUi(savedUi);updateGuidedView();
        check(!guidedCategories.count("unlikely")&&!guidedUseScale&&guidedShowProposals,"Category and scale preferences restore independently from MIDI");
        buildSide();run->stage=5;bool clicked=false;auto label=localisation::format("guided_add_category",{tr(String::fromUTF8("Probables")),"2"});for(auto* child:side.widgets)if(auto* button=dynamic_cast<juce::TextButton*>(child))if(button->getButtonText()==label){clicked=true;button->onClick();break;}check(clicked,"Category-specific Add command is reachable from the panel");
      }else if(run->stage==5&&items(result["editing"]["notes"]).size()==2){
        check(roll.selected.size()==2&&roll.guidedGhosts.empty()&&guidedReport["notes"].size()==1,"Only selected proposals become selected MIDI notes; remaining proposals stay available");check(!guidedShowCurve&&!roll.maskVisible&&roll.guidedCurve.isVoid()&&roll.maskMode==0&&roll.tool==0,"Accepting notes restores clear editing view without deleting the mask");guidedCategories.insert("unlikely");guidedShowProposals=true;updateGuidedView();check(roll.guidedGhosts.size()==1,"Remaining proposals can be shown again");run->stage=6;command(object({{"kind","undo"}}));
      }else if(run->stage==6&&items(result["editing"]["notes"]).empty()){
        check(roll.spectralMask.size()==3&&guidedReport.isVoid(),"One undo removes accepted group and invalidates proposals, preserving mask");run->stage=7;command(object({{"kind","redo"}}));
      }else if(run->stage==7&&items(result["editing"]["notes"]).size()==2){check(roll.spectralMask.size()==3,"Redo restores accepted MIDI with mask intact");check(buttonMap["open"]->getButtonText()==(localisation::language()==localisation::Language::english?"New transcription":"Nouvelle transcription"),"Requested UI language survives asynchronous analysis and edits");roll.maskMode=0;rebuildSide();run->stage=8;markerDialog(var(),1.25);
      }else if((run->stage==8||run->stage==10)&&juce::Component::getCurrentlyModalComponent()){
        auto* dialog=dynamic_cast<juce::DialogWindow*>(juce::Component::getCurrentlyModalComponent());auto* form=dialog?dynamic_cast<Form*>(dialog->getContentComponent()):nullptr;
        check(form&&form->textFields.count("name"),"Marker name dialog is reachable and editable");auto draft=form->state();draft.getDynamicObject()->setProperty("name",run->stage==8?"Check later":"Renamed");run->stage++;form->onApply(draft);
      }else if(run->stage==9&&roll.markers.size()==1){check(number(roll.markers[0],"time")==1.25&&roll.markers[0]["name"].toString()=="Check later","Marker dialog adds a named timeline flag through the real editor");run->stage=10;markerDialog(roll.markers[0],0);
      }else if(run->stage==11&&roll.markers.size()==1&&roll.markers[0]["name"].toString()=="Renamed"){
        check(items(result["editing"]["notes"]).size()==2,"Renaming a marker leaves accepted MIDI notes intact");run->stage=12;command(object({{"kind","undo"}}));
      }else if(run->stage==12&&roll.markers.size()==1&&roll.markers[0]["name"].toString()=="Check later"){check(true,"Marker rename is undone through the application history");
        roll.view.pitchZoom=4;roll.view.pitchPan=.5;roll.selectIds({roll.notes[0].id,roll.notes[1].id});
        auto centre=roll.rect(roll.notes[0]).getCentre();auto to=centre+juce::Point<float>(1,float(roll.row()*3));
        auto event=[this,centre](juce::Point<float> p){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),p,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier|juce::ModifierKeys::ctrlModifier|juce::ModifierKeys::shiftModifier),1,0,0,0,0,&roll,&roll,juce::Time::getCurrentTime(),centre,juce::Time::getCurrentTime(),1,true);};
        run->stage=13;roll.mouseDown(event(centre));roll.mouseDrag(event(to));roll.mouseUp(event(to));
      }else if(run->stage==13&&roll.notes.size()==4){check(roll.selected.size()==2,"Duplicate drag selects only the two new copies");
        for(const auto& copy:roll.notes)if(roll.selected.count(copy.id)){bool matched=false;for(const auto& old:roll.notes)if(!roll.selected.count(old.id)&&copy.pitch==old.pitch-3&&copy.start==old.start&&copy.end==old.end)matched=true;check(matched,"GUI duplicate keeps exact original timing at a lower pitch");}
        run->stage=14;command(object({{"kind","undo"}}));
      }else if(run->stage==14&&roll.notes.size()==2){check(true,"One GUI undo removes the entire duplicated group");run->stage=15;command(object({{"kind","redo"}}));
      }else if(run->stage==15&&roll.notes.size()==4){check(true,"GUI redo restores the duplicated group");finish(true);return;}
    }
    juce::Timer::callAfterDelay(75,[poll]{if(*poll)(*poll)();});
  }catch(const std::exception& e){run->checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));finish(false);}};
  setSize(1400,900);openAudioPath(source.getFullPathName());(*poll)();
}

void App::runWavExportTests(std::function<void(bool)> done,bool useBank){
  struct Run{int stage=0;double started=juce::Time::getMillisecondCounterHiRes();bool finished=false;juce::File directory,target;String document;std::vector<var> checks;};
  auto run=std::make_shared<Run>();auto poll=std::make_shared<std::function<void()>>();
  run->directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-wav-check-"+juce::Uuid().toString());run->directory.createDirectory();run->target=run->directory.getChildFile("new-render.wav");
  auto check=[run](bool ok,const String& label){run->checks.push_back(object({{"name",label},{"passed",ok}}));if(!ok)throw std::runtime_error(label.toStdString());};
  auto finish=[this,run,poll,done](bool ok){if(run->finished)return;run->finished=true;stopTimer();testSaveDestination={};auto output=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/wav-export-latest.json");output.getParentDirectory().createDirectory();output.replaceWithText(juce::JSON::toString(object({{"passed",ok},{"language",localisation::code()},{"directory",run->directory.getFullPathName()},{"checks",array(run->checks)}}),false,17));if(auto* alert=dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent()))alert->exitModalState(0);juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
  *poll=[this,run,poll,check,finish,useBank]{try{
    if(juce::Time::getMillisecondCounterHiRes()-run->started>30000)throw std::runtime_error("WAV export UI timeout");
    if(run->stage==0&&!locked&&notesReady){
      auto bank=bridge.call("soundbank_status",object({}))["value"];check((bool)bank["available"]==useBank,useBank?"Shared General MIDI bank is detected by a fresh application":"Regression fixture has no General MIDI bank");
      if(useBank)check(juce::File(bank["path"].toString()).existsAsFile(),"Shared bank resolves to an existing external SF2");
      run->document=juce::JSON::toString(result["editing"]);dirty=false;partId=Note::parse(result["editing"]["notes"][0]).part;midiDb=-12;reverbEnabled=true;reverbAmount=25;
      testSaveDestination=[run,check](const String& suggestion){check(suggestion.endsWith(".wav")&&!suggestion.endsWith("source.mid.wav"),"WAV export suggests a usable destination");return run->target;};
      run->stage=1;exportWav();
    }else if(run->stage==1&&!locked&&run->target.existsAsFile()){
      juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(new juce::FileInputStream(run->target),true));
      check(reader&&reader->sampleRate==48000&&reader->numChannels==2&&reader->bitsPerSample==24&&reader->lengthInSamples>24000,"Actual export UI creates a readable stereo 24-bit 48 kHz WAV");
      juce::AudioBuffer<float> samples(2,int(reader->lengthInSamples));check(reader->read(&samples,0,samples.getNumSamples(),0,true,true)&&samples.getMagnitude(0,samples.getNumSamples())>.001f,"Exported voice contains audible samples");
      check(!dirty&&juce::JSON::toString(result["editing"])==run->document,"Export preserves notes, instruments and saved state");
      check(status.getText().contains(run->target.getFullPathName()),"Successful export shows the actual created path");
      check(status.getText().contains(tr(String::fromUTF8("Banque General MIDI absente : WAV rendu au piano de secours, comme la lecture.")))!=useBank,"Export uses the detected bank, with an explicit notice only for fallback");
      run->stage=2;exportWav();
    }else if(run->stage==2&&!locked&&juce::Component::getCurrentlyModalComponent()){
      auto* alert=dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());check(alert!=nullptr,"Failed export opens a visible error dialog");
      check(status.getText().contains(tr(String::fromUTF8("Ce fichier existe déjà. Choisissez un autre nom pour préserver l’export précédent."))),"Failed export retains its actual error in status");
      check(run->target.getSize()>44,"Failed retry preserves the previous WAV");finish(true);return;
    }
    juce::Timer::callAfterDelay(75,[poll]{if(*poll)(*poll)();});
  }catch(const std::exception& e){run->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())},{"status",status.getText()}}));finish(false);}};
  // A short choir MIDI reproduces the user's missing-bank export, independently of their files.
  const unsigned char bytes[]={0x4d,0x54,0x68,0x64,0,0,0,6,0,0,0,1,1,0xe0,0x4d,0x54,0x72,0x6b,0,0,0,16,0,0xc0,52,0,0x90,60,100,0x83,0x60,0x80,60,0,0,0xff,0x2f,0};
  auto midi=run->directory.getChildFile("source.mid");midi.replaceWithData(bytes,sizeof(bytes));
  invoke("import_midi_audio",object({{"midiPath",midi.getFullPathName()},{"audioPath",var()}}),[this,poll](var imported){audio=imported["audio"];roll.audio=audio;updateResult(imported["result"]);prepare([poll](var){(*poll)();});},false,[finish](String){finish(false);});
}

void App::runToneFlowTests(std::function<void(bool)> done) {
  stopTimer();setSize(1400,900);
  struct Run {int stage=0;bool finished=false,newProject=false;double started=juce::Time::getMillisecondCounterHiRes();juce::File folder;var notes;std::set<std::int64_t> outside;std::vector<var> checks;};
  auto run=std::make_shared<Run>();auto poll=std::make_shared<std::function<void()>>();
  run->folder=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/tone-"+juce::Uuid().toString());run->folder.createDirectory();
  auto finish=[this,run,poll,done](bool ok){if(run->finished)return;run->finished=true;testSaveDestination={};
    auto report=object({{"passed",ok},{"stage",run->stage},{"language",localisation::code()},{"checks",array(run->checks)},{"directory",run->folder.getFullPathName()}});
    run->folder.getParentDirectory().getChildFile("tone-latest.json").replaceWithText(juce::JSON::toString(report));
    juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
  auto check=[run](bool ok,const String& name){run->checks.push_back(object({{"name",name},{"passed",ok}}));if(!ok)throw std::runtime_error(name.toStdString());};
  try {
    using namespace phthongos::midi;
    auto content=encode(1,480,{{{0,255,3,{'P','i','a','n','o'}},{0,192,0,{0}},{0,144,0,{60,90}},{480,128,0,{60,0}},{480,144,0,{61,90}},{960,128,0,{61,0}},{960,255,47,{}}},
      {{0,255,3,{'V','o','i','c','e'}},{0,193,0,{52}},{240,145,0,{64,90}},{480,129,0,{64,0}},{720,145,0,{66,90}},{960,129,0,{66,0}},{960,255,47,{}}}});
    auto midi=run->folder.getChildFile("voices.mid");midi.replaceWithData(content.data(),content.size());
    auto imported=bridge.call("import_midi_audio",object({{"midiPath",midi.getFullPathName()},{"audioPath",var()}}));check((bool)imported["ok"],"Synthetic two-voice MIDI imported");audio=imported["value"]["audio"];roll.audio=audio;updateResult(imported["value"]["result"],false);
    auto request=args();request.getDynamicObject()->setProperty("action",object({{"kind","toggle_part_lock"},{"part",(int)result["midi_parts"][1]["identity"]}}));
    auto lockedVoice=bridge.call("edit_notes",request);check((bool)lockedVoice["ok"],"Second voice locked");updateResult(lockedVoice["value"],false);
    run->notes=result["editing"]["notes"].clone();for(const auto& v:items(run->notes)){auto n=Note::parse(v);if(n.pitch==61||n.pitch==64||n.pitch==66)run->outside.insert(n.id);}
    tone=object({{"enabled",false},{"scope","all"},{"scale",object({{"tonic",0},{"mode","minor"}})}});toneReport=var();tonalityOpen=true;toneOptions();roll.clearSelection();
    newProjectRequested=[run]{run->newProject=true;};auto menu=projectMenu();bool tempo=false;int first=-1;for(juce::PopupMenu::MenuItemIterator it(menu);it.next();){if(first<0)first=it.getItem().itemID;tempo|=it.getItem().itemID==9;}
    check(first==10&&!tempo,"New project is first in File menu; tempo remains only beside grid");
    buttonMap["toneSelect"]->onClick();
  } catch(const std::exception& e){run->checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));finish(false);return;}
  *poll=[this,run,poll,finish,check]{
    try {
      if(juce::Time::getMillisecondCounterHiRes()-run->started>20000)throw std::runtime_error("Tone/new-project flow timed out");
      if(run->stage==0&&(bool)tone["enabled"]&&!toneReport.isVoid()){
        check(roll.tonal==run->outside&&roll.selected.size()==1,"Select performs analysis, displays all out-of-scale notes and selects only unlocked notes in one click");
        check(toneHelp.getText().contains("Magenta")&&toneCaption.getText().contains("magenta"),"Color captions use magenta in the selected language");
        check(buttonMap["toneAdjust"]->getX()<buttonMap["toneSelect"]->getX(),"Configure scale precedes select in the compact workflow");
        roll.displayMode=2;roll.spectral=true;roll.scoreDuration=2;roll.view.pitchZoom=8;roll.view.pitchPan=.5;
        roll.spectrum=juce::Image(juce::Image::ARGB,200,128,true);{juce::Graphics g(roll.spectrum);g.fillAll(juce::Colour(0xffbd687c));g.setColour(juce::Colour(0xffffdb42));g.fillRect(70,0,40,128);}roll.spectrumStart=0;roll.spectrumEnd=2;resized();
        {juce::FileOutputStream image(run->folder.getChildFile("magenta-notes.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);}
        buttonMap["toneHide"]->onClick();roll.clearSelection();run->stage=1;buttonMap["toneShow"]->onClick();
      } else if(run->stage==1&&(bool)tone["enabled"]&&!roll.tonal.empty()){
        check(roll.selected.empty()&&roll.tonal==run->outside,"Display-only action highlights without selecting");
        buttonMap["toneHide"]->onClick();check(roll.tonal.empty(),"Hide removes the diagnostic highlight");run->stage=2;buttonMap["toneSelect"]->onClick();
      } else if(run->stage==2&&(bool)tone["enabled"]&&!roll.tonal.empty()){
        check(roll.tonal==run->outside&&roll.selected.size()==1,"Select after hiding restores highlight and locked-safe selection");
        check(juce::JSON::toString(run->notes)==juce::JSON::toString(result["editing"]["notes"]),"Diagnostic workflow preserves every MIDI event");
        dirty=true;run->stage=3;requestClose(newProjectRequested);check(confirmation!=nullptr,"New project asks about unsaved changes");confirmation->answer(0);
      } else if(run->stage==3&&!confirmation){
        check(!run->newProject&&!result.isVoid(),"Cancelling new project preserves the session");run->stage=4;requestClose(newProjectRequested);confirmation->answer(2);
      } else if(run->stage==4&&!confirmation){
        check(run->newProject,"Discard choice authorizes a new project");run->newProject=false;dirty=true;
        testSaveDestination=[run](const String&){return run->folder.getChildFile("before-new.pht");};run->stage=5;requestClose(newProjectRequested);confirmation->answer(1);
      } else if(run->stage==5&&run->newProject&&!confirmation&&!locked){
        check(run->folder.getChildFile("before-new.pht").existsAsFile(),"Save choice completes project storage before authorizing replacement");
        App empty(run->folder.getChildFile("new-cache"));empty.stopTimer();check(empty.audio.isVoid()&&empty.result.isVoid()&&empty.projectPath.isEmpty()&&empty.roll.notes.empty()&&empty.roll.markers.empty()&&empty.roll.lockedParts.empty()&&!(bool)empty.tone["enabled"],"Replacement App starts with empty audio, MIDI, markers, locks and diagnostics");
        selectPart((int)result["midi_parts"][1]["identity"]);check(!buttonMap["partDelete"]->isEnabled(),"Locked voice trash stays disabled");
        selectPart((int)result["midi_parts"][0]["identity"]);check(buttonMap["partDelete"]->isEnabled(),"Nonempty unlocked voice can be deleted");
        run->stage=6;prepare([this](var){buttonMap["partDelete"]->onClick();});
      } else if(run->stage==6&&confirmation){
        bool named=false,counted=false;for(auto* child:confirmation->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child)){named|=label->getText().contains(part.getText());counted|=label->getText().contains("2");}
        check(named&&counted,"Deletion confirmation names the voice and its total note count");
        check(juce::JSON::toString(run->notes)==juce::JSON::toString(result["editing"]["notes"]),"Asking deletion leaves notes intact");
        run->stage=7;confirmation->answer(0);
      } else if(run->stage==7&&!confirmation){
        check(juce::JSON::toString(run->notes)==juce::JSON::toString(result["editing"]["notes"]),"Cancelling deletion preserves every note");run->stage=8;buttonMap["partDelete"]->onClick();
      } else if(run->stage==8&&confirmation){run->stage=9;confirmation->answer(1);
      } else if(run->stage==9&&!confirmation&&!locked&&result["midi_parts"].size()==1){
        check(result["editing"]["notes"].size()==2,"Confirmed deletion removes just the chosen voice");
        for(const auto& note:items(result["editing"]["notes"]))check(Note::parse(note).part==(int)result["midi_parts"][0]["identity"],"Remaining notes belong to the other voice");
        run->stage=10;command(object({{"kind","undo"}}));
      } else if(run->stage==10&&!locked&&result["midi_parts"].size()==2){
        check(juce::JSON::toString(run->notes)==juce::JSON::toString(result["editing"]["notes"]),"One undo restores the entire deleted voice exactly");finish(true);return;
      }
    }catch(const std::exception& e){run->checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));finish(false);return;}
    juce::Timer::callAfterDelay(30,[poll]{if(*poll)(*poll)();});
  };
  (*poll)();
}

void App::runVersionSaveTests(std::function<void(bool)> done){
  stopTimer();setSize(1400,900);setVisible(true);
  struct Run{int stage=0;double started=juce::Time::getMillisecondCounterHiRes();bool finished=false;juce::File folder,a,b,first,second,last;var dynamicsNotes;String firstHash;std::map<String,int> calls;double savedAt=0;var goodOutput;std::vector<var> checks;};
  auto r=std::make_shared<Run>();auto poll=std::make_shared<std::function<void()>>();
  r->folder=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/version-"+juce::Uuid().toString());r->folder.createDirectory();r->a=r->folder.getChildFile("Initial");r->b=r->folder.getChildFile("Other");r->a.createDirectory();r->b.createDirectory();
  auto finish=[this,r,poll,done](bool ok){if(r->finished)return;r->finished=true;testSaveDestination={};bridge.requestObserver={};auto report=object({{"passed",ok},{"stage",r->stage},{"language",localisation::code()},{"directory",r->folder.getFullPathName()},{"checks",array(r->checks)}});r->folder.getParentDirectory().getChildFile("version-latest.json").replaceWithText(juce::JSON::toString(report));juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
  auto check=[r](bool ok,const String& text){r->checks.push_back(object({{"name",text},{"passed",ok}}));if(!ok)throw std::runtime_error(text.toStdString());};
  auto save=[this,r,finish](bool as,int stage){r->stage=-1;saveProject(as,[r,finish,stage](bool ok){if(!ok){finish(false);return;}r->stage=stage;});};
  try{
    check(!timestampSaves&&defaultProjectFolder.isEmpty(),"Defaults retain classic saving and no mandatory folder");
    storeSavePreferences(true,r->a.getFullPathName());
    {App fresh(savePreferencesFile.getParentDirectory().getChildFile("audio"));fresh.stopTimer();check(fresh.timestampSaves&&fresh.defaultProjectFolder==r->a.getFullPathName(),"Application restart retains both save preferences");}
    auto clock=juce::Time(2026,9,20,11,51,32);auto one=versionedProjectPath(r->a,"collision",clock);one.replaceWithText("one");auto two=versionedProjectPath(r->a,"collision",clock);two.replaceWithText("two");auto three=versionedProjectPath(r->a,"collision",clock);
    check(one.getFileName()=="collision_2026_10_20_115132.pht"&&two.getFileName().endsWith("_02.pht")&&three.getFileName().endsWith("_03.pht")&&one.loadFileAsString()=="one","Same-second collisions use _02/_03 and preserve prior files");
    const unsigned char bytes[]={0x4d,0x54,0x68,0x64,0,0,0,6,0,0,0,1,1,0xe0,0x4d,0x54,0x72,0x6b,0,0,0,16,0,0xc0,52,0,0x90,60,100,0x83,0x60,0x80,60,0,0,0xff,0x2f,0};auto midi=r->folder.getChildFile("source.mid");midi.replaceWithData(bytes,sizeof(bytes));auto imported=bridge.call("import_midi_audio",object({{"midiPath",midi.getFullPathName()},{"audioPath",var()}}));check((bool)imported["ok"],"Source MIDI imported");audio=imported["value"]["audio"];roll.audio=audio;updateResult(imported["value"]["result"],false);
    testSaveDestination=[r](const String& initial){if(juce::File(initial).getParentDirectory()!=r->a)throw std::runtime_error("Default folder not proposed");return r->a.getChildFile("session@#.pht");};
    prepare([save](var){save(false,1);});
  }catch(const std::exception& e){r->checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));finish(false);return;}
  *poll=[this,r,poll,finish,check,save]{try{
    if(r->finished)return;if(juce::Time::getMillisecondCounterHiRes()-r->started>45000)throw std::runtime_error("Version-save workflow timed out: "+status.getText().toStdString());
    if(r->stage==1&&!locked){
      r->first=juce::File(projectPath);r->firstHash=phthongos::services::fileHash(r->first);
      check(r->first.existsAsFile()&&r->first.getParentDirectory()==r->a&&r->first.getFileName().startsWith("session@#_")&&projectSaveBase=="session@#","First Ctrl+S proposes default folder and creates a timestamped version");
      auto saved=phthongos::project::read(r->first);check(saved.metadata["ui"]["saveBaseName"].toString()=="session@#"&&saved.metadata["ui"]["saveFolder"].toString()==r->a.getFullPathName(),"PHT stores stable base name and actual last save folder");
      auto beforeAudio=audio;auto beforeSections=openSections;audio=audio.clone();audio.getDynamicObject()->setProperty("midi_only",false);openSections.insert("dynamics");buildSide();
      juce::Slider* minimum=nullptr;juce::Slider* maximum=nullptr;for(auto* child:side.getChildren())if(auto* slider=dynamic_cast<juce::Slider*>(child)){if(slider->getName()=="Min")minimum=slider;if(slider->getName()=="Max")maximum=slider;}
      check(minimum&&maximum,"The dynamics panel exposes both Min/Max controls");minimum->setValue(36,juce::sendNotificationSync);maximum->setValue(119,juce::sendNotificationSync);
      check(dynMapping["min"].isInt()&&dynMapping["max"].isInt()&&int(dynMapping["min"])==36&&int(dynMapping["max"])==119,"Actual Min/Max field changes retain integer types");audio=beforeAudio;openSections=beforeSections;buildSide();
      dynMapping=object({{"level",80.},{"contrast",100.},{"min",35.0},{"max",120.0}});hasDynDraft=true;manualRange=true;r->dynamicsNotes=result["editing"]["notes"].clone();save(false,2);
    }else if(r->stage==2&&!locked){
      r->second=juce::File(projectPath);check(r->second!=r->first&&r->second.existsAsFile()&&phthongos::services::fileHash(r->first)==r->firstHash&&projectSaveBase=="session@#","Next Ctrl+S creates a distinct file without changing the previous version or accumulating dates");
      check(recentProjects.size()==2&&recentProjects[0]["path"].toString()==projectPath&&recentProjects[0]["name"].toString()==r->second.getFileName(),"Timestamped versions of the same project retain separate recent-file entries with actual filenames");
      auto saved=phthongos::project::read(r->second);auto mapping=saved.metadata["ui"]["dynamicsDraft"]["mapping"];
      check(mapping["min"].isInt()&&int(mapping["min"])==35&&mapping["max"].isInt()&&int(mapping["max"])==120,"A PHT saved from a legacy decimal dynamics draft reopens with exact integer settings");
      check(juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->dynamicsNotes),"Saving the normalised dynamics draft preserves all note values and velocities");
      testSaveDestination=[r](const String& initial){if(juce::File(initial).getParentDirectory()!=r->a||juce::File(initial).getFileName()!="session@#.pht")throw std::runtime_error("Save As forgot base or project folder");return r->b.getChildFile("renamed.pht");};save(true,3);
    }else if(r->stage==3&&!locked){
      check(juce::File(projectPath)==r->b.getChildFile("renamed.pht")&&projectSaveBase=="renamed"&&projectSaveFolder==r->b.getFullPathName(),"Save As honors exact chosen name and remembers its new folder");save(false,4);
    }else if(r->stage==4&&!locked){
      r->last=juce::File(projectPath);check(r->last.getParentDirectory()==r->b&&r->last.getFileName().startsWith("renamed_")&&recentProjects.size()==4,"Later Ctrl+S uses Save As folder and retains each saved file in recents");
      auto opened=bridge.call("open_project",object({{"path",projectPath}}));check((bool)opened["ok"],"Timestamped PHT reopens");audio=opened["value"]["audio"];roll.audio=audio;loadUi(opened["value"]["ui"]);updateResult(opened["value"]["result"],false);
      auto info=bridge.call("project_save_info",object({}))["value"];check(projectSaveBase=="renamed"&&juce::File(projectSaveSuggestion(info)).getParentDirectory()==r->b,"Reopening retains project folder despite a different application default");r->stage=-1;prepare([save](var){save(false,5);});
    }else if(r->stage==5&&!locked){
      check(juce::File(projectPath)!=r->last&&projectSaveBase=="renamed"&&juce::File(projectPath).getParentDirectory()==r->b,"Saving after reopen preserves the base without timestamp stacking");
      r->last=juce::File(projectPath);storeSavePreferences(false,r->a.getFullPathName());save(false,6);
    }else if(r->stage==6&&!locked){
      check(juce::File(projectPath)==r->last,"Disabled increment keeps classic overwrite at the same path");
      auto hash=phthongos::services::fileHash(r->last);auto request=args();request.getDynamicObject()->setProperty("path",projectPath);request.getDynamicObject()->setProperty("ui",ui());request.getDynamicObject()->setProperty("create_only",true);auto refused=bridge.call("save_project",request);check(!(bool)refused["ok"]&&phthongos::services::fileHash(r->last)==hash,"Exclusive save refuses an existing destination without modifying it");
      dirty=true;testSaveDestination=[](const String&){return juce::File();};r->stage=-1;saveProject(true,[r,check](bool ok){check(!ok,"Cancelling Save As reports cancellation");r->stage=7;});
    }else if(r->stage==7&&!locked){
      check(juce::File(projectPath)==r->last&&projectSaveBase=="renamed"&&dirty,"Cancelled Save As preserves path, base and unsaved state");
      openProject(r->first.getFullPathName());r->stage=70;
    }else if(r->stage==70&&confirmation){
      check(dirty&&juce::File(projectPath)==r->last,"Opening a recent project first protects unsaved changes");
      resized();check(getChildComponent(getNumChildComponents()-1)==confirmation.get(),"Confirmation stays above every toolbar after layout");auto* under=getComponentAt(buttonMap["partInstrument"]->getBounds().getCentre());check(under&&(under==confirmation.get()||confirmation->isParentOf(under)),"Modal veil intercepts clicks over the voice toolbar");r->stage=71;confirmation->answer(0);
    }else if(r->stage==71&&!confirmation){
      check(dirty&&juce::File(projectPath)==r->last,"Cancelling recent-project opening preserves the current session");dirty=false;r->stage=72;openProject(r->first.getFullPathName());
    }else if(r->stage==72&&!locked&&notesReady&&juce::File(projectPath)==r->first){
      check(recentProjects.size()==5&&std::any_of(recentProjects.begin(),recentProjects.end(),[this](const var& e){return e["path"].toString()==projectPath;}),"Opening an older recent file preserves all versions without duplicating that path");
      auto identity=projectIdentity;for(int i=0;i<12;++i){projectIdentity=identity;projectSaveBase="Same name";auto file=r->folder.getChildFile("recent-"+String(i)+".pht");file.replaceWithText("placeholder");file.setLastModificationTime(juce::Time::getCurrentTime()+juce::RelativeTime::seconds(i+1));rememberProject(file.getFullPathName());}
      check(recentProjects.size()==10&&recentProjects.front()["name"].toString()=="recent-11.pht","Recent files are capped at ten and sorted by modification time, even with one shared project identity");projectIdentity=identity;projectSaveBase="renamed";rememberProject(r->last.getFullPathName());auto menu=projectMenu();bool submenu=false;for(juce::PopupMenu::MenuItemIterator it(menu);it.next();)if(it.getItem().subMenu)submenu|=it.getItem().text==tr(String::fromUTF8("Projets récents"));check(submenu,"File menu exposes recent-project submenu");
      bool labelFound=false;auto expectedFile=r->folder.getChildFile("recent-11.pht");auto date=expectedFile.getLastModificationTime().formatted("%Y-%m-%d %H:%M");
      for(juce::PopupMenu::MenuItemIterator it(menu);it.next();)if(auto* sub=it.getItem().subMenu.get())for(juce::PopupMenu::MenuItemIterator item(*sub);item.next();)if(item.getItem().itemID==100)labelFound=item.getItem().text.startsWith("recent-11.pht")&&item.getItem().text.contains(date)&&item.getItem().text.contains(expectedFile.getParentDirectory().getFullPathName());
      check(labelFound,"Recent menu shows the real filename, modification date/time and actual folder");
      recentProjects={object({{"identity",identity},{"path",r->last.getFullPathName()},{"name","Old logical alias"}})};check(storeRecentProjects(),"Prepare legacy recent-file preferences");loadRecentProjects();check(recentProjects.size()==1&&recentProjects[0]["name"].toString()==r->last.getFileName()&&static_cast<juce::int64>(recentProjects[0]["modified"])==r->last.getLastModificationTime().toMilliseconds(),"Legacy recent entries recover the real filename and filesystem modification date");
      recentProjects.clear();check(storeRecentProjects(),"Clearing recent list persists");loadRecentProjects();check(recentProjects.empty()&&r->last.existsAsFile(),"Clearing recent list preserves all project files");storeSavePreferences(false,{});check(defaultProjectFolder.isEmpty(),"Project folder remains optional and can be cleared");
      bridge.requestObserver=[r](const String& cmd){++r->calls[cmd];};settings();r->stage=8;
    }else if(r->stage==8){
      auto* dialog=dynamic_cast<juce::DialogWindow*>(juce::Component::getCurrentlyModalComponent());auto* form=dialog?dynamic_cast<Form*>(dialog->getContentComponent()):nullptr;if(form){check(form->toggleFields.count("timestamped")&&form->textFields.count("project_folder"),"Both save preferences are available in editor settings");
        auto* toggle=form->toggleFields["timestamped"];check(toggle->isEnabled()&&form->state()["project_folder"].toString().isEmpty(),"Increment checkbox is enabled without a project folder");auto at=juce::Point<float>(10,15);auto now=juce::Time::getCurrentTime();juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),at,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,toggle,toggle,now,at,now,1,false);static_cast<juce::Component*>(toggle)->mouseDown(event);static_cast<juce::Component*>(toggle)->mouseUp(event);check((bool)form->state()["timestamped"],"Mouse click enables incrementing with the project-folder field blank");juce::FileOutputStream out(r->folder.getChildFile("save-settings.png"));juce::PNGImageFormat().writeImageToStream(form->createComponentSnapshot(form->getLocalBounds()),out);form->onApply(form->state());r->stage=9;}
    }else if(r->stage==9&&!juce::Component::getCurrentlyModalComponent()){
      check(r->calls["audio_output_select"]==0&&r->calls["muscriptor_settings"]==0,"Saving only increment preference never rebuilds audio or reconfigures inference");App fresh(savePreferencesFile.getParentDirectory().getChildFile("audio"));fresh.stopTimer();check(fresh.timestampSaves&&fresh.defaultProjectFolder.isEmpty(),"Restart retains incremented saving without a default folder");save(false,10);
    }else if(r->stage==10&&!locked){check(timestampSaves&&defaultProjectFolder.isEmpty()&&juce::File(projectPath).getParentDirectory()==r->a&&juce::File(projectPath)!=r->first&&juce::File(projectPath).existsAsFile(),"Ctrl+S increments in the remembered project folder without a configured default folder");projectPath={};projectSaveBase={};projectSaveFolder={};projectIdentity=juce::Uuid().toString();testSaveDestination=[r](const String&){return r->b.getChildFile("no-default.pht");};save(false,11);
    }else if(r->stage==11&&!locked){check(defaultProjectFolder.isEmpty()&&projectSaveBase=="no-default"&&juce::File(projectPath).getFileName().startsWith("no-default_")&&juce::File(projectPath).existsAsFile(),"First save with no default folder asks for a destination and creates an incremented file");r->calls.clear();settings();r->stage=12;
    }else if(r->stage==12&&settingsForm){
      auto* f=settingsForm.getComponent();r->savedAt=juce::Time::getMillisecondCounterHiRes();auto state=f->state();f->onApply(state);f->onApply(state);
      check(r->calls["audio_output_select"]==0&&r->calls["muscriptor_settings"]==0,"Repeated unchanged Save never submits heavy settings requests");r->stage=13;
    }else if(r->stage==13&&!juce::Component::getCurrentlyModalComponent()){
      check(juce::Time::getMillisecondCounterHiRes()-r->savedAt<1000,"Unchanged settings close promptly without queued audio reconstruction");
      result.getDynamicObject()->setProperty("engine","settings-regression");r->savedAt=juce::Time::getMillisecondCounterHiRes();settings();settings();r->stage=14;
    }else if(r->stage==14&&settingsForm){
      check(juce::Time::getMillisecondCounterHiRes()-r->savedAt<2000,"Settings can reopen immediately after saving");
      auto* f=settingsForm.getComponent();check(f->toggleFields.count("third_party")&&r->calls["audio_outputs"]==2,"Repeated opening launches only one settings form");
      auto state=f->state();f->onApply(state);check(r->calls["muscriptor_settings"]==0&&r->calls["audio_output_select"]==0,"Unchanged inference settings also save without a device scan or audio reload");r->stage=15;
    }else if(r->stage==15&&!juce::Component::getCurrentlyModalComponent()){
      settings();r->stage=16;
    }else if(r->stage==16&&settingsForm){
      auto* f=settingsForm.getComponent();r->goodOutput=f->state()["output"];auto v=f->state().clone();v.getDynamicObject()->setProperty("output","invalid-device-without-separator");f->onApply(v);f->onApply(v);
      check(r->calls["audio_output_select"]==1,"Double Save submits one changed audio request");r->stage=17;
    }else if(r->stage==17&&settingsForm){
      auto* f=settingsForm.getComponent();bool ready=false;for(auto* child:f->getChildren())if(auto* button=dynamic_cast<juce::TextButton*>(child))if(button->getButtonText()==tr(String::fromUTF8("Enregistrer")))ready=button->isEnabled();
      if(ready){check(true,"Audio failure re-enables Save and leaves settings recoverable");auto v=f->state().clone();v.getDynamicObject()->setProperty("output",r->goodOutput);f->onApply(v);r->stage=18;}
    }else if(r->stage==18&&!juce::Component::getCurrentlyModalComponent()){
      check(r->calls["audio_output_select"]==1,"Retrying original output avoids unnecessary audio reopening");finish(true);return;
    }
    juce::Timer::callAfterDelay(30,[poll]{if(*poll)(*poll)();});
  }catch(const std::exception& e){r->checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));finish(false);}};
  (*poll)();
}

void App::runVoiceLayerTests(std::function<void(bool)> done){
  stopTimer();setSize(1400,900);
  struct Run{int stage=0,key=-1,source=-1;bool finished=false;double started=juce::Time::getMillisecondCounterHiRes();juce::File folder,midi;var original,beforeColour;std::vector<var> checks;};
  auto r=std::make_shared<Run>();auto poll=std::make_shared<std::function<void()>>();r->folder=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/layers-"+juce::Uuid().toString());r->folder.createDirectory();
  auto finish=[this,r,poll,done](bool ok){if(r->finished)return;r->finished=true;auto report=object({{"passed",ok},{"stage",r->stage},{"language",localisation::code()},{"directory",r->folder.getFullPathName()},{"checks",array(r->checks)}});r->folder.getParentDirectory().getChildFile("layers-latest.json").replaceWithText(juce::JSON::toString(report));juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
  auto check=[r](bool ok,const String& text){r->checks.push_back(object({{"name",text},{"passed",ok}}));if(!ok)throw std::runtime_error(text.toStdString());};
  auto form=[]()->Form*{auto* dialog=dynamic_cast<juce::DialogWindow*>(juce::Component::getCurrentlyModalComponent());return dialog?dynamic_cast<Form*>(dialog->getContentComponent()):nullptr;};
  auto chooseColour=[check](Form* f,unsigned choice){int count=0;juce::Button* target=nullptr;std::function<void(juce::Component*)> visit=[&](juce::Component* c){for(auto* child:c->getChildren()){if(auto* button=dynamic_cast<juce::Button*>(child))for(unsigned i=0;i<12;++i)if(button->getName()==tr(String::fromUTF8(voicePalette::names[i]))){++count;if(i==choice)target=button;}visit(child);}};visit(f);check(count==12&&target,"Colour form presents exactly twelve accessible named swatches");if(target&&target->onClick)target->onClick();check((unsigned)(int)f->state()["colour"]==choice,"Swatch click updates selected colour");};
  auto checkMenu=[this,r,check](const String& name){
    juce::Image image(juce::Image::RGB,500,(part.getNumItems())*34,true,juce::SoftwareImageType());juce::Graphics g(image);g.fillAll(theme.paper);int row=0,icons=0;bool allPlain=false,tick=false,colour=true;
    for(juce::PopupMenu::MenuItemIterator it(*part.getRootMenu());it.next();++row){auto& item=it.getItem();bool selected=item.itemID==part.getSelectedId();theme.drawPopupMenuItem(g,{0,row*34,500,34},false,true,selected,selected,false,item.text,String(),item.image.get(),nullptr);
      if(item.itemID==1){allPlain=!item.image;continue;}++icons;int key=item.itemID-2,program=0;for(auto p:items(result["midi_parts"]))if((p["identity"].isVoid()?int(p["program"]):int(p["identity"]))==key)program=int(p["program"]);
      colour&=item.image&&image.getPixelAt(38,row*34+17)==roll.voiceColour(key,program);if(selected)for(int y=row*34+10;y<row*34+24;++y)for(int x=9;x<27;++x)tick|=image.getPixelAt(x,y)!=juce::Colour(0xffe9e0d1);
    }
    juce::FileOutputStream out(r->folder.getChildFile(name+".png"));juce::PNGImageFormat().writeImageToStream(image,out);
    check(allPlain&&icons==int(parts.size())&&colour,"Voice-menu dots match current piano-roll colours, including empty voices; All parts has no dot");check(partId<0||tick,"The selected voice retains its checkmark beside its colour dot");
  };
  try{
    using namespace phthongos::midi;auto bytes=encode(0,480,{{{0,192,0,{0}},{240,144,0,{60,91}},{480,128,0,{60,0}},{720,144,0,{64,67}},{960,128,0,{64,0}},{960,255,47,{}}}});r->midi=r->folder.getChildFile("source.mid");r->midi.replaceWithData(bytes.data(),bytes.size());auto imported=bridge.call("import_midi_audio",object({{"midiPath",r->midi.getFullPathName()},{"audioPath",var()}}));check((bool)imported["ok"],"MIDI imported for layer workflow");audio=imported["value"]["audio"];roll.audio=audio;updateResult(imported["value"]["result"],false);r->original=result["editing"]["notes"].clone();r->source=Note::parse(r->original[0]).part;
    std::set<juce::uint32> unique;for(auto c:voicePalette::colours){unique.insert(c);check(c!=Roll::tonalColour&&c!=0xffd38a3f&&c!=0xff367e80&&c!=0xffe9b843&&c!=0xff939393,"Voice colour excludes all reserved functional colours");}check(unique.size()==12,"All twelve voice colours are distinct");for(auto colour:voicePalette::colours){auto a=voicePalette::luminance(colour),t=voicePalette::luminance(voicePalette::textColour(colour));check((std::max(a,t)+.05)/(std::min(a,t)+.05)>=4.5,"Note label contrast reaches 4.5 on every voice colour");}
    prepare([this](var){buttonMap["partAdd"]->onClick();});
  }catch(const std::exception& e){r->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())}}));finish(false);return;}
  *poll=[this,r,poll,finish,check,form,chooseColour,checkMenu]{try{
    if(r->finished)return;if(juce::Time::getMillisecondCounterHiRes()-r->started>45000)throw std::runtime_error("Layer workflow timed out: "+status.getText().toStdString());
    if(r->stage==0&&form()){
      auto* f=form();check(f->textFields.count("name")&&f->compactFields.count("program"),"Plus opens name/instrument settings for an empty voice");f->setValue("name","Test layer");f->setValue("program",40);chooseColour(f,4);
      {juce::FileOutputStream image(r->folder.getChildFile("new-voice.png"));juce::PNGImageFormat().writeImageToStream(f->createComponentSnapshot(f->getLocalBounds()),image);}r->stage=1;f->onApply(f->state());
    }else if(r->stage==1&&!locked&&notesReady&&!form()&&parts.size()==2){
      r->key=partId;check(r->key!=r->source&&juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->original),"New empty voice becomes active and leaves source notes untouched");check(roll.partColours.at(r->key)==4,"Created voice has its selected colour");checkMenu("voice-menu-created");r->stage=101;roll.clearSelection();buttonMap["partInstrument"]->onClick();
    }else if(r->stage==101&&form()){
      check(roll.selected.empty()&&form()->comboFields.count("preset"),"Toolbar instrument opens for an empty voice without selecting notes");auto* f=form();f->setValue("program",0);r->stage=102;f->onApply(f->state());
    }else if(r->stage==102&&!locked&&notesReady&&!form()){
      bool preserved=false;for(const auto& p:items(result["midi_parts"]))if((int)p["identity"]==r->key)preserved=p["label"].toString()=="Test layer"&&(int)p["program"]==0;
      check(preserved&&roll.partColours.at(r->key)==4&&juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->original),"Empty voice instrument changes preserve name, colour and source notes");r->stage=103;command(object({{"kind","undo"}}));
    }else if(r->stage==103&&!locked&&notesReady){r->stage=2;command(object({{"kind","undo"}}));
    }else if(r->stage==2&&!locked&&notesReady&&parts.size()==1){check(roll.partColours.empty(),"One undo removes empty voice and colour together");r->stage=3;command(object({{"kind","redo"}}));
    }else if(r->stage==3&&!locked&&notesReady&&parts.size()==2){
      check(roll.partColours.at(r->key)==4,"Redo restores the voice identity and colour");selectPart(r->source);std::set<std::int64_t> ids;for(const auto& n:items(r->original))ids.insert((juce::int64)n["id"]);roll.selectIds(ids);copyNotes();selectPart(r->key);r->stage=4;control("seek",.1,[this](var){juce::Timer::callAfterDelay(50,[s=safe()]{if(s)s->invoke("player_snapshot",object({}),[s](var v){if(s){s->playback=v;s->pasteNotes();}});});});
    }else if(r->stage==4&&!locked&&notesReady&&result["editing"]["notes"].size()==4){
      check(roll.selected.size()==2,"Inter-voice paste selects only the two new copies");for(const auto& value:items(result["editing"]["notes"]))if(roll.selected.count((juce::int64)value["id"])){auto n=Note::parse(value);bool match=false;for(const auto& v:items(r->original)){auto old=Note::parse(v);match|=n.start==old.start&&n.end==old.end&&n.pitch==old.pitch&&n.velocity==old.velocity;}check(match&&n.part==r->key&&n.program==40,"Inter-voice paste preserves exact coordinates and uses destination instrument");}
      copyNotes();r->stage=5;control("seek",.1,[this](var){juce::Timer::callAfterDelay(50,[s=safe()]{if(s)s->invoke("player_snapshot",object({}),[s](var v){if(s){s->playback=v;s->pasteNotes();}});});});
    }else if(r->stage==5&&!locked&&notesReady&&result["editing"]["notes"].size()==6){
      double first=1200;for(const auto& n:items(result["editing"]["notes"]))if(roll.selected.count((juce::int64)n["id"]))first=std::min(first,number(n,"start_time_s"));check(std::abs(first-.1)<.001,"Same-voice paste retains playhead positioning");r->stage=6;command(object({{"kind","undo"}}));
    }else if(r->stage==6&&!locked&&notesReady&&result["editing"]["notes"].size()==4){r->stage=7;command(object({{"kind","undo"}}));
    }else if(r->stage==7&&!locked&&notesReady&&result["editing"]["notes"].size()==2){
      check(parts.size()==2&&juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->original),"Undoing both pastes restores an empty layer and preserves source notes");auto request=args();request.getDynamicObject()->setProperty("path",r->midi.getFullPathName());auto preview=bridge.call("preview_midi_addition",request);check((bool)preview["ok"],"MIDI addition preview opens for the empty voice");auto* f=midiAdditionForm(preview["value"],args());f->setValue("target0",r->key);f->show(tr(String::fromUTF8("Ajouter un MIDI · voix")));r->stage=8;f->onApply(f->state());
    }else if(r->stage==8&&!locked&&notesReady&&!form()&&result["editing"]["notes"].size()==4){
      check(parts.size()==2,"Import into empty layer reuses the destination instead of creating another voice");int imported=0;for(const auto& value:items(result["editing"]["notes"])){auto n=Note::parse(value);if(n.part==r->key){++imported;check(n.program==40,"Imported notes use the empty layer instrument");}}
      check(imported==2&&roll.partColours.at(r->key)==4,"MIDI import retains empty layer identity and colour");selectPart(r->key);r->beforeColour=result["editing"]["notes"].clone();r->stage=9;buttonMap["partColour"]->onClick();
    }else if(r->stage==9&&form()){
      auto* f=form();check(!f->textFields.count("name"),"Existing voice colour is editable without selecting notes");chooseColour(f,1);r->stage=10;f->onApply(f->state());
    }else if(r->stage==10&&!locked&&notesReady&&!form()&&roll.partColours.at(r->key)==1){
      check(juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->beforeColour),"Recolouring never alters MIDI notes");checkMenu("voice-menu-recoloured");voiceVolume.setValue(-12,juce::sendNotificationSync);selectPart(r->source);check(voiceVolume.getValue()==0,"Selecting another voice restores its own default listening level");selectPart(r->key);check(voiceVolume.getValue()==-12,"Returning to a voice restores its individual level");r->stage=11;command(object({{"kind","undo"}}));
    }else if(r->stage==11&&!locked&&notesReady&&roll.partColours.at(r->key)==4){
      check(true,"One undo restores previous colour");checkMenu("voice-menu-undo");selectPart(r->source);others.onClick();selectPart(r->key);check(hiddenParts.count(r->source)&&roll.notes.size()==2,"Hidden source remains hidden when selecting the other voice before saving");r->stage=-1;saveProjectTo(r->folder.getChildFile("layers.pht").getFullPathName(),[r,finish](bool ok){if(ok)r->stage=12;else finish(false);});
    }else if(r->stage==12&&!locked){
      auto opened=bridge.call("open_project",object({{"path",projectPath}}));check((bool)opened["ok"],"Layer project reopens");audio=opened["value"]["audio"];roll.audio=audio;loadUi(opened["value"]["ui"]);updateResult(opened["value"]["result"],false);check(parts.size()==2&&roll.partColours.at(r->key)==4&&juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->beforeColour),"PHT preserves layer identity, colour, original notes and imported notes");check(hiddenParts.count(r->source)&&!hiddenParts.count(r->key)&&roll.notes.size()==2,"PHT reopening preserves independent voice visibility");check(double(voiceLevels.getProperty(juce::Identifier(String(r->key)),var()))==-12&&voiceVolume.getValue()==-12,"PHT reopening restores each voice listening level");selectPart(r->source);others.onClick();selectPart(r->key);r->stage=-1;prepare([this,r](var){r->stage=13;command(object({{"kind","toggle_part_lock"},{"part",r->key}}));});
    }else if(r->stage==13&&!locked&&notesReady&&roll.lockedParts.count(r->key)){
      check(!buttonMap["partColour"]->isEnabled()&&!buttonMap["partInstrument"]->isEnabled()&&!buttonMap["partDelete"]->isEnabled(),"Locked layer protects colour and deletion");r->stage=14;command(object({{"kind","undo"}}));
    }else if(r->stage==14&&!locked&&notesReady&&!roll.lockedParts.count(r->key)){
      check(buttonMap["partColour"]->isEnabled()&&buttonMap["partInstrument"]->isEnabled(),"Unlock restores colour and instrument editing");r->stage=15;roll.clearSelection();buttonMap["partInstrument"]->onClick();
    }else if(r->stage==15&&form()){
      auto* f=form();f->setValue("program",0);r->stage=16;f->onApply(f->state());
    }else if(r->stage==16&&!locked&&notesReady&&!form()){
      bool named=false;for(const auto& p:items(result["midi_parts"]))if((int)p["identity"]==r->key)named=p["label"].toString()=="Test layer"&&(int)p["program"]==0;
      bool geometry=true;for(const auto& v:items(result["editing"]["notes"])){auto n=Note::parse(v);for(const auto& old:items(r->beforeColour))if(old["id"]==v["id"]){auto x=Note::parse(old);geometry&=n.pitch==x.pitch&&n.start==x.start&&n.end==x.end&&n.velocity==x.velocity;}}
      check(named&&geometry&&roll.partColours.at(r->key)==4,"Toolbar changes instrument of populated voice without renaming, moving or recolouring it");r->stage=17;command(object({{"kind","undo"}}));
    }else if(r->stage==17&&!locked&&notesReady){
      check(juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->beforeColour),"Undo restores the original instrument and exact notes");std::set<std::int64_t> ids;for(const auto& n:items(result["editing"]["notes"]))if(Note::parse(n).part==r->key)ids.insert((juce::int64)n["id"]);roll.selectIds(ids);r->stage=18;partDialog(false);
    }else if(r->stage==18&&form()){
      auto* f=form();f->setValue("program",0);r->stage=19;f->onApply(f->state());
    }else if(r->stage==19&&!locked&&notesReady&&!form()){
      bool named=false;for(const auto& p:items(result["midi_parts"]))if((int)p["identity"]==r->key)named=p["label"].toString()=="Test layer"&&(int)p["program"]==0;check(named,"Context-menu instrument change also preserves the voice name");
      {juce::FileOutputStream image(r->folder.getChildFile("layer-toolbar.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);}
      check((int)bridge.call("player_snapshot",object({}))["value"]["note_count"]==4,"Selecting a voice keeps all four unmuted notes loaded for playback");
      r->stage=20;selectPart(r->source);
    }else if(r->stage==20&&notesReady&&!locked){
      check((int)bridge.call("player_snapshot",object({}))["value"]["note_count"]==4,"Changing editing voice does not act as solo");r->stage=21;buttonMap["partMute"]->onClick();
    }else if(r->stage==21&&notesReady&&!locked){
      check((int)bridge.call("player_snapshot",object({}))["value"]["note_count"]==2&&idSet(result["editing"]["muted_parts"])==std::set<std::int64_t>{r->source},"Voice mute silences only its own two notes");selectPart(r->key);r->stage=22;
    }else if(r->stage==22&&notesReady&&!locked){
      check((int)bridge.call("player_snapshot",object({}))["value"]["note_count"]==2&&idSet(result["editing"]["muted_parts"]).count(r->source),"Switching to another voice preserves the previous voice mute");
      std::int64_t id=-1;for(const auto& value:items(result["editing"]["notes"]))if(Note::parse(value).part==r->key){id=(juce::int64)value["id"];break;}
      r->stage=23;command(object({{"kind","toggle_mute"},{"ids",idsVar({id})}}));
    }else if(r->stage==23&&notesReady&&!locked){
      check((int)bridge.call("player_snapshot",object({}))["value"]["note_count"]==1,"Individual note mute combines with independent voice mute");selectPart(r->source);r->stage=24;
    }else if(r->stage==24&&notesReady&&!locked){r->stage=25;buttonMap["partMute"]->onClick();
    }else if(r->stage==25&&notesReady&&!locked){
      check((int)bridge.call("player_snapshot",object({}))["value"]["note_count"]==3&&idSet(result["editing"]["muted_notes"]).size()==1,"Unmuting a voice preserves individual note mutes elsewhere");finish(true);return;
    }
    juce::Timer::callAfterDelay(30,[poll]{if(*poll)(*poll)();});
  }catch(const std::exception& e){r->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())}}));finish(false);}};
  (*poll)();
}
