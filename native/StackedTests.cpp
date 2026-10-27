#include "App.h"
void App::runStackedTests(std::function<void(bool)> done){
 stopTimer();setSize(1500,1050);setVisible(true);
 struct Run{int stage=0;bool finished=false;double started=juce::Time::getMillisecondCounterHiRes();juce::File folder;std::vector<var> checks;var notes,state;std::int64_t id=0;int source=256,target=257;};
 auto r=std::make_shared<Run>();auto poll=std::make_shared<std::function<void()>>();r->folder=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/stacked-"+juce::Uuid().toString());r->folder.createDirectory();
 auto check=[r](bool ok,const String& label){r->checks.push_back(object({{"name",label},{"passed",ok}}));if(!ok)throw std::runtime_error(label.toStdString());};
 auto finish=[r,poll,done](bool ok){if(r->finished)return;r->finished=true;r->folder.getParentDirectory().getChildFile("stacked-latest.json").replaceWithText(juce::JSON::toString(object({{"passed",ok},{"stage",r->stage},{"language",localisation::code()},{"directory",r->folder.getFullPathName()},{"checks",array(r->checks)}})));juce::MessageManager::callAsync([poll,done,ok]{*poll={};done(ok);});};
 auto capture=[this,r](String name){juce::FileOutputStream stream(r->folder.getChildFile(name+".png"));juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()),stream);};
 try{
  auto created=bridge.call("create_empty_midi_project",object({}));check(bool(created["ok"]),"Create isolated MIDI document for stacked workflow");audio=created["value"]["audio"];roll.audio=audio;partId=256;mode=1;updateResult(created["value"]["result"],false,false);
  auto edit=[this,check](var action){auto a=args().clone();a.getDynamicObject()->setProperty("action",action);auto reply=bridge.call("edit_notes",a);check(bool(reply["ok"]),"Prepare shared MIDI fixture: "+action["kind"].toString());updateResult(reply["value"],false,false);};
  edit(object({{"kind","create_empty_part"},{"name","Bass test"},{"program",32},{"colour",10}}));
  auto partValue=[this](int key){for(auto p:items(result["midi_parts"]))if(int(p["identity"])==key)return p;return var();};
  for(auto values:std::vector<std::tuple<int,int,double,int>>{{256,72,.5,97},{256,76,1.5,68},{256,79,2.5,105},{257,43,.5,81},{257,48,2.,63}}){auto [key,pitch,start,velocity]=values;edit(object({{"kind","add"},{"pitch",pitch},{"start",start},{"end",start+.7},{"velocity",velocity},{"part",partValue(key)}}));}
  roll.displayMode=0;displayMode.setSelectedId(1,juce::dontSendNotification);roll.view.zoom=1;roll.scoreDuration=8;correctionOpen=false;r->notes=result["editing"]["notes"].clone();r->id=Note::parse(r->notes[0]).id;
  prepare([r](var){r->stage=1;});
 }catch(const std::exception& e){r->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())}}));finish(false);return;}
 *poll=[this,r,poll,check,finish,capture]{try{
  if(r->finished)return;if(juce::Time::getMillisecondCounterHiRes()-r->started>60000)throw std::runtime_error("Stacked workflow timed out: "+status.getText().toStdString());
  if(r->stage==1&&notesReady&&!locked){
   {
    Roll keyboard;keyboard.setSize(1000,700);keyboard.audio=object({{"duration",8.},{"midi_only",false}});
    keyboard.view.load(object({{"keyHeight",400},{"audioHeight",116}}));
    check(keyboard.view.keyHeight==56&&int(keyboard.view.json()["keyHeight"])==56,"A legacy project with a stretched keyboard loads and saves the compact default height");
    for(int mode=0;mode<3;++mode){keyboard.displayMode=mode;keyboard.resized();auto before=keyboard.layout();check(before.keys==56,"The horizontal keyboard keeps its compact height in every display mode");
     auto now=juce::Time::getCurrentTime();juce::Point<float> origin(500,float(before.dividerKeys+2));auto event=[&](juce::Point<float> point){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&keyboard,&keyboard,now,origin,now,1,true);};
     keyboard.mouseDown(event(origin));keyboard.mouseDrag(event(origin+juce::Point<float>(0,-150)));keyboard.mouseUp(event(origin+juce::Point<float>(0,-150)));
     check(keyboard.layout().keys==56&&keyboard.view.keyHeight==56,"Dragging the former keyboard divider cannot stretch the keyboard");
     check(keyboard.keyAt({100,float(keyboard.layout().keyTop+10)})>=0,"The fixed keyboard retains working audition key hit areas");
    }
   }
   {
    Roll document;document.audio=object({{"duration",8.},{"midi_only",true}});document.scoreDuration=8;document.editable=true;
    std::vector<std::pair<int,String>> voices;
    for(int key=256;key<262;++key){voices.push_back({key,"Voice "+String(key-255)});for(int pitch:{12,48,96,120}){Note n;n.id=key*128+pitch;n.part=key;n.pitch=pitch;n.start=.5;n.end=2;document.notes.push_back(n);}}
    StackedVoices view(document);view.setVisible(true);view.setSize(1000,400);view.sync({voices[0],voices[1]},256,{},object({}),false);
    check(view.frames[256].height==200&&view.frames[257].height==200&&view.body.getHeight()==400&&!view.viewport.getVerticalScrollBar().isVisible(),"Two wide-register voices share the available height without global overflow");
    auto wheelOverHeader=[&](int mods,float delta){auto& header=view.laneFor(256);auto stamp=juce::Time::getCurrentTime();juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),juce::Point<float>(800,20),juce::ModifierKeys(mods),1,0,0,0,0,&header,&header,stamp,juce::Point<float>(800,20),stamp,1,false);juce::MouseWheelDetails w{};w.deltaY=delta;header.mouseWheelMove(event,w);};
    for(const auto hiddenSet:std::vector<std::set<std::int64_t>>{{},{256},{256,257}}){
     view.sync({voices[0],voices[1]},256,hiddenSet,object({}),false);
     auto notesBefore=juce::JSON::toString(document.view.json());
     for(int i=0;i<40;++i)for(int mods:std::initializer_list<int>{0,juce::ModifierKeys::altModifier,juce::ModifierKeys::ctrlModifier,juce::ModifierKeys::shiftModifier})wheelOverHeader(mods,i%2?.2f:-.2f);
     check(view.viewport.getViewPositionY()==0&&juce::JSON::toString(document.view.json())==notesBefore,"Header wheel with no global overflow terminates without recursion or unintended zoom, including hidden voices");
    }
    view.sync({voices[0],voices[1]},256,{},object({}),false);
    check(view.editorFor(256).layout().width==924&&view.editorFor(257).layout().width==924,"Lanes reclaim the obsolete 16-pixel local-scrollbar strip");
    for(int key:{256,257}){auto& lane=view.laneFor(key);auto* grip=lane.findChildWithID("laneGrip");auto point=view.getLocalPoint(grip,grip->getLocalBounds().getCentre());check(view.getLocalBounds().contains(point)&&view.getComponentAt(point)==grip,"Every initial separator is visible and receives pointer hits inside the viewport");auto& editor=view.editorFor(key);for(auto& n:editor.notes)check(editor.rect(n).getY()>=0&&editor.rect(n).getBottom()<=editor.getHeight(),"A wide pitch range is fitted inside its compact lane");}
    auto& editor=view.editorFor(256);double top=editor.top(),row=editor.row(),other=view.editorFor(257).top();auto now=juce::Time::getCurrentTime();auto pointer=[&](juce::Point<float> p,int mods){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),p,juce::ModifierKeys(mods),1,0,0,0,0,&editor,&editor,now,juce::Point<float>(400,60),now,1,true);};
    editor.mouseDown(pointer({400,60},juce::ModifierKeys::middleButtonModifier));editor.mouseDrag(pointer({430,70},juce::ModifierKeys::middleButtonModifier));editor.mouseUp(pointer({430,70},0));check(editor.top()!=top&&editor.row()==row&&view.editorFor(257).top()==other,"Middle-button drag pans the local register without affecting another voice");
    view.sync(voices,256,{},object({}),false);check(view.body.getHeight()==900&&view.viewport.getVerticalScrollBar().isVisible(),"Six voices use compact minimum heights and exactly one global scrollbar");
    view.viewport.setViewPosition(0,0);wheelOverHeader(0,-.3f);check(view.viewport.getViewPositionY()>0,"Header wheel still scrolls the single global viewport when voices overflow");
    for(int edge:{0,100000}){view.viewport.setViewPosition(0,edge);for(int i=0;i<40;++i)for(int mods:std::initializer_list<int>{0,juce::ModifierKeys::altModifier,juce::ModifierKeys::ctrlModifier})wheelOverHeader(mods,edge==0?.2f:-.2f);check(view.viewport.getViewPositionY()>=0&&view.viewport.getViewPositionY()<=view.body.getHeight()-view.getHeight(),"Header wheel at a global scroll limit terminates safely, including modified wheel gestures");}
    auto* resizeGrip=view.laneFor(256).findChildWithID("laneGrip");auto resizeTime=juce::Time::getCurrentTime();auto resizeEvent=[&](float y){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),juce::Point<float>(20,y),juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,resizeGrip,resizeGrip,resizeTime,juce::Point<float>(20,4),resizeTime,1,true);};
    resizeGrip->mouseDown(resizeEvent(4));resizeGrip->mouseDrag(resizeEvent(54));check(view.frames[256].height==200&&view.frames[257].height==150,"A voice remains resizable when its neighbour is already at minimum height");
    view.viewport.setViewPosition(0,100000);auto& last=view.laneFor(261);auto* grip=last.findChildWithID("laneGrip");auto point=view.getLocalPoint(grip,grip->getLocalBounds().getCentre());check(view.getLocalBounds().contains(point)&&view.getComponentAt(point)==grip,"Global scrolling makes the last lane separator reachable above the reserved keyboard area");
    juce::FileOutputStream stream(r->folder.getChildFile("six-voices-compact.png"));juce::PNGImageFormat().writeImageToStream(view.createComponentSnapshot(view.getLocalBounds()),stream);
   }
   {Roll blank;blank.audio=object({{"duration",8.},{"midi_only",true}});blank.scoreDuration=8;blank.editable=true;Note held;held.pitch=48;held.velocity=83;held.part=256;held.start=.1;held.end=.3;blank.midiPreview.push_back(held);StackedVoices view(blank);view.setSize(900,300);view.sync({{256,"Piano"}},256,{},object({}),false);auto& editor=view.editorFor(256);check(editor.midiPreview.size()==1&&editor.rect(held).intersects(editor.getLocalBounds().toFloat()),"A held MIDI note in a previously empty lane is framed and displayed immediately");}
   auto beforeView=juce::JSON::toString(roll.view.json());auto beforeCount=roll.notes.size();roll.selectIds({r->id});
   voicePresentation.setSelectedId(2,juce::sendNotificationSync);refresh();
   check(stackedView&&stacked->isVisible()&&stacked->lanes.size()==2&&stacked->order.size()==2,"Stacked presentation uses one lane per existing MIDI voice");
   check(juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->notes)&&roll.notes.size()==beforeCount&&roll.selected.count(r->id),"Switching view preserves all note values, identities and selection");
   auto& high=stacked->editorFor(r->source);auto& low=stacked->editorFor(r->target);
   check(high.notes.size()==3&&low.notes.size()==2&&high.top()!=low.top(),"Independent initial framing displays each voice in its own register");
   for(double t:{0.,.5,2.,5.})check(high.x(t)==roll.x(t)&&low.x(t)==roll.x(t),"Both lanes share exact timeline pixel positions with the common ruler");
   for(int key:stacked->order){auto& editor=stacked->editorFor(key);for(auto& n:editor.notes)check(editor.rect(n).getY()>=0&&editor.rect(n).getBottom()<=editor.getHeight(),"Initial stacked framing includes every note in the voice register");}
   check(!stacked->laneFor(r->source).findChildWithID("lanePitchScroll")&&!stacked->laneFor(r->target).findChildWithID("lanePitchScroll"),"Stacked lanes expose no competing local scrollbars");
   auto wheelEvent=[&](Roll& editor,int mods=0){auto now=juce::Time::getCurrentTime();return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),juce::Point<float>(300,60),juce::ModifierKeys(mods),1,0,0,0,0,&editor,&editor,now,juce::Point<float>(300,60),now,1,false);};
   juce::MouseWheelDetails wheel{};wheel.deltaY=.3f;
   auto highFrame=stacked->frames[r->source],lowFrame=stacked->frames[r->target];double otherTop=low.top();int outerScroll=stacked->viewport.getViewPositionY();high.mouseWheelMove(wheelEvent(high),wheel);
   check(high.top()!=highFrame.top&&low.top()==otherTop&&stacked->viewport.getViewPositionY()==outerScroll,"Wheel over a voice moves only that register, not other voices or global scrolling");
   auto manualTop=high.top();stacked->focusOnEntry();refreshStacked();check(high.top()==manualTop&&stacked->frames[r->source].manual,"Re-entering stacked view preserves a manually scrolled register");
   stacked->frames[r->source]=highFrame;stacked->frames[r->target]=lowFrame;stacked->refreshFrames();
   {Roll loading;loading.audio=object({{"duration",8.},{"midi_only",true}});loading.editable=true;Note n;n.part=256;n.pitch=36;n.start=0;n.end=1;loading.notes={n};StackedVoices view(loading);view.setSize(900,300);view.sync({{256,"Piano"}},256,{},object({}),true);loading.notes.front().pitch=84;view.focusOnEntry();view.sync({{256,"Piano"}},256,{},object({}),false);auto& editor=view.editorFor(256);check(editor.rect(loading.notes.front()).getY()>=0&&editor.rect(loading.notes.front()).getBottom()<=editor.getHeight(),"Initial framing waits for the fully loaded register instead of keeping a partial result");}
   auto oldRow=high.row();auto oldTop=high.top();stacked->frames[r->source].manual=true;stacked->frames[r->source].heightManual=true;stacked->frames[r->source].height=360;stacked->layoutLanes();check(high.row()==oldRow&&high.top()==oldTop&&high.getHeight()==312,"Resizing a lane reveals more pitches without changing note size or top pitch");
   stacked->frames[r->source].heightManual=true;stacked->frames[r->source].height=220;stacked->layoutLanes();
   voiceFraming.setSelectedId(2,juce::sendNotificationSync);check(high.row()==low.row()&&high.y(60)==low.y(60),"Shared pitch scale gives identical pitch spacing and relative pitch coordinates");
   high.mouseWheelMove(wheelEvent(high),wheel);check(high.top()==low.top()&&stacked->commonManual,"Wheel in shared scale moves all voice registers together");
   voiceFraming.setSelectedId(1,juce::sendNotificationSync);check(high.top()==oldTop&&high.row()==oldRow,"Leaving the shared scale restores independent voice framing");
   auto& lane=stacked->laneFor(r->source);auto grip=lane.findChildWithID("laneGrip");check(grip!=nullptr,"Each lane exposes a draggable bottom separator");
   auto nowGrip=juce::Time::getCurrentTime();auto gripEvent=[&](juce::Point<float> point){return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,grip,grip,nowGrip,juce::Point<float>(10,2),nowGrip,1,true);};
   auto beforeHeight=stacked->frames[r->source].height;grip->mouseDown(gripEvent({10,2}));grip->mouseDrag(gripEvent({10,22}));check(stacked->frames[r->source].height==beforeHeight+20&&high.row()==oldRow,"Dragging the separator redistributes lane height without changing note size");grip->mouseDoubleClick(gripEvent({10,2}));check(!stacked->frames[r->source].heightManual,"Double-clicking the separator restores automatic height allocation");
   auto frozen=juce::JSON::toString(result["editing"]["notes"]);int fitHeight=stacked->frames[r->source].height;stacked->fit(r->source);check(juce::JSON::toString(result["editing"]["notes"])==frozen&&stacked->frames[r->source].height==fitHeight,"Fit voice changes register without growing the lane or editing notes");
   stacked->frames[r->source].heightManual=true;stacked->frames[r->source].height=220;stacked->layoutLanes();
   auto values=voiceLevels.clone();auto muted=result["editing"]["muted_parts"].clone();stacked->activate(r->target);stacked->activate(r->source);check(juce::JSON::toString(voiceLevels)==juce::JSON::toString(values)&&juce::JSON::toString(result["editing"]["muted_parts"])==juce::JSON::toString(muted),"Activating another lane does not change audible voices or individual listening levels");
   ListeningVolumeSlider* laneVolume=nullptr;for(auto* c:stacked->laneFor(r->source).getChildren())if(auto* slider=dynamic_cast<ListeningVolumeSlider*>(c))laneVolume=slider;check(laneVolume!=nullptr,"Each lane exposes its own volume control");laneVolume->setValue(-9.345,juce::sendNotificationSync);refreshStacked();check(std::abs(number(voiceLevels,"256")+9.345)<1e-8&&number(voiceLevels,"257")==0,"Each lane volume targets only its own existing listening level");
   auto& editor=stacked->editorFor(r->source);auto original=editor.notes.front();editor.selected={original.id};bool modified=false;Note candidate;
   auto oldModify=roll.modify;roll.modify=[&](const Note& n){modified=true;candidate=n;};editor.modify=roll.modify;editor.beginEdit=[]{};editor.audition=[](const Note&){};
   auto centre=editor.rect(original).getCentre();auto source=juce::Desktop::getInstance().getMainMouseSource();auto now=juce::Time::getCurrentTime();
   auto event=[&](juce::Point<float> point,int mods=juce::ModifierKeys::leftButtonModifier){return juce::MouseEvent(source,point,juce::ModifierKeys(mods),1,0,0,0,0,&editor,&editor,now,centre,now,1,true);};
   editor.mouseDown(event(centre));auto end=centre+juce::Point<float>(35,-float(editor.row()));editor.mouseDrag(event(end));editor.mouseUp(event(end));
   check(modified&&candidate.pitch==original.pitch+1&&candidate.start>original.start&&candidate.part==original.part,"Ordinary lane drag reuses note movement and transposition without changing voice: modified="+String(int(modified))+" pitch="+String(candidate.pitch)+" expected="+String(original.pitch+1)+" start="+String(candidate.start)+" original="+String(original.start)+" part="+String(candidate.part)+" expectedPart="+String(original.part)+" editable="+String(int(editor.editable))+" noteEditable="+String(int(original.editable)));
   roll.modify=oldModify;refreshStacked();
   auto fold=dynamic_cast<juce::Button*>(stacked->laneFor(r->target).findChildWithID("laneFold"));check(fold!=nullptr,"Each lane has an independent fold button");fold->onClick();check(stacked->laneFor(r->target).getHeight()==42&&!stacked->editorFor(r->target).isVisible()&&!hiddenParts.count(r->target),"Folding a lane preserves its visibility state and sound");fold->onClick();
   stacked->viewport.setViewPosition(0,0);capture("stacked-independent");voiceFraming.setSelectedId(2,juce::sendNotificationSync);capture("stacked-common");voiceFraming.setSelectedId(1,juce::sendNotificationSync);
   voicePresentation.setSelectedId(1,juce::sendNotificationSync);check(!roll.stackHost&&!stacked->isVisible()&&juce::JSON::toString(roll.view.json())==beforeView,"Returning to overlaid view restores its original pitch and time transform");capture("overlaid");voicePresentation.setSelectedId(2,juce::sendNotificationSync);
   auto& from=stacked->editorFor(r->source);auto n=from.notes.front();r->id=n.id;r->notes=result["editing"]["notes"].clone();from.selected={n.id};roll.selectIds({n.id});
   stacked->transferring=true;auto origin=from.rect(n).getCentre();auto targetScreen=stacked->laneFor(r->target).localPointToGlobal(juce::Point<int>(150,15));auto target=from.getLocalPoint(nullptr,targetScreen).toFloat();
   auto ev=[&](juce::Point<float> point){return juce::MouseEvent(source,point,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),1,0,0,0,0,&from,&from,now,origin,now,1,true);};
   from.mouseDown(ev(origin));from.mouseDrag(ev(target));check(stacked->dropTarget==r->target,"Transfer gesture highlights the destination lane");from.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));check(stacked->dropTarget==-1&&juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->notes),"Escape cancels transfer without editing notes");
   stacked->transferring=true;from.mouseDown(ev(origin));from.mouseDrag(ev(target));r->stage=2;from.mouseUp(ev(target));
  }else if(r->stage==2&&!locked&&notesReady){
   auto old=Note::parse(r->notes[0]);for(auto v:items(r->notes))if(Note::parse(v).id==r->id)old=Note::parse(v);Note after;bool found=false;for(auto& n:roll.notes)if(n.id==r->id){after=n;found=true;}
   check(found&&after.part==r->target&&after.pitch==old.pitch&&after.start==old.start&&after.end==old.end&&after.velocity==old.velocity&&roll.notes.size()==size_t(r->notes.size()),"Dropping into another voice changes only affiliation, retaining pitch, timing, velocity, ID and note count");
   r->stage=3;command(object({{"kind","undo"}}));
  }else if(r->stage==3&&!locked&&notesReady){
   check(juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->notes),"One Undo restores the complete transfer");
   auto eye=dynamic_cast<juce::Button*>(stacked->laneFor(r->target).findChildWithID("eye"));check(eye!=nullptr,"Every lane exposes the existing voice visibility control");auto priorMutes=result["editing"]["muted_parts"].clone();eye->onClick();check(hiddenParts.count(r->target)&&!stacked->editorFor(r->target).isVisible()&&juce::JSON::toString(result["editing"]["muted_parts"])==juce::JSON::toString(priorMutes),"Hiding a lane affects its display without muting it");eye->onClick();check(!hiddenParts.count(r->target)&&stacked->editorFor(r->target).isVisible(),"The lane eye restores hidden notes");
   r->stage=31;stacked->toggleLock(r->target);
  }else if(r->stage==31&&!locked&&notesReady){
   check(stacked->locked.count(r->target)&&!stacked->allowed(r->target,{r->id}),"An actually locked destination rejects transfer");check(stacked->editorFor(r->target).x(1)==roll.x(1),"Locking a voice never changes its shared timeline scale");r->stage=32;stacked->toggleLock(r->target);
  }else if(r->stage==32&&!locked&&notesReady){r->stage=33;stacked->toggleLock(r->source);
  }else if(r->stage==33&&!locked&&notesReady){
   check(stacked->locked.count(r->source)&&!stacked->allowed(r->target,{r->id}),"An actually locked source rejects transfer");r->stage=34;stacked->toggleLock(r->source);
  }else if(r->stage==34&&!locked&&notesReady){r->stage=35;stacked->toggleMute(r->target);
  }else if(r->stage==35&&!locked&&notesReady){
   check(roll.mutedParts.count(r->target)&&!roll.mutedParts.count(r->source),"Lane mute affects only its own voice");stacked->activate(r->target);stacked->activate(r->source);check(roll.mutedParts.count(r->target)&&!roll.mutedParts.count(r->source),"Changing lanes preserves individual mute states");r->stage=36;stacked->toggleMute(r->target);
  }else if(r->stage==36&&!locked&&notesReady){
   check(roll.mutedParts.empty(),"The same lane control restores its voice sound");stacked->instrument(r->target);r->stage=37;
  }else if(r->stage==37&&juce::Component::getCurrentlyModalComponent()){
   auto* dialog=dynamic_cast<juce::DialogWindow*>(juce::Component::getCurrentlyModalComponent());auto* form=dialog?dynamic_cast<Form*>(dialog->getContentComponent()):nullptr;
   check(form&&part.getSelectedId()==r->target+2&&int(form->state()["part"])==r->target&&form->comboFields.count("preset")&&form->comboFields.count("soundfont_bank"),"Lane instrument opens the existing instrument selector for the correct voice");form->close();r->stage=38;
  }else if(r->stage==38&&!juce::Component::getCurrentlyModalComponent()&&!locked&&notesReady){
   auto beforeAudio=roll.audio;auto beforeSpectrum=roll.spectrum;int beforeMode=roll.displayMode;bool beforeSpectral=roll.spectral;
   roll.audio=roll.audio.clone();roll.audio.getDynamicObject()->setProperty("midi_only",false);roll.spectral=true;roll.displayMode=2;roll.spectrum=juce::Image(juce::Image::RGB,32,384,true);roll.spectrumStart=0;roll.spectrumEnd=8;{juce::Graphics g(roll.spectrum);g.fillAll(juce::Colour(0xffc7a7b8));g.setColour(juce::Colour(0xffd98c4a));g.fillRect(4,40,20,4);g.fillRect(4,140,20,4);}refresh();
   check(!stackedView&&!stacked->isVisible()&&!roll.stackHost&&roll.layout().audio==0,"Superposition uses the original grouped MIDI overlay without an extra source-audio strip");capture("grouped-spectrogram-overlay");
   roll.displayMode=1;refresh();check(!stackedView&&!stacked->isVisible()&&roll.layout().audio>0,"Audio plus MIDI keeps grouped voices and a separate common audio strip");capture("grouped-audio-midi");
   roll.displayMode=0;roll.audio=beforeAudio;roll.spectrum=beforeSpectrum;roll.spectral=beforeSpectral;refresh();check(stackedView&&stacked->isVisible()&&roll.layout().audio==0,"Returning to MIDI restores the stacked choice without a source audio strip");
   setSize(1280,850);stacked->frames[r->source].heightManual=false;stacked->frames[r->target].heightManual=false;stacked->frames[r->source].manual=false;stacked->frames[r->target].manual=false;refresh();
   check(stacked->getBottom()<=roll.layout().dividerKeys&&stacked->body.getHeight()==stacked->getHeight(),"At a realistic window size both voices fit completely above the fixed keyboard");capture("compact-window-midi");
   {auto savedNotes=roll.notes;for(auto& n:roll.notes)n.pitch=n.part==r->source?(n.start<1?24:n.start<2?60:96):(n.start<1?36:84);stacked->frames[r->source].manual=false;stacked->frames[r->source].framed=false;stacked->frames[r->target].manual=false;stacked->frames[r->target].framed=false;refreshStacked();for(int key:stacked->order){auto& lane=stacked->laneFor(key);auto* separator=lane.findChildWithID("laneGrip");auto p=getLocalPoint(separator,separator->getLocalBounds().getCentre());check(getComponentAt(p)==separator,"Compact application window exposes every separator to actual pointer hit testing");auto& editor=stacked->editorFor(key);for(auto& n:editor.notes)check(editor.rect(n).getY()>=0&&editor.rect(n).getBottom()<=editor.getHeight(),"Wide-register notes remain visible in the compact application layout");}capture("compact-window-wide-registers");roll.notes=savedNotes;stacked->frames[r->source].framed=false;stacked->frames[r->target].framed=false;refreshStacked();}
   roll.audio=roll.audio.clone();roll.audio.getDynamicObject()->setProperty("midi_only",false);roll.displayMode=0;refresh();check(roll.layout().audio==0,"MIDI-only display hides the source audio strip and its resize handle in stacked view");
   displayMode.setSelectedId(2,juce::sendNotificationSync);check(roll.layout().audio>0&&!stackedView&&!voicePresentation.isVisible(),"Selecting Audio plus MIDI hides stacked controls and restores the common source audio strip");displayMode.setSelectedId(3,juce::sendNotificationSync);check(!stackedView&&!roll.stackHost&&roll.layout().audio==0,"Selecting Superposition restores the original grouped overlay layout");displayMode.setSelectedId(1,juce::sendNotificationSync);check(stackedView&&voicePresentation.isVisible()&&roll.layout().audio==0,"Selecting MIDI restores the retained stacked preference");roll.displayMode=0;roll.audio=beforeAudio;refresh();
   stacked->frames[r->source].heightManual=true;stacked->frames[r->source].manual=true;stacked->frames[r->source].height=340;stacked->frames[r->target].folded=true;stacked->frames[r->source].top=88.5;stacked->frames[r->source].row=14.25;stacked->layoutLanes();r->state=stacked->state().clone();r->stage=-1;
   saveProjectTo(r->folder.getChildFile("stacked.pht").getFullPathName(),[r,finish](bool ok){if(ok)r->stage=4;else finish(false);});
  }else if(r->stage==4&&!locked){
   auto opened=bridge.call("open_project",object({{"path",projectPath}}));check(bool(opened["ok"]),"Saved stacked project reopens");auto state=opened["value"]["ui"];
   check(bool(state["stackedView"])&&juce::JSON::toString(state["stackedVoices"]["voices"])==juce::JSON::toString(r->state["voices"]),"PHT persists per-voice heights, folds, register and note zoom");
   stacked->frames.clear();loadUi(state);updateResult(opened["value"]["result"],false,false);refresh();check(stackedView&&stacked->frames[r->source].height==340&&stacked->frames[r->target].folded&&stacked->frames[r->source].row==14.25&&stacked->frames[r->source].top==88.5,"Reopened UI restores the saved stacked presentation");
   check(juce::JSON::toString(result["editing"]["notes"])==juce::JSON::toString(r->notes),"View persistence never duplicates or converts MIDI notes");
   auto valid=stacked->state();auto invalid=object({{"voices",object({{"256",object({{"height",-100},{"top",999},{"row",1000}})}})}});stacked->load(invalid);check(stacked->frames[256].height==150&&stacked->frames[256].top==127&&stacked->frames[256].row==36,"Saved display settings are bounded before layout");stacked->load(valid);refresh();finish(true);return;
  }
  juce::Timer::callAfterDelay(30,[poll]{if(*poll)(*poll)();});
 }catch(const std::exception& e){r->checks.push_back(object({{"passed",false},{"error",String::fromUTF8(e.what())}}));finish(false);}};(*poll)();
}
