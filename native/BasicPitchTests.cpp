#include "App.h"
#include "../core/project/BasicPitch.h"
#include "../core/project/Prepared.h"
#include "../core/midi/Midi.h"
#include "../core/services/BasicPitchInference.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

void App::runBasicPitchTests(std::function<void(bool)> done){
 stopTimer();setSize(1400,900);
 struct Run {bool finished=false;int stage=0;double start=juce::Time::getMillisecondCounterHiRes();juce::File folder,wav,saved,second,multiple;juce::Component::SafePointer<Form> tempo;double originalBpm=0;int addedPart=-1;var original,afterReload;var first,edited,settings;double firstAnalysis=0;std::vector<var> checks;};
 auto r=std::make_shared<Run>();auto poll=std::make_shared<std::function<void()>>();
 r->folder=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/basic-pitch-"+juce::Uuid().toString());r->folder.createDirectory();r->wav=r->folder.getChildFile("piano-stem.wav");r->saved=r->folder.getChildFile("basic-pitch.pht");
 auto check=[r](bool ok,const String& label){r->checks.push_back(object({{"name",label},{"passed",ok}}));if(!ok)throw std::runtime_error(label.toStdString());};
 auto call=[this,check](const String& command,var args){auto reply=bridge.call(command,args);check(bool(reply["ok"]),command+" : "+reply["error"].toString());return reply["value"];};
 auto finish=[this,r,poll,done](bool ok){if(r->finished)return;r->finished=true;dirty=false;
  r->folder.getParentDirectory().getChildFile("basic-pitch-latest.json").replaceWithText(juce::JSON::toString(object({{"passed",ok},{"stage",r->stage},{"language",localisation::code()},{"directory",r->folder.getFullPathName()},{"checks",array(r->checks)}}),false,17));
  juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
 try{
  // Deterministic model-free decoder fixtures: onset split, duration, density,
  // silence, exact window timing, malformed maps and cancellation.
  namespace bp=phthongos::basicPitch;bp::Cache cache;cache.samples=22050*3;auto frames=std::size_t(double(cache.samples)*bp::keptFrames/bp::hop);cache.notes.resize(frames*88,0);cache.onsets.resize(frames*88,0);
  for(int f=20;f<90;++f)cache.notes[std::size_t(f)*88+39]=.8f;cache.onsets[20*88+39]=.9f;cache.onsets[55*88+39]=.55f;
  auto options=bp::Options();auto normal=bp::decode(cache,options);options.splitSensitivity=.8;auto split=bp::decode(cache,options);
  check(normal["notes"].size()==2&&split["notes"].size()==2,"Decoder preserves two attacks on a sustained pitch");
  options.splitSensitivity=.05;auto merged=bp::decode(cache,options);check(merged["notes"].size()==1,"Lower split sensitivity retains one longer note");
  options.noteSensitivity=.1;check(bp::decode(cache,options)["notes"].size()==0,"Lower note sensitivity removes low-confidence detections");
  options=bp::Options();options.minNoteMs=580;check(bp::decode(cache,options)["notes"].size()<normal["notes"].size(),"Minimum duration filters short note fragments");
  {auto legacy=object({{"noteSensitivity",.7},{"splitSensitivity",.5},{"minNoteMs",125}});auto defaults=bp::Options::read(legacy);check(defaults.minPitch==21&&defaults.maxPitch==108&&defaults.inferOnsets&&defaults.melodiaTrick,"Legacy settings retain the full range and both original decoding mechanisms");
   auto originalNotes=cache.notes,originalOnsets=cache.onsets;auto limited=bp::Options();limited.minPitch=limited.maxPitch=60;
   check(bp::decode(cache,limited)["notes"].size()==normal["notes"].size(),"Both range limits include the selected note");limited.minPitch=limited.maxPitch=61;
   check(bp::decode(cache,limited)["notes"].size()==0,"Range exclusion is applied before reconstruction");
   check(juce::JSON::toString(bp::decode(cache,bp::Options())["notes"])==juce::JSON::toString(normal["notes"])&&cache.notes==originalNotes&&cache.onsets==originalOnsets,"Restoring range recovers every note from untouched complete maps");
   auto noOnsets=cache;std::fill(noOnsets.onsets.begin(),noOnsets.onsets.end(),0.f);auto mechanism=bp::Options();mechanism.inferOnsets=false;mechanism.melodiaTrick=false;
   check(bp::decode(noOnsets,mechanism)["notes"].size()==0,"Disabling both reconstruction mechanisms leaves no invented attacks");mechanism.melodiaTrick=true;check(bp::decode(noOnsets,mechanism)["notes"].size()>0,"Additional recovery reconstructs notes from remaining energy");
   auto attackCache=cache;std::fill(attackCache.onsets.begin(),attackCache.onsets.end(),0.f);attackCache.onsets[10*88+2]=.9f;mechanism.melodiaTrick=false;mechanism.inferOnsets=true;check(bp::decode(attackCache,mechanism)["notes"].size()>0,"Additional attacks use frame variations when neural onset peaks are absent");mechanism.inferOnsets=false;check(bp::decode(attackCache,mechanism)["notes"].size()==0,"Additional attack toggle changes decoding independently of recovery");
   auto bad=limited.json();bad.getDynamicObject()->setProperty("minPitch",72);bool rejected=false;try{bp::Options::read(bad);}catch(...){rejected=true;}check(rejected,"Inverted note ranges are rejected");
  }
  {auto interrupted=cache;for(int f=48;f<54;++f)interrupted.notes[f*88+39]=0;std::fill(interrupted.onsets.begin(),interrupted.onsets.end(),0.f);interrupted.onsets[20*88+39]=.9f;
   auto shortTolerance=bp::Options();shortTolerance.inferOnsets=false;shortTolerance.melodiaTrick=false;shortTolerance.energyTolerance=1;auto shortNote=bp::decode(interrupted,shortTolerance);shortTolerance.energyTolerance=11;auto heldNote=bp::decode(interrupted,shortTolerance);
   check(shortNote["notes"].size()==1&&heldNote["notes"].size()==1&&double(heldNote["notes"][0]["offset"])>double(shortNote["notes"][0]["offset"])+.3,"Interruption tolerance bridges a short activation gap during onset reconstruction");
   check(std::abs(double(heldNote["notes"][0]["offset"])-bp::frameTime(90))<.00001,"Gap tolerance trims the trailing inactivity rather than adding a fixed sustain tail");
   shortTolerance.melodiaTrick=true;std::fill(interrupted.onsets.begin(),interrupted.onsets.end(),0.f);shortTolerance.energyTolerance=1;auto fragmented=bp::decode(interrupted,shortTolerance);shortTolerance.energyTolerance=25;auto recovered=bp::decode(interrupted,shortTolerance);check(fragmented["notes"].size()==2&&recovered["notes"].size()==1,"Interruption tolerance also controls both directions of additional note recovery");
   check(bp::Options::read(object({{"noteSensitivity",.7},{"splitSensitivity",.5},{"minNoteMs",125}})).energyTolerance==11,"Old projects retain the original eleven-frame tolerance");
   for(double invalid:{0.,26.,2.5}){auto bad=bp::Options().json();bad.getDynamicObject()->setProperty("energyTolerance",invalid);bool rejected=false;try{bp::Options::read(bad);}catch(...){rejected=true;}check(rejected,"Invalid interruption tolerance is rejected: "+String(invalid));}
  }
  check(std::abs(bp::frameTime(142*200)-double(bp::hop)*200/bp::rate)<1e-12,"Window timing remains anchored to input samples over long stems");
  auto compressed=bp::encodeCache(cache);auto recovered=bp::decodeCache(compressed);check(recovered.notes==cache.notes&&recovered.onsets==cache.onsets,"Per-voice compression preserves every probability exactly");
  auto corrupt=compressed.clone();corrupt.getDynamicObject()->setProperty("compressed","9999999999999999.invalid");bool invalidCache=false;try{bp::decodeCache(corrupt);}catch(...){invalidCache=true;}check(invalidCache,"Malformed compressed cache is rejected without trusting an encoded allocation size");
  auto bytes=bp::pack(cache.notes);check(bp::unpack(bytes)==cache.notes,"Posterior maps survive canonical float serialization exactly");
  std::fill(cache.notes.begin(),cache.notes.end(),0);std::fill(cache.onsets.begin(),cache.onsets.end(),0);check(bp::decode(cache,bp::Options())["notes"].size()==0,"Silence produces no notes or inferred-onset division by zero");
  bool refused=false;cache.notes[0]=std::numeric_limits<float>::quiet_NaN();try{bp::decode(cache,bp::Options());}catch(...){refused=true;}check(refused,"Malformed non-finite cached probabilities are rejected");
  auto status=call("muscriptor_status",object({}));updateTranscription(status);r->settings=status["settings"].clone();
  check(r->settings["engine"].toString()=="basic_pitch"&&model.getSelectedId()==1&&model.getNumItems()==1&&bool(status["basic_pitch_available"]),"A fresh distribution defaults to bundled Basic Pitch without optional models");
  constexpr int sampleRate=44100;juce::AudioBuffer<float> signal(2,int(sampleRate*7.2));signal.clear();
  struct Event {int pitch;double start,end,level;};
  for(auto e:std::vector<Event>{{60,.5,1.4,.24},{64,2.1,3.1,.45},{60,3.6,4.6,.18},{64,3.6,4.6,.18},{67,3.6,4.6,.18},{72,5.2,6.3,.28}})
   for(int i=int(e.start*sampleRate);i<int(e.end*sampleRate);++i){double t=double(i)/sampleRate-e.start,hz=440*std::pow(2.,(e.pitch-69)/12.);double envelope=std::min(1.,t/.012)*std::min(1.,(e.end-double(i)/sampleRate)/.045)*std::exp(-t*.6);float value=float(e.level*envelope*(std::sin(juce::MathConstants<double>::twoPi*hz*t)+.3*std::sin(juce::MathConstants<double>::twoPi*hz*2*t)+.12*std::sin(juce::MathConstants<double>::twoPi*hz*3*t)));signal.addSample(0,i,value);signal.addSample(1,i,value);}
  juce::WavAudioFormat format;{std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(new juce::FileOutputStream(r->wav),sampleRate,2,16,{},0));check(writer&&writer->writeFromAudioSampleBuffer(signal,0,signal.getNumSamples()),"Create independent polyphonic test stem");}
  r->second=r->folder.getChildFile("second-stem.wav");r->multiple=r->folder.getChildFile("multiple-voices.pht");signal.applyGain(.7f);
  {std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(new juce::FileOutputStream(r->second),sampleRate,2,16,{},0));check(writer&&writer->writeFromAudioSampleBuffer(signal,0,signal.getNumSamples()),"Create a second distinct source stem");}
  auto runtime=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("runtime");phthongos::services::BasicPitchInference runner(runtime);std::atomic_bool cancel{true};bool cancelled=false;try{runner.run(r->wav,cancel,[](float){});}catch(...){cancelled=true;}check(cancelled,"Pre-cancelled inference returns without model loading or partial output");
  cancel=false;bool progressed=false;cancelled=false;try{runner.run(r->wav,cancel,[&](float){progressed=true;cancel=true;});}catch(...){cancelled=true;}check(progressed&&cancelled,"Running ONNX inference can be cancelled between windows without returning partial outputs");
  auto empty=call("create_empty_midi_project",object({}));audio=empty["audio"];updateResult(empty["result"],false,false);dirty=false;
  check(items(result["editing"]["notes"]).empty(),"Start from an empty MIDI voice before WAV drop");
  r->stage=1;filesDropped({r->wav.getFullPathName()},10,10);check(!confirmation,"Dropping a WAV into an empty session opens transcription without a replacement prompt");
 }catch(const std::exception& e){r->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())}}));finish(false);return;}
 *poll=[this,r,poll,check,call,finish]{try{
  if(r->finished)return;if(juce::Time::getMillisecondCounterHiRes()-r->start>(juce::SystemStats::getEnvironmentVariable("PHT_TEST_MUSCRIPTOR_DIR",String()).isEmpty()?120000:600000))throw std::runtime_error("Basic Pitch integration test timed out: "+status.getText().toStdString());
  if(r->stage==1&&!locked&&notesReady){
   check(result["engine"].toString()!="midi"&&buttonMap["analyze"]->isVisible()&&buttonMap["analyze"]->isEnabled()&&model.isVisible(),"A dropped WAV retains the engine and Analyse controls");
   check(!bool(bridge.call("redecode_basic_pitch",object({{"audioId",audio["id"]}}))["ok"]),"Cache-only decoding refuses a WAV before first inference");
   for(auto* child:pitchControls->getChildren())if(auto* slider=dynamic_cast<juce::Slider*>(child))if(slider->getMaximum()==95){slider->setValue(80,juce::sendNotificationSync);break;}
   r->stage=12;
  }else if(r->stage==12&&!locked&&!pitchControlsPending&&number(engine["settings"]["basic_pitch"],"noteSensitivity")==.8){
   check(result.isVoid()&&buttonMap["analyze"]->isVisible(),"Before first inference inline controls save preferences without creating notes or running ONNX");
   for(auto* child:pitchControls->getChildren())if(auto* slider=dynamic_cast<juce::Slider*>(child))if(slider->getMaximum()==95){slider->setValue(70,juce::sendNotificationSync);break;}
   r->stage=13;
  }else if(r->stage==13&&!locked&&!pitchControlsPending&&number(engine["settings"]["basic_pitch"],"noteSensitivity")==.7){r->stage=2;analyze();
  }
  else if(r->stage==2&&!locked&&notesReady&&!result.isVoid()){
   check(result["engine"].toString()=="basic_pitch"&&items(result["editing"]["notes"]).size()>=5,"Real ONNX CPU inference yields polyphonic editable notes");r->first=result["editing"]["notes"].clone();
   auto found=[this](int pitch,double onset){for(auto v:items(result["editing"]["notes"])){auto n=Note::parse(v);if(n.pitch==pitch&&std::abs(n.start-onset)<.16)return true;}return false;};
   check(found(60,.5)&&found(64,2.1)&&found(67,3.6)&&found(72,5.2),"Real model detects independent attacks and the chord, including beyond window boundaries");
   auto spectralArgs=args();spectralArgs.getDynamicObject()->setProperty("start",0.);spectralArgs.getDynamicObject()->setProperty("end",number(audio,"duration"));spectralArgs.getDynamicObject()->setProperty("pitchRange",array({0,127}));
   auto spectral=call("verify_midi_audio",spectralArgs);
   check(int(spectral["sample_rate"])==44100&&int(spectral["searched_low"])==0&&int(spectral["searched_high"])==127,"Audio verification searches the full MIDI range using 44.1 kHz audio");
   spectralArgs.getDynamicObject()->setProperty("pitchRange",array({0,47}));auto narrowed=call("verify_midi_audio",spectralArgs);
   bool inside=true;for(const auto& n:items(narrowed["missing"]))inside&=int(n["pitch"])<=47;
   check(bool(narrowed["cached"])&&inside&&int(narrowed["searched_low"])==0&&juce::JSON::toString(r->first)==juce::JSON::toString(result["editing"]["notes"]),"Bass-only register reuses the spectrum without changing notes or running ONNX");

   check(pitchControls&&pitchControls->isVisible()&&!calculation.isVisible(),"Inline controls replace the completed calculation label");
   setSize(1800,900);check(pitchControls->getY()==124&&pitchControls->getWidth()==getWidth()-36&&pitchControls->getBottom()<buttonMap["play"]->getY(),"Wide layout gives all seven controls a dedicated full-width stage");
   {juce::FileOutputStream image(r->folder.getChildFile("inline-wide.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);}
   setSize(1160,850);check(pitchControls->getY()==124&&pitchControls->getHeight()==108&&pitchControls->getBottom()<buttonMap["play"]->getY(),"Narrow layout keeps two compact rows above transport");
   {juce::FileOutputStream image(r->folder.getChildFile("inline-narrow.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);}
   auto* fold=dynamic_cast<juce::TextButton*>(pitchControls->findChildWithID("basicPitchFold"));check(fold&&fold->isEnabled(),"The band always exposes its fold button");
   auto before=juce::JSON::toString(result["editing"]["notes"]);setBusy(true);fold->onClick();check(!pitchControlsExpanded&&pitchControls->getHeight()==28&&fold->isEnabled()&&juce::JSON::toString(result["editing"]["notes"])==before,"Band folds during calculation without changing notes or disabling settings");{App fresh(savePreferencesFile.getParentDirectory().getChildFile("audio"));fresh.stopTimer();check(!fresh.pitchControlsExpanded,"Collapsed state also survives a restart while reconstruction settings stay enabled");}fold->onClick();setBusy(false);
   check(pitchControlsExpanded&&pitchControls->getHeight()==108&&roll.getBottom()<getHeight(),"Unfolding reserves space while keeping the piano roll inside the window");
   {App fresh(savePreferencesFile.getParentDirectory().getChildFile("audio"));fresh.stopTimer();check(fresh.pitchControlsExpanded,"Band expansion preference survives a new instance");}
   // Regression: an App child callout used to move down on every refresh.
   {auto oldMode=roll.displayMode,oldPresentation=voicePresentation.getSelectedId();bool oldExpanded=pitchControlsExpanded;
    for(bool stackedMode:{false,true})for(bool expanded:{false,true}){
     roll.displayMode=0;displayMode.setSelectedId(1,juce::dontSendNotification);voicePresentation.setSelectedId(stackedMode?2:1,juce::dontSendNotification);pitchControlsExpanded=expanded;refresh();
     juce::Component menuContent;menuContent.setSize(300,180);juce::CallOutBox menuBox(menuContent,{250,350,1,1},this);menuBox.setArrowSize(0);menuBox.setVisible(true);const auto beforeBounds=menuBox.getBounds();
     for(int tick=0;tick<30;++tick)refresh();
     check(menuBox.getBounds()==beforeBounds&&menuBox.isVisible(),String("Piano-roll context callout stays fixed through 30 refreshes: ")+(stackedMode?"stacked":"grouped")+(expanded?", band expanded":", band collapsed"));
    }
    pitchControlsExpanded=oldExpanded;roll.displayMode=oldMode;displayMode.setSelectedId(oldMode+1,juce::dontSendNotification);voicePresentation.setSelectedId(oldPresentation,juce::dontSendNotification);refresh();
   }
   auto menu=projectMenu();bool settingsEntry=false,manualEntry=false,creditsEntry=false,oldInfo=false;for(juce::PopupMenu::MenuItemIterator it(menu);it.next();){auto item=it.getItem();settingsEntry|=item.itemID==12;manualEntry|=item.itemID==13;creditsEntry|=item.itemID==7&&item.text==tr(String::fromUTF8("Crédits et licences"));oldInfo|=item.text==tr(String::fromUTF8("Informations"));}
   check(settingsEntry&&manualEntry&&creditsEntry&&!oldInfo&&!buttonMap["settings"]->isVisible(),"Hamburger menu contains settings, PDF help and credits without the obsolete information entry or gear");check(manualFile().existsAsFile()&&manualFile().getFileName().contains(localisation::code()=="fr"?"utilisateur":"User_Manual"),"Offline PDF help resolves the manual matching the interface language");
   information();check(!Form::activeForms.empty(),"Credits and offline licences open from their own view");auto* credits=*Form::activeForms.rbegin();{juce::FileOutputStream image(r->folder.getChildFile("credits.png"));juce::PNGImageFormat().writeImageToStream(credits->createComponentSnapshot(credits->getLocalBounds()),image);}credits->close();
   juce::Slider* minimum=nullptr;for(auto* child:pitchControls->getChildren())if(auto* slider=dynamic_cast<juce::Slider*>(child))if(slider->getMaximum()==580)minimum=slider;
   check(minimum!=nullptr,"Minimum-duration control is directly accessible");
   for(auto* child:minimum->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child)){label->showEditor();auto* editor=label->getCurrentTextEditor();check(editor&&editor->findColour(juce::TextEditor::textColourId)==theme.ink&&editor->findColour(juce::TextEditor::highlightedTextColourId)==theme.ink,"Typed and selected numeric text keeps the theme ink color");label->hideEditor(true);}
   setSize(1400,900);r->firstAnalysis=number(result,"analysis_id");r->stage=21;minimum->setValue(200,juce::sendNotificationSync);minimum->setValue(580,juce::sendNotificationSync);
  }else if(r->stage==21&&!locked&&!pitchControlsPending&&number(engine["settings"]["basic_pitch"],"minNoteMs")==580){
   check(bool(result["reused_inference"])&&number(result,"analysis_id")==r->firstAnalysis+1,"Inline duration adjustment uses cached decoding");
   for(auto* child:pitchControls->getChildren())if(auto* slider=dynamic_cast<juce::Slider*>(child))if(slider->getMaximum()==580)slider->setValue(125,juce::sendNotificationSync);
   r->stage=22;
  }else if(r->stage==22&&!locked&&!pitchControlsPending&&number(engine["settings"]["basic_pitch"],"minNoteMs")==125){
   check(bool(result["reused_inference"])&&number(engine["settings"]["basic_pitch"],"noteSensitivity")==.7,"Restoring duration retains the other controls and reuses inference");
   auto* energy=dynamic_cast<juce::Slider*>(pitchControls->findChildWithID("basicPitchEnergy"));check(energy&&energy->getMinimum()==1&&energy->getMaximum()==25&&energy->getValue()==11&&energy->getTooltip().contains("128 ms"),"Interruption slider exposes one to twenty-five frames and an approximate duration: "+(energy?String(energy->getValue())+" / "+energy->getTooltip():String("missing")));
   r->stage=221;energy->setValue(25,juce::sendNotificationSync);
  }else if(r->stage==221&&!locked&&!pitchControlsPending&&int(engine["settings"]["basic_pitch"]["energyTolerance"])==25){
   check(bool(result["reused_inference"]),"Changing interruption tolerance after inference immediately reconstructs from the cache");
   r->stage=23;
   dynamic_cast<juce::ComboBox*>(pitchControls->findChildWithID("basicPitchMin"))->setSelectedId(61,juce::sendNotificationSync);
   dynamic_cast<juce::ComboBox*>(pitchControls->findChildWithID("basicPitchMax"))->setSelectedId(73,juce::sendNotificationSync);
   for(auto id:{"basicPitchInfer","basicPitchMelodia"}){auto* toggle=dynamic_cast<juce::ToggleButton*>(pitchControls->findChildWithID(id));toggle->setToggleState(false,juce::dontSendNotification);toggle->onClick();}
  }else if(r->stage==23&&!locked&&!pitchControlsPending&&int(engine["settings"]["basic_pitch"]["minPitch"])==60&&!bool(engine["settings"]["basic_pitch"]["melodiaTrick"])){
   auto settings=engine["settings"]["basic_pitch"];check(bool(result["reused_inference"])&&int(settings["maxPitch"])==72&&!bool(settings["inferOnsets"]),"Both note selectors and both switches update notes interactively from the complete cache");
   r->stage=24;dynamic_cast<juce::TextButton*>(pitchControls->findChildWithID("basicPitchReset"))->onClick();
  }else if(r->stage==24&&!locked&&!pitchControlsPending&&int(engine["settings"]["basic_pitch"]["minPitch"])==21&&bool(engine["settings"]["basic_pitch"]["melodiaTrick"])){
   auto settings=engine["settings"]["basic_pitch"];check(bool(result["reused_inference"])&&int(settings["maxPitch"])==108&&bool(settings["inferOnsets"])&&number(settings,"minNoteMs")==125&&int(settings["energyTolerance"])==11,"Reset restores all seven settings and reconstructs from the original maps");
   r->first=result["editing"]["notes"].clone();auto info=call("project_tempo",object({{"audioId",audio["id"]}}));r->originalBpm=number(info,"bpm");r->tempo=tempoForm(info);r->tempo->show(tr(String::fromUTF8("Tempo / calculette rythmique")));
   std::function<juce::Component*(juce::Component*)> findButton=[&](juce::Component* c)->juce::Component*{if(c->getComponentID()=="estimateNotesTempo")return c;for(auto* child:c->getChildren())if(auto* found=findButton(child))return found;return nullptr;};auto* button=dynamic_cast<juce::TextButton*>(findButton(r->tempo.getComponent()));check(button&&button->isEnabled(),"BPM panel exposes estimation from Basic Pitch notes");r->stage=241;button->onClick();
  }else if(r->stage==241&&midiTempoEstimate["source"].toString()=="notes"&&!r->tempo){
   Form* form=nullptr;for(auto* candidate:Form::activeForms)if(candidate->comboFields.count("bpm")==0&&candidate->textFields.count("bpm"))form=candidate;if(!form)return;
   check(midiTempoEstimate["part"].isVoid()&&number(midiTempoEstimate,"attacks")>=2&&juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->first),"All-parts estimation reuses current MIDI attacks and leaves every note untouched");
   auto info=call("project_tempo",object({{"audioId",audio["id"]}}));check(number(info,"bpm")==r->originalBpm&&form->state()["take_tempo"]==var(0.),"Estimation never changes project BPM and defaults to retaining it");
   check(form->comboFields.count("take_tempo")&&form->comboFields["take_tempo"]->getNumItems()>=4,"Note estimation offers candidates and half/double choices");form->comboFields["take_tempo"]->setSelectedId(3,juce::sendNotificationSync);check(number(call("project_tempo",object({{"audioId",audio["id"]}})),"bpm")==r->originalBpm,"Choosing a tempo proposal still requires explicit application");
   {juce::FileOutputStream image(r->folder.getChildFile("tempo-from-notes.png"));juce::PNGImageFormat().writeImageToStream(form->createComponentSnapshot(form->getLocalBounds()),image);}form->close();r->stage=242;
  }else if(r->stage==242&&!juce::Component::getCurrentlyModalComponent()){
   saveProjectTo(r->saved.getFullPathName(),[r,finish](bool ok){if(ok)r->stage=3;else finish(false);});r->stage=20;
  }else if(r->stage==3&&!locked){
   auto saved=phthongos::project::read(r->saved);check(!saved.data[1].empty()&&!saved.data[2].empty()&&saved.metadata["basic_pitch"]["model_sha256"].toString()==phthongos::basicPitch::modelHash,"PHT stores Basic Pitch provenance, settings and both posterior maps");
   auto opened=call("open_project",object({{"path",r->saved.getFullPathName()}}));audio=opened["audio"];loadUi(opened["ui"]);updateResult(opened["result"],false,false);
   check(juce::JSON::toString(r->first)==juce::JSON::toString(result["editing"]["notes"]),"Reopening the Basic Pitch project preserves every note exactly");
   auto configured=r->settings.clone();auto options=configured["basic_pitch"].clone();options.getDynamicObject()->setProperty("minNoteMs",200.);configured.getDynamicObject()->setProperty("basic_pitch",options);call("muscriptor_settings",object({{"settings",configured}}));
   check(juce::JSON::toString(r->first)==juce::JSON::toString(result["editing"]["notes"]),"Changing segmentation settings alone never edits existing notes");
   auto decoded=call("analyze_transcription",object({{"audioId",audio["id"]}}));check(bool(decoded["reused_inference"])&&number(decoded["basic_pitch"]["settings"],"minNoteMs")==200,"Applying new settings after reopen reuses persisted neural outputs");updateResult(decoded,false,false);
   auto dynamics=call("estimate_dynamics",args());check(!items(dynamics["estimates"]).empty(),"Existing harmonic audio dynamics accepts Basic Pitch notes");
   auto frozen=result["editing"]["notes"].clone();auto first=items(frozen).front();auto request=args().clone();request.getDynamicObject()->setProperty("action",object({{"kind","delete"},{"ids",array({first["id"]})}}));auto changed=call("edit_notes",request);updateResult(changed,false,false);r->edited=result["editing"]["notes"].clone();
   r->stage=4;prepare([this,r](var){for(auto* child:pitchControls->getChildren())if(auto* slider=dynamic_cast<juce::Slider*>(child))if(slider->getMaximum()==580)slider->setValue(580,juce::sendNotificationSync);r->stage=5;});
  }else if(r->stage==5&&confirmation){
   check(juce::JSON::toString(r->edited)==juce::JSON::toString(result["editing"]["notes"]),"Reanalysis asks before replacing manual corrections");confirmation->answer(0);r->stage=6;
  }else if(r->stage==6&&!confirmation&&!locked){
   check(juce::JSON::toString(r->edited)==juce::JSON::toString(result["editing"]["notes"]),"Cancelling reanalysis retains manual corrections and cached inference");
   auto folder=r->folder.getChildFile("declared-models");folder.createDirectory();folder.getChildFile("muscriptor-small-f16.gguf").replaceWithText("invalid optional weights");folder.getChildFile("muscriptor-large-f16.gguf").replaceWithText("invalid optional weights");
   auto configured=r->settings.clone();configured.getDynamicObject()->setProperty("third_party",true);configured.getDynamicObject()->setProperty("models_dir",folder.getFullPathName());call("muscriptor_settings",object({{"settings",configured}}));auto available=call("muscriptor_status",object({}));updateTranscription(available);
   check(model.getNumItems()==3&&model.getItemId(0)==1&&model.getItemId(1)==2&&model.getItemId(2)==4&&model.getSelectedId()==1,"One selector lists Basic Pitch and only the manually declared MuScriptor variants");
   r->stage=7;model.setSelectedId(4,juce::sendNotificationSync);
  }else if(r->stage==7&&engine["settings"]["engine"].toString()=="muscriptor"&&engine["settings"]["model"].toString()=="large"){
   check(model.getSelectedId()==4&&juce::JSON::toString(r->edited)==juce::JSON::toString(result["editing"]["notes"]),"Selecting MuScriptor Large in the single list changes engine and variant without running analysis");
   auto bad=bridge.call("analyze_muscriptor",object({{"audioId",audio["id"]}}));check(!bool(bad["ok"])&&juce::JSON::toString(r->edited)==juce::JSON::toString(result["editing"]["notes"]),"Invalid optional weights fail without replacing the active Basic Pitch document");
   {juce::FileOutputStream image(r->folder.getChildFile("basic-pitch-result.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);}
   r->stage=8;model.setSelectedId(1,juce::sendNotificationSync);
  }else if(r->stage==8&&engine["settings"]["engine"].toString()=="basic_pitch"){
   check(model.getSelectedId()==1&&juce::JSON::toString(r->edited)==juce::JSON::toString(result["editing"]["notes"]),"Selecting Basic Pitch restores its options without modifying the document");
   auto configured=r->settings.clone();auto options=configured["basic_pitch"].clone();options.getDynamicObject()->setProperty("minNoteMs",580.);configured.getDynamicObject()->setProperty("basic_pitch",options);call("muscriptor_settings",object({{"settings",configured}}));
   dirty=false;r->stage=9;openProject(r->saved.getFullPathName());
  }else if(r->stage==9&&!locked&&notesReady&&number(engine["settings"]["basic_pitch"],"minNoteMs")==125){
   check(juce::JSON::toString(r->first)==juce::JSON::toString(result["editing"]["notes"])&&model.getSelectedId()==1,"Opening through the real project workflow restores its Basic Pitch settings and notes instead of global settings");
   auto restored=call("analyze_transcription",object({{"audioId",audio["id"]}}));check(bool(restored["reused_inference"]),"The project workflow retains reusable neural outputs after restoring settings");
   {juce::FileOutputStream image(r->folder.getChildFile("basic-pitch-default.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);}
   auto optional=juce::SystemStats::getEnvironmentVariable("PHT_TEST_MUSCRIPTOR_DIR",{});
   if(optional.isNotEmpty()){
    auto configured=r->settings.clone();configured.getDynamicObject()->setProperty("engine","muscriptor");configured.getDynamicObject()->setProperty("model","small");configured.getDynamicObject()->setProperty("device","cpu");configured.getDynamicObject()->setProperty("third_party",true);configured.getDynamicObject()->setProperty("models_dir",optional);call("muscriptor_settings",object({{"settings",configured}}));
    r->stage=10;invoke("analyze_transcription",object({{"audioId",audio["id"]}}),[this,r,check,finish](var value){try{check(value["engine"].toString()=="muscriptor"&&!items(value["editing"]["notes"]).empty(),"The same selector backend runs the installed MuScriptor Small CPU engine on the test stem");updateResult(value,false,false);r->stage=11;}catch(const std::exception& e){r->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())}}));finish(false);}},true,[r,finish](String error){r->checks.push_back(object({{"passed",false},{"error",error}}));finish(false);});
   }else {updateResult(restored,false,false);auto lockRequest=args().clone();lockRequest.getDynamicObject()->setProperty("action",object({{"kind","toggle_part_lock"},{"part",0}}));updateResult(call("edit_notes",lockRequest),false,false);r->original=result["editing"]["notes"].clone();r->stage=30;prepare([this,r](var){filesDropped({r->second.getFullPathName()},10,10);r->stage=31;});}

  }else if(r->stage==31&&!locked&&notesReady){
   check(audio["name"].toString()==r->second.getFileName()&&juce::JSON::toString(r->original)==juce::JSON::toString(result["editing"]["notes"]),"Dropping a different stem preserves every existing note");
   check(buttonMap["analyze"]->isVisible()&&buttonMap["analyze"]->isEnabled()&&voiceSourcePath(0)==r->wav.getFullPathName(),"Analyse remains available while the first voice retains its original source");r->stage=32;analyze();
  }else if(r->stage==32&&!locked&&notesReady){
   auto added=items(result["transcribed_parts"]);check(added.size()==1,"Analysing another stem creates a new voice");r->addedPart=int(added.front());
   std::vector<var> old;for(auto v:items(result["editing"]["notes"]))if(Note::parse(v).part!=r->addedPart)old.push_back(v);
   check(juce::JSON::toString(array(old))==juce::JSON::toString(r->original)&&voiceSourcePath(r->addedPart)==r->second.getFullPathName(),"New-voice transcription preserves original IDs, timing, velocity and source association");
   check(idSet(result["editing"]["locked_parts"]).count(0)>0,"Adding a new transcription preserves locked neighbouring voices");
   auto request=args().clone();request.getDynamicObject()->setProperty("action",object({{"kind","undo"}}));auto undone=call("edit_notes",request);updateResult(undone,false,false);
   check(juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->original)&&voiceSourcePath(r->addedPart).isEmpty(),"One undo removes the added transcription and its source label");
   request=args().clone();request.getDynamicObject()->setProperty("action",object({{"kind","redo"}}));updateResult(call("edit_notes",request),false,false);selectPart(r->addedPart);
   check(voiceSourcePath(r->addedPart)==r->second.getFullPathName(),"Redo restores the voice and its source association");
   {juce::FileOutputStream image(r->folder.getChildFile("voice-source-grouped.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);}
   r->stage=34;saveProjectTo(r->multiple.getFullPathName(),[this,r,finish](bool ok){if(!ok){finish(false);return;}dirty=false;openProject(r->multiple.getFullPathName());r->stage=33;});
  }else if(r->stage==33&&!locked&&notesReady){
   auto container=phthongos::project::read(r->multiple);auto key=juce::Identifier(String(r->addedPart));
   check(container.metadata["voice_transcriptions"][key]["cache"]["compressed"].toString().isNotEmpty()&&voiceSourcePath(r->addedPart)==r->second.getFullPathName(),"Reopening retains each voice's source path and compressed neural cache");
   auto request=args().clone();request.getDynamicObject()->setProperty("part",r->addedPart);auto options=phthongos::basicPitch::Options();options.energyTolerance=3;options.noteSensitivity=.05;options.minPitch=48;options.maxPitch=84;options.inferOnsets=false;options.melodiaTrick=false;request.getDynamicObject()->setProperty("options",options.json());updateResult(call("redecode_voice",request),false,false);
   std::vector<var> old;for(auto v:items(result["editing"]["notes"]))if(Note::parse(v).part!=r->addedPart)old.push_back(v);
   check(bool(result["reused_inference"])&&juce::JSON::toString(array(old))==juce::JSON::toString(r->original),"Cached per-voice decoding after reopening preserves all neighbouring notes");
   {auto request=args().clone();request.getDynamicObject()->setProperty("part",r->addedPart);auto estimate=call("estimate_notes_tempo",request);check(int(estimate["part"])==r->addedPart&&estimate["source"].toString()=="notes","Estimator targets the selected voice independently of its audio source");request.getDynamicObject()->setProperty("part",0);auto other=call("estimate_notes_tempo",request);check(int(other["part"])==0&&number(other,"attacks")>0,"Playback-locked neighbouring voice remains available for independent BPM estimation");}
   auto advancedPath=r->folder.getChildFile("advanced-voices.pht");auto saveRequest=args().clone();saveRequest.getDynamicObject()->setProperty("path",advancedPath.getFullPathName());saveRequest.getDynamicObject()->setProperty("ui",ui());call("save_project",saveRequest);auto reopened=call("open_project",object({{"path",advancedPath.getFullPathName()}}));audio=reopened["audio"];updateResult(reopened["result"],false,false);
   auto restoredOptions=result["voice_transcriptions"][juce::Identifier(String(r->addedPart))]["settings"];check(int(restoredOptions["energyTolerance"])==3&&int(restoredOptions["minPitch"])==48&&int(restoredOptions["maxPitch"])==84&&!bool(restoredOptions["inferOnsets"])&&!bool(restoredOptions["melodiaTrick"]),"All advanced settings persist independently in each voice cache through PHT reopening");
   selectPart(r->addedPart);auto* low=dynamic_cast<juce::ComboBox*>(pitchControls->findChildWithID("basicPitchMin"));auto* high=dynamic_cast<juce::ComboBox*>(pitchControls->findChildWithID("basicPitchMax"));auto* infer=dynamic_cast<juce::ToggleButton*>(pitchControls->findChildWithID("basicPitchInfer"));check(dynamic_cast<juce::Slider*>(pitchControls->findChildWithID("basicPitchEnergy"))->getValue()==3&&low&&high&&infer&&low->getSelectedId()==49&&high->getSelectedId()==85&&!infer->getToggleState(),"Selecting the reconstructed voice restores its own advanced controls");selectPart(0);check(dynamic_cast<juce::Slider*>(pitchControls->findChildWithID("basicPitchEnergy"))->getValue()==11&&low->getSelectedId()==22&&infer->getToggleState(),"Switching voices restores the other analysis settings without changing either voice");
   partId=r->addedPart;syncPitchControls();voicePresentation.setSelectedId(2,juce::dontSendNotification);roll.displayMode=0;refresh();
   {juce::FileOutputStream image(r->folder.getChildFile("voice-source-stacked.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),image);}
   check(stackedView&&stacked->audioSources[0]==r->wav.getFullPathName()&&stacked->audioSources[r->addedPart]==r->second.getFullPathName(),"Stacked voices each display their own source");
   r->afterReload=result["editing"]["notes"].clone();r->stage=35;reloadVoiceAudio(0);
  }else if(r->stage==35&&!locked&&notesReady){
   check(audio["name"].toString()==r->wav.getFileName()&&juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->afterReload),"Reloading a voice source changes only reference audio, never MIDI notes");
   auto request=args().clone();request.getDynamicObject()->setProperty("part",r->addedPart);request.getDynamicObject()->setProperty("options",phthongos::basicPitch::Options().json());auto decoded=call("redecode_voice",request);
   check(bool(decoded["reused_inference"]),"A voice retains its own cache while another stem is the active audio reference");updateResult(decoded,false,false);
   // Start a genuine imported-MIDI document and retain an edited note when adding transcription.
   auto midi=phthongos::midi::Source::blank("imported.mid");midi.bytes={77,84,104,100,0,0,0,6,0,0,0,1,1,224,77,84,114,107,0,0,0,13,0,144,60,80,131,96,128,60,0,0,255,47,0};auto path=r->folder.getChildFile("imported.mid");check(path.replaceWithData(midi.bytes.data(),midi.bytes.size()),"Prepare isolated imported MIDI");
   auto imported=call("import_midi_audio",object({{"midiPath",path.getFullPathName()},{"audioPath",r->second.getFullPathName()}}));audio=imported["audio"];updateResult(imported["result"],false,false);
   request=args().clone();request.getDynamicObject()->setProperty("action",object({{"kind","add"},{"pitch",48},{"start",.2},{"end",.8},{"velocity",91},{"part",result["midi_parts"][0]}}));updateResult(call("edit_notes",request),false,false);r->original=result["editing"]["notes"].clone();
   check(buttonMap["analyze"]->isVisible()&&model.isVisible(),"Imported MIDI with reference audio keeps the AI controls available");
   r->stage=36;prepare([this](var){analyze();});
  }else if(r->stage==36&&!locked&&notesReady&&items(result["transcribed_parts"]).size()>0){
   auto original=items(r->original).front();bool retained=false;for(auto n:items(result["editing"]["notes"]))if(n["id"]==original["id"]&&juce::JSON::toString(n)==juce::JSON::toString(original))retained=true;
   check(result["engine"].toString()=="midi"&&retained&&items(result["midi_parts"]).size()>=2,"An imported MIDI document accepts transcription in a new voice without replacing its edited note");
   {auto estimate=call("estimate_notes_tempo",args());check(estimate["source"].toString()=="notes","Imported MIDI and appended AI voices share the same note-tempo estimator");}
   auto request=args().clone();request.getDynamicObject()->setProperty("path",r->folder.getChildFile("imported-with-voice.pht").getFullPathName());request.getDynamicObject()->setProperty("ui",ui());call("save_project",request);auto reopened=call("open_project",object({{"path",r->folder.getChildFile("imported-with-voice.pht").getFullPathName()}}));check(juce::JSON::toString(reopened["result"]["editing"]["notes"])==juce::JSON::toString(result["editing"]["notes"]),"A MIDI-origin project persists added neural voices and their caches");
   r->stage=80;dirty=false;openAudioPath(r->wav.getFullPathName());
  }else if(r->stage==80&&!locked&&notesReady&&result.isVoid()){
   verifyScope="all";roll.bandEnabled=false;r->stage=81;verify();
  }else if(r->stage==81&&!locked&&!verification.isVoid()){
   check(result["engine"].toString()=="midi"&&items(result["editing"]["notes"]).empty()&&items(result["midi_parts"]).empty()&&!roll.ghosts.empty(),"WAV-only verification creates an empty editor and acoustic proposals without AI");
   auto missing=items(verification["missing"]);int index=0;
   while(missing.size()<305){missing.push_back(object({{"id","pagination-fixture-"+String(index)},{"pitch",36+index%48},{"start",.2+index*.02},{"end",.21+index*.02}}));++index;}
   verification.getDynamicObject()->setProperty("missing",array(missing));updateDiagnostics();
   check(roll.ghosts.size()==305&&int(diagnosticBatchPlan("all")["missing"])==305,"All 305 proposals remain eligible without pre-existing voices");
   openSections={"verification","missing"};diagnosticPages["missing"]=4;buildSide();int rows=0;for(auto* w:side.widgets)rows+=dynamic_cast<SidePanel::DiagnosticRow*>(w)!=nullptr;
   check(rows==50&&roll.ghosts.size()==305,"Pagination displays fifty rows while retaining every proposal");
   for(auto* w:side.widgets)if(w->getComponentID()=="diagnosticNext-missing")dynamic_cast<juce::TextButton*>(w)->onClick();
   buildSide();check(diagnosticPages["missing"]==5&&int(diagnosticBatchPlan("all")["missing"])==305,"Global batch includes every page after navigating the list");
   applyDiagnosticBatch("all");Form* form=nullptr;for(auto* f:Form::activeForms)if(f->getComponentID()=="diagnosticBatchConfirmation")form=f;
   check(form&&form->state()["destination"].toString()=="new","WAV-only confirmation defaults to a new voice");
   {juce::FileOutputStream image(r->folder.getChildFile("wav-only-batch.png"));juce::PNGImageFormat().writeImageToStream(form->createComponentSnapshot(form->getLocalBounds()),image);}
   bool applied=false;r->stage=82;for(auto* child:form->getChildren())if(auto* b=dynamic_cast<juce::TextButton*>(child))if(b->getButtonText()==tr(String::fromUTF8("Appliquer"))){check(b->isEnabled(),"Apply is enabled without previous MIDI transcription");b->onClick();applied=true;break;}
   check(applied,"Click the actual Apply confirmation button");
  }else if(r->stage==82&&!locked&&items(result["editing"]["notes"]).size()==305){
   check(items(result["midi_parts"]).size()==1&&int(result["editing"]["revision"])==1&&result["engine"].toString()=="midi","Batch creates one voice and all 305 notes in one revision without AI");
   r->edited=result["editing"]["notes"].clone();r->stage=83;command(object({{"kind","undo"}}));
  }else if(r->stage==83&&!locked){
   check(items(result["editing"]["notes"]).empty()&&items(result["midi_parts"]).empty(),"One undo removes the voice and entire spectral batch");
   r->stage=84;command(object({{"kind","redo"}}));
  }else if(r->stage==84&&!locked){
   check(juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->edited)&&items(result["midi_parts"]).size()==1,"Redo restores every spectral note and its voice");
   auto request=args().clone();auto path=r->folder.getChildFile("wav-only-spectral.pht");request.getDynamicObject()->setProperty("path",path.getFullPathName());request.getDynamicObject()->setProperty("ui",ui());call("save_project",request);
   auto reopened=call("open_project",object({{"path",path.getFullPathName()}}));check(juce::JSON::toString(reopened["result"]["editing"]["notes"])==juce::JSON::toString(r->edited),"WAV-only spectral notes survive project reopening without a neural cache");
   audio=reopened["audio"];roll.audio=audio;updateResult(reopened["result"],false,false);
   auto originalVelocities=result["editing"]["notes"].clone();auto beforeRevision=result["editing"]["revision"];
   auto measured=call("estimate_dynamics",args());check(!bool(measured["cached"])&&!items(measured["estimates"]).empty(),"Spectral and manually inserted notes are eligible for acoustic dynamics");
   auto reused=call("estimate_dynamics",args());check(bool(reused["cached"]),"Normal dynamics estimation reuses the available measures");auto forcedRequest=args().clone();forcedRequest.getDynamicObject()->setProperty("force",true);auto forced=call("estimate_dynamics",forcedRequest);check(!bool(forced["cached"])&&result["editing"]["revision"]==beforeRevision&&juce::JSON::toString(originalVelocities)==juce::JSON::toString(result["editing"]["notes"]),"Explicit recalculation bypasses the cache without editing notes or adding history");
   dynamics=forced;openSections.insert("dynamics");buildSide();bool recalculate=false;for(auto* w:side.widgets)if(auto* b=dynamic_cast<juce::TextButton*>(w))if(b->getButtonText()==tr(String::fromUTF8("Recalculer depuis l’audio")))recalculate=b->isEnabled();check(recalculate,"The dynamics panel keeps an enabled Recalculate button after estimation");
   auto chosen=items(forced["estimates"]).front()["note"];auto chosenId=chosen["id"];auto mapping=object({{"level",90.},{"contrast",0.},{"min",1},{"max",127}});auto apply=args().clone();apply.getDynamicObject()->setProperty("ids",array({chosenId}));apply.getDynamicObject()->setProperty("mapping",mapping);auto applied=call("apply_dynamics",apply);updateResult(applied,false,false);
   auto selected=[this,chosenId]{for(auto n:items(result["editing"]["notes"]))if(n["id"]==chosenId)return n;return var();};check(int(selected()["velocity_midi"])==90&&selected()["velocity_source"].toString()=="audioEstimated"&&bool(selected()["protected"]),"Audio velocity applies to a protected spectral note while preserving edit protection");
   auto saveRequest=args().clone();auto dynamicPath=r->folder.getChildFile("manual-audio-dynamics.pht");saveRequest.getDynamicObject()->setProperty("path",dynamicPath.getFullPathName());saveRequest.getDynamicObject()->setProperty("ui",ui());call("save_project",saveRequest);auto dynamicOpen=call("open_project",object({{"path",dynamicPath.getFullPathName()}}));audio=dynamicOpen["audio"];roll.audio=audio;updateResult(dynamicOpen["result"],false,false);check(int(selected()["velocity_midi"])==90&&selected()["velocity_source"].toString()=="audioEstimated","PHT reopening preserves audio dynamics on a manually stored spectral note");
   auto editRequest=args().clone();editRequest.getDynamicObject()->setProperty("action",object({{"kind","undo"}}));updateResult(call("edit_notes",editRequest),false,false);check(juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(originalVelocities),"One undo restores all original spectral velocities after reopening");
   editRequest=args().clone();editRequest.getDynamicObject()->setProperty("action",object({{"kind","redo"}}));updateResult(call("edit_notes",editRequest),false,false);check(int(selected()["velocity_midi"])==90,"Redo restores the audio velocity on the spectral note");
   int target=Note::parse(selected()).part;editRequest=args().clone();editRequest.getDynamicObject()->setProperty("action",object({{"kind","toggle_part_lock"},{"part",target}}));updateResult(call("edit_notes",editRequest),false,false);auto lockedMeasures=call("cached_dynamics",args());check(items(lockedMeasures["estimates"]).empty(),"Locked spectral voice is excluded from dynamics proposals");
   finish(true);
  }else if(r->stage==11&&!locked&&notesReady){
   auto request=args().clone();request.getDynamicObject()->setProperty("path",r->folder.getChildFile("muscriptor.pht").getFullPathName());request.getDynamicObject()->setProperty("ui",ui());call("save_project",request);
   check(phthongos::project::read(r->folder.getChildFile("muscriptor.pht")).metadata["basic_pitch"].isVoid(),"Switching to MuScriptor clears the previous Basic Pitch cache and preserves its own project source");
   r->original=result["editing"]["notes"].clone();r->stage=37;invoke("analyze_transcription",object({{"audioId",audio["id"]},{"append",true}}),[this,r,check,finish](var value){try{
    auto added=items(value["transcribed_parts"]);check(!added.empty(),"Installed MuScriptor Small can append transcription as new voices");std::set<int> keys;for(auto key:added)keys.insert(int(key));std::vector<var> original;for(auto n:items(value["editing"]["notes"]))if(!keys.count(Note::parse(n).part))original.push_back(n);
    check(juce::JSON::toString(array(original))==juce::JSON::toString(r->original),"MuScriptor append preserves every pre-existing MIDI event");updateResult(value,false,false);for(auto key:keys)check(voiceSourcePath(key)==r->wav.getFullPathName(),"Appended MuScriptor voices retain the source stem");finish(true);
   }catch(const std::exception& e){r->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())}}));finish(false);}},true,[finish](String){finish(false);});
  }
 }catch(const std::exception& e){r->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())}}));finish(false);}};
 struct Poll:juce::Timer{std::shared_ptr<std::function<void()>> callback;explicit Poll(std::shared_ptr<std::function<void()>> p):callback(std::move(p)){startTimer(50);}void timerCallback()override{if(!*callback){delete this;return;}(*callback)();}};new Poll(poll);
}
