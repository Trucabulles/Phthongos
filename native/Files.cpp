#include "App.h"
namespace {
var put(var a, const String &key, var value) {
  a = a.clone();
  a.getDynamicObject()->setProperty(key, value);
  return a;
}
} // namespace
void App::ask(const String &titleText, const String &message,
              const String &action, const String &discard,
              std::function<void(int)> fn) {
  if (confirmation)
    return;
  confirmation =
      std::make_unique<ConfirmOverlay>(titleText, message, action, discard);
  addAndMakeVisible(*confirmation);
  confirmation->setBounds(getLocalBounds());
  confirmation->setAlwaysOnTop(true);
  confirmation->toFront(false);
  auto answered=std::make_shared<bool>(false);
  confirmation->answer = [s = safe(), fn,answered](int answer) {
    if (!s || *answered)
      return;
    *answered=true;
    s->confirmation->exitModalState(answer);
    juce::MessageManager::callAsync([s, fn, answer] {
      if (s) {
        s->confirmation.reset();
        fn(answer);
      }
    });
  };
  confirmation->enterModalState(false);
  confirmation->focusCancel();
}
void App::confirm(const String &titleText, const String &message,
                  std::function<void(bool)> fn) {
  ask(titleText, message, tr(String::fromUTF8("Continuer")), {},
      [fn](int answer) { fn(answer == 1); });
}
void App::permitReplace(std::function<void()> fn,std::function<void()> cancelled) {
  if (!dirty) {
    fn();
    return;
  }
  ask(tr(String::fromUTF8("Projet modifié")),
      tr(String::fromUTF8("Enregistrer votre projet avant de remplacer la session ?")), tr(String::fromUTF8("Enregistrer")),
      tr(String::fromUTF8("Continuer sans enregistrer")), [s = safe(), fn,cancelled](int answer) {
        if (!s)
          return;
        if (answer == 1)
          s->saveProject(false, [fn,cancelled](bool ok) {
            if (ok)fn();else if(cancelled)cancelled();
          });
        else if (answer == 2)
          fn();
        else if(cancelled)cancelled();
      });
}
void App::requestClose(std::function<void()> fn) {
  if(midiRecording){stopMidiRecording([this,fn]{requestClose(fn);});return;}
  if (locked || closing){phthongos::diagnostics::event("close_deferred",locked?"busy":"already_closing");return;}
  closing = true;
  permitReplace([this, fn] {
    closing = false;
    fn();
  },[this]{closing=false;});
}
void App::choose(const String &label, bool saving, const String &filter,
                 std::function<void(String)> fn, String initial,std::function<void()> cancelled) {
  if(saving&&testSaveDestination){auto result=testSaveDestination(initial);juce::MessageManager::callAsync([s=safe(),fn,cancelled,result]{if(!s)return;if(result!=juce::File())fn(result.getFullPathName());else if(cancelled)cancelled();});return;}
  chooser = std::make_unique<juce::FileChooser>(
      label, initial.isEmpty() ? juce::File() : juce::File(initial), filter,
      true);
  auto flags = saving ? juce::FileBrowserComponent::saveMode |
                            juce::FileBrowserComponent::canSelectFiles |
                            juce::FileBrowserComponent::warnAboutOverwriting
                      : juce::FileBrowserComponent::openMode |
                            juce::FileBrowserComponent::canSelectFiles;
  auto s = safe();
  chooser->launchAsync(flags, [s, fn,cancelled](const juce::FileChooser &c) {
    if (s && c.getResult() != juce::File())
      fn(c.getResult().getFullPathName());
    else if(s&&cancelled)cancelled();
  });
}
juce::PopupMenu App::projectMenu() {
  juce::PopupMenu menu;
  menu.addItem(10,tr(String::fromUTF8("Nouveau projet")),!locked&&bool(newProjectRequested));
  menu.addSeparator();
  menu.addItem(1, tr(juce::String::fromUTF8("Importer un MIDI…")), !locked);
  menu.addItem(8,tr(juce::String::fromUTF8("Ajouter un MIDI au projet…")),!locked && !result.isVoid());
  if (!result.isVoid())
    menu.addItem(2,
                 (bool)audio["midi_only"]
                     ? tr(juce::String::fromUTF8("Associer un audio source…"))
                     : tr(juce::String::fromUTF8("Remplacer l’audio source…")),
                 !locked);
  menu.addItem(3, tr(juce::String::fromUTF8("Ouvrir un projet…")), !locked);
  loadRecentProjects();juce::PopupMenu recent;
  for(int i=0;i<(int)recentProjects.size();++i){auto entry=recentProjects[i];auto path=juce::File(entry["path"].toString());auto modified=juce::Time(static_cast<juce::int64>(entry["modified"]));
    auto label=path.getFileName()+" — ";if(modified.toMilliseconds()>0)label+=modified.formatted("%Y-%m-%d %H:%M")+" — ";label+=path.getParentDirectory().getFullPathName();recent.addItem(100+i,label,path.existsAsFile());}
  if(recentProjects.empty())recent.addItem(0,tr(String::fromUTF8("Aucun projet récent")),false);
  recent.addSeparator();recent.addItem(110,tr(String::fromUTF8("Effacer la liste des projets récents")),!recentProjects.empty());
  menu.addSubMenu(tr(String::fromUTF8("Projets récents")),recent,!locked);
  menu.addItem(4, tr(String::fromUTF8("Enregistrer      Ctrl+S")), !locked && !audio.isVoid());
  menu.addItem(5, tr(juce::String::fromUTF8("Enregistrer sous…      Ctrl+Maj+S")),
               !locked && !audio.isVoid());
  menu.addItem(11,tr(String::fromUTF8("Renommer le projet…")),!locked&&!audio.isVoid());
  menu.addItem(6, tr(juce::String::fromUTF8("Exporter en WAV…")),
               !locked && !result.isVoid());
  menu.addSeparator();
  menu.addItem(12,tr(String::fromUTF8("Paramètres…")),!locked);
  menu.addItem(13,tr(String::fromUTF8("Manuel utilisateur (PDF)")));
  menu.addItem(7,tr(String::fromUTF8("Crédits et licences")));
  return menu;
}
void App::fileMenu() {
  auto menu=projectMenu();
  auto s=safe();
  menu.showMenuAsync(
      juce::PopupMenu::Options().withTargetComponent(buttonMap["file"]),
      [s,recents=recentProjects](int id) {
        if (!s)return;
        if(id>=100&&id<100+(int)recents.size()){s->openProject(recents[id-100]["path"].toString());return;}
        if(id==110){s->recentProjects.clear();if(!s->storeRecentProjects())s->status.setText(tr(String::fromUTF8("Enregistrement des préférences impossible.")),juce::dontSendNotification);return;}
        switch (id) {
        case 11:
          s->renameProject();
          break;
        case 10:
          if(s->newProjectRequested)s->requestClose(s->newProjectRequested);
          break;
        case 8:
          s->appendMidi();
          break;
        case 1:
          s->importMidi();
          break;
        case 2:
          s->associateAudio();
          break;
        case 3:
          s->openProject();
          break;
        case 4:
          s->saveProject();
          break;
        case 5:
          s->saveProject(true);
          break;
        case 6:
          s->exportWav();
          break;
        case 12:s->settings();break;
        case 13:s->openManual();break;
        case 7:
          s->information();
          break;
        }
      });
}
void App::openAudio() {
  if (locked)
    return;
  if(!items(result["editing"]["notes"]).empty()){associateAudio();return;}
  permitReplace([this] {
    choose(tr(String::fromUTF8("Ouvrir un audio")), false, "*.wav;*.mp3;*.flac;*.m4a",
           [this](String path) { openAudioPath(path); });
  });
}
void App::importMidi(const String& requested) {
  if (locked)
    return;
  auto open=[this](String path) {
           permitReplace([this, path] {
             setBusy(true, tr(juce::String::fromUTF8("Import MIDI…")));
             control("pause");
             invoke(
                 "import_midi_audio",
                 object({{"midiPath", path}, {"audioPath", var()}}),
                 [this](var v) {
                   audio = v["audio"];
                   refreshTempo();
                   referenceAudio = false;
                   loadGridUi(var());
                   roll.audio = audio;
                   projectPath = String();projectSaveBase={};projectSaveFolder={};projectIdentity=juce::Uuid().toString();
                   projectName=juce::File(audio["name"].toString()).getFileNameWithoutExtension();
                   roll.view = View();
                   roll.clearSelection();
                   roll.clearDetail();
                   verification = var();
                   clean = var();
                   dynamics = var();
                   hasDynDraft = false;
                   partId = -1;
                   spectralBands=object({});loadSpectralBand();
                   hiddenParts.clear();legacyVisiblePart=-1;voiceLevels=object({});
                   mode = 1;
                   updateResult(v["result"]);
                   prepare([this](var) {
                     setBusy(false, tr(juce::String::fromUTF8(
                                        "MIDI importé.")));
                   });
                   resized();
                 },
                 true, [this](String) { setBusy(false); });
           });
         };
  if(requested.isNotEmpty())open(requested);
  else choose(tr(juce::String::fromUTF8("Choisir le MIDI à éditer")),false,"*.mid;*.midi",open);
}
void App::associateAudio(const String& requested,int sourcePart) {
  if (locked || result.isVoid())
    return;
  auto open=[this,sourcePart](String path) {
           double p = number(playback, "position");auto originalName=projectDisplayName();
           setBusy(true);
           control("pause");
           invoke(
               "associate_audio", sourcePart<0?put(args(), "path", path):put(put(args(), "path", path),"sourcePart",sourcePart),
               [this, p,originalName](var v) {
                 if(projectName.isEmpty())projectName=originalName;
                 audio = v["audio"];
                   refreshTempo();
                 referenceAudio = true;
                 roll.audio = audio;
                 roll.clearDetail();
                 if (!verification.isVoid())
                   verification.getDynamicObject()->setProperty("audio_id",
                                                                audio["id"]);
                 updateResult(v["result"]);
                 prepare([this, p](var) {
                   control("seek", p);
                   setBusy(false,
                           tr(juce::String::fromUTF8(
                               "Audio de référence remplacé · MIDI, mesures et "
                               "corrections conservés. Aucun recalcul.")));
                 });
               },
               true, [this](String) { setBusy(false); });
         };
  if(requested.isNotEmpty())open(requested);
  else choose((bool)audio["midi_only"] ? tr(String::fromUTF8("Associer un audio source"))
             : tr(String::fromUTF8("Remplacer l’audio source")),false,"*.wav;*.mp3;*.flac;*.m4a",open);
}
var App::ui() const {
  auto savedDynamics=dynMapping.clone();
  // Existing sessions may hold whole-valued doubles from the former Min/Max
  // controls. Canonicalise their type without rounding invalid values or
  // changing the editing draft, notes, or applied dynamics.
  if(auto* mapping=savedDynamics.getDynamicObject())for(auto* key:{"min","max"}){
    auto value=mapping->getProperty(key);double n=double(value);
    if(value.isDouble()&&std::isfinite(n)&&n>=1&&n<=127&&n==std::floor(n))mapping->setProperty(key,int(n));
  }
  return object(
      {{"basicPitchExpanded",pitchControlsExpanded},{"transcription",engine["settings"].isObject()?object({{"engine",engine["settings"]["engine"]},{"model",engine["settings"]["model"]},{"basic_pitch",engine["settings"]["basic_pitch"].clone()}}):var()},{"projectName",projectName},{"stackedView",voicePresentation.getSelectedId()==2},{"stackedVoices",stacked?stacked->state():var()},{"metronome",object({{"enabled",clickEnabled},{"db",clickDb},{"preCount",preCountBars}})},{"midiTempoEstimate",midiEstimateAudio==audio["id"]?midiTempoEstimate:var()},{"midiEstimatePending",midiEstimatePending},{"projectIdentity",projectIdentity},{"saveFolder",projectSaveFolder},{"saveBaseName",projectSaveBase},{"rhythmicGrid",object({{"mode",String(roll.rhythmicGrid.mode==0?"musical":roll.rhythmicGrid.mode==1?"seconds":"none")},{"subdivision",gridSubdivision.getText()},{"offset",roll.rhythmicGrid.origin}})},{"guided",object({{"use_mask",guidedUseMask},{"zone",guidedZone},{"brush",roll.brushSemitones},{"visible",roll.maskVisible},{"curve",guidedShowCurve},{"proposals",guidedShowProposals},{"use_scale",guidedUseScale},{"show_probable",guidedCategories.count("probable")>0},{"show_review",guidedCategories.count("review")>0},{"show_unlikely",guidedCategories.count("unlikely")>0}})},{"linkOverlapMs",linkOverlapMs},{"spectralPalette",spectralPalette},{"spectralStyle",spectralStyle},{"spectralContrast",spectralContrast},{"spectralBands",spectralBands},
       {"displayMode",roll.displayMode},{"spectral",roll.spectral},{"opacity",roll.overlayOpacity},{"visualGain",roll.visualGain},{"waveReferenceZoom",roll.waveReferenceZoom},{"waveCalibrated",roll.waveCalibrated},
       {"waveOpacity",roll.spectral?waveOpacity:opacity.getValue()/100.},
       {"waveVisualGain",roll.spectral?waveVisualGain:roll.visualGain},
       {"referenceAudio", referenceAudio},
       {"dynamicsDraft", hasDynDraft ? object({{"mapping", savedDynamics},
                                               {"manualRange", manualRange}})
                                     : var()},
       {"partTarget", partId < 0 ? String("all") : String(partId)},
       {"visiblePart","all"},{"hiddenParts",idsVar(hiddenParts)},{"voiceLevels",voiceLevels.clone()},
       {"profile", "versatile"},
       {"sensitivity", number(result, "sensitivity", 100)},
       {"position", number(playback, "position")},
       {"reverbEnabled", reverbEnabled},
       {"reverbAmount", reverbAmount},
       {"mode", mode},
       {"audioDb", audioDb},
       {"midiDb", midiDb},
       {"follow", follow.getToggleState()},
       {"view", roll.view.json()},
       {"tone", tone},
       {"short", shortState},
       {"selection", roll.selection},
       {"correctionOpen", correctionOpen},
       {"tonalityOpen", tonalityOpen}});
}
void App::loadUi(const var &v) {
  stackedView=bool(v["stackedView"]);roll.stackHost=stackedView;if(stacked){stacked->load(v["stackedVoices"]);voicePresentation.setSelectedId(stackedView?2:1,juce::dontSendNotification);voiceFraming.setSelectedId(stacked->common?2:1,juce::dontSendNotification);}
  auto click=v["metronome"];clickEnabled=bool(click["enabled"]);clickDb=number(click,"db",-12);if(!std::isfinite(clickDb)||clickDb< -60||clickDb>0)clickDb=-12;preCountBars=juce::jlimit(0,2,int(number(click,"preCount",1)));
  midiTempoEstimate=var();midiEstimateAudio=audio["id"];midiEstimatePending=false;
  auto estimate=v["midiTempoEstimate"];double bpm=number(estimate,"bpm",-1);
  if(estimate.isObject()&&std::isfinite(bpm)&&bpm>=0&&bpm<=600){midiTempoEstimate=estimate.clone();midiEstimatePending=bool(v["midiEstimatePending"]);}

  if(v["basicPitchExpanded"].isBool())pitchControlsExpanded=bool(v["basicPitchExpanded"]);
  projectIdentity=v["projectIdentity"].isString()&&v["projectIdentity"].toString().length()<=64&&v["projectIdentity"].toString().isNotEmpty()?v["projectIdentity"].toString():juce::Uuid().toString();
  auto savedName=v["projectName"].toString().trim();
  projectName=savedName.length()<=120&&!savedName.containsAnyOf("\r\n")?savedName:String();
  auto savedFolder=v["saveFolder"].toString();projectSaveFolder=juce::File::isAbsolutePath(savedFolder)?savedFolder:String();
  auto base=v["saveBaseName"].toString();
  projectSaveBase=base.length()<=255&&!base.containsAnyOf("\\/:*?\"<>|\r\n")&&base!="."&&base!=".."?base:String();
  loadGridUi(v["rhythmicGrid"]);
  auto guidance=v["guided"];guidedUseMask=guidance.isObject()?(bool)guidance["use_mask"]:true;guidedZone=guidance.isObject()?guidance["zone"].toString():String("mask");guidedShowCurve=guidance.isObject()?(bool)guidance["curve"]:true;roll.brushSemitones=number(guidance,"brush",1);roll.maskVisible=guidance.isObject()?(bool)guidance["visible"]:true;roll.maskMode=0;

  guidedUseScale=(bool)guidance["use_scale"];guidedShowProposals=!guidance.hasProperty("proposals")||(bool)guidance["proposals"];guidedCategories.clear();
  if(!guidance.hasProperty("show_probable")||(bool)guidance["show_probable"])guidedCategories.insert("probable");
  if(!guidance.hasProperty("show_review")||(bool)guidance["show_review"])guidedCategories.insert("review");
  if((bool)guidance["show_unlikely"])guidedCategories.insert("unlikely");
  linkOverlapMs=View::clamp(number(v,"linkOverlapMs",20),0,500);
  spectralPalette=juce::jlimit(0,2,(int)number(v,"spectralPalette",0));
  spectralStyle=juce::jlimit(0,1,(int)number(v,"spectralStyle",0));spectralContrast=View::clamp(number(v,"spectralContrast",100),0,100);
  spectralBands=v["spectralBands"].isObject()?v["spectralBands"].clone():object({});
  recolourSpectrum();
  roll.displayMode=juce::jlimit(0,2,(int)number(v,"displayMode",1));
  stackedView=voicePresentation.getSelectedId()==2&&roll.displayMode==0;roll.stackHost=stackedView;
  roll.spectral=(bool)v["spectral"]; roll.overlayOpacity=(float)View::clamp(number(v,"opacity",.2),0,1);
  roll.visualGain=View::clamp(number(v,"visualGain",4.9),.25,8);
  waveOpacity=View::clamp(number(v,"waveOpacity",roll.spectral?.2:roll.overlayOpacity),0,1);
  waveVisualGain=View::clamp(number(v,"waveVisualGain",roll.spectral?4.9:roll.visualGain),.25,8);
  displayMode.setSelectedId(roll.displayMode+1,juce::dontSendNotification);
  audioLayer.setSelectedId(roll.spectral?2:1,juce::dontSendNotification);
  opacity.setValue(roll.overlayOpacity*100,juce::dontSendNotification);
  visualGain.setValue(roll.visualGain,juce::dontSendNotification);
  opacity.setVisible(roll.displayMode==2);audioLayer.setVisible(roll.displayMode!=0);visualGain.setVisible(roll.displayMode==2);
  referenceAudio = (bool)v["referenceAudio"];
  roll.view.load(v["view"]);
  roll.waveReferenceZoom=View::clamp(number(v,"waveReferenceZoom",roll.view.pitchZoom),1,64);
  roll.waveCalibrated=v["waveCalibrated"].isVoid()?true:(bool)v["waveCalibrated"];
  partId = v["partTarget"].toString() == "all"
               ? -1
               : v["partTarget"].toString().getIntValue();
  voiceLevels=object({});if(auto* levels=v["voiceLevels"].getDynamicObject())for(const auto& entry:levels->getProperties()){auto db=double(entry.value);if(std::isfinite(db)&&db>=-60&&db<=0)voiceLevels.getDynamicObject()->setProperty(entry.name,db);}
  hiddenParts.clear();for(auto key:idSet(v["hiddenParts"]))if(key>=0&&(key<=128||(key>=256&&key<32768)))hiddenParts.insert(key);
  legacyVisiblePart=!v.hasProperty("hiddenParts")&&v["visiblePart"].toString()!="all"?partId:-1;
  loadSpectralBand();
  others.setToggleState(!hiddenParts.count(partId), juce::dontSendNotification);
  mode = (int)v["mode"];
  audioDb = number(v, "audioDb");
  midiDb = number(v, "midiDb");
  audioVolume.setValue(audioDb, juce::dontSendNotification);
  midiVolume.setValue(midiDb, juce::dontSendNotification);
  follow.setToggleState((bool)v["follow"], juce::dontSendNotification);
  roll.follow = follow.getToggleState();
  tone = v["tone"];
  if (!v["short"].isVoid())
    shortState = v["short"];
  reverbEnabled = (bool)v["reverbEnabled"];
  reverbAmount = number(v, "reverbAmount", 50);
  hasDynDraft = !v["dynamicsDraft"].isVoid();
  if (hasDynDraft) {
    dynMapping = v["dynamicsDraft"]["mapping"];
    manualRange = (bool)v["dynamicsDraft"]["manualRange"];
  }
  roll.selection = v["selection"];
  roll.selected = idSet(v["selection"]["ids"]);
  correctionOpen = (bool)v["correctionOpen"];
  tonalityOpen = (bool)v["tonalityOpen"];
  if (buttonMap.count("toneSuggest") || tonalityOpen) toneOptions();
  resized();
  repaint();
}
void App::projectSaveFailed(const String& error,std::function<void(bool)> done){
  setBusy(false,error);
  juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,tr(juce::String::fromUTF8("Enregistrement impossible")),error);
  if(done)done(false);
}
void App::loadRecentProjects(){
  recentProjects.clear();auto entries=juce::JSON::parse(savePreferencesFile.getSiblingFile("recent-projects.json").loadFileAsString());
  for(const auto& entry:items(entries)){
    auto path=entry["path"].toString();if(!juce::File::isAbsolutePath(path))continue;auto file=juce::File(path);
    if(!file.hasFileExtension("pht")||std::any_of(recentProjects.begin(),recentProjects.end(),[&](const var& e){return juce::File(e["path"].toString())==file;}))continue;
    auto modified=file.existsAsFile()?file.getLastModificationTime().toMilliseconds():static_cast<juce::int64>(entry["modified"]);
    recentProjects.push_back(object({{"path",file.getFullPathName()},{"name",file.getFileName()},{"modified",modified}}));
  }
  std::stable_sort(recentProjects.begin(),recentProjects.end(),[](const var& a,const var& b){return static_cast<juce::int64>(a["modified"])>static_cast<juce::int64>(b["modified"]);});
  if(recentProjects.size()>10)recentProjects.resize(10);
}
bool App::storeRecentProjects(){
  auto file=savePreferencesFile.getSiblingFile("recent-projects.json");if(file.getParentDirectory().createDirectory().failed())return false;
  juce::TemporaryFile temp(file);return temp.getFile().replaceWithText(juce::JSON::toString(array(recentProjects)))&&temp.overwriteTargetFileWithTemporary();
}
void App::rememberProject(const String& path){
  loadRecentProjects();auto file=juce::File(path);
  recentProjects.erase(std::remove_if(recentProjects.begin(),recentProjects.end(),[&](const var& e){return juce::File(e["path"].toString())==file;}),recentProjects.end());
  recentProjects.insert(recentProjects.begin(),object({{"path",file.getFullPathName()},{"name",file.getFileName()},{"modified",file.getLastModificationTime().toMilliseconds()}}));
  std::stable_sort(recentProjects.begin(),recentProjects.end(),[](const var& a,const var& b){return static_cast<juce::int64>(a["modified"])>static_cast<juce::int64>(b["modified"]);});if(recentProjects.size()>10)recentProjects.resize(10);
  if(!storeRecentProjects())status.setText(tr(String::fromUTF8("Projet accessible, mais la liste des projets récents n’a pas pu être enregistrée.")),juce::dontSendNotification);
}
juce::File App::versionedProjectPath(const juce::File& folder,const String& base,juce::Time time){
  auto stem=base+time.formatted("_%Y_%m_%d_%H%M%S");auto path=folder.getChildFile(stem+".pht");
  for(int i=2;path.exists();++i)path=folder.getChildFile(stem+"_"+String(i).paddedLeft('0',2)+".pht");
  return path;
}
String App::projectSaveSuggestion(const var& info) const {
  if(projectPath.isNotEmpty())return projectSaveBase.isNotEmpty()?juce::File(projectPath).getSiblingFile(projectSaveBase+".pht").getFullPathName():projectPath;
  if(projectSaveFolder.isNotEmpty()&&projectSaveBase.isNotEmpty()&&juce::File(projectSaveFolder).isDirectory())return juce::File(projectSaveFolder).getChildFile(projectSaveBase+".pht").getFullPathName();
  auto suggested=info["suggested_path"].toString();
  if(projectName.isNotEmpty())suggested=juce::File(suggested).getSiblingFile(midiExportName()+".pht").getFullPathName();
  if(defaultProjectFolder.isNotEmpty()&&juce::File(defaultProjectFolder).isDirectory())return juce::File(defaultProjectFolder).getChildFile(juce::File(suggested).getFileName()).getFullPathName();
  return suggested;
}
void App::chooseProjectDestination(const String& initial,std::function<void(bool)> done,bool versioned){
  choose(tr(juce::String::fromUTF8("Enregistrer le projet en PHT8")),true,"*.pht",
    [s=safe(),done,versioned](String path){if(s)s->saveProjectTo(path,done,versioned);},initial,
    [done]{if(done)done(false);});
}
void App::saveProject(bool as,std::function<void(bool)> done){
  if(locked||audio.isVoid()){if(done)done(false);return;}
  invoke("project_save_info",object({}),[this,as,done](var info){
    if(as||projectPath.isEmpty())chooseProjectDestination(projectSaveSuggestion(info),done,!as&&timestampSaves);
    else saveProjectTo(projectPath,done,timestampSaves);
  },false,[this,done](String e){projectSaveFailed(e,done);});
}
void App::saveProjectTo(const String& requested,std::function<void(bool)> done,bool versioned){
  auto base=requested==projectPath&&projectSaveBase.isNotEmpty()?projectSaveBase:juce::File(requested).getFileNameWithoutExtension();
  auto path=versioned?versionedProjectPath(juce::File(requested).getParentDirectory(),base,juce::Time::getCurrentTime()).getFullPathName():requested;
  if(locked||audio.isVoid()){if(done)done(false);return;}
  invoke("project_save_info",object({}),[this,path,base,done,versioned](var info){
    auto original=info["protected_path"].toString();
    if(original.isNotEmpty()&&juce::File(path)==juce::File(original)){
      ask(tr(juce::String::fromUTF8("Enregistrer en PHT8")),
          tr(juce::String::fromUTF8("Le projet d’origine est conservé. Choisissez un nouveau nom pour enregistrer votre travail en PHT8.")),
          tr(juce::String::fromUTF8("Enregistrer sous…")),{},[s=safe(),done,info](int answer){
            if(!s)return;if(answer==1)s->chooseProjectDestination(info["suggested_path"].toString(),done);else if(done)done(false);
          });return;
    }
    setBusy(true,tr(juce::String::fromUTF8("Enregistrement…")));
    invoke("player_snapshot",object({}),[this,path,base,done,versioned](var snapshot){
      playback=snapshot;
      auto a=put(put(put(args(),"path",path),"create_only",versioned),"ui",put(put(ui(),"saveBaseName",base),"saveFolder",juce::File(path).getParentDirectory().getFullPathName()));
      if(result.isVoid()){a=put(a,"analysisId",0);a=put(a,"revision",0);}
      invoke("save_project",a,[this,path,base,done](var){
        projectPath=path;projectSaveBase=base;projectSaveFolder=juce::File(path).getParentDirectory().getFullPathName();dirty=false;setBusy(false,tr(juce::String::fromUTF8("Projet enregistré · "))+path);
        rememberProject(path);
        if(done)done(true);
      },true,[this,done](String e){projectSaveFailed(e,done);});
    },false,[this,done](String e){projectSaveFailed(e,done);});
  },false,[this,done](String e){projectSaveFailed(e,done);});
}
void App::openProject(String requested) {
  if (locked)
    return;
  permitReplace([this,requested] {
    auto open=[this](String path) {
      setBusy(true, tr(juce::String::fromUTF8("Restauration du projet…")));
      control("pause");
      invoke(
          "open_project", object({{"path", path}}),
          [this, path](var v) {
            audio = v["audio"];
                   refreshTempo();
            roll.audio = audio;
            roll.clearDetail();
            verification = var();
            clean = var();
            dynamics = var();
            toneReport = var();
            shortReport = var();
            loadUi(v["ui"]);
            updateResult(v["result"]);
            restoreTranscription(v["ui"],v["result"]);
            const bool upgraded=(int)v["upgraded_from"]>0&&(int)v["upgraded_from"]<8;
            projectPath = upgraded?String():path;
            if(projectName.isEmpty())projectName=projectSaveBase.isNotEmpty()?projectSaveBase:juce::File(path).getFileNameWithoutExtension();
            rememberProject(path);
            double p = number(v["ui"], "position");
            prepare([this, p,upgraded](var) {
              control("seek", p, [this,upgraded](var) {
                dirty = upgraded;
                setBusy(false,
                        upgraded?tr(juce::String::fromUTF8("Projet restauré en pause · Enregistrer sous pour créer sa version PHT8.")):tr(juce::String::fromUTF8("Projet restauré en pause.")));
                restoreDynamics();
                if ((bool)tone["enabled"])
                  analyzeTone(false);
                if ((bool)shortState["enabled"])
                  findShort();
              });
            });
            resized();
          },
          true, [this](String error) {
            setBusy(false,error);
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,tr(juce::String::fromUTF8("Ouverture du projet impossible")),error);
          });
    };
    if(requested.isNotEmpty())open(requested);
    else choose(tr(String::fromUTF8("Ouvrir un projet Phthongos")),false,"*.pht",open);
  });
}
void App::exportMidi() {
  if(locked||result.isVoid())return;
  midiExportForm()->show(tr(String::fromUTF8("Exporter MIDI")));
}
Form* App::midiExportForm() {
  auto f=juce::Component::SafePointer<Form>(new Form);
  f->actions(tr(String::fromUTF8("Exporter MIDI")),tr(String::fromUTF8("Annuler")));
  std::set<int> audible;for(auto value:items(result["editing"]["notes"])){auto n=Note::parse(value);if(!n.muted)audible.insert(n.part);}
  const bool multiple=audible.size()>1;
  std::vector<std::pair<String,var>> choices{{(multiple?tr(String::fromUTF8("Un fichier par voix")):tr(String::fromUTF8("Toutes les voix"))),multiple?"separate":"single"}};
  if(partId>=0&&multiple)choices.emplace_back(tr(String::fromUTF8("Voix sélectionnée"))+" : "+part.getText(),"selected");
  f->combo(tr(String::fromUTF8("Contenu de l’export")),"scope",choices,multiple?"separate":"single");
  f->label(tr(String::fromUTF8("BPM et positions temporelles conservés. Les voix muettes ne sont pas exportées.")));
  f->onApply=[s=safe(),f](var state){if(!s||s->locked)return;auto scope=state["scope"].toString();auto selected=s->partId;f->close();
    juce::MessageManager::callAsync([s,scope,selected]{if(!s||s->locked||s->result.isVoid())return;
    if(scope=="separate"){
      s->chooser=std::make_unique<juce::FileChooser>(tr(String::fromUTF8("Choisir le dossier parent des voix MIDI")),juce::File(),"",true);
      s->chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[s](const juce::FileChooser& c){if(!s||c.getResult()==juce::File())return;
        auto folder=c.getResult().getNonexistentChildFile(s->midiExportName()+"-MIDI","");s->exportMidiVoicesTo(folder.getFullPathName());});return;}
    s->invoke("suggest_export_path",put(s->args(),"name",s->midiExportName()),[s,scope,selected](var suggested){if(!s)return;
      s->choose(tr(String::fromUTF8("Exporter MIDI")),true,"*.mid;*.midi",[s,scope,selected](String path){if(s)s->exportMidiTo(path,scope=="selected"?var(selected):var());},suggested.toString());});
    });
  };
  return f.getComponent();
}
void App::exportMidiTo(const String& path,var selected) {
  setBusy(true,tr(String::fromUTF8("Export MIDI…")));
  invoke("export_midi",put(put(args(),"path",path),"part",selected),[this,path](var count){setBusy(false,localisation::count("notes_exported",(int)count)+String::fromUTF8(" · ")+path);},true,[this](String){setBusy(false);});
}
void App::exportMidiVoicesTo(const String& folder) {
  setBusy(true,tr(String::fromUTF8("Export MIDI…")));
  invoke("export_midi_voices",put(put(args(),"path",folder),"name",midiExportName()),[this,folder](var receipt){setBusy(false,localisation::count("notes_exported",(int)receipt["notes"])+String::fromUTF8(" · ")+folder);},true,[this](String){setBusy(false);});
}
void App::exportWav() {
  if (locked || result.isVoid())return;
  choose(tr(String::fromUTF8("Exporter le rendu MIDI en WAV")),true,"*.wav",
    [this](String path){
      setBusy(true,tr(String::fromUTF8("Export WAV…")));
      auto failed=[this](String error){
        phthongos::diagnostics::event("wav_export_failed",error);
        setBusy(false,error);
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,tr(String::fromUTF8("Export WAV impossible")),error);
      };
      auto request=put(put(args(),"path",path),"options",object({{"part",partId<0?var():var(partId)},{"midiDb",midiDb},{"reverb",reverbEnabled?reverbAmount:0}}));
      // Await pause before rendering so its callback cannot obscure the result.
      invoke("player_control",put(put(args(),"action","pause"),"position",0.),
        [this,path,request,failed](var){
          invoke("export_wav",request,[this,path,failed](var receipt){
            if(!juce::File(path).existsAsFile()||juce::File(path).getSize()<=44){failed(tr(String::fromUTF8("Le rendu est terminé mais le fichier WAV est introuvable ou vide.")));return;}
            setBusy(false,tr(String::fromUTF8("WAV exporté · "))+String(number(receipt,"duration"),1)+String::fromUTF8(" s · ")+path+
              (receipt["notice"].toString().isNotEmpty()?String::fromUTF8(" · ")+receipt["notice"].toString():String())+
              (number(receipt,"clipped_samples")>0?tr(String::fromUTF8(" · Saturation : réduisez le volume MIDI.")):String()));
          },true,failed);
        },false,failed);
    },juce::File(audio["name"].toString()).getFileNameWithoutExtension()+" - MIDI "+juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S")+".wav");
}
void App::audioCalculation(const String &kind, std::function<void()> fn) {
  if(kind=="verification"&&items(result["editing"]["notes"]).empty()){fn();return;}
  invoke(
      "audio_calculation_status",
      object({{"audioId", audio["id"]}, {"kind", kind}}),
      [this, fn](var comparison) {
        if (comparison["matched"].isBool() && (bool)comparison["matched"]) {
          fn();
          return;
        }
        bool differs =
            comparison["matched"].isBool() && !(bool)comparison["matched"];
        if (!differs && result["engine"].toString() != "midi" &&
            !referenceAudio) {
          fn();
          return;
        }
        String message =
            differs
                ? tr(juce::String::fromUTF8("Cet audio diffère de celui utilisé "
                                         "pour les mesures conservées (« ")) +
                      comparison["previous_name"].toString() +
                      juce::String::fromUTF8(" »).")
                : tr(juce::String::fromUTF8("Vérifiez que cet audio correspond "
                                         "aux notes MIDI à analyser."));
        confirm(
            tr(juce::String::fromUTF8("Vérifier l’audio avant le calcul")),
            message + tr(juce::String::fromUTF8("\nAudio actuel : « ")) +
                audio["name"].toString() +
                tr(juce::String::fromUTF8(
                    " ». Les corrections déjà appliquées restent conservées.")),
            [fn](bool ok) {
              if (ok)
                fn();
            });
      });
}
void App::analyze() {
  if(locked||(bool)audio["midi_only"]||audio.isVoid())return;
  bool append=!items(result["editing"]["notes"]).empty();
  auto start=[this,append]{
    setBusy(true,tr(String::fromUTF8("Préparation de la transcription…")));reportText="transcription";refresh();control("pause");
    invoke("analyze_transcription",object({{"audioId",audio["id"]},{"append",append}}),[this,append](var value){
      if(!append){partId=-1;hiddenParts.clear();legacyVisiblePart=-1;voiceLevels=object({});}
      auto added=items(value["transcribed_parts"]);if(!added.empty())partId=int(added.front());
      roll.clearSelection();verification=var();clean=var();dynamics=var();hasDynDraft=false;
      updateResult(value);syncPitchControls();reportText={};
      setBusy(false,append?tr(String::fromUTF8("Transcription ajoutée dans une nouvelle voix · Ctrl+Z pour annuler.")):tr(String::fromUTF8("Transcription terminée.")));resized();
    },true,[this](String){reportText={};setBusy(false);});
  };
  if(append)start();else audioCalculation("transcription",start);
}
namespace {
class CreditsPanel:public juce::Component {
 juce::ComboBox documents;juce::TextEditor text;std::vector<juce::File> files;String summary;
public:
 CreditsPanel(const juce::File& root){
  addAndMakeVisible(documents);addAndMakeVisible(text);documents.setName(tr(String::fromUTF8("Crédits et licences")));text.setMultiLine(true,true);text.setReadOnly(true);text.setScrollbarsShown(true);text.setCaretVisible(false);text.setFont(juce::FontOptions(14));
  text.getProperties().set("preserveLanguageText",true);
  summary=root.getChildFile("credits-"+localisation::code()+".txt").loadFileAsString();
  if(summary.isEmpty())summary=tr(String::fromUTF8("Consultez les textes de licence dans le dossier third-party livré avec Phthongos."));
  documents.addItem(tr(String::fromUTF8("Crédits et composants")),1);
  auto ownLicence=root.getChildFile("phthongos").getChildFile(localisation::code()=="fr"?"LICENSE_PHTHONGOS.md":"LICENSE_PHTHONGOS_EN.md");
  if(ownLicence.existsAsFile()){files.push_back(ownLicence);documents.addItem(tr(String::fromUTF8("Licence de Phthongos")),2);}
  auto candidates=root.findChildFiles(juce::File::findFiles,true,"*");candidates.sort();
  for(auto file:candidates){auto name=file.getFileName().toLowerCase(),relative=file.getRelativePathFrom(root).replaceCharacter('\\','/');
   if(relative.startsWith("sources/")||relative.startsWith("phthongos/")||relative.startsWith("lzma-native/xz-5.2/src/")||file.getSize()>2000000)continue;
   if(name.contains("license")||name.contains("licence")||name.contains("notice")||name.contains("copying")||name=="copyright"||name=="third-party.txt"||name=="cc0.txt"||name=="sample_sources.csv"||relative.startsWith("optional-models/")){
    files.push_back(file);documents.addItem(relative,int(files.size())+1);
   }
  }
  documents.onChange=[this]{int i=documents.getSelectedId()-2;text.setText(i>=0&&i<int(files.size())?files[i].loadFileAsString():summary,false);text.moveCaretToTop(false);};documents.setSelectedId(1,juce::sendNotificationSync);
 }
 void resized()override{documents.setBounds(0,0,getWidth(),30);text.setBounds(0,40,getWidth(),getHeight()-40);}
};
}
juce::File App::manualFile()const{
 auto root=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
 return root.getChildFile(localisation::code()=="fr"?"documentation/manuel/Phthongos_Manuel_utilisateur.pdf":"documentation/manuel/Phthongos_User_Manual.pdf");
}
void App::openManual(){auto file=manualFile();if(!file.existsAsFile()||!file.startAsProcess())status.setText(tr(String::fromUTF8("Impossible d’ouvrir le manuel PDF livré avec Phthongos.")),juce::dontSendNotification);}
void App::information(){
 auto f=juce::Component::SafePointer<Form>(new Form);
 auto root=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("third-party");
 f->custom(new CreditsPanel(root),460);
 f->actions(tr(String::fromUTF8("Fermer")),{});f->onApply=[f](var){if(f)f->close();};
 f->show(tr(String::fromUTF8("Crédits et licences")));
}

void App::openAudioPath(const String &path) {

  setBusy(true, tr(juce::String::fromUTF8("Ouverture de l’audio…")));
  control("pause");
  invoke(
      "open_audio", object({{"path", path}}),
      [this](var a) {
        audio = a;
        refreshTempo();
        referenceAudio = false;
                   loadGridUi(var());
        result = var();invalidateGuided();roll.spectralMask.clear();roll.maskMode=0;++roll.maskRevision;
        verification = var();
        updateDiagnostics();
        diagnosticActive.clear();roll.diagnosticGhost.clear();
        clean = var();
        dynamics = var();
        hasDynDraft = false;
        toneReport = var();
        shortReport = var();
        partId = -1;
        spectralBands=object({});loadSpectralBand();
        hiddenParts.clear();legacyVisiblePart=-1;voiceLevels=object({});
        projectPath = String();projectSaveBase={};projectSaveFolder={};projectIdentity=juce::Uuid().toString();
        projectName=juce::File(audio["name"].toString()).getFileNameWithoutExtension();
        roll.audio = a;
        roll.view = View();
        roll.clearDetail();
        roll.clearSelection();
        roll.notes.clear();roll.markers.clear();roll.markerDragging=false;
        roll.editable = false;
        roll.scoreDuration = number(a, "duration", 1);
        mode = 0;
        prepare([this](var) {
          setBusy(false,
                  tr(juce::String::fromUTF8(
                      "Audio prêt · dessinez des notes au crayon ou par double-clic, ou lancez Analyser.")));
        });
        dirty = true;
        refresh();
        resized();
        rebuildSide();
      },
      true, [this](String) { setBusy(false); });
}
