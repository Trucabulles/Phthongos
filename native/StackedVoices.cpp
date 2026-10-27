#include "StackedVoices.h"
class StackedVoices::Lane : public juce::Component {
public:
 StackedVoices& owner;int key;String label;juce::TextButton fold,name,eye,mute,lock,instrument,fitButton,colour;
 juce::Label sourceLabel;juce::TextButton sourceReload;
 ListeningVolumeSlider volume;
 class Editor : public Roll {
 public:
  Lane& lane;bool transferDrag=false,panning=false;std::set<std::int64_t> transferIds;juce::Point<float> anchor;double panTop=0,timeStart=0;
  Editor(Lane& l):lane(l){laneMode=true;displayMode=0;follow=false;}
  void mouseDown(const juce::MouseEvent& e)override{
   if(lane.owner.busy)return;
   if(lane.owner.activate)lane.owner.activate(lane.key);
   if(e.mods.isMiddleButtonDown()||(tool==2&&e.mods.isLeftButtonDown())){panning=true;anchor=e.position;panTop=laneTop;timeStart=start();return;}
   if(lane.owner.transferring||(e.mods.isAltDown()&&e.mods.isShiftDown())){
    if(!editable)return;
    for(const auto& n:notes)if(rect(n).contains(e.position)){transferIds=selected.count(n.id)?selected:std::set<std::int64_t>{n.id};transferDrag=true;anchor=e.position;selectIds(transferIds);if(beginEdit)beginEdit();lane.owner.dragTo(e.getScreenPosition());return;}return;
   }
   Roll::mouseDown(e);
  }
  void mouseDrag(const juce::MouseEvent& e)override{
   if(transferDrag){lane.owner.dragTo(e.getScreenPosition());return;}
   if(panning){laneTop=juce::jlimit(0.,127.,panTop+(e.position.y-anchor.y)/laneRow);frameTime(std::max(0.,timeStart-(e.position.x-anchor.x)/layout().width*span()),span());lane.owner.publish(lane);return;}
   Roll::mouseDrag(e);if(pointerEditing())lane.owner.publish(lane);
  }
  void mouseUp(const juce::MouseEvent& e)override{
   if(transferDrag){transferDrag=false;int target=lane.owner.targetAt(e.getScreenPosition());auto ids=transferIds;transferIds.clear();bool moved=e.position.getDistanceFrom(anchor)>4;lane.owner.cancelTransfer();if(moved&&target!=lane.key&&lane.owner.allowed(target,ids)&&lane.owner.transfer)lane.owner.transfer(ids,target);return;}
   if(panning){panning=false;return;}Roll::mouseUp(e);
  }
  void mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& w)override{
   if(e.mods.isAltDown()||e.mods.isCtrlDown()||e.mods.isCommandDown()){
    double pitch=laneTop-(e.position.y-8)/laneRow;laneRow=juce::jlimit(.5,36.,laneRow*std::exp(w.deltaY*2));laneTop=juce::jlimit(0.,127.,pitch+(e.position.y-8)/laneRow);
    if(e.mods.isCtrlDown()||e.mods.isCommandDown()){double a=start(),s=span(),factor=std::exp(-w.deltaY*2);auto next=juce::jlimit(.0001,duration(),s*factor);auto t=a+(e.position.x-58)/layout().width*(s-next);frameTime(t,next);}
    lane.owner.publish(lane);return;
   }
   if(e.mods.isShiftDown()||std::abs(w.deltaX)>std::abs(w.deltaY)){frameTime(start()-(w.deltaX?w.deltaX:w.deltaY)*span()*.2,span());lane.owner.publish(lane);return;}
   laneTop=juce::jlimit(0.,127.,laneTop+w.deltaY*12);lane.owner.publish(lane);
  }
  bool keyPressed(const juce::KeyPress& k)override{
   if(k==juce::KeyPress::escapeKey&&(transferDrag||lane.owner.transferring)){transferDrag=false;transferIds.clear();lane.owner.cancelTransfer();lane.owner.transferring=false;if(lane.owner.changed)lane.owner.changed();return true;}
   if(k.getKeyCode()==juce::KeyPress::leftKey||k.getKeyCode()==juce::KeyPress::rightKey)return lane.owner.master.seekByKey(k);
   return Roll::keyPressed(k);
  }
 } editor;
 class Grip : public juce::Component {
 public:Lane& lane;int height=0,next=-1,nextHeight=0;Grip(Lane& l):lane(l){setMouseCursor(juce::MouseCursor::UpDownResizeCursor);}
  void mouseDown(const juce::MouseEvent&)override{
   height=lane.owner.frames[lane.key].height;next=-1;bool found=false;
   for(int key:lane.owner.order){if(found&&!lane.owner.frames[key].folded){next=key;nextHeight=lane.owner.frames[key].height;break;}if(key==lane.key)found=true;}
  }
  void mouseDrag(const juce::MouseEvent& e)override{
   auto& f=lane.owner.frames[lane.key];int delta=e.getDistanceFromDragStartY();
   delta=juce::jlimit(150-height,1200-height,delta);
   f.manual=true;f.heightManual=true;f.height=height+delta;f.folded=false;
   if(next>=0){auto& following=lane.owner.frames[next];following.manual=true;following.heightManual=true;following.height=juce::jlimit(150,1200,nextHeight-delta);}
   lane.owner.layoutLanes();if(lane.owner.changed)lane.owner.changed();
  }
  void mouseDoubleClick(const juce::MouseEvent&)override{
   lane.owner.frames[lane.key].heightManual=false;
   if(next>=0)lane.owner.frames[next].heightManual=false;
   lane.owner.layoutLanes();if(lane.owner.changed)lane.owner.changed();
  }
  void paint(juce::Graphics& g)override{
   g.fillAll(juce::Colour(0xffd4c7b2));g.setColour(juce::Colour(0xff7e929d));
   g.fillRect(0,0,getWidth(),1);g.fillRoundedRectangle(getWidth()/2.f-24,3,48,4,2);
  }
 } grip;
 Lane(StackedVoices& o,int id):owner(o),key(id),editor(*this),grip(*this){
  grip.setComponentID("laneGrip");sourceLabel.setFont(juce::FontOptions(13));sourceReload.setButtonText(String::fromUTF8("↺"));sourceReload.setName(tr(String::fromUTF8("Recharger la source audio de la voix")));sourceReload.setTooltip(sourceReload.getName());sourceReload.onClick=[this]{if(owner.reloadAudio)owner.reloadAudio(key);};
  for(auto* c:std::initializer_list<juce::Component*>{&fold,&name,&eye,&mute,&lock,&instrument,&fitButton,&volume,&editor,&grip,&colour,&sourceLabel,&sourceReload})addAndMakeVisible(c);
  name.getProperties().set("preserveLanguageText",true);fold.setComponentID("laneFold");fitButton.setComponentID("laneFit");
  eye.setComponentID("eye");mute.setComponentID("partMute");colour.setComponentID("partColour");lock.setComponentID("partLock");
  fold.onClick=[this]{auto& f=owner.frames[key];f.folded=!f.folded;owner.layoutLanes();if(owner.changed)owner.changed();};
  name.onClick=[this]{if(owner.activate)owner.activate(key);};
  eye.onClick=[this]{if(owner.toggleVisible)owner.toggleVisible(key);};mute.onClick=[this]{if(owner.toggleMute)owner.toggleMute(key);};lock.onClick=[this]{if(owner.toggleLock)owner.toggleLock(key);};
  instrument.onClick=[this]{if(owner.instrument)owner.instrument(key);};colour.onClick=[this]{if(owner.colour)owner.colour(key);};fitButton.onClick=[this]{owner.fit(key);};
  volume.onValueChange=[this]{if(!owner.syncing&&owner.level)owner.level(key,volume.getValue());};
  editor.changed=[this]{owner.publish(*this);};editor.inspect=[this](String text){if(owner.master.inspect)owner.master.inspect(text);};
 }
 void resized()override{
  int x=4;const int compact=std::max(0,672-getWidth());
  auto place=[&](juce::Component& c,int width){c.setBounds(x,5,width,26);x+=width+5;};
  place(fold,26);place(name,144-std::min(74,compact/2));place(colour,27);place(eye,27);place(mute,27);
  place(volume,135-std::min(55,compact-std::min(74,compact/2)-std::min(30,compact/4)));
  place(instrument,100-std::min(30,compact/4));place(lock,27);place(fitButton,110);
  auto source=owner.audioSources[key];int header=source.isEmpty()?38:62;sourceLabel.setVisible(source.isNotEmpty());sourceReload.setVisible(source.isNotEmpty());sourceReload.setEnabled(!owner.busy);
  sourceLabel.setText(tr(String::fromUTF8("Source : "))+juce::File(source).getFileName(),juce::dontSendNotification);sourceLabel.setTooltip(source);sourceLabel.setBounds(38,34,std::max(0,getWidth()-82),24);sourceReload.setBounds(getWidth()-36,35,26,21);
  editor.setBounds(0,header,getWidth(),std::max(0,getHeight()-header-10));grip.setBounds(0,getHeight()-10,getWidth()-18,10);grip.setVisible(!owner.frames[key].folded);
 }
 void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(owner.active==key?0xffe4d5bb:0xffe9e0d1));if(owner.dropTarget==key){g.setColour(juce::Colour(owner.locked.count(key)?0xffb85c4a:0xff4f9b78));g.drawRect(getLocalBounds().reduced(1),3);}}
};
StackedVoices::StackedVoices(Roll& r):master(r){addAndMakeVisible(viewport);viewport.setViewedComponent(&body,false);viewport.setScrollBarsShown(true,false);viewport.setScrollBarThickness(14);}
StackedVoices::~StackedVoices(){viewport.setViewedComponent(nullptr,false);}
void StackedVoices::resized(){viewport.setBounds(getLocalBounds());layoutLanes();}
void StackedVoices::layoutLanes(){
 // Heights use the viewport, never the pitch range. Manual heights are retained.
 int reserved=0,automatic=0;
 for(int key:order){auto& f=frames[key];if(f.folded)reserved+=audioSources[key].isEmpty()?42:66;else if(f.heightManual)reserved+=f.height;else ++automatic;}
 int available=std::max(0,getHeight()-reserved),index=0,y=0;
 for(int key:order){auto& f=frames[key];auto& l=*lanes.at(key);int previous=f.height;
  if(!f.folded&&!f.heightManual){f.height=std::max(150,automatic?available/automatic+(index<available%automatic?1:0):220);++index;}
  int height=f.folded?(audioSources[key].isEmpty()?42:66):f.height;l.setBounds(0,y,getWidth(),height);
  l.editor.setVisible(!f.folded&&!hidden.count(key));l.fold.setButtonText(f.folded?String::fromUTF8("▸"):String::fromUTF8("▾"));
  if(!f.manual&&(!f.framed||previous!=f.height))frameVoice(key);
  y+=height;
 }
 body.setSize(getWidth(),std::max(getHeight(),y));
 if(common&&!commonManual)focusCommon();refreshFrames();
}
void StackedVoices::frameVoice(int key){
 int low=127,high=0;bool any=false;
 auto collect=[&](const auto& notes){for(auto& n:notes)if(n.part==key){low=std::min(low,n.pitch);high=std::max(high,n.pitch);any=true;}};
 collect(master.notes);if(!any)collect(master.midiPreview);if(!any){low=48;high=72;}
 auto& f=frames[key];f.top=std::min(127,high+3);
 f.row=juce::jlimit(.5,24.,double(std::max(1,f.height-64-(audioSources[key].isEmpty()?0:24)))/(high-low+7));f.framed=any&&!busy;
}
void StackedVoices::refreshFrames(){for(auto& pair:lanes){auto& l=*pair.second;auto& f=frames[pair.first];l.editor.laneRow=common?commonRow:f.row;l.editor.laneTop=common?commonTop:f.top;l.editor.repaint();l.repaint();}}
void StackedVoices::fit(int key){auto it=lanes.find(key);if(it==lanes.end())return;int low=127,high=0;bool any=false;
 for(const auto& n:master.notes)if(common||n.part==key){low=std::min(low,n.pitch);high=std::max(high,n.pitch);any=true;}
 if(!any){low=48;high=84;}auto& f=frames[key];f.folded=false;layoutLanes();f.top=std::min(127,high+3);f.row=juce::jlimit(.5,24.,double(std::max(1,f.height-64-(audioSources[key].isEmpty()?0:24)))/(high-low+7));f.framed=true;f.manual=true;
 if(common){commonTop=f.top;commonRow=f.row;commonFramed=true;commonManual=true;}layoutLanes();if(changed)changed();}
void StackedVoices::focusCommon(){
 int high=0,low=127;for(auto& n:master.notes){high=std::max(high,n.pitch);low=std::min(low,n.pitch);}if(master.notes.empty()){high=84;low=48;}
 double available=std::max(1,getHeight()-64);for(auto& p:lanes)if(!frames[p.first].folded&&!hidden.count(p.first))available=std::min(available,double(std::max(1,frames[p.first].height-64-(audioSources[p.first].isEmpty()?0:24))));
 commonTop=std::min(127,high+3);commonRow=juce::jlimit(.5,24.,available/(high-low+7));
 commonFramed=true;
}
void StackedVoices::setCommon(bool value){common=value;if(common&&!commonFramed)focusCommon();layoutLanes();if(changed)changed();}
void StackedVoices::sync(const std::vector<std::pair<int,String>>& parts,int current,const std::set<std::int64_t>& hiddenParts,const var& levels,bool isBusy){
 syncing=true;active=current;busy=isBusy;hidden.clear();for(auto k:hiddenParts)hidden.insert(int(k));locked.clear();for(auto k:master.lockedParts)locked.insert(int(k));
 std::set<int> keep;order.clear();for(auto p:parts){keep.insert(p.first);order.push_back(p.first);if(!lanes.count(p.first)){auto l=std::make_unique<Lane>(*this,p.first);body.addAndMakeVisible(*l);lanes[p.first]=std::move(l);}}
 for(auto it=lanes.begin();it!=lanes.end();)if(!keep.count(it->first))it=lanes.erase(it);else ++it;
 std::map<int,std::vector<Note>> byVoice;for(auto n:master.notes)byVoice[n.part].push_back(n);bool first=entryFit&&!busy;
 for(auto p:parts){auto& l=*lanes.at(p.first);auto& r=l.editor;auto& f=frames[p.first];l.label=p.second;
  if(first&&!f.manual)f.framed=false;

  l.name.setButtonText(p.second);l.name.setTooltip(p.second);l.volume.setName(tr(String::fromUTF8("Volume de la voix"))+" · "+p.second);l.eye.setName(tr(String::fromUTF8("Visibilité"))+" · "+p.second);l.mute.setName(tr(String::fromUTF8("Muet"))+" · "+p.second);l.fold.setName(tr(String::fromUTF8("Replier / déplier la voix"))+" · "+p.second);l.instrument.setButtonText(tr(String::fromUTF8("Instrument…")));l.lock.setButtonText((locked.count(p.first)?tr(String::fromUTF8("Déverrouiller")):tr(String::fromUTF8("Verrouiller"))));l.fitButton.setButtonText(tr(String::fromUTF8("Ajuster à la voix")));
  l.lock.setToggleState(locked.count(p.first)!=0,juce::dontSendNotification);
  l.lock.setName((locked.count(p.first)?tr(String::fromUTF8("Déverrouiller cette voix")):tr(String::fromUTF8("Verrouiller cette voix")))+" · "+p.second);
  l.eye.setToggleState(!hidden.count(p.first),juce::dontSendNotification);l.mute.setToggleState(master.mutedParts.count(p.first),juce::dontSendNotification);
  l.colour.getProperties().set("voiceColour",juce::int64(master.voiceColour(p.first,byVoice[p.first].empty()?(p.first<256?p.first:0):byVoice[p.first].front().program).getARGB()));
  l.volume.setValue(number(levels,String(p.first).toRawUTF8(),0),juce::dontSendNotification);
  for(auto* c:std::initializer_list<juce::Component*>{&l.fold,&l.name,&l.eye,&l.mute,&l.lock,&l.fitButton})c->setEnabled(!busy);
  for(auto* c:std::initializer_list<juce::Component*>{&l.instrument,&l.colour})c->setEnabled(!busy&&!locked.count(p.first));l.volume.setEnabled(!busy);
  if(!r.pointerEditing()&&!r.panning&&!r.transferDrag){r.notes=byVoice[p.first];for(auto& n:r.notes)n.editable=!locked.count(p.first)&&!hidden.count(p.first);r.selected.clear();for(auto& n:r.notes)if(master.selected.count(n.id))r.selected.insert(n.id);r.selection=r.selected.empty()?var():master.selection.clone();r.view.zoom=master.view.zoom;r.view.pan=master.view.pan;r.scoreDuration=master.scoreDuration;r.laneDuration=master.duration();}
  r.audio=object({{"id",master.audio["id"]},{"duration",master.audio["duration"]},{"midi_only",true}});r.playback=master.playback;r.rhythmicGrid=master.rhythmicGrid;r.busy=busy;r.editable=!busy&&!locked.count(p.first);r.tool=master.tool;r.partColours=master.partColours;r.lockedParts=master.lockedParts;r.manuallyMuted=master.manuallyMuted;r.mutedParts=master.mutedParts;r.extra=master.extra;r.tonal=master.tonal;r.shortIds=master.shortIds;r.suspect=master.suspect;r.focused=master.focused;
  r.midiPreview.clear();for(auto n:master.midiPreview)if(n.part==p.first)r.midiPreview.push_back(n);
  r.modify=master.modify;r.split=master.split;r.moveGroup=master.moveGroup;r.duplicateGroup=master.duplicateGroup;r.add=master.add;r.remove=master.remove;r.merge=master.merge;r.mute=master.mute;r.audition=master.audition;r.context=master.context;r.beginEdit=master.beginEdit;r.seek=master.seek;
  r.refreshOverlaps();
 }if(first){entryFit=false;if(common&&!commonManual)focusCommon();}layoutLanes();if(getProperties().contains("restoreScroll")){viewport.setViewPosition(0,int(getProperties()["restoreScroll"]));getProperties().remove("restoreScroll");}syncing=false;
}
void StackedVoices::publish(Lane& l){if(syncing)return;auto& r=l.editor;auto& f=frames[l.key];if(common){if(std::abs(commonTop-r.laneTop)>1e-8||std::abs(commonRow-r.laneRow)>1e-8)commonManual=true;commonTop=r.laneTop;commonRow=r.laneRow;commonFramed=true;}else{if(std::abs(f.top-r.laneTop)>1e-8||std::abs(f.row-r.laneRow)>1e-8)f.manual=true;f.top=r.laneTop;f.row=r.laneRow;f.framed=true;}
 master.selected=r.selected;master.selection=r.selection.clone();master.mirrorLaneTime(r.duration(),r.start(),r.span());refreshFrames();if(master.changed)master.changed();}
int StackedVoices::targetAt(juce::Point<int> screen)const{auto local=viewport.getLocalPoint(nullptr,screen);if(!viewport.getLocalBounds().contains(local))return -1;auto p=body.getLocalPoint(nullptr,screen);for(auto key:order)if(lanes.at(key)->getBounds().contains(p))return key;return -1;}
void StackedVoices::dragTo(juce::Point<int> screen){auto p=viewport.getLocalPoint(nullptr,screen);if(p.y<20)viewport.setViewPosition(viewport.getViewPositionX(),viewport.getViewPositionY()-12);else if(p.y>viewport.getHeight()-20)viewport.setViewPosition(viewport.getViewPositionX(),viewport.getViewPositionY()+12);dropTarget=targetAt(screen);for(auto& l:lanes)l.second->repaint();}
bool StackedVoices::allowed(int target,const std::set<std::int64_t>& ids)const{if(busy||target<0||!lanes.count(target)||locked.count(target)||ids.empty())return false;for(auto& n:master.notes)if(ids.count(n.id)&&locked.count(n.part))return false;return true;}
void StackedVoices::cancelTransfer(){dropTarget=-1;for(auto& l:lanes){l.second->editor.transferDrag=false;l.second->editor.transferIds.clear();l.second->repaint();}}
void StackedVoices::mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& w){
 // Do not call Viewport::mouseWheelMove here: an unconsumed wheel bubbles
 // back to this parent, recursively, when there is no scrollbar or with Alt/Ctrl.
 viewport.useMouseWheelMoveIfNeeded(e.getEventRelativeTo(&viewport),w);
}
var StackedVoices::state()const{auto entries=object({});for(auto p:frames){auto& f=p.second;entries.getDynamicObject()->setProperty(String(p.first),object({{"height",f.height},{"folded",f.folded},{"top",f.top},{"row",f.row},{"framed",f.framed},{"manual",f.manual},{"heightManual",f.heightManual}}));}return object({{"common",common},{"commonFramed",commonFramed},{"commonManual",commonManual},{"top",commonTop},{"row",commonRow},{"scroll",viewport.getViewPositionY()},{"voices",entries}});}
void StackedVoices::load(const var& value){transferring=false;cancelTransfer();for(auto& l:lanes){l.second->editor.cancelPointerEdit();l.second->editor.panning=false;}frames.clear();entryFit=true;commonManual=bool(value["commonManual"]);common=bool(value["common"]);commonFramed=bool(value["commonFramed"]);auto finite=[](double n,double fallback,double low,double high){return std::isfinite(n)?juce::jlimit(low,high,n):fallback;};commonTop=finite(number(value,"top",96),96,0,127);commonRow=finite(number(value,"row",12),12,.5,36);
 if(auto* entries=value["voices"].getDynamicObject())for(auto p:entries->getProperties()){auto text=p.name.toString();int id=text.getIntValue();if(text!=String(id)||id<0||(id>128&&(id<256||id>=32768)))continue;auto v=p.value;Frame f;f.height=int(finite(number(v,"height",220),220,150,1200));f.folded=bool(v["folded"]);f.top=finite(number(v,"top",84),84,0,127);f.row=finite(number(v,"row",12),12,.5,36);f.framed=bool(v["framed"]);f.manual=bool(v["manual"]);f.heightManual=bool(v["heightManual"]);frames[id]=f;}
 getProperties().set("restoreScroll",int(finite(number(value,"scroll",0),0,0,1000000)));refreshFrames();}

#include "App.h"
void App::initialiseStacked(){
 stacked=std::make_unique<StackedVoices>(roll);roll.addChildComponent(*stacked);
 for(auto* c:std::initializer_list<juce::Component*>{&voicePresentation,&voiceFraming,&transferVoices})addAndMakeVisible(c);
 voicePresentation.addItem(tr(String::fromUTF8("Superposées")),1);voicePresentation.addItem(tr(String::fromUTF8("Empilées")),2);voicePresentation.setSelectedId(1,juce::dontSendNotification);voicePresentation.setName(tr(String::fromUTF8("Présentation des voix")));
 voiceFraming.addItem(tr(String::fromUTF8("Cadrage par voix")),1);voiceFraming.addItem(tr(String::fromUTF8("Échelle commune")),2);voiceFraming.setSelectedId(1,juce::dontSendNotification);voiceFraming.setName(tr(String::fromUTF8("Cadrage des hauteurs")));
 transferVoices.setButtonText(tr(String::fromUTF8("Transférer")));transferVoices.setClickingTogglesState(true);transferVoices.setTooltip(tr(String::fromUTF8("Activer puis glisser une note vers une autre voix. Hauteur et temps conservés. Échap annule.")));
 voicePresentation.onChange=[this]{stackedView=voicePresentation.getSelectedId()==2&&roll.displayMode==0;roll.stackHost=stackedView;if(stackedView)stacked->focusOnEntry();roll.cancelPointerEdit();stacked->transferring=false;stacked->cancelTransfer();edited();refresh();};
 voiceFraming.onChange=[this]{stacked->setCommon(voiceFraming.getSelectedId()==2);};
 transferVoices.onClick=[this]{stacked->transferring=transferVoices.getToggleState();stacked->cancelTransfer();};
 stacked->reloadAudio=[this](int key){reloadVoiceAudio(key);};
 stacked->activate=[this](int key){if(partId!=key){selectPart(key);refreshStacked();}};
 stacked->instrument=[this](int key){if(partId!=key)selectPart(key);partDialog(false,key);};
 stacked->colour=[this](int key){if(partId!=key)selectPart(key);showVoiceForm(false);};
 stacked->toggleVisible=[this](int key){if(!hiddenParts.erase(key))hiddenParts.insert(key);rebuildNotes();edited();refresh();};
 stacked->toggleMute=[this](int key){bool playing=bool(playback["playing"]);command(object({{"kind","toggle_part_mute"},{"part",key}}),[this,playing](var){if(playing)control("play");});};
 stacked->toggleLock=[this](int key){command(object({{"kind","toggle_part_lock"},{"part",key}}));};
 stacked->level=[this](int key,double db){voiceLevels.getDynamicObject()->setProperty(String(key),db);edited();syncVoiceLevels();};
 stacked->transfer=[this](const auto& ids,int key){if(!stacked->allowed(key,ids))return;command(object({{"kind","transfer"},{"ids",idsVar(ids)},{"program",key}}),[this,ids,key](var){if(partId!=key)selectPart(key);roll.selectIds(ids);refresh();});};
 stacked->changed=[this]{edited();voiceFraming.setSelectedId(stacked->common?2:1,juce::dontSendNotification);transferVoices.setToggleState(stacked->transferring,juce::dontSendNotification);};
}
void App::refreshStacked(){if(!stacked)return;
 bool requested=voicePresentation.getSelectedId()==2&&roll.displayMode==0;
 if(requested!=stackedView){roll.cancelPointerEdit();stacked->cancelTransfer();stacked->transferring=false;if(requested)stacked->focusOnEntry();}
 stackedView=requested;roll.stackHost=requested;voicePresentation.setVisible(roll.displayMode==0);voicePresentation.setEnabled(!locked);voiceFraming.setEnabled(!locked);transferVoices.setEnabled(!locked&&notesReady);transferVoices.setToggleState(stacked->transferring,juce::dontSendNotification);
 if(stackedView){stacked->audioSources.clear();for(auto& p:parts)stacked->audioSources[p.first]=voiceSourcePath(p.first);stacked->sync(parts,partId,hiddenParts,voiceLevels,locked||!notesReady);for(int key:stacked->order)stacked->editorFor(key).addKeyListener(this);}
}
void App::layoutStacked(){if(!stacked)return;voicePresentation.setVisible(roll.displayMode==0);stacked->setVisible(stackedView);if(!stackedView)return;auto l=roll.layout();stacked->setBounds(0,int(l.rollTop)+8,roll.getWidth(),std::max(0,int(l.dividerKeys-l.rollTop)-8));stacked->toFront(false);if(midiCounting)countOverlay.toFront(false);

}

Roll& StackedVoices::editorFor(int key){return lanes.at(key)->editor;}
juce::Component& StackedVoices::laneFor(int key){return *lanes.at(key);}
