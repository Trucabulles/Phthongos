#include "App.h"
void App::syncClick(double candidate){
 if(audio.isVoid()||preparedAudioId!=audio["id"])return;
 auto map=roll.rhythmicGrid;
 if(candidate>0){auto beat=map.beatAt(previewPosition);auto meter=map.meters.front();for(auto m:map.meters)if(m.beat<=beat)meter=m;map.origin=previewPosition;map.tempos={{0,0,60/candidate}};map.meters={{0,meter.numerator,meter.denominator}};}
 std::vector<var> tempos,meters;
 for(auto t:map.tempos)tempos.push_back(object({{"beat",t.beat},{"seconds",t.seconds},{"quarter",t.quarter}}));
 for(auto m:map.meters)meters.push_back(object({{"beat",m.beat},{"numerator",m.numerator},{"denominator",m.denominator}}));
 invoke("player_metronome",object({{"audioId",audio["id"]},{"enabled",candidate>0||clickEnabled},{"db",clickDb},{"origin",map.origin},{"tempos",array(tempos)},{"meters",array(meters)}}));
}
void App::clickOptions(){
 if(locked)return;
 auto f=juce::Component::SafePointer<Form>(new Form);
 f->toggle(tr(String::fromUTF8("Clic pendant la lecture et l’enregistrement")),"enabled",clickEnabled);
 auto panel=new ClickOptionsPanel(clickDb,preCountBars);f->compactFields["click"]=panel;f->custom(panel,194);
 f->onApply=[s=safe(),f,panel](var v){if(!s||!f)return;
  s->clickEnabled=bool(v["enabled"]);s->clickDb=panel->volume.getValue();s->preCountBars=panel->count;s->syncClick();s->edited();s->refresh();f->close();};
 f->show(tr(String::fromUTF8("Métronome et précompte")));
}
void App::finishMidiCountIn(){
 if(!midiCounting)return;midiCounting=false;
 auto held=countHeld;countHeld.clear();
 for(auto entry:held){auto on=juce::MidiMessage::noteOn(int(entry.first/128)+1,entry.second.first,juce::uint8(entry.second.second));recordMidiEvent(on.withTimeStamp(midiCaptureStartTime));}
 updateTakeDuration();refreshMidiInput();
}
void App::previewTempo(double bpm){
 if(!std::isfinite(bpm)||bpm<10||bpm>600||midiRecording||audio.isVoid())return;
 auto saved=tempoPreview?double(getProperties()["tempoPreviewPosition"]):number(playback,"position");
 if(tempoPreview)stopTempoPreview();
 previewPosition=0;
 if(!roll.notes.empty())previewPosition=std::max(0.,std::min_element(roll.notes.begin(),roll.notes.end(),[](auto a,auto b){return a.start<b.start;})->start);
 previewBpm=bpm;tempoPreview=true;getProperties().set("tempoPreviewPosition",saved);
 invoke("player_control",object({{"audioId",audio["id"]},{"action","pause"},{"position",0}}));syncClick(bpm);
 invoke("player_mix",object({{"audioId",audio["id"]},{"mode",1},{"audioDb",audioDb},{"midiDb",midiDb}}));
 invoke("player_control",object({{"audioId",audio["id"]},{"action","seek"},{"position",previewPosition}}));
 invoke("player_control",object({{"audioId",audio["id"]},{"action","play"},{"position",0}}));
}
void App::stopTempoPreview(){
 if(!tempoPreview)return;tempoPreview=false;
 invoke("player_control",object({{"audioId",audio["id"]},{"action","pause"},{"position",0}}));
 invoke("player_control",object({{"audioId",audio["id"]},{"action","seek"},{"position",double(getProperties()["tempoPreviewPosition"])}}));
 invoke("player_mix",object({{"audioId",audio["id"]},{"mode",mode},{"audioDb",audioDb},{"midiDb",midiDb}}));syncClick();
}

void App::updateClickVisuals(){
 beatIndicator.setVisible(clickEnabled||midiCounting||tempoPreview);
 const double now=juce::Time::getMillisecondCounterHiRes()/1000.;
 if(midiCounting){
  int remaining=std::max(1,int(std::ceil((midiCaptureStartTime-now)/countPulseSeconds)));
  int pulse=(countMeter-remaining%countMeter)%countMeter;
  countOverlay.setText(String((remaining-1)%countMeter+1),juce::dontSendNotification);
  auto area=roll.getLocalBounds();countOverlay.setBounds(area.getCentreX()-70,area.getCentreY()-55,140,110);countOverlay.setVisible(true);countOverlay.toFront(false);
  beatIndicator.update(countMeter,pulse);return;
 }
 countOverlay.setVisible(false);
 auto map=roll.rhythmicGrid;
 if(tempoPreview){map.origin=previewPosition;map.tempos={{0,0,60/previewBpm}};auto meter=map.meters.front();double at=roll.rhythmicGrid.beatAt(previewPosition);for(auto m:map.meters)if(m.beat<=at)meter=m;map.meters={{0,meter.numerator,meter.denominator}};}
 double position=number(playback,"clock_position",number(playback,"position"));
 bool playing=bool(playback["playing"]);if(playing)position+=juce::jlimit(0.,.15,now-number(playback,"clock_time",now));
 double beat=map.beatAt(position);auto meter=map.meters.front();for(auto m:map.meters)if(m.beat<=beat)meter=m;
 int pulse=int(std::floor(std::max(0.,beat-meter.beat)*meter.denominator/4.+1e-8))%meter.numerator;
 beatIndicator.update(meter.numerator,playing&&(clickEnabled||tempoPreview)?pulse:-1);
}
