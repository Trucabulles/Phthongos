#include "App.h"
#include "../core/midi/Midi.h"
#include "../core/midi/TempoEstimate.h"
#include "../core/project/DocumentJson.h"
namespace {var with(var value,const String& key,var entry){value=value.clone();value.getDynamicObject()->setProperty(key,entry);return value;}}

void App::initialiseMidiInput(){
  for(auto* c:std::vector<juce::Component*>{&midiStepTool,&midiRecord})addAndMakeVisible(c);
  midiLength.setName(tr(String::fromUTF8("Durée de saisie MIDI")));
  int i=1;for(auto text:{"1/1","1/2","1/4","1/8","1/16","1/32","1/8T","1/16T"})midiLength.addItem(text,i++);
  midiLength.setSelectedId(3,juce::dontSendNotification);
  midiStepTool.onClick=[this]{midiStepOptions();};
  midiStep.onClick=[this]{resetMidiInput();refreshMidiInput();};midiLength.onChange=[this]{resetMidiInput();};
  midiRecord.setComponentID("midiRecord");
  midiRecord.onClick=[this]{if(midiRecording||!midiRecorded.empty())stopMidiRecording();else startMidiRecording();};
  midiInput=std::make_unique<MidiInputController>(savePreferencesFile.getSiblingFile("midi-input.json"));
  midiInput->onMessage=[this](const juce::MidiMessage& m){handleMidiInput(m);};
  midiInput->onReset=[this]{resetMidiInput(true);};
  midiInput->onConnection=[this](bool ready){if(!ready)resetMidiInput(true);refreshMidiInput();};
  midiInput->start();resized();refreshMidiInput();
}
void App::refreshMidiInput(){
  if(!midiInput)return;
  bool writable=!locked&&!audio.isVoid()&&!result.isVoid()&&notesReady&&partId>=0&&!roll.lockedParts.count(partId)&&!(bool)playback["playing"];
  midiStepTool.setEnabled(!locked&&!audio.isVoid()&&!midiRecordPending);
  midiStepTool.setToggleState(midiStep.getToggleState(),juce::dontSendNotification);
  midiStepTool.setTooltip(tr(String::fromUTF8("Saisie pas à pas : maintenir les touches voulues, puis → pour inscrire et avancer. Réglages dans ce panneau.")));
  midiRecord.setEnabled(!midiRecordPending&&(midiRecording||!midiRecorded.empty()||(writable&&midiInput->connected())));
  auto recordAction=(midiRecordPending?tr(String::fromUTF8("Validation MIDI…")):midiRecording?tr(String::fromUTF8("Arrêter la prise")):!midiRecorded.empty()?tr(String::fromUTF8("Réessayer la prise")):tr(String::fromUTF8("Enregistrer MIDI en temps réel")));
  midiRecord.setName(recordAction);midiRecord.setButtonText({});
  midiRecord.getProperties().set("recordPending",midiRecordPending);
  midiRecord.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xffb44735));
  midiRecord.setToggleState(midiRecording,juce::dontSendNotification);
  midiRecord.setTooltip({});
  midiRecord.repaint();
}
double App::midiStepBeats()const{switch(midiLength.getSelectedId()){case 1:return 4;case 2:return 2;case 4:return .5;case 5:return .25;case 6:return .125;case 7:return 1./3;case 8:return 1./6;default:return 1;}}
void App::resetMidiInput(bool disarm){
  if(midiRecording)stopMidiRecording();
  midiCounting=false;countHeld.clear();physicalMidi.clear();
  midiChord.reset();midiChords.clear();midiDown.clear();midiNext=-1;++midiGeneration;
  if(disarm)midiStep.setToggleState(false,juce::dontSendNotification);
  updateMidiPreview();
  if(!preparedAudioId.isVoid()&&preparedAudioId==audio["id"])
    bridge.request("player_midi",object({{"audioId",audio["id"]},{"action","panic"}}),[](var){});
}
void App::midiSettings(Form* form){
  std::vector<std::pair<String,var>> choices{{tr(String::fromUTF8("Aucune entrée MIDI")),String()}};bool found=false;
  const auto selected=midiInput?midiInput->identifier():String();
  for(const auto& device:juce::MidiInput::getAvailableDevices()){choices.emplace_back(device.name,device.identifier);found|=device.identifier==selected;}
  if(selected.isNotEmpty()&&!found)choices.emplace_back(tr(String::fromUTF8("Clavier MIDI déconnecté")),selected);
  form->combo(tr(String::fromUTF8("Entrée USB / MIDI")),"midi_input",choices,selected);form->comboFields["midi_input"]->getProperties().set("preserveLanguageText",true);
}
void App::handleMidiInput(const juce::MidiMessage& message){
  if(audio.isVoid()||preparedAudioId!=audio["id"])return;
  const int channel=message.getChannel()-1;if(channel<0||channel>15)return;
  if(message.isAllNotesOff()||message.isAllSoundOff()){resetMidiInput();return;}
  unsigned program=0;for(const auto& p:items(result["midi_parts"]))if((p["identity"].isVoid()?(int)p["program"]:(int)p["identity"])==partId){program=unsigned(int(p["program"]));break;}
  auto send=[&](const String& action,int pitch,int velocity){bridge.request("player_midi",object({{"audioId",audio["id"]},{"action",action},{"channel",channel},{"pitch",pitch},{"velocity",velocity},{"program",int(program)},{"voice",partId},{"db",partId>=0?number(voiceLevels,String(partId).toRawUTF8(),0):0}}),[](var){});};
  const bool modal=confirmation||juce::Component::getNumCurrentlyModalComponents()>0;
  if(message.isController()){if(message.getControllerNumber()==64&&(!modal||message.getControllerValue()<64))send("sustain",0,message.getControllerValue());return;}
  if(!message.isNoteOnOrOff())return;
  const auto pitch=message.getNoteNumber();const unsigned key=unsigned(channel*128+pitch);const bool on=message.isNoteOn();
  const double stamp=message.getTimeStamp()>0?message.getTimeStamp():juce::Time::getMillisecondCounterHiRes()/1000.;
  if(midiCounting&&stamp>=midiCaptureStartTime)finishMidiCountIn();
  if(on)physicalMidi[key]={pitch,int(message.getVelocity())};else physicalMidi.erase(key);
  if(midiCounting){send("note",pitch,on?int(message.getVelocity()):0);if(on)countHeld[key]={pitch,int(message.getVelocity())};else countHeld.erase(key);return;}
  if(midiRecording){send("note",pitch,on?int(message.getVelocity()):0);recordMidiEvent(message);return;}
  if(!on){send("note",pitch,0);midiDown.erase(key);
    if(midiChord){midiChord->notes.erase(key);if(midiChord->notes.empty())midiChord.reset();updateMidiPreview();}return;
  }
  if(modal||(locked&&!midiCommitting))return;
  send("note",pitch,int(message.getVelocity()));midiDown.insert(key);
  bool writable=midiStep.getToggleState()&&partId>=0&&!roll.lockedParts.count(partId)&&!result.isVoid()&&!(bool)playback["playing"];
  if(!writable)return;
  if(!midiChord){double start=midiNext>=0?midiNext:(roll.keyboardSeekTarget>=0?roll.keyboardSeekTarget:number(playback,"position"));auto& grid=roll.rhythmicGrid;double beat=grid.beatAt(start),length=grid.secondsAt(beat+midiStepBeats())-start;
    if(!std::isfinite(length)||length<=0||start+length>1200){resetMidiInput(true);status.setText(tr(String::fromUTF8("La saisie MIDI dépasse la durée maximale de 20 minutes.")),juce::dontSendNotification);return;}
    midiChord=StepChord{audio["id"],partId,start,length,{}};
  }
  midiChord->notes[key]={pitch,int(message.getVelocity())};
  updateMidiPreview();
}
void App::midiStepOptions(){
  if(locked)return;
  if(midiStepForm){midiStepForm->getTopLevelComponent()->toFront(true);return;}
  auto f=juce::Component::SafePointer<Form>(new Form);midiStepForm=f;
  f->toggle(tr(String::fromUTF8("Activer la saisie pas à pas")),"enabled",midiStep.getToggleState());
  std::vector<std::pair<String,var>> lengths;for(int i=1;i<=midiLength.getNumItems();++i)lengths.emplace_back(midiLength.getItemText(i-1),i);
  f->combo(tr(String::fromUTF8("Durée des notes et avancement")),"length",lengths,midiLength.getSelectedId());
  f->label(tr(String::fromUTF8("Maintenir une ou plusieurs touches, puis appuyer sur → dans le piano roll pour inscrire ces notes et avancer de la durée choisie. Relâcher sans avancer ne crée aucune note. La grille visuelle reste indépendante.")));
  f->label(tr(String::fromUTF8("Pour capturer les attaques et les durées jouées : fermer ce panneau, puis R ou le bouton ●. Espace termine la prise, sans quantification.")));
  f->onApply=[s=safe(),f](var value){if(!s)return;s->resetMidiInput();s->midiLength.setSelectedId(int(value["length"]),juce::dontSendNotification);s->midiStep.setToggleState(bool(value["enabled"]),juce::dontSendNotification);s->refresh();if(f)f->close();s->roll.grabKeyboardFocus();};
  f->actions(tr(String::fromUTF8("Appliquer")),tr(String::fromUTF8("Annuler")));f->show(tr(String::fromUTF8("Saisie pas à pas")));
}
void App::advanceMidiStep(){
  if(!midiStep.getToggleState()||!midiChord||midiChord->notes.empty()||midiRecording||midiRecordPending||(locked&&!midiCommitting))return;
  auto next=*midiChord;queueMidiChord();
  if(!midiStep.getToggleState())return;
  next.start=midiNext;auto& grid=roll.rhythmicGrid;next.duration=grid.secondsAt(grid.beatAt(next.start)+midiStepBeats())-next.start;
  if(next.start+next.duration<=1200)midiChord=std::move(next);updateMidiPreview();
}
void App::queueMidiChord(){
  if(!midiChord)return;
  if(midiChords.size()>=128){status.setText(tr(String::fromUTF8("Saisie MIDI interrompue : trop de notes en attente.")),juce::dontSendNotification);resetMidiInput(true);return;}
  midiNext=midiChord->start+midiChord->duration;midiChords.push_back(std::move(*midiChord));midiChord.reset();updateMidiPreview();commitMidiChord();
}
void App::commitMidiChord(){
  if(midiCommitting||midiChords.empty()||locked)return;
  auto chord=std::move(midiChords.front());midiChords.pop_front();
  if(chord.audio!=audio["id"]||chord.part!=partId||roll.lockedParts.count(chord.part)||(bool)playback["playing"]||!midiStep.getToggleState()){resetMidiInput();return;}
  std::vector<var> notes;for(const auto& key:chord.notes){phthongos::Note n;n.pitch=key.second.first;n.velocity=key.second.second;n.velocityNormalised=float(n.velocity)/127;n.start=0;n.end=float(chord.duration);notes.push_back(phthongos::project::writeNote(n));}
  auto request=args();request.getDynamicObject()->setProperty("action",object({{"kind","paste_notes"},{"notes",array(notes)},{"at",chord.start},{"part",chord.part}}));
  midiInFlight=chord;midiCommitting=true;const auto generation=midiGeneration;setBusy(true);
  invoke("edit_notes",request,[this,chord,generation](var value){
    verificationStale=true;midiInFlight.reset();updateResult(value,false,false);updateMidiPreview();
    syncNotes(false,[this,chord,generation](var){
      double next=chord.start+chord.duration;
      if(generation==midiGeneration){playback=playback.clone();playback.getDynamicObject()->setProperty("position",next);roll.keyboardSeekTarget=-1;roll.updatePlayback(playback);}
      // Keep the transaction in flight through the seek: rapid notes cannot race its callback.
      invoke("player_control",with(with(args(),"action","seek"),"position",next),[this,generation](var){
        midiCommitting=false;setBusy(false,tr(String::fromUTF8("Saisie MIDI ajoutée · Ctrl+Z pour annuler.")));
        if(generation==midiGeneration)commitMidiChord();
      },false,[this](String error){midiCommitting=false;setBusy(false,error);commitMidiChord();});
    });
  },false,[this](String error){midiCommitting=false;midiInFlight.reset();resetMidiInput(true);setBusy(false,error);});
}
void App::updateMidiPreview(){
  roll.midiPreview.clear();
  auto add=[this](const StepChord& chord){for(const auto& key:chord.notes){Note n;n.pitch=key.second.first;n.velocity=key.second.second;n.start=chord.start;n.end=chord.start+chord.duration;n.part=chord.part;roll.midiPreview.push_back(n);}};
  if(midiInFlight)add(*midiInFlight);for(const auto& chord:midiChords)add(chord);if(midiChord)add(*midiChord);
  for(const auto& n:midiRecorded)roll.midiPreview.push_back(n);
  const double now=midiEventTime(juce::Time::getMillisecondCounterHiRes()/1000.);
  for(const auto& entry:midiHeld){auto n=entry.second;n.end=std::max(n.start+1./48000,now);roll.midiPreview.push_back(n);}
  if(midiRecording&&!midiCounting){roll.scoreDuration=std::max(roll.scoreDuration,now+.5);if(now>=1200)stopMidiRecording();}
  roll.repaint();
}
double App::midiEventTime(double stamp)const{
  if(!std::isfinite(stamp)||stamp<=0)stamp=juce::Time::getMillisecondCounterHiRes()/1000.;
  return std::clamp(midiClockPosition+stamp-midiClockTime,0.,1200.);
}
void App::updateMidiClock(const var& snapshot){
  if(!midiRecording)return;
  if(!(bool)snapshot["ready"]||!(bool)snapshot["playing"]){stopMidiRecording();return;}
  // Keep one audio-anchored monotonic origin for the entire take. Re-anchoring
  // between Note On/Off would turn buffer scheduling jitter into changed lengths.
}
void App::startMidiRecording(){
  if(locked||midiRecordPending||audio.isVoid()||result.isVoid()||!notesReady||partId<0||roll.lockedParts.count(partId)||(bool)playback["playing"])return;
  auto heldBefore=physicalMidi;resetMidiInput(true);physicalMidi=heldBefore;syncClick();midiRecorded.clear();midiHeld.clear();midiTakeStarts.clear();midiRecordPart=partId;midiRecordAudio=audio["id"];
  double start=roll.keyboardSeekTarget>=0?roll.keyboardSeekTarget:number(playback,"position");
  midiRecordPending=true;setBusy(true,tr(String::fromUTF8("Préparation de la prise MIDI…")));
  invoke("player_mix",with(with(with(args(),"mode",mode),"audioDb",audioDb),"midiDb",midiDb));
  invoke("player_reverb",with(args(),"amount",reverbEnabled?reverbAmount:0));
  invoke("player_record",object({{"audioId",audio["id"]},{"start",true},{"position",start},{"preCount",preCountBars}}),[this,start](var snapshot){
    midiClockTime=number(snapshot,"clock_time")+number(snapshot,"count_remaining");midiClockPosition=number(snapshot,"clock_position");
    midiCaptureStartTime=midiClockTime+start-midiClockPosition;
    midiCounting=true;countHeld=physicalMidi;
    auto beat=roll.rhythmicGrid.beatAt(start);auto meter=roll.rhythmicGrid.meters.front();for(auto m:roll.rhythmicGrid.meters)if(m.beat<=beat)meter=m;
    auto tempo=roll.rhythmicGrid.tempos.front();for(auto t:roll.rhythmicGrid.tempos)if(t.seconds+roll.rhythmicGrid.origin<=start)tempo=t;
    countPulseSeconds=tempo.quarter*4./meter.denominator;countMeter=meter.numerator;
    midiRecording=true;if(number(snapshot,"count_remaining")<=0)finishMidiCountIn();midiRecordPending=false;playback=snapshot;roll.keyboardSeekTarget=-1;roll.updatePlayback(snapshot);
    calculationStarted=0;operationLabel={};updateTakeDuration();status.setText(tr(String::fromUTF8("Prise MIDI en cours · Sans quantification · Stop ou Espace pour terminer.")),juce::dontSendNotification);refresh();updateClickVisuals();
  },false,[this](String error){midiRecordPending=false;setBusy(false,error);});
}
void App::recordMidiEvent(const juce::MidiMessage& message){
  if(message.getTimeStamp()>0&&message.getTimeStamp()<midiCaptureStartTime)return;
  const unsigned key=unsigned((message.getChannel()-1)*128+message.getNoteNumber());const double at=midiEventTime(message.getTimeStamp());
  auto found=midiHeld.find(key);
  if(found!=midiHeld.end()){auto n=found->second;n.end=std::max(n.start+1./48000,at);midiRecorded.push_back(n);midiHeld.erase(found);}
  if(message.isNoteOn()){
    midiTakeStarts.push_back({at,message.getNoteNumber(),int(message.getVelocity())});
    Note n;n.pitch=message.getNoteNumber();n.velocity=message.getVelocity();n.start=at;n.end=at+1./48000;n.part=midiRecordPart;
    for(const auto& p:items(result["midi_parts"]))if((int)p["identity"]==midiRecordPart)n.program=int(p["program"]);
    midiHeld[key]=n;
  }
  updateMidiPreview();
}
void App::stopMidiRecording(std::function<void()> done){
  if(midiRecording&&midiInput)midiInput->drain();
  if(midiRecordPending)return;
  if(!midiRecording&&midiRecorded.empty()){if(done)done();return;}
  const double at=midiEventTime(juce::Time::getMillisecondCounterHiRes()/1000.);
  for(auto& entry:midiHeld){auto n=entry.second;n.end=std::max(n.start+1./48000,at);midiRecorded.push_back(n);}midiHeld.clear();
  const bool countCancelled=midiCounting;midiCounting=false;countHeld.clear();
  midiRecording=false;midiRecordPending=true;updateMidiPreview();refreshMidiInput();
  auto failed=[this](String error){midiRecordPending=false;setBusy(false,error);updateMidiPreview();};
  invoke("player_record",object({{"audioId",midiRecordAudio},{"start",false}}),[this,done,failed,countCancelled](var snapshot){
    playback=snapshot;roll.updatePlayback(snapshot);
    bridge.request("player_midi",object({{"audioId",midiRecordAudio},{"action","panic"}}),[](var){});
    if(midiRecorded.empty()){midiRecordPending=false;setBusy(false,(countCancelled?tr(String::fromUTF8("Précompte annulé.")):tr(String::fromUTF8("Prise MIDI vide."))));if(done)done();return;}
    std::vector<var> notes;for(const auto& original:midiRecorded){phthongos::Note n;n.pitch=original.pitch;n.velocity=original.velocity;n.velocityNormalised=float(n.velocity)/127;n.start=float(original.start);n.end=std::max(float(original.end),std::nextafter(n.start,std::numeric_limits<float>::infinity()));notes.push_back(phthongos::project::writeNote(n));}
    auto request=args();request.getDynamicObject()->setProperty("action",object({{"kind","paste_notes"},{"notes",array(notes)},{"at",double(float(std::min_element(midiRecorded.begin(),midiRecorded.end(),[](const Note& a,const Note& b){return a.start<b.start;})->start))},{"part",midiRecordPart}}));
    invoke("edit_notes",request,[this,done](var value){
      midiRecorded.clear();updateMidiPreview();verificationStale=true;updateResult(value,false,false);
      syncNotes(false,[this,done](var){midiRecordPending=false;estimateMidiTake();setBusy(false,tr(String::fromUTF8("Prise MIDI ajoutée · Ctrl+Z pour annuler toute la prise.")));if(done)done();});
    },false,failed);
  },false,failed);
}

void App::updateTakeDuration(){
  if(!midiRecording)return;
  if(midiCounting){auto pulses=std::max(1,int(std::ceil((midiCaptureStartTime-juce::Time::getMillisecondCounterHiRes()/1000.)/countPulseSeconds)));calculation.setText(localisation::format(String::fromUTF8("Précompte · {0}"),{String(pulses)}),juce::dontSendNotification);return;}
  const auto seconds=int(std::max(0.,juce::Time::getMillisecondCounterHiRes()/1000.-midiCaptureStartTime));
  const auto elapsed=String(seconds/60)+":"+String(seconds%60).paddedLeft('0',2);
  calculation.setText(localisation::format(String::fromUTF8("● Prise MIDI · {0}"),{elapsed}),juce::dontSendNotification);
}
void App::estimateMidiTake(){
  auto estimate=phthongos::midi::estimateTempo(midiTakeStarts);
  std::vector<var> candidates,histogram;
  for(auto c:estimate.candidates)candidates.push_back(object({{"bpm",c.bpm},{"score",c.score}}));
  for(auto weight:estimate.histogram)histogram.push_back(weight);
  midiTempoEstimate=object({{"bpm",estimate.bpm},{"support",estimate.support},{"attacks",int(estimate.attacks)},{"seconds",estimate.seconds},{"reason",String(estimate.reason)},{"candidates",array(candidates)},{"histogram",array(histogram)},{"shortestSeconds",estimate.shortestSeconds},{"sampleRate",int(estimate.sampleRate)},{"engine","midi-bpm-cpp/e14a86c"}});
  midiEstimateAudio=audio["id"];midiEstimatePending=estimate.bpm>0;refreshTempo();
}
String App::midiTempoUnavailable()const{
  const auto reason=midiTempoEstimate["reason"].toString();
  if(midiTempoEstimate["source"].toString()=="notes"){
   if(reason=="few_attacks")return localisation::format(String::fromUTF8("Notes analysées : {0} attaques distinctes. Au moins 2 sont nécessaires pour estimer le BPM."),{String(int(midiTempoEstimate["attacks"]))});
   if(reason=="short_span")return tr(String::fromUTF8("Notes analysées : les attaques sont trop rapprochées pour estimer le BPM."));
   if(reason=="few_intervals")return tr(String::fromUTF8("Notes analysées : trop peu d’intervalles exploitables pour estimer le BPM."));
   return tr(String::fromUTF8("Notes analysées : plusieurs pulsations restent possibles. Aucun BPM suffisamment fiable à proposer."));
  }
  if(reason=="few_attacks")return localisation::format(String::fromUTF8("Dernière prise : {0} attaques distinctes. Au moins 2 sont nécessaires pour estimer le BPM."),{String(int(midiTempoEstimate["attacks"]))});
  if(reason=="short_span")return localisation::format(String::fromUTF8("Dernière prise : {0} s entre la première et la dernière attaque. Elles sont trop rapprochées pour estimer le BPM."),{String(number(midiTempoEstimate,"seconds"),1)});
  if(reason=="few_intervals")return tr(String::fromUTF8("Dernière prise : trop peu d’intervalles exploitables pour estimer le BPM."));
  return tr(String::fromUTF8("Dernière prise : plusieurs pulsations restent possibles. Aucun BPM suffisamment fiable à proposer."));
}
void App::addMidiTempoEstimate(Form* f,var info){
  if(midiTempoEstimate.isVoid()||midiEstimateAudio!=audio["id"])return;
  bool fromNotes=midiTempoEstimate["source"].toString()=="notes";
  if(fromNotes)f->label(tr(String::fromUTF8("Voix analysée : "))+(midiTempoEstimate["part"].isVoid()?tr(String::fromUTF8("Toutes les parties")):midiTempoEstimate["scope_label"].toString()),false);
  if(!hasMidiTempoEstimate()){f->label(midiTempoUnavailable());return;}
  auto bpm=number(midiTempoEstimate,"bpm");
  f->label(fromNotes?localisation::format(String::fromUTF8("Tempo estimé depuis les notes : ≈ {0} BPM"),{String(bpm,1)}):localisation::format(String::fromUTF8("Tempo approximatif de la dernière prise : ≈ {0} BPM"),{String(bpm,1)}));
  auto tempoText=[](double value){auto text=String(value,2).trimCharactersAtEnd("0").trimCharactersAtEnd(".");return localisation::code()=="fr"?text.replaceCharacter('.',','):text;};
  String alternatives;
  if(bpm*2>=10&&bpm*2<=600)alternatives=localisation::format(String::fromUTF8("Alternative ×2 : {0} BPM"),{tempoText(bpm*2)});
  if(bpm/2>=10&&bpm/2<=600){if(alternatives.isNotEmpty())alternatives+=String::fromUTF8(" · ");alternatives+=localisation::format(String::fromUTF8("Alternative ÷2 : {0} BPM"),{tempoText(bpm/2)});}
  if(alternatives.isNotEmpty())f->label(alternatives,false);
  std::vector<std::pair<String,var>> choices{{tr(String::fromUTF8("Conserver le BPM du projet")),0.}};
  for(auto factor:{.5,1.,2.})if(bpm*factor>=10&&bpm*factor<=600)choices.emplace_back(String(bpm*factor,1)+" BPM"+(factor==.5?tr(String::fromUTF8(" · moitié")):factor==2?tr(String::fromUTF8(" · double")):tr(String::fromUTF8(" · estimation"))),bpm*factor);
  for(auto candidate:items(midiTempoEstimate["candidates"])){
    double value=number(candidate,"bpm"),weight=number(candidate,"score");
    if(!std::isfinite(value)||value<10||value>600||!std::isfinite(weight)||weight<0||weight>1)continue;
    bool duplicate=false;for(const auto& choice:choices)duplicate|=std::abs(double(choice.second)-value)<.5;
    if(!duplicate)choices.emplace_back(localisation::format(String::fromUTF8("{0} BPM · score relatif {1} %"),{String(value,1),String(int(std::round(weight*100)))}),value);
  }
  if(!items(midiTempoEstimate["candidates"]).empty())f->label(tr(String::fromUTF8("Autres hypothèses disponibles dans la liste. Les scores comparent les pics du calcul ; ce ne sont pas des probabilités de justesse.")));
  f->combo(tr(String::fromUTF8("Choisir une proposition avant d’appliquer")),"take_tempo",choices,0.,[f,info](var value){
    double selected=double(value);f->setValue("bpm",String(selected>0?selected:number(info,"bpm"),3));f->comboFields["mode"]->setSelectedId(selected>0?1:((bool)info["has_midi"]&&!(bool)info["fixed"]?2:1),juce::sendNotificationSync);
  });
  f->button(fromNotes?tr(String::fromUTF8("Écouter avec ce tempo")):tr(String::fromUTF8("Écouter la prise avec ce tempo")),[s=safe(),f]{if(s&&f)try{s->previewTempo(decimal(f->state()["bpm"]));}catch(const std::exception& e){f->setHeading(localisation::error(String::fromUTF8(e.what())));}});
  f->button(tr(String::fromUTF8("Arrêter l’écoute du tempo")),[s=safe()]{if(s)s->stopTempoPreview();});
  f->onDismiss=[s=safe()]{if(s)s->stopTempoPreview();};
  f->label(tr(String::fromUTF8("Proposition indicative, ambiguë à la moitié ou au double. Aucune note n’est déplacée ou quantifiée. Le BPM change uniquement après confirmation.")));
}

void App::runMidiInputTests(std::function<void(bool)> done){
  if(juce::JUCEApplication::getCommandLineParameters().contains("--stacked-midi-input-test"))voicePresentation.setSelectedId(2,juce::sendNotificationSync);
  stopTimer();setSize(1400,900);setVisible(true);
  struct Run{int stage=0,cancel=0;bool finished=false;double mark=0,anchor=0,base=0,latency=0;juce::Component::SafePointer<Form> tempo;std::vector<Note> expected;double started=juce::Time::getMillisecondCounterHiRes();std::vector<var> checks;juce::File folder;var notes,savedEstimate;};
  auto r=std::make_shared<Run>();r->folder=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getChildFile("test-results/midi-input-"+juce::Uuid().toString());r->folder.createDirectory();
  auto poll=std::make_shared<std::function<void()>>();
  auto finish=[this,r,poll,done](bool ok){if(r->finished)return;r->finished=true;resetMidiInput(true);r->folder.getParentDirectory().getChildFile("midi-input-latest.json").replaceWithText(juce::JSON::toString(object({{"passed",ok},{"stage",r->stage},{"language",localisation::code()},{"checks",array(r->checks)},{"directory",r->folder.getFullPathName()}})));juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
  auto check=[r](bool ok,const String& text){r->checks.push_back(object({{"name",text},{"passed",ok}}));if(!ok)throw std::runtime_error(text.toStdString());};
  auto key=[this](int pitch,int velocity){handleMidiInput(velocity?juce::MidiMessage::noteOn(1,pitch,juce::uint8(velocity)):juce::MidiMessage::noteOff(1,pitch));};
  startEmptyProject([r,finish](bool ok){if(ok)r->stage=1;else finish(false);});
  *poll=[this,r,poll,finish,check,key]{try{
    if(r->stage==53&&juce::Time::getMillisecondCounterHiRes()-r->mark>5000)throw std::runtime_error(("Preview restore timeout: preview="+String(int(tempoPreview))+" modal="+String(juce::Component::getNumCurrentlyModalComponents())+" "+juce::JSON::toString(bridge.call("player_snapshot",object({})))).toStdString());
    if(r->finished)return;if(juce::Time::getMillisecondCounterHiRes()-r->started>60000)throw std::runtime_error("MIDI input workflow timeout: "+status.getText().toStdString());
    auto keys=[this]{auto v=bridge.call("player_snapshot",object({}))["value"];std::set<int> active;for(const auto& n:items(v["active_keys"]))active.insert(int(n));return active;};
    if(r->stage==1&&!locked&&notesReady){
      check(preCountBars==1,"A new project defaults to one measure of pre-count");preCountBars=0;
      check(midiInput&&midiInput->identifier().isEmpty()&&!midiStep.getToggleState(),"Fresh sessions default to no external input and disarmed step entry");key(60,45);key(64,85);key(67,110);r->stage=2;
    }else if(r->stage==2){auto active=keys();if(active.count(60)&&active.count(64)&&active.count(67)){check(roll.notes.empty()&&!dirty,"Polyphonic MIDI monitoring lights all chord keys without creating notes or dirtying the project");key(60,0);r->stage=3;}
    }else if(r->stage==3){auto active=keys();if(!active.count(60)&&active.count(64)&&active.count(67)){check(true,"Releasing one external key leaves the other chord tones held");key(64,0);key(67,0);auto v=bridge.call("set_project_tempo",object({{"audioId",audio["id"]},{"bpm",65}}));check((bool)v["ok"],"Step input can use an explicitly declared 65 BPM");refreshTempo();r->stage=4;}
    }else if(r->stage==4&&std::abs(roll.rhythmicGrid.tempos.front().quarter-60./65)<1e-8){
      midiStep.setToggleState(true,juce::dontSendNotification);roll.keyboardSeekTarget=.347;key(60,41);key(64,83);key(67,111);check(roll.notes.empty()&&midiChord&&midiChord->notes.size()==3,"Held notes remain an audition and preview until the explicit advance gesture");
      check(keyPressed(juce::KeyPress(juce::KeyPress::rightKey),&roll),"Step input owns the unmodified right arrow in the roll");key(60,0);key(64,0);key(67,0);
      key(72,64);advanceMidiStep();key(72,0);key(76,100);advanceMidiStep();key(76,0);r->stage=5;
    }else if(r->stage==5&&!locked&&notesReady&&!midiCommitting&&midiChords.empty()&&!seekInFlight&&roll.notes.size()==5){
      const double q=60./65;bool geometry=true,velocity=true;for(const auto& n:roll.notes){double at=n.pitch==72?.347+q:n.pitch==76?.347+2*q:.347;geometry&=std::abs(n.start-at)<2e-6&&std::abs(n.end-n.start-q)<2e-6;int vel=n.pitch==60?41:n.pitch==64?83:n.pitch==67?111:n.pitch==72?64:100;velocity&=n.velocity==vel&&n.part==256;}
      check(geometry&&velocity,"Chord entry and two rapidly played steps preserve hardware velocities, active voice and off-grid start times at 65 BPM");check(std::abs(number(playback,"position")-(.347+3*q))<2e-6,"The playhead advances once per chord, never once per chord tone");r->stage=6;command(object({{"kind","undo"}}));
    }else if(r->stage==6&&!locked&&notesReady){check(roll.notes.size()==4,"Undo removes one complete MIDI input step");r->stage=7;command(object({{"kind","redo"}}));
    }else if(r->stage==7&&!locked&&notesReady){check(roll.notes.size()==5,"Redo restores the MIDI input step");midiLength.setSelectedId(4,juce::sendNotificationSync);roll.keyboardSeekTarget=10.125;key(50,95);advanceMidiStep();key(50,0);r->stage=8;
    }else if(r->stage==8&&!locked&&notesReady&&!midiCommitting&&roll.notes.size()==6&&!seekInFlight){
      auto i=std::find_if(roll.notes.begin(),roll.notes.end(),[](const Note& n){return n.pitch==50;});check(i!=roll.notes.end()&&std::abs(i->start-10.125)<1e-6&&std::abs((i->end-i->start)-30./65)<2e-6,"Choosing an eighth note sets its duration from BPM and extends the score beyond the original empty timeline");r->notes=result["editing"]["notes"].clone();r->stage=-1;saveProjectTo(r->folder.getChildFile("step-input.pht").getFullPathName(),[r,finish](bool ok){if(ok)r->stage=9;else finish(false);});
    }else if(r->stage==9&&!locked){r->stage=10;openProject(projectPath);
    }else if(r->stage==10&&!locked&&notesReady){check(juce::JSON::toString(r->notes)==juce::JSON::toString(result["editing"]["notes"]),"Saving and reopening preserves every MIDI-input note, velocity, duration and timing");check(!midiStep.getToggleState(),"Opening a project never arms MIDI step entry automatically");r->stage=11;command(object({{"kind","toggle_part_lock"},{"part",256}}));
    }else if(r->stage==11&&!locked&&notesReady){
      check(roll.lockedParts.count(256)>0,"The active voice is locked for the MIDI-input protection test");midiStep.setToggleState(true,juce::dontSendNotification);key(83,100);key(83,0);check(!midiChord&&roll.notes.size()==6,"Locked voices allow monitoring but cannot receive MIDI input notes");r->stage=12;command(object({{"kind","toggle_part_lock"},{"part",256}}));
    }else if(r->stage==12&&!locked&&notesReady){
      midiStep.setToggleState(true,juce::dontSendNotification);key(79,90);check(bool(midiChord),"A new step waits for the held key to be released");midiInput->onReset();key(79,0);check(!midiChord&&!midiStep.getToggleState()&&roll.notes.size()==6,"Device reset or unplug cancels incomplete entry, disarms writing and preserves existing notes");r->stage=13;
    }else if(r->stage==13&&!keys().count(79)){
      check(true,"Disconnect cleanup also releases the monitored note");midiStep.setToggleState(true,juce::dontSendNotification);key(80,90);selectPart(-1);key(80,0);check(!midiChord&&roll.notes.size()==6,"Changing editing voice cancels a pending chord instead of inserting it into another layer");selectPart(256);r->stage=14;
    }else if(r->stage==14&&!locked&&notesReady){
      settings();r->stage=15;
    }else if(r->stage==15&&settingsForm){auto* f=settingsForm.getComponent();check(f->comboFields.count("midi_input")&&f->state().hasProperty("midi_input"),"Settings expose the USB/MIDI input selector, including the no-device choice");{juce::FileOutputStream out(r->folder.getChildFile("midi-settings.png"));juce::PNGImageFormat().writeImageToStream(f->createComponentSnapshot(f->getLocalBounds()),out);}r->stage=16;f->onApply(f->state());
    }else if(r->stage==16&&!juce::Component::getCurrentlyModalComponent()&&!locked){
      auto preference=savePreferencesFile.getSiblingFile("midi-input.json");midiInput->choose("__unavailable_test_keyboard__");{MidiInputController restored(preference);check(restored.identifier()=="__unavailable_test_keyboard__"&&!restored.connected(),"Input device choice is retained independently of projects, including when the keyboard is disconnected");}midiInput->choose("");
      check(juce::JSON::toString(r->notes)==juce::JSON::toString(result["editing"]["notes"]),"Changing MIDI settings never modifies the score");r->stage=17;exportMidiTo(r->folder.getChildFile("step-input.mid").getFullPathName(),var());
    }else if(r->stage==17&&!locked){
      auto file=r->folder.getChildFile("step-input.mid");check(file.existsAsFile(),"Entered notes can be exported as MIDI");juce::MemoryBlock bytes;file.loadFileAsData(bytes);phthongos::midi::Source source;source.name="step-input.mid";auto* data=static_cast<const std::uint8_t*>(bytes.getData());source.bytes.assign(data,data+bytes.getSize());auto parsed=source.parse({});bool exact=parsed.notes.size()==6;for(const auto& original:items(r->notes)){bool found=false;for(const auto& note:parsed.notes)found|=note.pitch==int(original["pitch_midi"])&&note.velocity==int(original["velocity_midi"])&&std::abs(note.start-double(original["start_time_s"]))<.003&&std::abs(note.end-double(original["end_time_s"]))<.003;exact&=found;}check(exact,"MIDI export retains all step-input pitches, velocities and timings within MIDI tick precision");
      midiStep.setToggleState(true,juce::dontSendNotification);refreshMidiInput();resized();
      {juce::FileOutputStream out(r->folder.getChildFile("midi-step-editor.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),out);}r->stage=18;
    }else if(r->stage==18&&!locked&&notesReady){
      midiStep.setToggleState(true,juce::dontSendNotification);midiLength.setSelectedId(5,juce::sendNotificationSync);roll.keyboardSeekTarget=12.123;
      for(int pitch=80;pitch<88;++pitch){key(pitch,50+pitch-80);advanceMidiStep();key(pitch,0);}
      check(roll.midiPreview.size()+roll.notes.size()==14,"Rapid explicitly advanced steps immediately appear, including queued editor transactions");
      for(int pitch=80;pitch<88;++pitch)key(pitch,0);r->mark=juce::Time::getMillisecondCounterHiRes();r->stage=19;
    }else if(r->stage==19&&!locked&&!midiCommitting&&notesReady&&midiChords.empty()){
      bool exact=roll.notes.size()==14;for(int pitch=80;pitch<88;++pitch){auto i=std::find_if(roll.notes.begin(),roll.notes.end(),[pitch](const Note& n){return n.pitch==pitch;});exact&=i!=roll.notes.end()&&std::abs(i->start-(12.123+(pitch-80)*15./65))<3e-6;}
      check(exact&&roll.midiPreview.empty(),"Eight explicit advances create eight consecutive sixteenth-note steps, independent of the displayed grid");
      r->latency=juce::Time::getMillisecondCounterHiRes()-r->mark;r->checks.push_back(object({{"name","Eight rapid steps completion time"},{"milliseconds",r->latency},{"passed",true}}));
      r->stage=20;command(object({{"kind","undo"}}));
    }else if(r->stage==20&&!locked&&notesReady){
      check(roll.notes.size()==13,"Melody Undo removes exactly one step");r->stage=21;command(object({{"kind","redo"}}));
    }else if(r->stage==21&&!locked&&notesReady){
      check(roll.notes.size()==14,"Melody Redo restores the last step without duplicates");midiStep.setToggleState(false,juce::dontSendNotification);roll.keyboardSeekTarget=19.95;
      {juce::TextEditor field;check(!keyPressed(juce::KeyPress('R'),&field)&&!keyPressed(juce::KeyPress(juce::KeyPress::spaceKey),&field)&&!midiRecording&&!midiRecordPending,"R and Space remain text input without arming MIDI recording");}
      midiRecord.setEnabled(true);pressedKeys.clear();check(keyPressed(juce::KeyPress('R'),&roll)&&midiRecordPending,"R starts a take when recording is available");r->stage=22;r->mark=juce::Time::getMillisecondCounterHiRes();
    }else if(r->stage==22&&midiRecording&&juce::Time::getMillisecondCounterHiRes()-r->mark>320){
      check(keyPressed(juce::KeyPress('R'),&roll)&&midiRecording&&!midiRecordPending,"Holding R does not retrigger or stop recording through key repeats");
      auto snapshot=bridge.call("player_snapshot",object({}))["value"];check((bool)snapshot["playing"]&&(bool)snapshot["recording"]&&number(snapshot,"position")>20.1,"Recording clock advances past the original score duration without stopping or allocating a twenty-minute audio file");
      updateMidiClock(snapshot);r->anchor=juce::Time::getMillisecondCounterHiRes()/1000.-.2;r->base=midiEventTime(r->anchor);
      auto event=[this,r](int pitch,int velocity,double delta){auto m=velocity?juce::MidiMessage::noteOn(1,pitch,juce::uint8(velocity)):juce::MidiMessage::noteOff(1,pitch);handleMidiInput(m.withTimeStamp(r->anchor+delta));};
      event(60,41,.013);check(midiHeld.size()==1&&!roll.midiPreview.empty(),"Real-time note appears on Note On before its release");auto before=roll.midiPreview.back().end;midiClockPosition+=.025;updateMidiPreview();check(roll.midiPreview.back().end>before+.02,"A held real-time note grows visually as the capture clock advances");midiClockPosition-=.025;
      event(64,83,.031);event(60,0,.167);event(67,111,.049);event(64,0,.098);event(67,0,.181);
      event(72,70,.070);event(72,92,.109);event(72,0,.193);
      check(midiRecorded.size()==5&&midiHeld.empty(),"Overlapping notes, chords and repeated same-pitch attacks are retained independently");
      r->expected=midiRecorded;bool exact=true;for(const auto& n:r->expected)if(n.pitch==60)exact&=std::abs(n.start-(r->base+.013))<1e-8&&std::abs(n.end-n.start-.154)<1e-8&&n.velocity==41;
      check(exact,"Original callback timestamps retain off-grid attacks and real key durations even when events reach the UI late");
      event(76,66,.195);check(midiHeld.size()==1,"A held note remains pending until release or recording stop");
      {juce::FileOutputStream out(r->folder.getChildFile("midi-recording.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),out);}
      pressedKeys.clear();check(keyPressed(juce::KeyPress(juce::KeyPress::spaceKey),&roll)&&midiRecordPending,"Space stops and validates the recording instead of starting playback");r->stage=23;
    }else if(r->stage==23&&!locked&&!midiRecordPending&&notesReady){key(76,0);
      bool exact=roll.notes.size()==20;for(const auto& original:r->expected){bool found=false;for(const auto& n:roll.notes)found|=n.pitch==original.pitch&&n.velocity==original.velocity&&n.part==256&&std::abs(n.start-original.start)<3e-6&&std::abs(n.end-original.end)<3e-6;exact&=found;}
      check(exact&&midiHeld.empty()&&roll.midiPreview.empty(),"Stopping commits all six real-time notes once, closes the held note, and preserves timings, overlaps and velocities without quantization");r->notes=result["editing"]["notes"].clone();r->stage=24;command(object({{"kind","undo"}}));
    }else if(r->stage==24&&!locked&&notesReady){check(roll.notes.size()==14,"One Undo removes the entire real-time take");r->stage=25;command(object({{"kind","redo"}}));
    }else if(r->stage==25&&!locked&&notesReady){check(juce::JSON::toString(r->notes)==juce::JSON::toString(result["editing"]["notes"]),"Redo restores the complete original take exactly");r->stage=-1;saveProjectTo(r->folder.getChildFile("recorded-input.pht").getFullPathName(),[r,finish](bool ok){if(ok)r->stage=26;else finish(false);});
    }else if(r->stage==26&&!locked){r->stage=27;openProject(projectPath);
    }else if(r->stage==27&&!locked&&notesReady){check(juce::JSON::toString(r->notes)==juce::JSON::toString(result["editing"]["notes"]),"PHT save/reopen preserves every original real-time event and disarms recording");check(!midiRecording&&!midiRecordPending,"Opening a project never starts a recording");r->stage=28;exportMidiTo(r->folder.getChildFile("recorded-input.mid").getFullPathName(),var());
    }else if(r->stage==28&&!locked){
      juce::MemoryBlock bytes;r->folder.getChildFile("recorded-input.mid").loadFileAsData(bytes);phthongos::midi::Source source;source.name="recorded-input.mid";auto* data=static_cast<const std::uint8_t*>(bytes.getData());source.bytes.assign(data,data+bytes.getSize());auto parsed=source.parse({});bool exact=parsed.notes.size()==20;
      for(const auto& original:items(r->notes)){bool found=false;for(const auto& n:parsed.notes)found|=n.pitch==int(original["pitch_midi"])&&n.velocity==int(original["velocity_midi"])&&std::abs(n.start-double(original["start_time_s"]))<.003&&std::abs(n.end-double(original["end_time_s"]))<.003;exact&=found;}
      check(exact,"MIDI export retains all recorded events within MIDI tick precision");
      roll.keyboardSeekTarget=22;startMidiRecording();r->stage=29;
    }else if(r->stage==29&&midiRecording){key(93,87);pressedKeys.clear();check(keyPressed(juce::KeyPress('R'),&roll)&&midiRecordPending,"R also stops and validates an active recording");r->stage=30;
    }else if(r->stage==30&&!locked&&!midiRecordPending&&notesReady){key(93,0);check(roll.notes.size()==21&&!midiRecording&&midiHeld.empty(),"R stop closes held notes and retains the take");roll.keyboardSeekTarget=23;startMidiRecording();r->stage=31;
    }else if(r->stage==31&&midiRecording){key(94,88);midiInput->onReset();r->stage=32;
    }else if(r->stage==32&&!locked&&!midiRecordPending&&notesReady){key(94,0);check(roll.notes.size()==22&&!midiRecording&&midiHeld.empty(),"Unplug stops recording and preserves completed and held notes in a recoverable take");midiStepOptions();r->stage=33;
    }else if(r->stage==33&&midiStepForm){auto f=midiStepForm;check(f->toggleFields.count("enabled")&&f->comboFields.size()==1,"The secondary step-input panel exposes activation and duration without Melody or Chords selection");
      {juce::FileOutputStream out(r->folder.getChildFile("midi-step-options.png"));juce::PNGImageFormat().writeImageToStream(f->createComponentSnapshot(f->getLocalBounds()),out);}
      check(!keyPressed(juce::KeyPress('R'),&roll)&&!keyPressed(juce::KeyPress(juce::KeyPress::spaceKey),&roll)&&!midiRecordPending,"Modal options protect R and Space from recording actions");f->close();r->stage=34;
    }else if(r->stage==34&&!locked&&!juce::Component::getCurrentlyModalComponent()){
      roll.keyboardSeekTarget=30;startMidiRecording();r->stage=35;
    }else if(r->stage==35&&midiRecording){
      midiClockTime-=10;midiCaptureStartTime-=10;updateTakeDuration();
      check(calculation.getText().contains("0:10")&&!calculation.getText().contains("0:30"),"Recording duration counts from the beginning of a take starting at 30 seconds, independently of the playhead");
      const double origin=midiCaptureStartTime;
      for(int i=0;i<15;++i){auto on=juce::MidiMessage::noteOn(1,60+i%5,juce::uint8(50+i));handleMidiInput(on.withTimeStamp(origin+.013+i*.25));auto off=juce::MidiMessage::noteOff(1,60+i%5);handleMidiInput(off.withTimeStamp(origin+.013+i*.25+.16));}
      {juce::FileOutputStream out(r->folder.getChildFile("midi-take-duration.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),out);}
      stopMidiRecording();r->stage=36;
    }else if(r->stage==36&&!locked&&notesReady&&!midiRecordPending&&buttonMap["tempo"]->getButtonText().contains(String::fromUTF8("≈"))){
      check(hasMidiTempoEstimate()&&std::abs(number(midiTempoEstimate,"bpm")-120)<.5&&midiEstimatePending,"A short fifteen-note take under six seconds proposes a stable 120 BPM without applying it");
      auto info=bridge.call("project_tempo",object({{"audioId",audio["id"]}}))["value"];check(std::abs(number(info,"bpm")-65)<.001,"Project BPM stays 65 until explicit confirmation of the estimate");
      r->notes=result["editing"]["notes"].clone();r->tempo=tempoForm(info);r->tempo->show(tr(String::fromUTF8("Tempo / calculette rythmique")));r->stage=37;
    }else if(r->stage==37&&r->tempo){
      auto f=r->tempo;check(f->comboFields.count("take_tempo")&&f->comboFields["take_tempo"]->getNumItems()>=4&&double(f->state()["take_tempo"])==0,"BPM panel defaults to retaining the project BPM and offers estimate, half and double");
      f->comboFields["take_tempo"]->setSelectedId(3,juce::sendNotificationSync);check(std::abs(decimal(f->state()["bpm"])-120)<.01,"Choosing the estimate changes only the proposed BPM field before confirmation");
      {juce::FileOutputStream out(r->folder.getChildFile("midi-tempo-proposal.png"));juce::PNGImageFormat().writeImageToStream(f->createComponentSnapshot(f->getLocalBounds()),out);}
      f->comboFields["take_tempo"]->setSelectedId(f->comboFields["take_tempo"]->getNumItems(),juce::sendNotificationSync);
      check(std::abs(decimal(f->state()["bpm"])-double(f->state()["take_tempo"]))<.001,"An alternate scored candidate updates the proposed BPM field without applying the project tempo");
      f->comboFields["take_tempo"]->setSelectedId(1,juce::sendNotificationSync);check(std::abs(decimal(f->state()["bpm"])-65)<.01,"Keep project BPM restores the original value after browsing candidates");f->close();r->stage=38;
    }else if(r->stage==38&&!juce::Component::getCurrentlyModalComponent()){
      auto info=bridge.call("project_tempo",object({{"audioId",audio["id"]}}))["value"];check(std::abs(number(info,"bpm")-65)<.001&&midiEstimatePending,"Cancelling tempo proposals leaves project BPM and pending estimate unchanged");
      r->tempo=tempoForm(info);r->tempo->show(tr(String::fromUTF8("Tempo / calculette rythmique")));r->tempo->comboFields["take_tempo"]->setSelectedId(2,juce::sendNotificationSync);r->stage=39;r->tempo->onApply(r->tempo->state());
    }else if(r->stage==39&&!juce::Component::getCurrentlyModalComponent()&&!midiEstimatePending){
      auto info=bridge.call("project_tempo",object({{"audioId",audio["id"]}}))["value"];check(std::abs(number(info,"bpm")-60)<.001,"Applying the half-tempo candidate explicitly sets the retained project BPM to 60");
      check(juce::JSON::toString(r->notes)==juce::JSON::toString(result["editing"]["notes"]),"Confirming an estimated BPM preserves every recorded attack, end, velocity and overlap byte-for-byte");
      r->savedEstimate=midiTempoEstimate.clone();
      r->stage=-1;saveProjectTo(r->folder.getChildFile("estimated-take.pht").getFullPathName(),[r,finish](bool ok){if(ok)r->stage=40;else finish(false);});
    }else if(r->stage==40&&!locked){r->stage=41;openProject(projectPath);
    }else if(r->stage==41&&!locked&&notesReady){
      auto info=bridge.call("project_tempo",object({{"audioId",audio["id"]}}))["value"];check(hasMidiTempoEstimate()&&!midiEstimatePending&&std::abs(number(info,"bpm")-60)<.001&&std::abs(number(midiTempoEstimate,"bpm")-120)<.5,"PHT save/reopen retains the estimate and independently confirmed project tempo");
      check(juce::JSON::toString(r->notes)==juce::JSON::toString(result["editing"]["notes"]),"Saving and reopening after BPM confirmation leaves all captured events unchanged");
      check(juce::JSON::toString(midiTempoEstimate["candidates"])==juce::JSON::toString(r->savedEstimate["candidates"])&&juce::JSON::toString(midiTempoEstimate["histogram"])==juce::JSON::toString(r->savedEstimate["histogram"]),"PHT save/reopen preserves every candidate score and histogram bin exactly");
      check(!items(midiTempoEstimate["candidates"]).empty()&&!items(midiTempoEstimate["histogram"]).empty()&&midiTempoEstimate["engine"].toString()=="midi-bpm-cpp/e14a86c","PHT save/reopen retains scored candidates, histogram and C++ detector provenance");
      r->savedEstimate=midiTempoEstimate.clone();
      midiTempoEstimate=object({{"bpm",0},{"reason","few_attacks"},{"attacks",1},{"seconds",2.}});
      check(midiTempoUnavailable().contains("1")&&midiTempoUnavailable().contains("2"),"Insufficient-onset feedback shows the actual count and required minimum");
      auto sparseMessage=midiTempoUnavailable();midiTempoEstimate.getDynamicObject()->setProperty("reason","short_span");
      check(midiTempoUnavailable()!=sparseMessage&&midiTempoUnavailable().contains("2.0"),"An insufficient time span gets its own factual explanation");
      midiTempoEstimate.getDynamicObject()->setProperty("reason","uncertain");
      check(midiTempoUnavailable()!=sparseMessage,"An uncertain pulse is distinguished from insufficient data");
      midiTempoEstimate.getDynamicObject()->setProperty("reason","few_attacks");
      r->tempo=tempoForm(info);r->tempo->show(tr(String::fromUTF8("Tempo / calculette rythmique")));r->stage=42;
    }else if(r->stage==42&&r->tempo){
      {juce::FileOutputStream out(r->folder.getChildFile("midi-tempo-insufficient.png"));juce::PNGImageFormat().writeImageToStream(r->tempo->createComponentSnapshot(r->tempo->getLocalBounds()),out);}
      r->tempo->close();midiTempoEstimate=r->savedEstimate;midiEstimatePending=false;refreshTempo();r->stage=43;
    }else if(r->stage==43&&!juce::Component::getCurrentlyModalComponent()){
      auto v=bridge.call("set_project_tempo",object({{"audioId",audio["id"]},{"bpm",600}}));check(bool(v["ok"]),"Count-in tests can use a short 600 BPM reference");refreshTempo();r->notes=result["editing"]["notes"].clone();r->stage=44;
    }else if(r->stage==44&&std::abs(roll.rhythmicGrid.tempos.front().quarter-.1)<1e-8){
      preCountBars=2;clickEnabled=false;roll.keyboardSeekTarget=50;startMidiRecording();r->stage=45;
    }else if(r->stage==45&&midiRecording&&midiCounting){
      check(midiRecorded.empty()&&midiHeld.empty()&&number(playback,"count_remaining")>0&&std::abs(number(playback,"position")-50)<.001,"During count-in the transport stays at the requested start and creates no note events");
      key(60,70);key(60,0);key(64,83);check(midiRecorded.empty()&&midiHeld.empty()&&countHeld.size()==1,"Count-in notes are auditioned but only still-held keys remain pending");
      updateClickVisuals();check(countOverlay.isVisible()&&countOverlay.getText().getIntValue()>=1&&countOverlay.getText().getIntValue()<=countMeter&&beatIndicator.active>=0,"Count-in immediately shows a per-bar numeral and the active audio-clock beat");
      {juce::FileOutputStream out(r->folder.getChildFile("midi-count-in.png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),out);}
      pressedKeys.clear();if(r->cancel==0)check(keyPressed(juce::KeyPress('R'),&roll),"R cancels the count-in");else if(r->cancel==1)check(keyPressed(juce::KeyPress(juce::KeyPress::spaceKey),&roll),"Space cancels the count-in");else control("stop");r->stage=46;
    }else if(r->stage==46&&!locked&&!midiRecordPending){
      check(!midiRecording&&!midiCounting&&midiHeld.empty()&&juce::JSON::toString(r->notes)==juce::JSON::toString(result["editing"]["notes"]),"Cancelling the pre-count inserts no take and leaves every existing event unchanged");key(64,0);
      if(++r->cancel<3){preCountBars=2;roll.keyboardSeekTarget=50;startMidiRecording();r->stage=45;}
      else{preCountBars=1;roll.keyboardSeekTarget=50;startMidiRecording();r->stage=47;}
    }else if(r->stage==47&&midiRecording&&midiCounting){
      key(71,80);key(71,0);key(72,87);r->stage=48;
    }else if(r->stage==48&&midiRecording){
      if(juce::Time::getMillisecondCounterHiRes()/1000.<midiCaptureStartTime){juce::Timer::callAfterDelay(30,[poll]{if(*poll)(*poll)();});return;}
      if(midiInput)midiInput->drain();finishMidiCountIn();
      check(!midiCounting&&midiHeld.size()==1&&midiHeld.begin()->second.pitch==72&&midiHeld.begin()->second.velocity==87&&std::abs(midiHeld.begin()->second.start-50)<.00001,"A key held across the sample-clock count-in boundary starts at the exact insertion point with original velocity");
      auto off=juce::MidiMessage::noteOff(1,72);handleMidiInput(off.withTimeStamp(midiCaptureStartTime+.08));stopMidiRecording();r->stage=49;
    }else if(r->stage==49&&!locked&&!midiRecordPending&&notesReady){
      bool found=false;for(auto n:roll.notes)found|=n.pitch==72&&n.velocity==87&&std::abs(n.start-50)<.0001&&std::abs(n.end-n.start-.08)<.0001;
      check(found&&items(result["editing"]["notes"]).size()==items(r->notes).size()+1,"Count-in commits only the held note, retains its measured release, and discards notes released before recording");
      clickEnabled=true;clickDb=-18;preCountBars=2;syncClick();r->notes=result["editing"]["notes"].clone();r->stage=-1;saveProjectTo(r->folder.getChildFile("metronome.pht").getFullPathName(),[r,finish](bool ok){if(ok)r->stage=50;else finish(false);});
    }else if(r->stage==50&&!locked){r->stage=51;openProject(projectPath);
    }else if(r->stage==51&&!locked&&notesReady){
      check(clickEnabled&&clickDb==-18&&preCountBars==2,"PHT reopening restores click enablement, independent level and count-in length");
      auto info=bridge.call("project_tempo",object({{"audioId",audio["id"]}}))["value"];r->tempo=tempoForm(info);r->tempo->show(tr(String::fromUTF8("Tempo / calculette rythmique")));previewTempo(79.4);r->stage=52;
    }else if(r->stage==52&&tempoPreview){
      auto v=bridge.call("player_snapshot",object({}))["value"];if(!bool(v["playing"])||std::abs(number(v,"metronome_quarter")-60/79.4)>.0001){juce::Timer::callAfterDelay(30,[poll]{if(*poll)(*poll)();});return;}
      check(bool(v["metronome_enabled"])&&juce::JSON::toString(r->notes)==juce::JSON::toString(result["editing"]["notes"]),"Candidate audition plays a temporary 79.4 BPM click without altering recorded notes");
      auto info=bridge.call("project_tempo",object({{"audioId",audio["id"]}}))["value"];check(std::abs(number(info,"bpm")-600)<.001,"Listening to an estimated BPM does not apply it to the project");auto saved=double(getProperties()["tempoPreviewPosition"]);previewTempo(83.2);check(double(getProperties()["tempoPreviewPosition"])==saved,"Comparing a second tempo candidate retains the original playhead for restoration");r->mark=juce::Time::getMillisecondCounterHiRes();r->tempo->close();r->stage=53;
    }else if(r->stage==53&&!tempoPreview&&!juce::Component::getCurrentlyModalComponent()){
      auto v=bridge.call("player_snapshot",object({}))["value"];if(bool(v["playing"])||std::abs(number(v,"metronome_quarter")-.1)>.0001){juce::Timer::callAfterDelay(30,[poll]{if(*poll)(*poll)();});return;}
      check(bool(v["metronome_enabled"]),"Closing the tempo panel stops preview and restores the project metronome configuration");clickOptions();for(auto form:Form::activeForms)if(form->compactFields.count("click"))r->tempo=form;r->stage=54;
    }else if(r->stage==54&&r->tempo){auto f=r->tempo;
      auto panel=dynamic_cast<ClickOptionsPanel*>(f->compactFields["click"]);
      check(panel&&panel->counts[2].getButtonText().isNotEmpty()&&f->toggleFields.count("enabled"),"Metronome options expose volume slider and three count-in buttons");
      {juce::FileOutputStream out(r->folder.getChildFile("metronome-options.png"));juce::PNGImageFormat().writeImageToStream(f->createComponentSnapshot(f->getLocalBounds()),out);}
      check(std::abs(panel->volume.getValue()+18)<1e-9,"Opening percentage controls preserves the exact saved dB level");
      panel->volume.setValue(-9,juce::sendNotificationSync);panel->counts[1].setToggleState(true,juce::dontSendNotification);panel->counts[1].onClick();
      f->onApply(f->state());r->stage=55;
    }else if(r->stage==55&&!juce::Component::getCurrentlyModalComponent()){
      check(clickDb==-9&&preCountBars==1,"Valid metronome settings apply immediately");finish(true);return;
    }
    juce::Timer::callAfterDelay(30,[poll]{if(*poll)(*poll)();});
  }catch(const std::exception& e){r->checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));finish(false);}};(*poll)();
}
