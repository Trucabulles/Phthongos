#include "App.h"
#include "SpectralPalettes.h"
namespace {
juce::File root() {
  return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
      .getParentDirectory();
}
var with(var a, const String &k, var v) {
  a = a.clone();
  a.getDynamicObject()->setProperty(k, v);
  return a;
}
void refreshVoiceMenu(juce::ComboBox& selector,const var& result,const Roll& roll){
 std::map<int,int> programs;for(const auto& p:items(result["midi_parts"]))programs[p["identity"].isVoid()?int(p["program"]):int(p["identity"])]=int(p["program"]);
 for(const auto& value:items(result["editing"]["notes"])){auto n=Note::parse(value);programs.emplace(n.part,n.program);}
 for(juce::PopupMenu::MenuItemIterator it(*selector.getRootMenu());it.next();){auto& item=it.getItem();if(item.itemID<2){item.image.reset();continue;}
  int key=item.itemID-2;auto dot=std::make_unique<juce::DrawablePath>();juce::Path circle;circle.addEllipse(0.f,0.f,12.f,12.f);dot->setPath(circle);dot->setFill(juce::FillType(roll.voiceColour(key,programs[key])));dot->setStrokeFill(juce::FillType(juce::Colour(0xff254962).withAlpha(.35f)));dot->setStrokeThickness(.8f);item.image=std::move(dot);
 }
}
} // namespace
App::App(juce::File testCache)
    : bridge(root().getChildFile("runtime"),
             testCache == juce::File() ? root().getChildFile("data/audio")
                                       : testCache) {
  savePreferencesFile=testCache!=juce::File()?testCache.getParentDirectory().getChildFile("save-preferences.json"):
      root().getChildFile("portable.flag").existsAsFile()?root().getChildFile("data/save-preferences.json"):
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Phthongos/save-preferences.json");
  loadSavePreferences();loadRecentProjects();
  setLookAndFeel(&theme);
  setWantsKeyboardFocus(true);
  addKeyListener(this);
  roll.addKeyListener(this);
  addAndMakeVisible(roll);
  addAndMakeVisible(beatIndicator);beatIndicator.setName(tr(String::fromUTF8("Pulsation")));
  roll.addChildComponent(countOverlay);countOverlay.setInterceptsMouseClicks(false,false);countOverlay.setJustificationType(juce::Justification::centred);countOverlay.setFont(juce::FontOptions(68,juce::Font::bold));countOverlay.setColour(juce::Label::textColourId,juce::Colour(0xccad793c));
  addAndMakeVisible(sideView);
  side.committed = [this] { rebuildSide(); };
  sideView.setViewedComponent(&side, false);
  for (auto *c : {&title, &status, &device, &position, &inspector,
                  &modelCaption, &calculation, &editorBadge})
    addAndMakeVisible(c);
  calculation.setFont(juce::FontOptions(16));
  calculation.setMinimumHorizontalScale(1);
  title.setInterceptsMouseClicks(false, false);
  calculation.setInterceptsMouseClicks(false, false);
  modelCaption.setInterceptsMouseClicks(false, false);
  title.setFont(juce::FontOptions(18, juce::Font::bold));
  title.setJustificationType(juce::Justification::centred);
  title.setMinimumHorizontalScale(1);
  editorBadge.setText(tr(String::fromUTF8("Édition MIDI")), juce::dontSendNotification);
  editorBadge.setFont(juce::FontOptions(14));
  editorBadge.setJustificationType(juce::Justification::centred);
  editorBadge.setInterceptsMouseClicks(false, false);
  modelCaption.setText(tr(juce::String::fromUTF8("MOTEUR")),
                       juce::dontSendNotification);
  modelCaption.setFont(juce::FontOptions(13));
  inspector.setFont(juce::FontOptions(15));
  inspector.setText(
      tr(juce::String::fromUTF8("Survolez une note pour ses détails · Cliquer "
                             "pour garder la sélection · Échap pour effacer")),
      juce::dontSendNotification);
  roll.inspect = [this](String text) {
    inspector.setText(text, juce::dontSendNotification);
  };
  status.setFont(juce::FontOptions(15));
  device.setFont(juce::FontOptions(14));
  device.addMouseListener(this, false);
  logo = juce::ImageFileFormat::loadFrom(
      root().getChildFile("player-assets/logo.png"));
  button("windowMin", tr(String::fromUTF8("Réduire")), [this] {
    if (auto *w = findParentComponentOfClass<juce::DocumentWindow>())
      w->setMinimised(true);
  });
  button("windowMax", tr(String::fromUTF8("Agrandir")), [this] {
    if (auto *w = findParentComponentOfClass<juce::DocumentWindow>())
      w->setFullScreen(!w->isFullScreen());
  });
  button("windowClose", tr(String::fromUTF8("Fermer")), [this] {
    requestClose([] { juce::JUCEApplication::getInstance()->quit(); });
  });
  button("help", "?", [this] {
    auto content = std::make_unique<juce::TextEditor>();
    content->setMultiLine(true); content->setReadOnly(true); content->setCaretVisible(false);
    content->setFont(juce::FontOptions(16));
    content->setColour(juce::TextEditor::backgroundColourId,theme.paper);
    content->setColour(juce::TextEditor::textColourId,theme.ink);
    content->setText(tr(String::fromUTF8(
      "RACCOURCIS ET GESTES\n\n"
      "R : démarrer / arrêter la prise MIDI\nEspace : lecture / pause ; terminer la prise MIDI\nPas à pas actif : maintenir les touches + → pour inscrire et avancer\nCtrl+S : enregistrer · Ctrl+Maj+S : enregistrer sous\n"
      "Ctrl+Z : annuler · Ctrl+Y / Ctrl+Maj+Z : rétablir\n"
      "Ctrl+C : copier les notes · Ctrl+V : coller\nCollage : autre voix = mêmes temps ; même voix = tête de lecture\n\n"
      "Flèches ← / → : reculer / avancer de 0,5 s\nMaj + ← / → : pas fin de 50 ms (piano-roll actif)\n\n"
      "Ctrl+molette : zoom proportionnel temps et hauteurs\nAlt+molette : zoom des hauteurs\n"
      "Maj+molette : déplacement horizontal\nMain / molette enfoncée + glisser : déplacer la vue\n\n"
      "Clic sur une note : sélectionner et préécouter\nCtrl+clic : ajouter / retirer de la sélection\n"
      "Glisser dans le vide : sélectionner les notes traversées\nMaj+glisser : sélectionner une plage temporelle\n"
      "Glisser une note sélectionnée : déplacer tout le groupe\nMaj + glisser une note : verrouiller le déplacement sur un axe\nCtrl + glisser une note : dupliquer librement, originales conservées\nCtrl + Maj + glisser : dupliquer sur un axe, originales conservées\nClic droit sur la timeline : ajouter ou modifier un repère\nBords gauche / droit : régler la durée\n"
      "Bords haut / bas (en zoom) : régler la vélocité\n\n"
      "C maintenu + clic : scinder une note (10 ms minimum par fragment)\nL : lier la sélection · F : fusionner la sélection · M : muet / audible\nSuppr : supprimer la sélection · Alt+clic : supprimer une note\n"
      "Crayon / double-clic vide : ajouter une note de 500 ms\nÉchap : quitter l’insertion / effacer la sélection\n"
      "Double-clic dans la marge audio / hauteurs : revenir au début\n\n"
      "Clic droit : écouter, muet, modifier, réaffecter et corriger\n"
      "Les vues partagent le zoom, la tête de lecture et la sélection.\n"
      "Opacité et gain visuel modifient uniquement l’affichage.\n\nÉchap ou clic à l’extérieur : fermer l’aide.")));
    content->setSize(std::min(630,getWidth()-100),std::min(560,getHeight()-120));
    auto& popup = juce::CallOutBox::launchAsynchronously(std::move(content),buttonMap["help"]->getBounds(),this);
    helpPopup=&popup;
    positionHelp();
    popup.setDismissalMouseClicksAreAlwaysConsumed(true); popup.grabKeyboardFocus();
  });
  buttonMap["help"]->setName(tr(String::fromUTF8("Aide : raccourcis et gestes")));
  for (auto* c : {&displayMode,&audioLayer}) addAndMakeVisible(c);
  for(auto* control:{&gridMode,&gridSubdivision})addAndMakeVisible(control);
  gridMode.addItem(tr(String::fromUTF8("Musicale")),1);gridMode.addItem(tr(String::fromUTF8("Secondes")),2);gridMode.addItem(tr(String::fromUTF8("Aucune")),3);
  gridMode.setName(tr(String::fromUTF8("Grille")));
  int subdivisionId=1;for(auto text:{"1/4","1/8","1/16","1/8T","1/16T"})gridSubdivision.addItem(text,subdivisionId++);
  gridSubdivision.setName(tr(String::fromUTF8("Subdivision de grille")));
  gridMode.setSelectedId(1,juce::dontSendNotification);gridSubdivision.setSelectedId(2,juce::dontSendNotification);
  gridMode.onChange=[this]{roll.rhythmicGrid.mode=gridMode.getSelectedId()-1;gridSubdivision.setEnabled(!locked&&roll.rhythmicGrid.mode==0);roll.repaint();edited();};
  gridSubdivision.onChange=[this]{roll.rhythmicGrid.subdivision=gridSubdivision.getSelectedId()-1;roll.repaint();edited();};
  displayMode.addItem("MIDI",1); displayMode.addItem("Audio + MIDI",2);
  displayMode.addItem(tr(String::fromUTF8("Superposition")),3); displayMode.setSelectedId(2,juce::dontSendNotification);
  displayMode.setName(tr(String::fromUTF8("Vue")));
  audioLayer.addItem(tr(String::fromUTF8("Signal audio")),1); audioLayer.addItem(tr(String::fromUTF8("Spectrogramme")),2);
  audioLayer.setSelectedId(1,juce::dontSendNotification); audioLayer.setName(tr(String::fromUTF8("Fond audio")));
  for(auto* slider : {&opacity,&visualGain}) {
    addAndMakeVisible(slider); slider->setSliderStyle(juce::Slider::LinearHorizontal);
    slider->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
  }
  opacity.setRange(0,100,1); opacity.setValue(20); opacity.setName(tr(String::fromUTF8("Opacité du fond audio")));
  visualGain.setRange(.25,8,.05); visualGain.setValue(4.9); visualGain.setName(tr(String::fromUTF8("Gain visuel")));
  auto viewChanged = [this] {
    if(displayMode.getSelectedId()==3 && !roll.waveCalibrated) {
      roll.waveReferenceZoom=roll.view.pitchZoom;roll.waveCalibrated=true;
    }
    roll.displayMode=displayMode.getSelectedId()-1; roll.spectral=audioLayer.getSelectedId()==2;
    roll.overlayOpacity=(float)opacity.getValue()/100.f; roll.visualGain=visualGain.getValue();
    recolourSpectrum();
    opacity.setVisible(roll.displayMode==2); visualGain.setVisible(roll.displayMode==2);
    audioLayer.setVisible(roll.displayMode!=0); refreshStacked(); resized(); roll.repaint(); edited();
  };
  displayMode.onChange=viewChanged;
  audioLayer.onChange=[this,viewChanged] {
    const bool spectral=audioLayer.getSelectedId()==2;
    if(spectral!=roll.spectral) {
      if(spectral) {
        waveOpacity=opacity.getValue()/100.;waveVisualGain=visualGain.getValue();
      }
      opacity.setValue(spectral?100:waveOpacity*100,juce::dontSendNotification);
      visualGain.setValue(spectral?8:waveVisualGain,juce::dontSendNotification);
    }
    viewChanged();
  };
  opacity.setVisible(false);
  visualGain.setVisible(false);
  opacity.onValueChange=viewChanged;
  visualGain.onValueChange=[this,viewChanged] {roll.waveReferenceZoom=roll.view.pitchZoom;roll.waveCalibrated=true;viewChanged();};
  button("spectralOptions",tr(String::fromUTF8("Spectre / registre…")),[this]{spectralOptions();});
  button("guidedOptions",tr(String::fromUTF8("Masque / MIDI guidé")),[this]{correctionOpen=true;openSections.insert("guided");displayMode.setSelectedId(3,juce::sendNotificationSync);audioLayer.setSelectedId(2,juce::sendNotificationSync);buildSide();resized();});
  roll.maskCommitted=[this](const phthongos::MaskStroke& stroke){commitMask(stroke);};
  roll.guidedFocused=[this](const String& id){focusGuided(id);};

  roll.bandContext=[this](juce::Point<int> point,int pitch){spectralBandMenu(point,pitch);};
  roll.bandChanged=[this]{invalidateGuided();saveSpectralBand();verificationStale=true;updateDiagnostics();rebuildSide();edited();};
  button("file", juce::String::fromUTF8("☰"), [this] { fileMenu(); });
  button("open", tr(String::fromUTF8("Ouvrir audio")), [this] { openAudio(); });
  initialisePitchControls();
  addAndMakeVisible(voiceSourceLabel);addAndMakeVisible(voiceSourceReload);voiceSourceLabel.setFont(juce::FontOptions(13));voiceSourceReload.setButtonText(String::fromUTF8("↺"));voiceSourceReload.setName(tr(String::fromUTF8("Recharger la source audio de la voix")));voiceSourceReload.setTooltip(voiceSourceReload.getName());voiceSourceReload.onClick=[this]{int key=partId;if(key<0&&parts.size()==1)key=parts.front().first;reloadVoiceAudio(key);};
  button("settings", tr(juce::String::fromUTF8("Paramètres")),
         [this] { settings(); });
  button("tempo",tr(String::fromUTF8("Tempo / BPM")),[this]{tempoOptions();});
  button("analyze", tr(String::fromUTF8("Analyser")), [this] { analyze(); });
  buttonMap["analyze"]->setColour(juce::TextButton::buttonColourId,
                                  theme.bronze);
  buttonMap["analyze"]->setColour(juce::TextButton::textColourOffId,
                                  juce::Colour(0xfffffdf6));
  button("cancel", tr(juce::String::fromUTF8("Annuler")),
         [this] { invoke(guidedRunning?"guided_cancel":"muscriptor_cancel", object({})); });
  button("export", tr(String::fromUTF8("Exporter MIDI")), [this] { exportMidi(); });
  button("play", tr(juce::String::fromUTF8("▶ Lecture")),
         [this] { control("toggle"); });
  button("stop", juce::String::fromUTF8("■"), [this] { control("stop"); });
  button("begin",tr(String::fromUTF8("Début")),[this]{goBoundary(false);});
  button("end",tr(String::fromUTF8("Fin")),[this]{goBoundary(true);});
  button("audio", "Audio", [this] {
    mode = 0;
    invoke("player_mix",
           with(with(with(args(), "mode", mode), "audioDb", audioDb), "midiDb",
                midiDb));
    edited();
  });
  button("midi", "MIDI", [this] {
    mode = 1;
    invoke("player_mix",
           with(with(with(args(), "mode", mode), "audioDb", audioDb), "midiDb",
                midiDb));
    edited();
  });
  button("both", "Audio + MIDI", [this] {
    mode = 2;
    invoke("player_mix",
           with(with(with(args(), "mode", mode), "audioDb", audioDb), "midiDb",
                midiDb));
    edited();
  });
  button("reverb", tr(juce::String::fromUTF8("Réverb · arrêt")), [this] {
    auto panel = std::make_unique<ReverbPanel>(
        reverbEnabled, reverbAmount, [s = safe()](bool enabled, double amount) {
          if (!s)
            return;
          s->reverbEnabled = enabled;
          s->reverbAmount = amount;
          s->invoke("player_reverb",
                    with(s->args(), "amount", enabled ? amount : 0));
          s->edited();
        });
    juce::CallOutBox::launchAsynchronously(
        std::move(panel), buttonMap["reverb"]->getBounds(), this);
  });
  button("metronome",tr(String::fromUTF8("Métronome")),[this]{clickEnabled=!clickEnabled;syncClick();edited();refresh();});
  button("clickOptions",String::fromUTF8("⋯"),[this]{clickOptions();});
  button("correction", tr(String::fromUTF8("Correction / nettoyage")), [this] {
    correctionOpen = !correctionOpen;
    edited();
    resized();
  });
  button("undo", tr(juce::String::fromUTF8("↶ Annuler")),
         [this] { command(object({{"kind", "undo"}})); });
  button("redo", tr(juce::String::fromUTF8("↷ Rétablir")),
         [this] { command(object({{"kind", "redo"}})); });
  button("tone", tr(juce::String::fromUTF8("Tonalité / gamme")), [this] {
    tonalityOpen = !tonalityOpen;
    toneOptions();
    resized();
    edited();
  });
  button("all", tr(juce::String::fromUTF8("Tout sélectionner")), [this] {
    std::set<std::int64_t> ids;
    for (auto &n : roll.notes)
      if (n.editable)
        ids.insert(n.id);
    roll.selectIds(ids);
  });
  button("merge", tr(juce::String::fromUTF8("Fusionner · F")),
         [this] { mergeNotes(roll.selected); });
  button("clear", tr(juce::String::fromUTF8("Effacer la sélection")),
         [this] { roll.clearSelection(); });
  button("select", juce::String::fromUTF8("↖"), [this] {
    if(roll.maskMode){roll.maskMode=0;rebuildSide();}roll.tool = 0;
    roll.updateToolCursor();refresh();
  });
  button("add", juce::String::fromUTF8("✎"), [this] {
    if(roll.maskMode){roll.maskMode=0;rebuildSide();}roll.tool = 1;
    roll.updateToolCursor();refresh();
  });
  button("hand", tr(String::fromUTF8("Main")), [this] {
    if(roll.maskMode){roll.maskMode=0;rebuildSide();}roll.tool = 2;
    roll.updateToolCursor();refresh();
  });
  button("cut", tr(String::fromUTF8("Scinder")), [this] {
    if(roll.maskMode){roll.maskMode=0;rebuildSide();}roll.tool=roll.tool==3?0:3;
    roll.grabKeyboardFocus();roll.updateToolCursor();refresh();
  });
  button("eraser", tr(String::fromUTF8("Gomme")), [this] {
    if(roll.maskMode){roll.maskMode=0;rebuildSide();}
    roll.tool=roll.tool==4?0:4;
    roll.grabKeyboardFocus();roll.updateToolCursor();refresh();
  });
  button("minus", juce::String::fromUTF8("−"), [this] {
    roll.view.timeZoom(roll.duration(), roll.view.zoom / 2, .5);
    edited();
  });
  button("fit", tr(String::fromUTF8("Tout voir")), [this] {
    roll.view.zoom = roll.view.pitchZoom = 1;
    roll.view.pan = roll.view.pitchPan = 0;
    edited();
  });
  button("plus", "+", [this] {
    roll.view.timeZoom(roll.duration(), roll.view.zoom * 2, .5);
    edited();
  });
  button("panels", tr(juce::String::fromUTF8("Panneaux ↺")), [this] {
    roll.view.audioHeight = 116;
    roll.view.keyHeight = 56;
    edited();
  });
  for (auto *c : {&model, &part})
    addAndMakeVisible(c);
  model.addItem("Basic Pitch",1);model.setSelectedId(1);model.setName(tr(String::fromUTF8("Moteur de transcription")));
  model.onChange=[this]{
    if(locked||engine.isVoid())return;
    auto settings=engine["settings"].clone();auto* o=settings.getDynamicObject();int selected=model.getSelectedId();
    o->setProperty("engine",selected==1?"basic_pitch":"muscriptor");
    if(selected>1)o->setProperty("model",selected==2?"small":selected==3?"medium":"large");
    invoke("muscriptor_settings",object({{"settings",settings}}),[this](var){refreshTranscription();},false,[this](String){refreshTranscription();});
  };
  part.setComponentID("voiceSelector");
  part.onChange = [this] { selectPart(part.getSelectedId() - 2); };
  for (juce::Slider *c : std::initializer_list<juce::Slider*>{&zoom, &audioVolume, &midiVolume}) {
    addAndMakeVisible(c);
    c->setSliderStyle(juce::Slider::LinearHorizontal);
    c->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  }
  zoom.setRange(0, 9, 0);
  zoom.onValueChange = [this] {
    roll.view.proportional(roll.duration(),
                           std::pow(2., zoom.getValue()) / roll.view.zoom, .5,
                           .5);
    edited();
  };
  audioVolume.setName(tr(String::fromUTF8("Volume audio")));midiVolume.setName(tr(String::fromUTF8("Volume MIDI")));
  audioVolume.setRange(-60, 0, 0);
  midiVolume.setRange(-60, 12, 0);
  audioVolume.setValue(0);
  midiVolume.setValue(0);
  auto mix = [this] {
    audioDb = audioVolume.getValue();
    midiDb = midiVolume.getValue();
    invoke("player_mix",
           with(with(with(args(), "mode", mode), "audioDb", audioDb), "midiDb",
                midiDb));
    edited();
  };
  audioVolume.onValueChange = mix;
  midiVolume.onValueChange = mix;
  addAndMakeVisible(follow);
  follow.setToggleState(true, juce::dontSendNotification);
  follow.onClick = [this] {
    roll.follow = follow.getToggleState();
    edited();
  };
  addAndMakeVisible(others);
  others.setComponentID("eye");
  others.setClickingTogglesState(true);
  button("partMute",tr(String::fromUTF8("Couper / réactiver cette partie")),[this]{
    bool wasPlaying=(bool)playback["playing"];
    if(partId>=0)command(object({{"kind","toggle_part_mute"},{"part",partId}}),[this,wasPlaying](var){if(wasPlaying)control("play");});
  });
  addAndMakeVisible(voiceVolume);voiceVolume.setRange(-60,0,0);voiceVolume.setValue(0);voiceVolume.setSliderStyle(juce::Slider::LinearHorizontal);voiceVolume.setTextBoxStyle(juce::Slider::TextBoxRight,false,58,25);voiceVolume.setDoubleClickReturnValue(true,0);voiceVolume.setName(tr(String::fromUTF8("Volume de la voix")));
  voiceVolume.onValueChange=[this]{if(partId<0||locked)return;voiceLevels.getDynamicObject()->setProperty(String(partId),voiceVolume.getValue());edited();syncVoiceLevels();};
  button("partAdd",tr(String::fromUTF8("Ajouter une voix vide")),[this]{showVoiceForm(true);});
  buttonMap["partAdd"]->setButtonText("+");
  button("partInstrument",tr(String::fromUTF8("Instrument…")),[this]{if(partId>=0)partDialog(false,partId);});
  button("partColour",tr(String::fromUTF8("Couleur de la voix")),[this]{showVoiceForm(false);});
  button("partLock",tr(String::fromUTF8("Verrouiller")),[this]{if(partId>=0)command(object({{"kind","toggle_part_lock"},{"part",partId}}));});
  button("partDelete",tr(String::fromUTF8("Supprimer cette voix et ses notes")),[this]{
    if(locked||partId<0||idSet(result["editing"]["locked_parts"]).count(partId))return;
    auto request=args().clone();auto key=partId;
    invoke("part_delete_info",request,[this,request,key](var info){
      if(audio["id"]!=request["audioId"]||result["analysis_id"]!=request["analysisId"]||result["editing"]["revision"]!=request["revision"]||partId!=key)return;
      auto name=part.getText();
      ask(tr(String::fromUTF8("Supprimer cette voix ?")),name+" — "+String((int)info["count"])+tr(String::fromUTF8(" note(s), y compris masquées ou muettes.\nLa voix et toutes ses notes seront supprimées. Ctrl+Z permet de tout restaurer.")),tr(String::fromUTF8("Supprimer")),{},[s=safe(),request,key](int answer){
        if(!s||answer!=1||s->locked)return;
        if(s->audio["id"]!=request["audioId"]||s->result["analysis_id"]!=request["analysisId"]||s->result["editing"]["revision"]!=request["revision"]||s->partId!=key)return;
        s->command(object({{"kind","delete_part"},{"part",key}}));
      });
    });
  });
  others.setName(tr(String::fromUTF8("Masquer cette voix")));
  others.setToggleState(true, juce::dontSendNotification);
  others.onClick = [this] {
    if(partId>=0){if(!hiddenParts.erase(partId))hiddenParts.insert(partId);}
    else {bool hidden=false;for(const auto& p:parts)hidden|=hiddenParts.count(p.first)>0;
      if(hidden){for(const auto& p:parts)hiddenParts.erase(p.first);}else for(const auto& p:parts)hiddenParts.insert(p.first);
    }
    invalidateGuided();
    rebuildNotes();
    edited();
  };
  roll.changed = [this] {
    if ((bool)roll.selection["temporal"] &&
        number(roll.selection, "end") > number(roll.selection, "start"))
      verifyScope = "time";
    else if (!roll.selected.empty() && ((roll.selection["temporal"].isBool() &&
                                         !(bool)roll.selection["temporal"]) ||
                                        verifyScope == "time"))
      verifyScope = "selection";
    edited();
  };
  roll.seek = [this](double p) {
    resetMidiInput();
    listenEnd = -1;
    control("seek", p);
  };
  roll.beginEdit = [this] { control("pause"); };
  roll.modify = [this](auto &n) { editNote(n); };
  roll.split=[this](std::int64_t id,double at){
    std::set<std::int64_t> before;for(auto& n:items(result["editing"]["notes"]))before.insert((juce::int64)n["id"]);
    command(object({{"kind","split"},{"id",(juce::int64)id},{"at",at}}),[this,before,id](var result){
      std::set<std::int64_t> selected{id};for(auto& n:items(result["editing"]["notes"]))if(!before.count((juce::int64)n["id"]))selected.insert((juce::int64)n["id"]);
      roll.selectIds(selected);
    });
  };
  roll.markerChanged=[this](const var& action){command(action);};
  roll.moveGroup = [this](const auto& ids, double seconds, int semitones) {
    command(object({{"kind","move_group"},{"ids",idsVar(ids)},{"seconds",seconds},{"semitones",semitones}}),
      [this,ids](var){ roll.selectIds(ids); });
  };
  roll.duplicateGroup=[this](const auto& ids,double seconds,int semitones){
    std::set<std::int64_t> before;for(const auto& n:items(result["editing"]["notes"]))before.insert((juce::int64)n["id"]);
    command(object({{"kind","duplicate_notes"},{"ids",idsVar(ids)},{"seconds",seconds},{"semitones",semitones}}),[this,before](var){
      std::set<std::int64_t> copies;for(const auto& n:items(result["editing"]["notes"]))if(!before.count((juce::int64)n["id"]))copies.insert((juce::int64)n["id"]);
      roll.selectIds(copies);refresh();
    });
  };
  roll.add = [this](double t, int p) { addNote(t, p); };
  roll.remove = [this](auto ids) { removeNotes(ids); };
  roll.merge = [this](auto ids) { mergeNotes(ids); };
  roll.mute = [this](auto ids) {
    command(object({{"kind", "toggle_mute"}, {"ids", idsVar(ids)}}));
  };
  roll.audition = [this](auto &n) { audition(n); };
  roll.keyAudition=[this](int pitch){if(audio.isVoid()||(pitch>=0&&(!notesReady||locked)))return;unsigned program=0;for(const auto& value:items(result["midi_parts"]))if((value["identity"].isVoid()?(int)value["program"]:(int)value["identity"])==partId){program=unsigned((int)value["program"]);break;}invoke("player_key",object({{"audioId",audio["id"]},{"pitch",pitch},{"voice",partId},{"program",int(program)},{"db",partId>=0?number(voiceLevels,String(partId).toRawUTF8(),0):0}}));};
  roll.context = [this](auto p, auto t) { context(p, t); };
  refreshTranscription();
  invoke("soundbank_status", object({}), [this](var v) { bank = v; });
  initialiseStacked();
  auto pref = root().getChildFile("data/audio-output.json");
  if (pref.existsAsFile())
    invoke("audio_output_select", object({{"id", juce::JSON::parse(pref)}}));
  startTimerHz(60);
  status.setText(tr(String::fromUTF8("Ouvrez un audio ou importez un MIDI depuis le menu Fichier.")),
                 juce::dontSendNotification);
  rebuildSide();
  refresh();
  setSize(1400, 900);
  initialiseMidiInput();
}
void App::applyLanguage(localisation::Language language) {
  auto previous=localisation::language();if(previous==language)return;
  localisation::setLanguage(language);
  // These controls contain document names, device names or composite live data.
  for(auto* c:std::initializer_list<juce::Component*>{&roll,&title,&part,&model,&device,&inspector,&status,&calculation})c->getProperties().set("preserveLanguageText",true);
  refreshUiLanguage(*this,previous);
  for(auto* form:Form::activeForms){refreshUiLanguage(*form,previous);form->resized();}
  for(auto* data:{&verification,&clean,&dynamics,&toneReport,&shortReport,&guidedReport})*data=localisation::report(*data);
  for(auto& note:roll.notes)note.label=Note::parse(note.source).label;
  parts.clear();
  for(const auto& p:items(result["midi_parts"]))parts.emplace_back(p["identity"].isVoid()?(int)p["program"]:(int)p["identity"],displayPartLabel(p));
  if(parts.empty())for(const auto& note:roll.notes)if(std::none_of(parts.begin(),parts.end(),[&](const auto& p){return p.first==note.part;}))parts.emplace_back(note.part,note.label);
  part.clear(juce::dontSendNotification);part.addItem(tr(String::fromUTF8("Toutes les parties")),1);
  for(const auto& p:parts)part.addItem(p.second,p.first+2);
  part.setSelectedId(partId+2,juce::dontSendNotification);
  refreshVoiceMenu(part,result,roll);
  inspector.setText(tr(String::fromUTF8("Survolez une note pour ses détails · Cliquer pour garder la sélection · Échap pour effacer")),juce::dontSendNotification);
  operationLabel=localisation::retranslate(operationLabel,previous);
  if(buttonMap.count("toneSuggest"))toneOptions();
  buildSide();refresh();
}
App::~App() {
  midiInput.reset();
  stopTimer();
  setLookAndFeel(nullptr);
}
juce::TextButton &App::button(const String &key, const String &name,
                              std::function<void()> fn) {
  auto *b = buttons.add(key=="export"?new MidiDragButton(name):new juce::TextButton(name));
  if(auto* drag=dynamic_cast<MidiDragButton*>(b)) {
    drag->prepare=[this](MidiDragButton::Ready ready){prepareMidiDrag(std::move(ready));};
    drag->failed=[this]{status.setText(tr(String::fromUTF8("Export MIDI impossible.")),juce::dontSendNotification);};
  }
  buttonMap[key] = b;
  b->setComponentID(key);
  b->setName(name);
  addAndMakeVisible(b);
  b->onClick = [key,fn=std::move(fn)]{phthongos::diagnostics::event("ui_button",key);fn();};
  return *b;
}
void App::paint(juce::Graphics &g) {
  ++uiPaints;
  g.fillAll(theme.paper);
  if (logo.isValid())
    g.drawImageWithin(logo, 18, 15, 90, 90, juce::RectanglePlacement::centred);
  if (editorBadge.isVisible()) {
    g.setColour(theme.border);
    g.drawRoundedRectangle(editorBadge.getBounds().toFloat().reduced(.5f), 4,
                           1);
  }
  g.setColour(theme.border);
  g.drawHorizontalLine(120, 18, (float)getWidth() - 18);
  g.setColour(juce::Colour(0xff617889));
  g.setFont(juce::FontOptions(15));
  int vol = getWidth() - 338, volY=(getWidth()<1435?161:131)+pitchBandHeight();
  g.drawText("Audio", vol, volY, 40, 30, juce::Justification::centredLeft);
  g.drawText(audioVolume.getTextFromValue(audioDb), vol + 119, volY, 43, 30,
             juce::Justification::centredLeft);
  g.drawText("MIDI", vol + 174, volY, 36, 30, juce::Justification::centredLeft);
  g.drawText(midiVolume.getTextFromValue(midiDb), vol + 291, volY, 43, 30,
             juce::Justification::centredLeft);
  g.setColour(juce::Colour(0xffe9e0d1));
  g.fillRect(toolbarBounds);
  g.setColour(juce::Colour(0xff617889));
  g.setFont(juce::FontOptions(14));
  g.drawText(tr(String::fromUTF8("Grille")),gridCaptionBounds,juce::Justification::centredLeft);
  g.drawText("Zoom",zoomCaptionBounds,juce::Justification::centredLeft);
  if(roll.displayMode==2) {
    g.drawText(tr(String::fromUTF8("Opacité "))+String((int)opacity.getValue())+" %",opacityCaptionBounds,juce::Justification::centredRight);
    g.drawText(tr(String::fromUTF8("Gain visuel "))+String(visualGain.getValue(),1),gainCaptionBounds,juce::Justification::centredRight);
  }
  if(roll.spectral && roll.displayMode!=0)
    g.drawText(roll.bandEnabled?tr(String::fromUTF8("Registre : "))+pitchName(roll.bandLow)+String::fromUTF8(" → ")+pitchName(roll.bandHigh):tr(String::fromUTF8("Tout le registre")),registerCaptionBounds,juce::Justification::centredLeft);
  auto midiY = roll.getY() + (int)roll.layout().rollTop - 31;
  // Visibility is on the left of the part selector; sound is on its right.
  g.setFont(juce::FontOptions(15));

}
void App::resized() {
  const int contentOffset=pitchBandHeight();
  int w = getWidth(), h = getHeight()-contentOffset;
  if (confirmation) {
    confirmation->setBounds(getLocalBounds());
    confirmation->toFront(false);
  }
  bool badge = result["engine"].toString() == "midi";
  juce::GlyphArrangement glyphs;
  glyphs.addLineOfText(title.getFont(), title.getText(), 0, 0);
  int nameWidth = std::min(
      w - 360 - (badge ? 104 : 0),
      (int)std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()) + 16);
  int groupWidth = nameWidth + (badge ? 104 : 0), titleX = (w - groupWidth) / 2;
  title.setBounds(titleX, 8, nameWidth, 27);
  editorBadge.setBounds(titleX + nameWidth + 8, 9, 96, 25);
  int wx = w - 114;
  for (auto key : {"windowMin", "windowMax", "windowClose"}) {
    buttonMap[key]->setBounds(wx, 6, 32, 32);
    wx += 32;
  }
  int x = 178;
  buttonMap["file"]->setBounds(x, 58, 38, 38);
  x += 50;
  buttonMap["open"]->setBounds(x, 58, 182, 38);
  x += 196;
  bool editor = result["engine"].toString() == "midi"&&(bool)audio["midi_only"];
  modelCaption.setBounds(x, 58, 62, 38);
  model.setBounds(x+66,58,180,38);
  bool basic=model.getSelectedId()==1;
  if(!editor)x+=260;
  buttonMap["settings"]->setVisible(false);
  int progressWidth = w - x - 330;
  calculation.setVisible(locked&&!pitchControlsPending);
  if (progressWidth >= 180)
    calculation.setBounds(x + 50, 58, progressWidth, 38);
  else
    calculation.setBounds(178, 98, w - 356, 22);
  refreshPitchBand();
  if(pitchControls)pitchControls->setBounds(18,124,w-36,contentOffset);

  buttonMap["analyze"]->setBounds(w - 260, 58, 100, 38);
  buttonMap["cancel"]->setBounds(w - 260, 58, 100, 38);
  buttonMap["export"]->setBounds(w - 152, 58, 134, 38);
  x = 18;
  for (auto pair : std::vector<std::pair<String, int>>{{"begin", 30}, {"play", 84},
                                                       {"stop", 33}, {"end", 30},
                                                       {"audio", 48},
                                                       {"midi", 45},
                                                       {"both", 86}}) {
    if(pair.first=="stop"){midiRecord.setBounds(x,130,33,30);x+=38;}
    buttonMap[pair.first]->setBounds(x, 130, pair.second, 30);
    x += pair.second + 5;
  }
  position.setBounds(x, 130, 62, 30);
  x += 66;
  buttonMap["reverb"]->setBounds(x, 130, 94, 30);
  x += 101;
  buttonMap["metronome"]->setBounds(x,130,94,30);x+=99;buttonMap["clickOptions"]->setBounds(x,130,28,30);x+=33;
  follow.setBounds(x, 130, 128, 30);
  beatIndicator.setBounds(x+132,131,64,28);
  int vol = w - 338,volY=w<1435?161:131;
  audioVolume.setBounds(vol + 40, volY, 78, 28);
  midiVolume.setBounds(vol + 209, volY, 80, 28);
  device.setBounds(18, 161, w - (w<1435?374:36), 22);
  x = 18;
  for (auto pair : std::vector<std::pair<String, int>>{{"correction", 157},
                                                       {"undo", 83},
                                                       {"redo", 83},
                                                       {"tone", 128},
                                                       {"all", 113},
                                                       {"merge", 104},
                                                       {"clear", 132}}) {
    buttonMap[pair.first]->setBounds(x, 190, pair.second, 29);
    x += pair.second + 8;
  }
  if (buttonMap.count("toneSuggest")) {
    toneCaption.setBounds(27, 225, w - 54, 25);
    toneScope.setBounds(27, 255, 155, 29);
    buttonMap["toneSuggest"]->setBounds(190, 255, 166, 29);
    toneRoot.setBounds(366, 255, 140, 29);
    toneMode.setBounds(516, 255, 205, 29);
    buttonMap["toneShow"]->setBounds(731, 255, 270, 29);
    buttonMap["toneSelect"]->setBounds(27, 292, 255, 28);
    buttonMap["toneHide"]->setBounds(292, 292, 150, 28);
    buttonMap["toneDetails"]->setBounds(452,292,190,28);
    int sx = 652;
    for (auto *b : toneSuggestions) {
      int bw = std::min(130, (w - 670 - 5*toneSuggestions.size()) / std::max(1, toneSuggestions.size()));
      b->setBounds(sx, 292, bw, 28);
      sx += bw + 5;
    }
    toneHelp.setBounds(27, toneExpanded?327:292, w - 54, 36);
    if(!toneExpanded){buttonMap["toneSelect"]->setBounds(27,255,270,29);buttonMap["toneHide"]->setBounds(307,255,165,29);}
    buttonMap["toneAdjust"]->setBounds(toneExpanded?w-230:482,toneExpanded?225:255,203,29);
    if(!toneExpanded){
      buttonMap["toneAdjust"]->setBounds(27,255,203,29);
      buttonMap["toneSelect"]->setBounds(238,255,270,29);
      buttonMap["toneHide"]->setBounds(516,255,165,29);
      buttonMap["toneDetails"]->setBounds(689,255,190,29);
    }
  }
  int top = 225 + (tonalityOpen ? (toneExpanded?160:112) : 0);
  int sideWidth = correctionOpen && (!result.isVoid()||openSections.count("guided")) ? std::min(340, w / 3) : 0;
  int rw = w - 38 - (sideWidth ? sideWidth + 12 : 0);
  // Lay out related controls as groups. Narrow windows wrap whole groups;
  // painting uses these same rectangles so captions always follow their slider.
  const int left=30,right=18+rw-12;
  int rowY=top+7;x=left;
  auto group=[&](int width,int gap=8){if(x>left&&x+width>right){x=left;rowY+=34;}auto r=juce::Rectangle<int>(x,rowY,width,28);x+=width+gap;return r;};
  auto tools=group(155);int toolX=tools.getX();
  for(auto key:{"select","add","hand","cut","eraser"}){buttonMap[key]->setBounds(toolX,tools.getY(),28,28);toolX+=31;}
  displayMode.setBounds(group(150));
  if(roll.displayMode!=0)audioLayer.setBounds(group(130,12));
  if(roll.displayMode==0)voicePresentation.setBounds(group(130));
  auto zoomGroup=group(385);int zx=zoomGroup.getX(),zy=zoomGroup.getY();
  zoomCaptionBounds={zx,zy,42,28};zoom.setBounds(zx+46,zy,90,28);zx+=144;
  for(auto pair:std::vector<std::pair<String,int>>{{"minus",25},{"fit",74},{"plus",25},{"panels",105}}){buttonMap[pair.first]->setBounds(zx,zy,pair.second,28);zx+=pair.second+4;}
  x=left;rowY+=34;
  const int tempoWidth=hasMidiTempoEstimate()&&midiEstimatePending?166:100;
  auto grid=group(280+tempoWidth,12);int gx=grid.getX(),gy=grid.getY();
  gridCaptionBounds={gx,gy,48,28};gridMode.setBounds(gx+52,gy,132,28);
  gridSubdivision.setBounds(gx+192,gy,80,28);buttonMap["tempo"]->setBounds(gx+280,gy,tempoWidth,28);

  if(roll.displayMode==2){
    // Compact sliders retain both labels on the grid row at the supported
    // minimum width; large windows keep the usual 100px slider travel.
    int sliderWidth=right-left>=804?100:68;
    auto op=group(100+sliderWidth,12);opacityCaptionBounds=op.withWidth(94);opacity.setBounds(op.getX()+100,op.getY(),sliderWidth,28);
    auto gain=group(100+sliderWidth,12);gainCaptionBounds=gain.withWidth(94);visualGain.setBounds(gain.getX()+100,gain.getY(),sliderWidth,28);
  }
  midiStepTool.setBounds(group(116,12));
  voiceFraming.setVisible(stackedView);transferVoices.setVisible(stackedView);
  if(stackedView){voiceFraming.setBounds(group(155));transferVoices.setBounds(group(105));}
  bool spectral=roll.spectral&&roll.displayMode!=0;
  buttonMap["spectralOptions"]->setVisible(spectral);buttonMap["guidedOptions"]->setVisible(spectral);
  registerCaptionBounds={};
  if(spectral){x=left;rowY+=34;auto options=group(165,12);buttonMap["spectralOptions"]->setBounds(options);
    auto guided=group(174,8);buttonMap["guidedOptions"]->setBounds(guided);
    registerCaptionBounds={x,rowY,std::max(0,right-x),28};}
  toolbarBounds={18,top,rw,rowY+35-top};
  refreshVoiceSource();
  roll.setBounds(18,toolbarBounds.getBottom(),rw,std::max(100,h-toolbarBounds.getBottom()-63));
  int midiY = roll.getY() + (int)roll.layout().rollTop - 31-roll.voiceSourceHeader;
  voiceSourceLabel.setBounds(98,midiY+31,std::max(0,rw-138),24);voiceSourceReload.setBounds(18+rw-34,midiY+32,26,21);
  int headerX=98;const int compact=std::max(0,739-(18+rw-8));
  auto placeHeader=[&](juce::Component& c,int width){c.setBounds(headerX,midiY+3,width,25);headerX+=width+6;};
  placeHeader(others,29);placeHeader(part,170-std::min(90,compact/2));
  placeHeader(*buttonMap["partAdd"],29);placeHeader(*buttonMap["partColour"],29);
  placeHeader(*buttonMap["partInstrument"],104-std::min(34,compact/4));buttonMap["partInstrument"]->toFront(false);
  placeHeader(*buttonMap["partMute"],29);
  placeHeader(voiceVolume,145-std::min(65,compact-std::min(90,compact/2)-std::min(34,compact/4)));voiceVolume.toFront(false);
  placeHeader(*buttonMap["partDelete"],29);placeHeader(*buttonMap["partLock"],29);buttonMap["partLock"]->toFront(false);
  buttonMap["partAdd"]->toFront(false);buttonMap["partColour"]->toFront(false);
  buttonMap["partDelete"]->toFront(false);
  part.toFront(false);
  others.toFront(false);
  buttonMap["partMute"]->toFront(false);
  sideView.setVisible(sideWidth > 0);
  sideView.setBounds(w - sideWidth - 18, top, sideWidth,
                     std::max(100, h - top - 28));
  side.width = std::max(280, sideWidth - 18);
  inspector.setBounds(27, h - 62, rw - 18, 30);
  buttonMap["help"]->setBounds(w-52,h-44,30,30);
  status.setBounds(18, h - 27, w - 85, 24);
  // The full-width Basic Pitch stage reserves its own height. Everything
  // below the header uses the remaining viewport, including the fixed keyboard.
  if(contentOffset){
   // Translate only controls laid out above. Floating children (context menus,
   // callouts and overlays) own their coordinates and must never be shifted.
   auto shift=[contentOffset](juce::Component& child){if(child.getY()>=125)child.setTopLeftPosition(child.getX(),child.getY()+contentOffset);};
   for(auto* child:std::initializer_list<juce::Component*>{&roll,&sideView,&inspector,&status,&device,&position,&follow,&beatIndicator,&midiRecord,&midiStepTool,&voiceSourceLabel,&voiceSourceReload,&part,&others,&voiceVolume,&audioVolume,&midiVolume,&displayMode,&audioLayer,&voicePresentation,&voiceFraming,&transferVoices,&gridMode,&gridSubdivision,&zoom,&opacity,&visualGain,&toneCaption,&toneHelp,&toneScope,&toneRoot,&toneMode})shift(*child);
   for(auto* child:buttons)shift(*child);
   for(auto* child:toneSuggestions)shift(*child);
   toolbarBounds.translate(0,contentOffset);gridCaptionBounds.translate(0,contentOffset);zoomCaptionBounds.translate(0,contentOffset);opacityCaptionBounds.translate(0,contentOffset);gainCaptionBounds.translate(0,contentOffset);registerCaptionBounds.translate(0,contentOffset);
  }
  positionHelp();
  layoutStacked();
  if(initialisationPopup)syncInitialisationPopup();
  if(confirmation)confirmation->toFront(false);
}
void App::positionHelp() {
  if (helpPopup == nullptr) return;
  if (auto* content=helpPopup->getChildComponent(0))
    content->setSize(std::max(100,std::min(630,getWidth()-100)),std::max(100,std::min(560,getHeight()-120)));
  helpPopup->updatePosition(buttonMap["help"]->getBounds(),getLocalBounds().reduced(8));
  helpPopup->toFront(false);
}
void App::invoke(const String &cmd, var a, Done done, bool heavy,
                 std::function<void(String)> failed) {
  if(cmd.startsWith("player_")&&cmd!="player_snapshot")nextPlayerPoll=0;
  auto s = safe();
  bridge.request(
      cmd, a,
      [s, done = std::move(done), failed = std::move(failed)](var r) {
        if (!s)
          return;
        if (!(bool)r["ok"]) {
          auto e = r["error"].toString();
          if(s->initialisingProject)s->setBusy(false,e);
          s->status.setText(e, juce::dontSendNotification);
          if (failed)
            failed(e);
          return;
        }
        if (done)
          done(localisation::report(r["value"]));
      },
      heavy);
}
var App::args() const {
  return object(
      {{"audioId", audio["id"]},
       {"analysisId", result["analysis_id"]},
       {"sensitivity",
        result.hasProperty("sensitivity") ? result["sensitivity"] : var(100)},
       {"revision", result["editing"]["revision"]},
       {"part", partId < 0 ? var() : var(partId)}});
}
void App::setBusy(bool b, const String &message) {
  if(b&&!midiCommitting&&!midiRecording&&!midiRecordPending)resetMidiInput();
  if (b && !locked) {
    completedCalculation = {};
    calculationStarted = juce::Time::getMillisecondCounterHiRes();
    operationLabel = message;
  }
  if (b && message.isNotEmpty())
    operationLabel = message;
  if(b&&initialisingProject)calculation.setText(tr(String::fromUTF8("Initialisation…")),juce::dontSendNotification);
  double elapsed =
      calculationStarted > 0
          ? (juce::Time::getMillisecondCounterHiRes() - calculationStarted) /
                1000.
          : 0;
  locked = b;
  roll.busy = b;
  if (message.isNotEmpty())
    status.setText(message + (!b && elapsed >= 1 && !initialisingProject
                                  ? tr(String::fromUTF8(" · Durée du calcul : ")) +
                                        timeName(elapsed)
                                  : String()),
                   juce::dontSendNotification);
  if (!b) {
    if(initialisingProject){
      phthongos::diagnostics::event("initialization_finished","elapsed_ms="+String(elapsed*1000.,1));
      initialisingProject=false;completedCalculation={};
      calculation.setText(message==tr(String::fromUTF8("Prêt"))?message:String(),juce::dontSendNotification);
    }else if (elapsed >= 1) {
      completedCalculation = tr(String::fromUTF8("Calcul terminé · ")) + timeName(elapsed);
      calculation.setText(completedCalculation,juce::dontSendNotification);
    }
    calculationStarted = 0;
    operationLabel = {};
  }
  refresh();
  syncInitialisationPopup();
}
void App::edited() {
  dirty = !audio.isVoid();
  refresh();
}
void App::refresh() {
  ++uiRefreshes;
  refreshMidiInput();
  buttonMap["select"]->setToggleState(roll.tool == 0&&roll.maskMode==0,
                                      juce::dontSendNotification);
  buttonMap["add"]->setToggleState(roll.tool == 1, juce::dontSendNotification);
  buttonMap["hand"]->setToggleState(roll.tool == 2, juce::dontSendNotification);
  buttonMap["cut"]->setToggleState(roll.tool == 3 && roll.maskMode==0, juce::dontSendNotification);
  buttonMap["eraser"]->setToggleState(roll.tool == 4 && roll.maskMode==0, juce::dontSendNotification);
  buttonMap["audio"]->setToggleState(mode == 0, juce::dontSendNotification);
  buttonMap["midi"]->setToggleState(mode == 1, juce::dontSendNotification);
  buttonMap["both"]->setToggleState(mode == 2, juce::dontSendNotification);
  auto name=projectDisplayName();
  title.setText(name + (dirty ? " *" : ""),
                juce::dontSendNotification);
  bool editor = result["engine"].toString() == "midi"&&(bool)audio["midi_only"];
  editorBadge.setVisible(editor);
  model.setVisible(!editor);
  modelCaption.setVisible(!editor);
  model.setEnabled(!locked);
  refreshPitchBand();
  buttonMap["analyze"]->setVisible(!editor && !(locked && reportText=="transcription"));
  buttonMap["analyze"]->setEnabled(!locked && !audio.isVoid() &&
                                   !(bool)audio["midi_only"]);
  buttonMap["cancel"]->setVisible(locked && (reportText == "transcription"||guidedRunning));
  buttonMap["export"]->setEnabled(!locked && !result.isVoid());
  buttonMap["tempo"]->setEnabled(!locked && !audio.isVoid());
  if(buttonMap.count("toneDetails"))buttonMap["toneDetails"]->setEnabled(!locked&&(bool)tone["enabled"]&&!toneReport.isVoid());
  gridMode.setEnabled(!locked);gridSubdivision.setEnabled(!locked&&roll.rhythmicGrid.mode==0);
  buttonMap["settings"]->setEnabled(!locked && (editor || !engine.isVoid()));
  buttonMap["open"]->setButtonText(editor ? tr(String::fromUTF8("Nouvelle transcription"))
                                          : tr(String::fromUTF8("Ouvrir audio")));
  for (auto key : {"file", "open"})
    buttonMap[key]->setEnabled(!locked);
  for (auto key : {"all", "clear", "tone"})
    buttonMap[key]->setEnabled(!locked && !result.isVoid());
  for(auto key:{"select","add","hand","cut","eraser"})
    buttonMap[key]->setEnabled(!locked && !audio.isVoid());
  roll.editable=!locked && !audio.isVoid();
  buttonMap["undo"]->setEnabled(!locked && (bool)result["editing"]["can_undo"]);
  buttonMap["redo"]->setEnabled(!locked && (bool)result["editing"]["can_redo"]);
  buttonMap["merge"]->setEnabled(!locked && canMerge(roll.selected));
  buttonMap["play"]->setButtonText((bool)playback["playing"]
                                       ? juce::String::fromUTF8("Ⅱ Pause")
                                       : tr(juce::String::fromUTF8("▶ Lecture")));
  for (auto key : {"play", "stop", "begin", "end", "reverb"})
    buttonMap[key]->setEnabled(!locked && (bool)playback["ready"] &&
                               notesReady);
  buttonMap["audio"]->setEnabled(!locked && (bool)playback["ready"] &&
                                 !(bool)audio["midi_only"]);
  buttonMap["midi"]->setEnabled(!locked && notesReady && !result.isVoid());
  buttonMap["both"]->setEnabled(!locked && notesReady && !result.isVoid() &&
                                !(bool)audio["midi_only"]);
  buttonMap["reverb"]->setButtonText(
      reverbEnabled
          ? tr(juce::String::fromUTF8("Réverb · ")) + String(reverbAmount, 0) + " %"
          : tr(juce::String::fromUTF8("Réverb · arrêt")));
  part.setEnabled(!locked);
  bool voiceVisible=partId>=0?!hiddenParts.count(partId):std::none_of(parts.begin(),parts.end(),[this](const auto& p){return hiddenParts.count(p.first)>0;});
  others.setEnabled(!locked&&!parts.empty());
  others.setToggleState(voiceVisible,juce::dontSendNotification);
  auto visibilityLabel=(partId>=0?(voiceVisible?tr(String::fromUTF8("Masquer cette voix")):tr(String::fromUTF8("Afficher cette voix"))):(voiceVisible?tr(String::fromUTF8("Masquer toutes les voix")):tr(String::fromUTF8("Afficher toutes les voix"))));
  others.setName(visibilityLabel);others.setTooltip(visibilityLabel);
  bool voiceLocked=idSet(result["editing"]["locked_parts"]).count(partId)>0;
  buttonMap["partAdd"]->setEnabled(!locked&&!audio.isVoid());
  buttonMap["partColour"]->setEnabled(!locked&&partId>=0&&!voiceLocked);
  buttonMap["partInstrument"]->setEnabled(!locked&&partId>=0&&!voiceLocked);
  int colourProgram=0;for(const auto& p:items(result["midi_parts"]))if((p["identity"].isVoid()?(int)p["program"]:(int)p["identity"])==partId)colourProgram=(int)p["program"];
  buttonMap["partColour"]->getProperties().set("voiceColour",(juce::int64)roll.voiceColour(partId,colourProgram).getARGB());buttonMap["partColour"]->repaint();
  buttonMap["partLock"]->setEnabled(!locked && partId>=0);
  buttonMap["partLock"]->setButtonText((voiceLocked?tr(String::fromUTF8("Déverrouiller")):tr(String::fromUTF8("Verrouiller"))));
  buttonMap["partLock"]->setToggleState(voiceLocked,juce::dontSendNotification);
  buttonMap["partLock"]->setName((voiceLocked?tr(String::fromUTF8("Déverrouiller cette voix")):tr(String::fromUTF8("Verrouiller cette voix"))));
  buttonMap["partMute"]->setEnabled(!locked && partId>=0);
  buttonMap["partDelete"]->setEnabled(!locked && partId>=0 && !voiceLocked);
  voiceVolume.setEnabled(!locked&&partId>=0);voiceVolume.setValue(partId>=0?number(voiceLevels,String(partId).toRawUTF8(),0):0,juce::dontSendNotification);voiceVolume.setTooltip(tr(String::fromUTF8("Volume de la voix"))+" : "+part.getText());
  bool partMuted=idSet(result["editing"]["muted_parts"]).count(partId)>0;
  buttonMap["partMute"]->setToggleState(partMuted,juce::dontSendNotification);
  buttonMap["partMute"]->setName((partMuted?tr(String::fromUTF8("Réactiver cette partie")):tr(String::fromUTF8("Couper cette partie"))));
  double zoomLow =
             std::log2(std::max(1., roll.view.zoom / roll.view.pitchZoom)),
         zoomHigh = std::log2(
             std::min(512., roll.view.zoom * 64 / roll.view.pitchZoom));
  zoom.setRange(zoomLow, std::max(zoomLow + 1e-9, zoomHigh), 0);
  zoom.setEnabled(!audio.isVoid() && zoomHigh > zoomLow);
  zoom.setValue(std::log2(roll.view.zoom), juce::dontSendNotification);
  audioVolume.setEnabled(!(bool)audio["midi_only"]);
  audioLayer.setEnabled(!audio.isVoid() && !(bool)audio["midi_only"]);
  opacity.setEnabled(!audio.isVoid() && !(bool)audio["midi_only"]);
  visualGain.setEnabled(!audio.isVoid() && !(bool)audio["midi_only"]);
  buttonMap["spectralOptions"]->setEnabled(!locked);
  buttonMap["guidedOptions"]->setEnabled(!locked&&!audio.isVoid()&&!(bool)audio["midi_only"]);
  buttonMap["metronome"]->setToggleState(clickEnabled,juce::dontSendNotification);buttonMap["metronome"]->setEnabled(!locked&&notesReady);buttonMap["clickOptions"]->setEnabled(!locked&&notesReady);
  if(midiRecording){buttonMap["stop"]->setEnabled(true);buttonMap["play"]->setEnabled(true);}
  refreshStacked();
  resized();
  roll.repaint();
  repaint();
}
void App::updateResult(var v, bool makeReady, bool synchronize) {
  roll.clearTimelinePreview();invalidateGuided();result = std::move(v);updateMaskView();
  roll.scoreDuration = number(result, "duration", number(audio, "duration", 1));
  roll.editable = !result.isVoid();
  parts.clear();
  for (auto &p : items(result["midi_parts"]))
    parts.emplace_back(p["identity"].isVoid() ? (int)p["program"]
                                              : (int)p["identity"],
                       displayPartLabel(p));
  if (parts.empty())
    for (auto &n : items(result["editing"]["notes"])) {
      auto note = Note::parse(n);
      if (std::none_of(parts.begin(), parts.end(),
                       [&](auto &p) { return p.first == note.part; }))
        parts.emplace_back(note.part, note.label);
    }
  if(legacyVisiblePart>=0){for(const auto& p:parts)if(p.first!=legacyVisiblePart)hiddenParts.insert(p.first);legacyVisiblePart=-1;}
  if (partId >= 0 && std::none_of(parts.begin(), parts.end(), [this](auto &p) {
        return p.first == partId;
      }))
    partId = -1;
  part.clear(juce::dontSendNotification);
  part.addItem(tr(String::fromUTF8("Toutes les parties")), 1);
  for (auto &p : parts)
    part.addItem(p.second, p.first + 2);
  part.setSelectedId(partId + 2, juce::dontSendNotification);
  rebuildNotes();
  updateDiagnostics();
  dirty = true;
  refresh();
  rebuildSide();
  if ((bool)tone["enabled"]){toneReport=var();roll.tonal.clear();toneOptions();analyzeTone(false);}
  if ((bool)shortState["enabled"])
    findShort(true);
  if (!dynamics.isVoid() || openSections.count("dynamics")) {
    dynamics = var();
    restoreDynamics();
  }
  if (makeReady)
    prepare();
  else if (synchronize&&!preparedAudioId.isVoid()&&preparedAudioId==audio["id"])
    syncNotes();
}
void App::rebuildNotes() {
  roll.partColours.clear();if(auto* colours=result["editing"]["part_colours"].getDynamicObject())for(const auto& entry:colours->getProperties())roll.partColours[entry.name.toString().getIntValue()]=(unsigned)(int)entry.value;
  roll.markers=items(result["editing"]["markers"]);
  roll.notes.clear();
  roll.manuallyMuted=idSet(result["editing"]["muted_notes"]);
  roll.mutedParts=idSet(result["editing"]["muted_parts"]);
  roll.lockedParts=idSet(result["editing"]["locked_parts"]);
  int index = 0;
  for (auto &v : items(result["editing"]["notes"])) {
    auto n = Note::parse(v);
    bool target=partId<0||n.part==partId;
    n.editable = target && !roll.lockedParts.count(n.part) && !hiddenParts.count(n.part);
    if(!n.editable)roll.selected.erase(n.id);
    n.index = !n.muted ? index++ : -1;
    if (!hiddenParts.count(n.part))
      roll.notes.push_back(n);
  }
  if(roll.selected.empty())roll.selection=var();
  roll.refreshOverlaps();
  refreshVoiceMenu(part,result,roll);
  roll.repaint();
}
void App::selectPart(int id) {
  part.setSelectedId(id+2,juce::dontSendNotification);
  resetMidiInput();
  roll.stopKeyAudition();
  ++pitchControlsGeneration;invalidateGuided();partId = id;syncPitchControls();updateMaskView();
  loadSpectralBand();
  others.setToggleState(!hiddenParts.count(id), juce::dontSendNotification);
  roll.clearSelection();
  clean = var();
  ++toneVersion;toneReport = var();roll.tonal.clear();
  shortReport = var();
  rebuildNotes();
  if(!stackedView)syncNotes();
  if((bool)tone["enabled"]){toneOptions();analyzeTone(false);}
  edited();
  rebuildSide();
}
void App::prepare(Done done) {
  resetMidiInput(true);
  notesReady = false;++notesVersion;auto expectedAudio=audio["id"];
  invoke(
      "player_prepare", object({{"audioId", audio["id"]}}),
      [this, done,expectedAudio](var) {
        if(audio["id"]!=expectedAudio)return;preparedAudioId=expectedAudio;
        syncNotes(true, [this, done](var v) {
          if (done)
            done(v);
        });
      },
      true, [this](String) { setBusy(false); });
}
void App::syncNotes(bool initialise, Done done) {
  if (audio.isVoid()||preparedAudioId!=audio["id"])
    return;
  notesReady = false;
  auto version = ++notesVersion;
  auto a = with(args(),"part",var());
  a = with(a, "analysisId", result.isVoid() ? var() : result["analysis_id"]);
  a = with(a, "version", (juce::int64)version);
  invoke(
      "player_notes", a,
      [this, version, initialise, done](var v) {
        if (version != notesVersion)
          return;
        auto ready = [this, done, v,version](var) {
          if(version!=notesVersion)return;
          notesReady = true;
          syncVoiceLevels();
          refresh();
          if (done)
            done(v);
        };
        if (initialise)
          invoke("player_initialize_position",
                 object({{"audioId", audio["id"]}}), ready);
        else
          ready(v);
      },
      false,
      [this](String) {
        notesReady = false;
        if(midiCommitting){midiCommitting=false;resetMidiInput(true);setBusy(false);}
        if(midiRecordPending){midiRecordPending=false;setBusy(false);}
        refresh();
      });
}
void App::syncVoiceLevels(){
  if(audio.isVoid()||!notesReady)return;
  if(voiceLevelPending){voiceLevelDirty=true;return;}voiceLevelPending=true;voiceLevelDirty=false;
  auto complete=[this]{voiceLevelPending=false;if(voiceLevelDirty)syncVoiceLevels();};
  invoke("player_voice_levels",object({{"audioId",audio["id"]},{"levels",voiceLevels.clone()}}),[complete](var){complete();},false,[complete](String){complete();});
}
void App::control(const String &action, double p, Done done) {
  if (audio.isVoid())
    return;
  if(midiRecording){if(action=="stop"||action=="pause"||action=="toggle")stopMidiRecording([done]{if(done)done(var());});return;}
  if (action == "seek") {
    if (seekInFlight) {
      pendingSeek = p;
      return;
    }
    seekInFlight = true;
  }
  if (action == "play" || action == "toggle") {
    resetMidiInput(true);syncClick();
    if (!notesReady)
      return;
    invoke("player_mix",
           with(with(with(args(), "mode", mode), "audioDb", audioDb), "midiDb",
                midiDb));
    invoke("player_reverb",
           with(args(), "amount", reverbEnabled ? reverbAmount : 0));
  }
  invoke(
      "player_control", with(with(args(), "action", action), "position", p),
      [this, done, action](var v) {
        nextPlayerPoll=0;
        if (action == "seek") {
          seekInFlight = false;
          if (pendingSeek >= 0) {
            double next = pendingSeek;
            pendingSeek = -1;
            control("seek", next);
          }
        }
        if (done)
          done(v);
      },
      false,
      [this, action](String) {
        if (action == "seek") {
          seekInFlight = false;
          pendingSeek = -1;
        }
      });
}
void App::command(const var &action, Done done, bool preserveReview) {
  if (locked || result.isVoid())
    return;
  setBusy(true);
  listenEnd = -1;
  control("pause", 0, [this, action, done, preserveReview](var) {
    invoke(
        action["kind"].toString()=="diagnostic_batch" ? "edit_notes_batch" : "edit_notes",
        action["kind"].toString()=="diagnostic_batch" ? with(with(args(), "actions", action["actions"]),"new_voice",action["new_voice"]) : with(args(), "action", action),
        [this, action, done, preserveReview](var v) {
          auto kind = action["kind"].toString();
          if (kind == "undo" || kind == "redo" || kind == "delete" ||
              kind == "delete_part" || kind == "transfer" || kind == "new_part" ||
              kind == "set_instrument" || kind == "diagnostic_batch")
            roll.clearSelection();
          if(!v["created_part"].isVoid())partId=(int)v["created_part"];
          verificationStale = !preserveReview;
          auto visibleStart=roll.start(),visibleSpan=roll.span();
          roll.setScoreDuration(number(v,"duration",number(audio,"duration",1)));
          updateResult(v);roll.frameTime(visibleStart,visibleSpan);
          setBusy(false, tr(juce::String::fromUTF8(
                             "Résultat mis à jour · Ctrl+Z pour annuler.")));
          if (done)
            done(v);
        },
        false, [this](String) { roll.cancelTimelinePreview();setBusy(false); });
  });
}
void App::copyNotes() {
  if(locked || roll.selected.empty() || result.isVoid())return;
  auto muted=idSet(result["editing"]["muted_notes"]);
  std::vector<var> copied;
  for(auto n:items(result["editing"]["notes"]))if(roll.selected.count((juce::int64)n["id"])) {
    n=n.clone();n.getDynamicObject()->setProperty("muted",muted.count((juce::int64)n["id"])>0);
    copied.push_back(n);
  }
  if(copied.empty())return;
  noteClipboard=array(copied);
  status.setText(localisation::count("notes_copied",int(copied.size())),juce::dontSendNotification);
}
void App::pasteNotes() {
  if(locked || result.isVoid() || !noteClipboard.isArray() || noteClipboard.size()==0)return;
  std::set<std::int64_t> before;
  for(const auto& n:items(result["editing"]["notes"]))before.insert((juce::int64)n["id"]);
  bool otherVoice=false;double first=1200;
  for(const auto& value:items(noteClipboard)){auto note=Note::parse(value);first=std::min(first,note.start);otherVoice|=partId>=0&&note.part!=partId;}
  command(object({{"kind","paste_notes"},{"notes",noteClipboard.clone()},
                  {"at",otherVoice?first:number(playback,"position")},{"part",partId>=0?var(partId):var()}}),
    [this,before](var){
      std::set<std::int64_t> added;
      for(const auto& n:items(result["editing"]["notes"]))if(!before.count((juce::int64)n["id"]))added.insert((juce::int64)n["id"]);
      roll.selectIds(added);refresh();
    });
}
void App::editNote(const Note &n) {
  command(object({{"kind", "modify"},
                  {"id", (juce::int64)n.id},
                  {"pitch", n.pitch},
                  {"start", n.start},
                  {"end", n.end},
                  {"velocity", n.velocity}}),
          [this, id = n.id](var) { roll.selectIds({id}); });
}
void App::addNote(double t, int p) {
  if(roll.lockedParts.count(partId))return;
  if (locked || !roll.editable || t >= roll.duration())
    return;
  if(result.isVoid()) {
    setBusy(true,tr(String::fromUTF8("Création du MIDI vierge…")));
    invoke("create_blank_midi",object({{"audioId",audio["id"]}}),[this,t,p](var v) {
      referenceAudio=true;mode=2;
      updateResult(v);
      syncNotes(false,[this,t,p](var) {
        setBusy(false);
        addNote(t,p);
      });
    },false,[this](String){setBusy(false);});
    return;
  }
  var target;
  for (auto &v : items(result["midi_parts"]))
    if (partId < 0 ? !(bool)v["is_drum"]
                   : (v["identity"].isVoid() ? (int)v["program"]
                                             : (int)v["identity"]) == partId) {
      target = v;
      break;
    }
  if (target.isVoid())
    target =
        object({{"program", 0}, {"is_drum", false}, {"label", tr(String::fromUTF8("Piano droit"))}});
  std::set<std::int64_t> before;
  for (auto &n : items(result["editing"]["notes"]))
    before.insert((juce::int64)n["id"]);
  command(object({{"kind", "add"},
                  {"pitch", p},
                  {"start", t},
                  {"end", std::min(t + .5, roll.duration())},
                  {"velocity", 83},
                  {"part", target}}),
          [this, before](var) {
            for (auto &n : roll.notes)
              if (!before.count(n.id)) {
                roll.selectIds({n.id});
                audition(n);
                break;
              }
          });
}
void App::audition(const Note &n) {
  invoke("player_audition",
         with(with(with(with(args(), "pitch", n.pitch), "velocity", n.velocity), "voice",n.part),
              "program", n.program));
}
void App::removeNotes(std::set<std::int64_t> ids) {
  if (!verificationStale && ids.size() == 1 && roll.extra.count(*ids.begin())) {
    for (auto &e : items(verification["existing"]))
      if ((juce::int64)e["id"] == *ids.begin()) {
        applyDiagnostic("extra", e);
        return;
      }
  }
  if (!ids.empty())
    command(object({{"kind", "delete"}, {"ids", idsVar(ids)}}));
}
bool App::canMerge(const std::set<std::int64_t> &ids) const {
  if (ids.size() < 2)
    return false;
  std::vector<Note> ns;
  for (auto &n : roll.notes)
    if (ids.count(n.id))
      ns.push_back(n);
  if (ns.size() != ids.size())
    return false;
  auto first = *std::min_element(
      ns.begin(), ns.end(), [](auto &a, auto &b) { return a.start < b.start; });
  double end = 0;
  for (auto &n : ns) {
    if (n.pitch != first.pitch || n.part != first.part)
      return false;
    end = std::max(end, n.end);
  }
  return !std::any_of(roll.notes.begin(), roll.notes.end(), [&](auto &n) {
    return !ids.count(n.id) && n.pitch == first.pitch && n.part == first.part &&
           n.start < end - .000001 && n.end > first.start + .000001;
  });
}
void App::mergeNotes(std::set<std::int64_t> ids) {
  if (!verificationStale)
    for (auto &h : roll.holds)
      if (idSet(h["ids"]) == ids) {
        applyDiagnostic("hold", h);
        return;
      }
  if (canMerge(ids)) {
    std::set<std::int64_t> before;
    for (auto &n : roll.notes)
      before.insert(n.id);
    command(object({{"kind", "merge"}, {"ids", idsVar(ids)}}),
            [this, before](var) {
              for (auto &n : roll.notes)
                if (!before.count(n.id)) {
                  roll.selectIds({n.id});
                  break;
                }
            });
  } else
    status.setText(tr(juce::String::fromUTF8("Sélectionnez des notes consécutives "
                                          "de même hauteur et même partie.")),
                   juce::dontSendNotification);
}
void App::applyPlayerSnapshot(var v) {
  bool controls=false;
  for(auto key:{"ready","playing","error","sound_bank","device","sample_rate","latency_ms"})controls|=playback[key]!=v[key];
  playback=v;updateMidiClock(v);
  auto* window=findParentComponentOfClass<juce::DocumentWindow>();
  bool visible=isShowing()&&!(window&&window->isMinimised());
  bool restoreTransport=visible&&bool(getProperties()["transportNeedsPaint"]);
  if(!visible)getProperties().set("transportNeedsPaint",true);
  else getProperties().remove("transportNeedsPaint");
  roll.updatePlayback(v,visible);
  if(restoreTransport){roll.resized();roll.repaint();}
  if(stackedView&&stacked)for(int key:stacked->order){auto& lane=stacked->editorFor(key);double previous=lane.view.pan;lane.view.pan=roll.view.pan;lane.updatePlayback(v,visible);if(visible&&(restoreTransport||previous!=lane.view.pan)){lane.resized();lane.repaint();}}
  if(visible)position.setText(timeName(number(v,"position")),juce::dontSendNotification);
  if(controls){
    device.setMouseCursor(v["sound_bank"].toString().contains(tr(String::fromUTF8("General MIDI absente")))?juce::MouseCursor::PointingHandCursor:juce::MouseCursor::NormalCursor);
    device.setText(v["error"].toString().isNotEmpty()?v["error"].toString():tr(v["sound_bank"].toString())+String::fromUTF8(" · ")+v["device"].toString()+String::fromUTF8(" · ")+String(number(v,"sample_rate"),0)+String::fromUTF8(" Hz · ")+String(number(v,"latency_ms"),1)+" ms",juce::dontSendNotification);
    refresh();
  }
  if(listenEnd>=0&&bool(v["playing"])&&number(v,"position")>=listenEnd){listenEnd=-1;control("pause");}
}
void App::timerCallback() {
  restorePointerAfterInterruption();
  updateClickVisuals();
  if(midiRecording){if(midiCounting&&juce::Time::getMillisecondCounterHiRes()/1000.>=midiCaptureStartTime){if(midiInput)midiInput->drain();finishMidiCountIn();}updateMidiPreview();updateTakeDuration();}
  if(timerCount%60==0){
    auto* modal=juce::Component::getCurrentlyModalComponent();auto* window=findParentComponentOfClass<juce::DocumentWindow>();
    String state="locked="+String(int(locked))+" polling="+String(int(polling))+" ready="+String(int(notesReady))+" closing="+String(int(closing))+" operation="+reportText+" modal_count="+String(juce::Component::getNumCurrentlyModalComponents())+" modal_showing="+String(int(modal&&modal->isShowing()))+" forms="+String(int(Form::activeForms.size()))+" minimised="+String(int(window&&window->isMinimised()))+" window_visible="+String(int(window&&window->isVisible()));
    state+=" blocked="+String(int(isCurrentlyBlockedByAnotherModalComponent()))+" app_enabled="+String(int(isEnabled()))+" window_active="+String(int(window&&window->isActiveWindow()));
    if(modal)state+=" modal_type="+String(typeid(*modal).name());
    phthongos::diagnostics::guiHeartbeat(state);
  }
  auto* playbackWindow=findParentComponentOfClass<juce::DocumentWindow>();
  bool visible=isShowing()&&!(playbackWindow&&playbackWindow->isMinimised());
  double pollNow=juce::Time::getMillisecondCounterHiRes();
  if (!polling && !audio.isVoid() && pollNow>=nextPlayerPoll) {
    bool precise=midiRecording||midiCounting||listenEnd>=0||seekInFlight;
    bool moving=bool(playback["playing"])||tempoPreview||precise;
    nextPlayerPoll=pollNow+(precise||visible&&moving?1000./60.:visible?250.:1000.);
    polling=true;++playerPolls;
    invoke("player_snapshot",object({}),[this](var v){polling=false;if(v["audio_id"]==audio["id"])applyPlayerSnapshot(v);},false,[this](String){polling=false;});
  }
  if (++timerCount % 30 == 0) {
    if (locked && calculationStarted > 0)
      calculation.setText(
          operationLabel + (initialisingProject?String():tr(String::fromUTF8(" · Temps écoulé : ")) +
              timeName((juce::Time::getMillisecondCounterHiRes() -
                        calculationStarted) /
                       1000.)),
          juce::dontSendNotification);
    invoke("host_events", object({}), [this](var events) {
      for (auto &e : items(events))
        if(locked&&guidedRunning&&e["event"].toString()=="guided-progress"&&e["payload"]["audio_id"]==audio["id"])operationLabel=tr(String::fromUTF8("Analyse guidée · "))+String(number(e["payload"],"progress")*100,0)+" %";
        else if (locked && (reportText == "transcription"||guidedRunning) &&
            e["event"].toString() == "muscriptor-progress" &&
            e["payload"]["audio_id"] == audio["id"]) {
          auto p = e["payload"];
          auto phase = p["phase"].toString();
          operationLabel =
              (phase == "verify" ? tr(String::fromUTF8("Vérification des poids"))
               : phase == "load" ? String(tr(String::fromUTF8("Chargement")))
               : phase == "transcribe" ? String("Transcription")
                                       : phase) +
              String::fromUTF8(" · ") + String(number(p, "progress") * 100, 0) +
              " %";
        }
    });
    if (!settingsPolling) {
      settingsPolling = true;
      invoke(
          "soundbank_status", object({}),
          [this](var v) {
            settingsPolling = false;
            bank = v;
          },
          false, [this](String) { settingsPolling = false; });
    }
  }
  if(spectrumAudio!=audio["id"].toString()) {
    spectrumAudio=audio["id"].toString(); spectrumKey={}; spectrumWanted={}; spectrumData=var();roll.spectrum=juce::Image();
  }
  if(visible && roll.displayMode!=0 && roll.spectral && !audio.isVoid() && !(bool)audio["midi_only"] && !locked) {
    double sa=roll.start(), sb=sa+roll.span();
    String key=audio["id"].toString()+":"+String(sa,6)+":"+String(sb,6);
    auto now=juce::Time::getMillisecondCounterHiRes();
    if(key!=spectrumWanted){spectrumWanted=key;spectrumChangedAt=now;}
    if(!spectrumPending && key!=spectrumKey && now-spectrumChangedAt>180) {
      spectrumPending=true; auto audioId=audio["id"]; auto gain=roll.visualGain;
      invoke("spectrogram",object({{"audioId",audioId},{"start",sa},{"end",sb},{"columns",std::min(768,std::max(64,(int)roll.layout().width))}}),
        [this,key,sa,sb,audioId,gain](var data){
          spectrumPending=false;
          if(audio["id"]!=audioId || key!=spectrumWanted) return;
          spectrumData=data;roll.spectrumStart=sa;roll.spectrumEnd=sb;spectrumKey=key;recolourSpectrum();
        },true,[this,key](String){spectrumPending=false;spectrumKey=key;});
    }
  }
  static double previousStart = -1, previousSpan = -1;
  static var previousAudio;
  double a = roll.start(), b = roll.span();
  if (visible && !audio.isVoid() && !(bool)audio["midi_only"] && roll.view.zoom > 1 &&
      (a != previousStart || b != previousSpan || previousAudio!=audio["id"])) {
    previousAudio=audio["id"];
    previousStart = a;
    previousSpan = b;
    auto version = ++detailVersion;
    double end=std::min(a+b,number(audio,"duration"));
    if(end<=a){roll.clearDetail();roll.repaint();return;}
    invoke("waveform_detail",
           object({{"audioId", audio["id"]},
                   {"start", a},
                   {"end", end},
                   {"bins",
                    std::min(4096, std::max(1, (int)roll.layout().width))}}),
           [this, a, end, version](var p) {
             if (version == detailVersion)
               roll.setDetail(p, a, end);
           });
  }
}
bool App::keyPressed(const juce::KeyPress &k, juce::Component *origin) {
  if(k==juce::KeyPress::escapeKey)if(auto* drag=dynamic_cast<MidiDragButton*>(buttonMap["export"]))if(drag->cancelPendingDrag())return true;
  if (confirmation)
    return false;
  if (dynamic_cast<juce::TextEditor *>(origin) ||
      juce::Component::getNumCurrentlyModalComponents() > 0)
    return false;
  // Roll/head handlers own transport arrows; never forward arrows from a
  // combo, slider, text field or menu into the piano roll.
  if(k.getKeyCode()==juce::KeyPress::rightKey&&!k.getModifiers().isAnyModifierKeyDown()&&midiStep.getToggleState()&&(origin==&roll||roll.isParentOf(origin))){advanceMidiStep();return true;}
  if(k.getKeyCode()==juce::KeyPress::leftKey||k.getKeyCode()==juce::KeyPress::rightKey)return false;
  auto mods = k.getModifiers();
  const int code = k.getKeyCode();
  const bool single = code == juce::KeyPress::spaceKey ||
                      code == juce::KeyPress::deleteKey || code == 'F' ||
                      code == 'M' || code == 'L' || (code == 'R' && !mods.isAnyModifierKeyDown()) ||
                      ((mods.isCtrlDown() || mods.isCommandDown()) &&
                       (code == 'S' || code == 'Z' || code == 'Y' || code == 'C' || code == 'V'));
  if (code=='L' && !mods.isAnyModifierKeyDown()) {
    if(origin!=&roll&&!(stackedView&&dynamic_cast<Roll*>(origin))) return false;
    if(!pressedKeys.count(code)) {pressedKeys.insert(code);linkNotes(roll.selected);}
    return true;
  }
  if (single) {
    if (pressedKeys.count(code))
      return true;
    pressedKeys.insert(code);
  }
  if(code=='R'&&!mods.isAnyModifierKeyDown()){
    if(midiRecordPending)return true;
    if(midiRecording||!midiRecorded.empty())stopMidiRecording();
    else if(midiRecord.isEnabled())startMidiRecording();
    return true;
  }
  if (mods.isCtrlDown() || mods.isCommandDown()) {
    if(code=='C'){copyNotes();return true;}
    if(code=='V'){pasteNotes();return true;}
    if (k.getKeyCode() == 'S') {
      saveProject(mods.isShiftDown());
      return true;
    }
    if (k.getKeyCode() == 'Z') {
      command(object({{"kind", mods.isShiftDown() ? "redo" : "undo"}}));
      return true;
    }
    if (k.getKeyCode() == 'Y') {
      command(object({{"kind", "redo"}}));
      return true;
    }
  }
  if (k == juce::KeyPress::spaceKey) {
    if(midiRecording){stopMidiRecording();return true;}
    if (transportPending || locked || !notesReady || !(bool)playback["ready"])
      return true;
    listenEnd = -1;
    transportPending = true;
    control("toggle", 0, [this](var) { transportPending = false; });
    return true;
  }
  if(stackedView&&stacked&&stacked->isParentOf(origin)&&dynamic_cast<Roll*>(origin))return false;
  if (origin != &roll && k == juce::KeyPress::escapeKey) {
    roll.tool = 0;
    refresh();
    return true;
  }
  if (origin != &roll && (mods.isCtrlDown() || mods.isCommandDown()) &&
      code == 'A')
    return false;
  if (origin != &roll)
    return roll.keyPressed(k);
  return false;
}
bool App::keyStateChanged(bool down, juce::Component *) {
  roll.cutHeld=juce::KeyPress::isKeyCurrentlyDown('C') && !juce::ModifierKeys::getCurrentModifiers().isAnyModifierKeyDown();
  roll.updateToolCursor();
  if(stackedView&&stacked)for(int key:stacked->order){auto& lane=stacked->editorFor(key);lane.cutHeld=roll.cutHeld;lane.updateToolCursor();}
  if (!down) {
    for (auto it = pressedKeys.begin(); it != pressedKeys.end();)
      if (!juce::KeyPress::isKeyCurrentlyDown(*it))
        it = pressedKeys.erase(it);
      else
        ++it;
  }
  return false;
}

void App::mouseDown(const juce::MouseEvent &e) {
  if(e.mods.isPopupMenu()) {
    if(title.getBounds().contains(e.position.toInt())&&!locked&&!audio.isVoid()) {
      juce::PopupMenu menu;menu.addItem(1,tr(String::fromUTF8("Renommer le projet…")));
      menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&title),[s=safe()](int id){if(s&&id==1)s->renameProject();});
    }
    return;
  }
  if(e.eventComponent==&device) {
    if(playback["sound_bank"].toString().contains(tr(String::fromUTF8("General MIDI absente")))) settings();
    return;
  }
  if (e.position.y < 120)
    if (auto *w = findParentComponentOfClass<juce::DocumentWindow>())
      windowDragger.startDraggingComponent(w, e);
}
void App::mouseDrag(const juce::MouseEvent &e) {
  if (e.getMouseDownPosition().y < 120)
    if (auto *w = findParentComponentOfClass<juce::DocumentWindow>())
      windowDragger.dragComponent(w, e, nullptr);
}
void App::goBoundary(bool end) {
  if(locked || !(bool)playback["ready"] || !notesReady)return;
  double position=end?std::max(number(result,"duration"),number(audio,"duration")):0;
  listenEnd=-1;
  roll.view.pan=!end || roll.view.zoom==1 ? 0 : View::clamp((position-roll.span()*.8)/(roll.duration()-roll.span()),0,1);
  control("seek",position);refresh();
}
void App::mouseDoubleClick(const juce::MouseEvent &e) {
  if(e.mods.isPopupMenu())return;
  if(e.position.x<roll.getX()+58 && e.position.y>=roll.getY() && e.position.y<roll.getBottom()) {
    goBoundary(false);return;
  }
  if (e.position.y < 120)
    if (auto *w = findParentComponentOfClass<juce::DocumentWindow>())
      w->setFullScreen(!w->isFullScreen());
}
