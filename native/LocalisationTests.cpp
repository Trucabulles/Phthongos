#include "App.h"
bool App::runLocalisationTests(){
  auto original=localisation::language();auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-languages-"+juce::Uuid().toString());
  std::vector<var> checks;bool passed=false;
  auto check=[&](bool ok,const String& label){checks.push_back(object({{"name",label},{"passed",ok}}));if(!ok)throw std::runtime_error(label.toStdString());};
  try{
    check(localisation::catalogSize()>1000,"Complete embedded catalog loaded");
    for(auto language:{localisation::Language::french,localisation::Language::english}){
      localisation::setLanguage(language);bool english=language==localisation::Language::english;
      App app(folder.getChildFile(localisation::code()+"/audio"));app.setSize(1400,900);
      check(app.buttonMap["open"]->getButtonText()==(english?"Open audio":"Ouvrir audio"),"Open button follows language");
      check(app.buttonMap["play"]->getButtonText().contains(english?"Play":"Lecture"),"Transport follows language");
      check(app.follow.getButtonText()==(english?"Follow playback":"Suivre la lecture"),"Follow playback follows language");
      check(tr(String::fromUTF8("Spectrogramme"))==(english?"Spectrogram":"Spectrogramme"),"Spectrogram selection follows language");
      check(tr(String::fromUTF8("Supprimer"))==(english?"Delete":"Supprimer")&&tr(String::fromUTF8("Muet"))==(english?"Mute":"Muet")&&tr(String::fromUTF8("Aucun"))==(english?"None":"Aucun"),"Delete, mute and empty pre-count choice follow language");
      check(tr(String::fromUTF8("Licence de Phthongos"))==(english?"Phthongos license":"Licence de Phthongos"),"Application license document follows language");
      check(tr(String::fromUTF8("Vérifier le MIDI avec l’audio"))==(english?"Check MIDI against audio":String::fromUTF8("Vérifier le MIDI avec l’audio")),"Diagnostic heading follows language");
      check(localisation::count("notes",1)=="1 note"&&localisation::count("notes",2)=="2 notes","Count singular and plural");
      check(localisation::count("notes_added",1).contains(english?"note added":"note ajoutée")&&localisation::count("notes_added",2).contains(english?"notes added":"notes ajoutées"),"Gender and plural in dynamic status");
      auto name=String::fromUTF8("Fermer — voix {1} é / ç.wav");
      check(localisation::format("exported_notes",{"3",name}).endsWith(name),"Formatting preserves arbitrary names and placeholder-looking user text");
      check(localisation::error(String::fromUTF8("Fichier introuvable : ")+name)==(english?String("File not found: "):String::fromUTF8("Fichier introuvable : "))+name,"Error parameter is preserved");
      auto personal=object({{"identity",256},{"program",0},{"label",String::fromUTF8("Piano droit")}});
      check(displayPartLabel(personal)==String::fromUTF8("Piano droit"),"Custom voice names are never translated");
      check(displayPartLabel(object({{"program",0},{"label",String::fromUTF8("Piano droit")}}))==(english?"Upright piano":"Piano droit"),"Automatic General MIDI names are localized");
      auto raw=object({{"notes",array({object({{"part",personal}})})},{"warnings",array({String::fromUTF8("Aucun audio associé.")})},{"notice",String::fromUTF8("Transcription terminée.")}});
      auto before=juce::JSON::toString(raw);auto translated=localisation::report(raw);
      check(juce::JSON::toString(raw)==before&&translated["notes"][0]["part"]["label"]==personal["label"],"Presentation translation leaves source and project metadata untouched");
      check(translated["warnings"][0].toString()==(english?"No associated audio.":String::fromUTF8("Aucun audio associé.")),"Service warnings follow language");
      ConfirmOverlay confirmation(tr("Projet modifié"),tr("Continuer sans enregistrer"),tr("Enregistrer"),tr("Continuer"));confirmation.setSize(1160,800);
      bool foundCancel=false;for(auto* child:confirmation.getChildren())if(auto* b=dynamic_cast<juce::TextButton*>(child))foundCancel|=b->getButtonText()==(english?"Cancel":"Annuler");
      check(foundCancel,"Confirmation buttons follow language");
      for(auto dimensions:{juce::Point<int>(1400,900),juce::Point<int>(1160,800)}){
        app.setSize(dimensions.x,dimensions.y);check(app.buttonMap["open"]->getBounds().getRight()<app.getWidth()&&app.buttonMap["help"]->getBounds().getBottom()<=app.getHeight(),"Localized controls fit application frame");
      }
      auto images=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results");images.createDirectory();juce::FileOutputStream stream(images.getChildFile("language-"+localisation::code()+".png"));stream.setPosition(0);stream.truncate();juce::PNGImageFormat().writeImageToStream(app.createComponentSnapshot(app.getLocalBounds()),stream);
    }
    // Change language on the same populated editor, with an open, unsaved form.
    localisation::setLanguage(localisation::Language::french);
    {
      App app(folder.getChildFile("live/audio"));
      auto personal=object({{"identity",256},{"program",0},{"label","Fermer"}});
      auto note=object({{"id",42},{"pitch_midi",60},{"velocity_midi",83},{"start_time_s",1.25},{"end_time_s",2.5},{"part",personal}});
      app.audio=object({{"id","live"},{"name","Fermer.wav"},{"duration",10.0}});
      app.result=object({{"engine","midi"},{"midi_parts",array({personal})},{"editing",object({{"notes",array({note})},{"revision",7},{"can_undo",true},{"can_redo",true}})}});
      app.partId=256;app.rebuildNotes();app.roll.selected={42};app.roll.view.zoom=4;app.roll.view.pan=.3;app.roll.view.pitchZoom=2;app.roll.view.pitchPan=.4;
      app.roll.tool=1;app.roll.playback=object({{"position",1.5}});app.dirty=true;app.openSections.insert("guided");app.toneOptions();
      auto before=juce::JSON::toString(app.result),view=juce::JSON::toString(app.roll.view.json());
      Form form;form.text(tr("Nom"),"name","Fermer");form.textFields["name"]->setText(String::fromUTF8("Fermer — unsaved {1}"),false);form.textFields["name"]->onTextChange();
      form.compactChoice(tr("Note la plus grave"),"low",{{Roll::noteLabel(60),60}},60);
      auto* pitchPicker=dynamic_cast<InstrumentPicker*>(form.compactFields["low"]);pitchPicker->displayName=[](var pitch){return Roll::noteLabel((int)pitch);};
      form.compactChoice("Fermer","target0",{{tr("Créer une nouvelle voix"),"new"},{"Fermer",256}},256);
      auto* close=form.button(tr("Fermer"),[]{});form.combo(tr("Calcul"),"device",{{tr("Automatique"),"auto"},{"CPU","cpu"}},"cpu");
      form.comboFields["device"]->setSelectedId(1,juce::dontSendNotification);form.comboFields["device"]->onChange();form.comboFields["device"]->setItemEnabled(2,false);auto formState=juce::JSON::toString(form.state());
      for(auto target:{localisation::Language::english,localisation::Language::french}){
        app.applyLanguage(target);bool en=target==localisation::Language::english;
        check(app.buttonMap["export"]->getButtonText()==(en?"Export MIDI":"Exporter MIDI")&&app.follow.getButtonText()==(en?"Follow playback":"Suivre la lecture"),"Existing editor controls switch immediately");
        check(pitchPicker->getButtonText()==Roll::noteLabel(60)+String::fromUTF8(" ▾")&&dynamic_cast<InstrumentPicker*>(form.compactFields["target0"])->getButtonText()==String::fromUTF8("Fermer ▾"),"Open note pickers follow language while personal destination names stay intact");
        check(close->getButtonText()==(en?"Close":"Fermer")&&form.comboFields["device"]->getItemText(0)==(en?"Automatic":"Automatique")&&form.comboFields["device"]->getText()==(en?"Automatic":"Automatique"),"Open form labels and choices switch immediately");
        check(form.textFields["name"]->getText()==String::fromUTF8("Fermer — unsaved {1}")&&juce::JSON::toString(form.state())==formState&&!form.comboFields["device"]->isItemEnabled(2),"Open form drafts, values and disabled choices survive language change");
        check(juce::JSON::toString(app.result)==before&&app.partId==256&&app.part.getText()=="Fermer"&&app.roll.notes.front().label=="Fermer","Document, revisions, Undo/Redo and personal voice names stay intact");
        check(app.roll.selected==std::set<std::int64_t>{42}&&juce::JSON::toString(app.roll.view.json())==view&&app.roll.tool==1&&number(app.roll.playback,"position")==1.5&&app.dirty&&app.openSections.count("guided"),"Selection, zoom, pan, tool, playhead, dirty flag and open sections survive round trip");
      }
      app.dirty=false;app.applyLanguage(localisation::Language::english);check(!app.dirty,"Changing language never marks a saved document modified");
    }
    auto fresh=folder.getChildFile("no-language-choice");
    localisation::initialise(fresh);check(localisation::savedChoice(fresh)=="en"&&localisation::language()==localisation::Language::english,"Fresh installations default to English without a saved preference");
    fresh.getChildFile("data").createDirectory();fresh.getChildFile("data/language.json").replaceWithText("{invalid");localisation::initialise(fresh);check(localisation::language()==localisation::Language::english,"Unreadable language preference falls back to English");
    fresh.getChildFile("data/language.json").replaceWithText("{\"language\":\"xx\"}");localisation::initialise(fresh);check(localisation::language()==localisation::Language::english,"Unsupported stored language falls back to English");
    localisation::saveChoice(folder,"en");check(localisation::savedChoice(folder)=="en","English preference persisted outside PHT");localisation::initialise(folder);check(localisation::language()==localisation::Language::english,"English preference restored at startup");
    localisation::saveChoice(folder,"fr");localisation::initialise(folder);check(localisation::language()==localisation::Language::french,"French preference restored at startup");
    bool rejected=false;try{localisation::saveChoice(folder,"xx");}catch(...){rejected=true;}check(rejected&&localisation::savedChoice(folder)=="fr","Invalid language cannot overwrite valid preference");
    passed=true;
  }catch(const std::exception& e){checks.push_back(object({{"error",String::fromUTF8(e.what())},{"passed",false}}));}
  localisation::setLanguage(original);auto output=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("test-results/localisation-latest.json");output.getParentDirectory().createDirectory();output.replaceWithText(juce::JSON::toString(object({{"passed",passed},{"checks",array(checks)}}),false,17));return passed;
}
