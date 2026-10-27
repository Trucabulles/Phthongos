#include "App.h"
#include "../core/project/BasicPitch.h"
namespace {
class PitchControls : public juce::Component {
 std::array<juce::Label,4> labels;
 std::array<VisibleCursorSlider,4> sliders;
 juce::Label lowLabel,highLabel;
 juce::ComboBox low,high;
 juce::ToggleButton infer,melodia;
 juce::TextButton fold,reset;
 std::function<void(var)> changed;
 bool expanded=true;
 var options()const{return object({{"noteSensitivity",sliders[0].getValue()/100.},{"splitSensitivity",sliders[1].getValue()/100.},{"minNoteMs",sliders[2].getValue()},{"minPitch",low.getSelectedId()-1},{"maxPitch",high.getSelectedId()-1},{"inferOnsets",infer.getToggleState()},{"melodiaTrick",melodia.getToggleState()},{"energyTolerance",int(sliders[3].getValue())}});}
 void notify(){if(changed)changed(options());}
public:
 PitchControls(const var& initial,std::function<void(var)> onChange,std::function<void()> onFold):changed(std::move(onChange)){
  setComponentID("basicPitchBand");
  for(auto* c:std::initializer_list<juce::Component*>{&fold,&reset,&lowLabel,&highLabel,&low,&high,&infer,&melodia})addAndMakeVisible(c);
  fold.setComponentID("basicPitchFold");fold.onClick=std::move(onFold);
  reset.setComponentID("basicPitchReset");reset.onClick=[this]{setOptions(phthongos::basicPitch::Options().json());notify();};
  for(int p=21;p<=108;++p){low.addItem(pitchName(p),p+1);high.addItem(pitchName(p),p+1);}
  low.setComponentID("basicPitchMin");high.setComponentID("basicPitchMax");
  low.onChange=[this]{if(low.getSelectedId()>high.getSelectedId())high.setSelectedId(low.getSelectedId(),juce::dontSendNotification);notify();};
  high.onChange=[this]{if(high.getSelectedId()<low.getSelectedId())low.setSelectedId(high.getSelectedId(),juce::dontSendNotification);notify();};
  infer.setComponentID("basicPitchInfer");melodia.setComponentID("basicPitchMelodia");infer.onClick=[this]{notify();};melodia.onClick=[this]{notify();};
  for(int i=0;i<4;++i){addAndMakeVisible(labels[i]);labels[i].setFont(juce::FontOptions(12.f));auto& slider=sliders[i];addAndMakeVisible(slider);
   slider.setComponentID(i==0?"basicPitchSensitivity":i==1?"basicPitchSplit":i==2?"basicPitchDuration":"basicPitchEnergy");slider.setSliderStyle(juce::Slider::LinearHorizontal);slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,52,24);
   slider.setRange(i==3?1:i==2?35:5,i==3?25:i==2?580:95,1);slider.setDoubleClickReturnValue(true,i==0?70:i==1?50:i==2?125:11);slider.onValueChange=[this,i]{if(i==3)updateEnergyHint();notify();};
  }
  setOptions(initial);update(true,true);
 }
 void updateEnergyHint(){sliders[3].setTooltip(tr(String::fromUTF8("Tolérance aux coupures"))+" : "+String(int(sliders[3].getValue()))+tr(String::fromUTF8(" trames · environ "))+String(int(std::round(sliders[3].getValue()*256000./22050.)))+" ms");}
 void setOptions(const var& value){for(int i=0;i<4;++i)sliders[i].setValue(number(value,i==0?"noteSensitivity":i==1?"splitSensitivity":i==2?"minNoteMs":"energyTolerance")*(i>=2?1:100),juce::dontSendNotification);low.setSelectedId(int(value["minPitch"])+1,juce::dontSendNotification);high.setSelectedId(int(value["maxPitch"])+1,juce::dontSendNotification);infer.setToggleState(bool(value["inferOnsets"]),juce::dontSendNotification);melodia.setToggleState(bool(value["melodiaTrick"]),juce::dontSendNotification);updateEnergyHint();}
 void update(bool enabled,bool open){expanded=open;
  fold.setButtonText(String::fromUTF8(open?"▾ ":"▸ ")+tr(String::fromUTF8("Réglages Basic Pitch")));reset.setButtonText(tr(String::fromUTF8("Réinitialiser")));
  const std::array<String,4> names{tr(String::fromUTF8("Sensibilité (%)")),tr(String::fromUTF8("Séparation (%)")),tr(String::fromUTF8("Durée min. (ms)")),tr(String::fromUTF8("Tolérance aux coupures"))};
  for(int i=0;i<4;++i){labels[i].setText(names[i],juce::dontSendNotification);sliders[i].setName(names[i]);sliders[i].setTooltip(names[i]);}
  updateEnergyHint();
  lowLabel.setText(tr(String::fromUTF8("Note min.")),juce::dontSendNotification);highLabel.setText(tr(String::fromUTF8("Note max.")),juce::dontSendNotification);
  infer.setButtonText(tr(String::fromUTF8("Attaques supplémentaires")));melodia.setButtonText(tr(String::fromUTF8("Récupération complémentaire")));
  low.setName(lowLabel.getText());high.setName(highLabel.getText());
  for(auto* c:getChildren())if(c!=&fold){c->setVisible(open);c->setEnabled(enabled);}fold.setEnabled(true);resized();
 }
 void paint(juce::Graphics& g)override{g.setColour(juce::Colour(0xffe9e0d1).withAlpha(.45f));g.fillRect(getLocalBounds());g.setColour(juce::Colour(0xffcbbda5));g.drawHorizontalLine(getHeight()-1,0,float(getWidth()));}
 void resized()override{int w=getWidth();fold.setBounds(5,1,225,25);reset.setBounds(w-123,1,115,25);if(!expanded)return;
  int cw=(w-24)/4;for(int i=0;i<4;++i){int x=8+i*cw;labels[i].setBounds(x,27,cw-10,17);sliders[i].setBounds(x,44,cw-10,24);}
  int first=int(w*.19),second=int(w*.19),third=int(w*.29),x=8;
  lowLabel.setBounds(x,69,78,28);low.setBounds(x+78,69,first-90,28);x+=first;
  highLabel.setBounds(x,69,78,28);high.setBounds(x+78,69,second-90,28);x+=second;
  infer.setBounds(x,69,third,28);x+=third;melodia.setBounds(x,69,w-x-8,28);
 }
};
}
void App::updateTranscription(var value){
 engine=value;syncPitchControls();model.clear(juce::dontSendNotification);
 for(auto entry:items(value["models"]))model.addItem(entry["label"].toString(),int(entry["id"]));
 if(model.getNumItems()==0)model.addItem("Basic Pitch",1);
 auto settings=value["settings"];int selected=1;
 if(settings["engine"].toString()=="muscriptor")selected=settings["model"].toString()=="small"?2:settings["model"].toString()=="medium"?3:4;
 model.setSelectedId(selected,juce::dontSendNotification);refresh();resized();
}
void App::refreshTranscription(){invoke("muscriptor_status",object({}),[this](var value){updateTranscription(value);});}
void App::restoreTranscription(const var& savedUi,const var& source){
 auto saved=savedUi["transcription"];
 if(!saved.isObject()&&source["engine"].toString()=="basic_pitch")saved=object({{"engine","basic_pitch"},{"model","small"},{"basic_pitch",source["basic_pitch"]["settings"]}});
 if(!saved.isObject()||engine["settings"].isVoid())return;
 auto configured=engine["settings"].clone();auto* o=configured.getDynamicObject();
 if(saved["basic_pitch"].isObject())o->setProperty("basic_pitch",saved["basic_pitch"].clone());
 bool available=saved["engine"].toString()=="basic_pitch";
 for(auto entry:items(engine["models"]))if(saved["engine"].toString()=="muscriptor"&&entry["model"]==saved["model"])available=true;
 o->setProperty("engine",available?saved["engine"]:var("basic_pitch"));if(saved["model"].isString())o->setProperty("model",saved["model"]);
 invoke("muscriptor_settings",object({{"settings",configured}}),[this](var){refreshTranscription();});
}

void App::initialisePitchControls(){
 auto preference=juce::JSON::parse(savePreferencesFile.getSiblingFile("workspace-ui.json").loadFileAsString());
 pitchControlsExpanded=!preference["basicPitchExpanded"].isBool()||bool(preference["basicPitchExpanded"]);
 pitchControls=std::make_unique<PitchControls>(phthongos::basicPitch::Options().json(),[this](var options){pitchControlsChanged(options);},[this]{
  pitchControlsExpanded=!pitchControlsExpanded;
  auto file=savePreferencesFile.getSiblingFile("workspace-ui.json");file.getParentDirectory().createDirectory();juce::TemporaryFile temp(file);
  if(!temp.getFile().replaceWithText(juce::JSON::toString(object({{"basicPitchExpanded",pitchControlsExpanded}})))||!temp.overwriteTargetFileWithTemporary())status.setText(tr(String::fromUTF8("Enregistrement des préférences impossible.")),juce::dontSendNotification);
  resized();repaint();
 });
 addAndMakeVisible(*pitchControls);
}
int App::pitchBandHeight()const{
 bool editor=result["engine"].toString()=="midi"&&bool(audio["midi_only"]);
 return pitchControls&&!editor&&model.getSelectedId()==1?(pitchControlsExpanded?108:28):0;
}
void App::refreshPitchBand(){if(auto* c=dynamic_cast<PitchControls*>(pitchControls.get())){
 c->setVisible(pitchBandHeight()>0);c->update((!locked||pitchControlsPending)&&!(partId<0&&result["voice_transcriptions"].getDynamicObject()!=nullptr&&result["voice_transcriptions"].getDynamicObject()->getProperties().size()>1),pitchControlsExpanded);
}}
int App::pitchVoiceTarget()const{
 auto available=result["voice_transcriptions"];if(partId>=0)return available.hasProperty(String(partId))?partId:-1;
 if(auto* o=available.getDynamicObject())if(o->getProperties().size()==1)return o->getProperties().getName(0).toString().getIntValue();return -1;
}
void App::syncPitchControls(){
 auto options=engine["settings"]["basic_pitch"];int target=pitchVoiceTarget();if(target>=0)options=result["voice_transcriptions"][juce::Identifier(String(target))]["settings"];
 if(auto* controls=dynamic_cast<PitchControls*>(pitchControls.get()))controls->setOptions(phthongos::basicPitch::Options::read(options).json());
}
void App::pitchControlsChanged(var options){
 const auto generation=++pitchControlsGeneration;auto expectedAudio=audio["id"];int expectedTarget=pitchVoiceTarget();
 juce::Timer::callAfterDelay(220,[s=safe(),generation,expectedAudio,expectedTarget,options]{
  if(!s||s->closing||s->confirmation||generation!=s->pitchControlsGeneration||expectedAudio!=s->audio["id"]||s->model.getSelectedId()!=1||s->pitchVoiceTarget()!=expectedTarget)return;
  if(s->pitchControlsPending){s->pitchControlsChanged(options);return;}
  if(s->locked)return;
  auto apply=[s,options]{if(s)s->applyPitchControls(options);};
  if(expectedTarget>=0&&(bool)s->result["voice_transcriptions"][juce::Identifier(String(expectedTarget))]["has_changes"]){
   s->confirm(tr(String::fromUTF8("Relancer la transcription ?")),tr(String::fromUTF8("Les réglages remplaceront les corrections de cette voix. Les autres voix restent conservées.")),[s,apply](bool ok){if(ok)apply();else if(s)s->syncPitchControls();});
  }else apply();
 });
}
void App::applyPitchControls(var options){
 if(locked||closing||pitchControlsPending||model.getSelectedId()!=1)return;
 pitchControlsPending=true;setBusy(true);
 auto configured=engine["settings"].clone();configured.getDynamicObject()->setProperty("basic_pitch",options);
 const auto expectedAudio=audio["id"],expectedAnalysis=result["analysis_id"];int target=pitchVoiceTarget();auto request=args().clone();request.getDynamicObject()->setProperty("part",target);request.getDynamicObject()->setProperty("options",options);
 invoke("muscriptor_settings",object({{"settings",configured}}),[s=safe(),configured,expectedAudio,expectedAnalysis,target,request](var){
  if(!s)return;s->engine.getDynamicObject()->setProperty("settings",configured);
  auto finish=[s]{if(s){s->pitchControlsPending=false;s->setBusy(false);s->resized();}};
  if(s->audio["id"]!=expectedAudio||s->result["analysis_id"]!=expectedAnalysis||target<0||s->model.getSelectedId()!=1){finish();return;}
  s->setBusy(true); // Cache-only operation: never invokes the neural model.
  s->invoke("redecode_voice",request,[s,finish](var value){
   if(!s)return;s->roll.clearSelection();s->invalidateGuided();s->verification=var();s->clean=var();s->dynamics=var();s->hasDynDraft=false;
   s->updateResult(value);s->setBusy(false,tr(String::fromUTF8("Réglages appliqués · calcul neuronal réutilisé.")));finish();
  },true,[s,finish](String){if(s)s->setBusy(false);finish();});
 },false,[s=safe()](String){if(s){s->pitchControlsPending=false;s->syncPitchControls();s->setBusy(false);}});
}

String App::voiceSourcePath(int key)const{return result["editing"]["audio_sources"][juce::Identifier(String(key))].toString();}
void App::refreshVoiceSource(){
 int key=partId;if(key<0&&parts.size()==1)key=parts.front().first;auto path=voiceSourcePath(key);bool show=!stackedView&&path.isNotEmpty();
 roll.voiceSourceHeader=show?24:0;voiceSourceLabel.setVisible(show);voiceSourceReload.setVisible(show);voiceSourceReload.setEnabled(!locked);
 voiceSourceLabel.setText(tr(String::fromUTF8("Source : "))+juce::File(path).getFileName(),juce::dontSendNotification);voiceSourceLabel.setTooltip(path);
}
void App::reloadVoiceAudio(int key){
 if(locked||key<0)return;auto path=voiceSourcePath(key);if(path.isEmpty())return;
 if(juce::File(path).existsAsFile()){associateAudio(path,key);return;}
 choose(tr(String::fromUTF8("Retrouver la source audio de la voix")),false,"*.wav;*.mp3;*.flac;*.m4a",[s=safe(),key](String replacement){if(s)s->associateAudio(replacement,key);});
}
