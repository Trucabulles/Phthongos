#include "ActionMenu.h"
#include "App.h"
#include "../core/services/Resources.h"
#include "../core/midi/Tempo.h"
namespace {
var put(var a, const String &k, var v) {
  a = a.clone();
  a.getDynamicObject()->setProperty(k, v);
  return a;
}
} // namespace
void App::loadSavePreferences(){
  auto value=juce::JSON::parse(savePreferencesFile.loadFileAsString());
  timestampSaves=value["timestamped"].isBool()&&(bool)value["timestamped"];
  auto folder=value["folder"].toString();defaultProjectFolder=juce::File::isAbsolutePath(folder)?folder:String();
}
void App::storeSavePreferences(bool enabled,const String& folder){
  auto path=folder.trim();
  if(path.isNotEmpty()&&(!juce::File::isAbsolutePath(path)||!juce::File(path).isDirectory()))throw std::runtime_error(tr(juce::String::fromUTF8("Choisissez un dossier de projet existant, ou laissez le champ vide.")).toStdString());
  if(savePreferencesFile.getParentDirectory().createDirectory().failed())throw std::runtime_error(tr(juce::String::fromUTF8("Enregistrement des préférences impossible.")).toStdString());
  juce::TemporaryFile temp(savePreferencesFile);
  if(!temp.getFile().replaceWithText(juce::JSON::toString(object({{"timestamped",enabled},{"folder",path}})))||!temp.overwriteTargetFileWithTemporary())throw std::runtime_error(tr(juce::String::fromUTF8("Enregistrement des préférences impossible.")).toStdString());
  timestampSaves=enabled;defaultProjectFolder=path;
}
void App::settings() {
  resetMidiInput();
  if (locked || settingsOpening)
    return;
  if(settingsForm){if(auto* w=settingsForm->getTopLevelComponent())w->toFront(true);return;}
  settingsOpening=true;
  invoke("audio_outputs", object({}), [this](var outputs) {
    settingsOpening=false;
    auto f = juce::Component::SafePointer<Form>(new Form);settingsForm=f;
    auto appliedOutput=std::make_shared<var>(outputs["selected"]);
    auto folder=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    f->combo(tr(String::fromUTF8("Langue / Language")),"language",{{String::fromUTF8("Français"),"fr"},{"English","en"}},localisation::code(),[this,f,folder](var value){
      try{localisation::saveChoice(folder,value.toString());applyLanguage(value.toString()=="en"?localisation::Language::english:localisation::Language::french);status.setText(tr(String::fromUTF8("Langue appliquée et préférence enregistrée.")),juce::dontSendNotification);}
      catch(const std::exception& e){if(f)f->setHeading(localisation::error(String::fromUTF8(e.what())));}
    });
    f->label(tr(String::fromUTF8("La langue choisie s’applique immédiatement.")));
    f->toggle(tr(String::fromUTF8("Incrémenter les sauvegardes")),"timestamped",timestampSaves);
    f->text(tr(String::fromUTF8("Dossier de projet par défaut (facultatif)")),"project_folder",defaultProjectFolder);
    f->button(tr(String::fromUTF8("Choisir le dossier de projet…")),[this,f]{
      auto initial=f->state()["project_folder"].toString();
      chooser=std::make_unique<juce::FileChooser>(tr(String::fromUTF8("Dossier de projet")),juce::File::isAbsolutePath(initial)?juce::File(initial):juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),"*",true);
      chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[f](const juce::FileChooser& c){if(f&&c.getResult()!=juce::File())f->setValue("project_folder",c.getResult().getFullPathName());});
    });
    f->label(tr(String::fromUTF8("Champ vide : choisir au premier enregistrement. Un projet existant conserve son dernier dossier. Les versions horodatées conservent les fichiers précédents.")));
    bool editor=false;
    addThirdPartySettings(f);
    std::vector<std::pair<String, var>> choices{
        {tr(juce::String::fromUTF8("Par défaut du système")), var()}};
    for (auto &d : items(outputs["devices"]))
      choices.emplace_back(d["name"].toString() +
                               ((bool)d["is_default"]
                                    ? tr(juce::String::fromUTF8(" · par défaut"))
                                    : ""),
                           d["id"]);
    f->combo(tr(String::fromUTF8("Sortie audio")), "output", choices, outputs["selected"]);
    midiSettings(f);
    addSoundbankSettings(f);
    addCustomSoundfonts(f);
    auto s = safe();
    auto saving=std::make_shared<bool>(false);
    f->onApply = [s, f, editor, appliedOutput, saving](var v) {
      if (!s || !f || *saving)return;
      *saving=true;f->setApplyEnabled(false);
      auto failed=[s,f,saving](String error){*saving=false;if(f){f->setApplyEnabled(true);f->setHeading(error);}if(s)s->status.setText(error,juce::dontSendNotification);};
      try{
        if(s->midiInput)s->midiInput->choose(v["midi_input"].toString());
        if(s->timestampSaves!=(bool)v["timestamped"]||s->defaultProjectFolder!=v["project_folder"].toString().trim())
          s->storeSavePreferences((bool)v["timestamped"],v["project_folder"].toString());
      }catch(const std::exception& e){failed(localisation::error(String::fromUTF8(e.what())));return;}
      auto finish=[s,f]{if(!s)return;s->status.setText(tr(String::fromUTF8("Paramètres enregistrés.")),juce::dontSendNotification);if(f)f->close();};
      auto configure=[s,f,editor,v,failed,finish]{
        if(!s)return;
        finish();
      };
      if(v["output"]==*appliedOutput){configure();return;}
      f->setHeading(tr(String::fromUTF8("Application de la sortie audio…")));
      s->invoke("audio_output_select",object({{"id",v["output"]}}),[s,v,appliedOutput,configure,failed](var){
        if(!s)return;*appliedOutput=v["output"];
        try{phthongos::services::saveJson(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("data/audio-output.json"),v["output"]);}
        catch(const std::exception& e){failed(localisation::error(String::fromUTF8(e.what())));return;}
        configure();
      },false,failed);
    };
    f->actions(tr(String::fromUTF8("Enregistrer")),tr(String::fromUTF8("Annuler")));
    f->show(tr(juce::String::fromUTF8("Paramètres")));
  },false,[this](String){settingsOpening=false;});
}
void App::addSoundbankSettings(juce::Component::SafePointer<Form> f) {
  auto b = bank;
  f->label(tr(juce::String::fromUTF8(
      "Piano droit fourni · Upright Piano KW\nBanque optionnelle : MuseScore "
      "General HQ 0.2.1 — SF2")));
  f->label(tr(String::fromUTF8("Choix automatique par partie : piano droit pour le piano, General MIDI pour les voix et les autres instruments.")));
  f->label(b["path"].toString(),false);
  auto *downloadButton =
      f->button(tr(juce::String::fromUTF8("Télécharger")), [this] {
        invoke(
            "soundbank_download", object({}),
            [this](var v) {
              bank = v;
              status.setText(tr(juce::String::fromUTF8("Banque installée.")),
                             juce::dontSendNotification);
            },
            true);
      });
  f->button(tr(juce::String::fromUTF8("Parcourir…")), [this] {
    choose(tr(juce::String::fromUTF8("Choisir MuseScore General HQ 0.2.1 — SF2")),
           false, "*.sf2", [this](String path) {
             invoke("soundbank_select",
                    object({{"path", path}, {"useHq", true}}),
                    [this](var v) { bank = v; });
           });
  });
  auto *cancelButton =
      f->button(tr(juce::String::fromUTF8("Annuler le téléchargement")),
                [this] { invoke("soundbank_cancel", object({})); });
  auto app = safe();
  downloadButton->setEnabled(!(bool)b["available"] && !(bool)b["busy"]);
  cancelButton->setVisible((bool)b["busy"]);
  f->live([app, f, downloadButton, cancelButton] {
    if (!app || !f)
      return String();
    bool busy = (bool)app->bank["busy"],
         available = (bool)app->bank["available"];
    downloadButton->setEnabled(!busy && !available);
    downloadButton->setButtonText(available
                                      ? tr(String::fromUTF8("Banque disponible"))
                                      : tr(String::fromUTF8("Télécharger")));
    cancelButton->setVisible(busy);
    auto p = app->bank["progress"];
    if (!busy) {
      if (p["error"].toString().isNotEmpty())
        return p["error"].toString();
      return available ? tr(String::fromUTF8("Banque disponible · ")) +
                             app->bank["path"].toString()
                       : tr(String::fromUTF8("Banque General MIDI absente : téléchargez-la ou sélectionnez un fichier déjà installé avec Parcourir…"));
    }
    auto phase = p["phase"].toString();
    if (phase == "download")
      return number(p, "total") > 0
                 ? tr(String::fromUTF8("Téléchargement · ")) +
                       String(100 * number(p, "received") / number(p, "total"),
                              0) +
                       " %"
                 : tr(String::fromUTF8("Connexion au serveur de téléchargement…"));
    if (phase == "verify_package" || phase == "verify_bank")
      return tr(String::fromUTF8("Vérification du fichier…"));
    if (phase == "extract")
      return tr(String::fromUTF8("Extraction de la banque…"));
    return tr(String::fromUTF8("Préparation du téléchargement…"));
  });
}
void App::partDialog(bool create,int target) {
  if(!create&&target>=0){customInstrument(target);return;}
  if (locked || (roll.selected.empty()&&(create||target<0)))
    return;
  auto ids = roll.selected;
  auto f = juce::Component::SafePointer<Form>(new Form);
  if (create)
    f->text(tr(String::fromUTF8("Nom de la nouvelle partie")), "name", tr(String::fromUTF8("Nouvelle partie")));
  else {
    int selectedPart = target, program = 0;
    if(target>=0){
      bool found=false;for(const auto& p:items(result["midi_parts"]))if((p["identity"].isVoid()?(int)p["program"]:(int)p["identity"])==target){program=(int)p["program"];found=true;break;}
      if(!found||idSet(result["editing"]["locked_parts"]).count(target)){delete f.getComponent();return;}
    }
    if(target<0)for (auto &n : roll.notes)
      if (ids.count(n.id)) {
        if (selectedPart >= 0 && selectedPart != n.part) {
          status.setText(
              tr(juce::String::fromUTF8("Choisissez des notes d’une même partie.")),
              juce::dontSendNotification);
          delete f.getComponent();
          return;
        }
        selectedPart = n.part;
        program = n.program;
      }
    f->setValue("part", selectedPart);
    std::vector<std::pair<String, var>> choices;
    auto names = juce::JSON::parse(
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getSiblingFile("player-assets/gmPrograms.json"));
    auto list = items(names);
    for (int i = 0; i < (int)list.size(); ++i) {
      if (i!=program&&((i >= 96 && i <= 103) || (i >= 120 && i <= 127)))
        continue;
      if (!(bool)bank["active"] && i != 0 && i!=program)
        continue;
      choices.emplace_back(tr(list[(size_t)i].toString()), i);
    }
    f->instrument("program", choices, program);
    f->label(tr(String::fromUTF8("Le nom de la voix est conservé. Seul son instrument change.")));
    if (!(bool)bank["active"])
      f->label(tr(juce::String::fromUTF8(
          "Autres instruments disponibles dans Paramètres → Banque sonore.")));
  }
  auto s = safe();
  f->onApply = [s, f, create, ids](var v) {
    if (!s)
      return;
    var action = create ? object({{"kind", "new_part"},
                                  {"ids", idsVar(ids)},
                                  {"name", v["name"]}})
                        : object({{"kind", "set_instrument"},
                                  {"part", v["part"]},
                                  {"program", v["program"]},{"keep_name",true}});
    std::set<int> before;
    for (auto &p : s->parts)
      before.insert(p.first);
    bool hidden=!create&&s->hiddenParts.count((int)v["part"]);
    auto oldLevel=s->voiceLevels.getProperty(juce::Identifier(String((int)v["part"])),var());
    s->command(action, [s, f, create, before, v,hidden,oldLevel](var) {
      if (!s)
        return;
      if (create) {
        for (auto &p : s->parts)
          if (!before.count(p.first)) {
            s->selectPart(p.first);
            break;
          }
      } else {
        int target=(int)v["part"];
        if(std::none_of(s->parts.begin(),s->parts.end(),[&](const auto& p){return p.first==target;}))for(const auto& p:s->parts)if(!before.count(p.first)){target=p.first;break;}
        if(hidden)s->hiddenParts.insert(target);
        if(!oldLevel.isVoid())s->voiceLevels.getDynamicObject()->setProperty(String(target),oldLevel);
        s->selectPart(target);
      }
      if (f != nullptr)
        f->close();
    });
  };
  f->show(create
              ? tr(juce::String::fromUTF8("Créer une partie depuis la sélection"))
              : tr(juce::String::fromUTF8("Réaffecter l’instrument")));
}
void App::manualDialog() {
  auto f = juce::Component::SafePointer<Form>(new Form);
  std::optional<Note> note;
  for (auto &n : roll.notes)
    if (roll.selected.count(n.id)) {
      if (note) {
        note.reset();
        break;
      }
      note = n;
    }
  f->text(tr(String::fromUTF8("Hauteur MIDI")), "pitch", note ? note->pitch : 60);
  f->text(tr(juce::String::fromUTF8("Début (s)")), "start", note ? note->start : 0);
  f->text(tr(String::fromUTF8("Fin (s)")), "end", note ? note->end : 1);
  f->text(tr(juce::String::fromUTF8("Vélocité")), "velocity",
          note ? note->velocity : 83);
  auto ids = roll.selected;
  f->button(tr(juce::String::fromUTF8("Supprimer la sélection")),
            [this, ids] { removeNotes(ids); });
  f->button(tr(String::fromUTF8("Retirer correction manuelle")), [this, ids] {
    command(object({{"kind", "restore"}, {"ids", idsVar(ids)}}));
  });
  auto s = safe();
  f->onApply = [s, f, note](var v) {
    if (!s)
      return;
    double p = decimal(v["pitch"]), a = decimal(v["start"]),
           b = decimal(v["end"]), vel = decimal(v["velocity"]);
    if (!std::isfinite(p) || !std::isfinite(a) || !std::isfinite(b) ||
        !std::isfinite(vel) || a < 0 || b <= a || p < 0 || p > 127 || vel < 1 ||
        vel > 127) {
      s->status.setText(tr(juce::String::fromUTF8("Vérifiez les valeurs saisies.")),
                        juce::dontSendNotification);
      return;
    }
    var action = object({{"kind", note ? "modify" : "add"},
                         {"pitch", (int)p},
                         {"start", a},
                         {"end", b},
                         {"velocity", (int)vel}});
    if (note)
      action.getDynamicObject()->setProperty("id", (juce::int64)note->id);
    else
      for (auto &n : s->roll.notes)
        if (s->partId < 0 || n.part == s->partId) {
          action.getDynamicObject()->setProperty("part", n.source["part"]);
          break;
        }
    s->command(action, [f](var) {
      if (f != nullptr)
        f->close();
    });
  };
  f->show(note ? tr(juce::String::fromUTF8("Modifier / protéger la note"))
               : tr(String::fromUTF8("Ajouter une note")));
}
void App::markerDialog(var marker,double at){
  auto form=juce::Component::SafePointer<Form>(new Form);form->text(tr(String::fromUTF8("Nom du repère (facultatif)")),"name",marker.isVoid()?var(""):marker["name"]);
  form->onApply=[s=safe(),form,marker,at](var value){if(!s)return;auto name=value["name"].toString();if(name.getNumBytesAsUTF8()>256){s->status.setText(tr(String::fromUTF8("Repère invalide.")),juce::dontSendNotification);return;}
    auto action=object({{"kind",marker.isVoid()?"add_marker":"modify_marker"},{"at",marker.isVoid()?at:number(marker,"time")},{"name",name}});
    if(!marker.isVoid())action.getDynamicObject()->setProperty("id",marker["id"]);
    s->ensureGuidedEditor([s,action]{if(s)s->command(action);});form->close();};
  form->show(tr(String::fromUTF8("Repère")));
}
void App::context(juce::Point<int> p, var target) {
  if (locked)
    return;
  if(target["kind"].toString()=="timeline"){
    auto marker=target["marker"];double at=number(target,"time");juce::PopupMenu menu;
    menu.addItem(1,tr(String::fromUTF8("Ajouter un repère ici")),!audio.isVoid());
    if(!marker.isVoid()){menu.addItem(2,tr(String::fromUTF8("Renommer le repère")));menu.addItem(3,tr(String::fromUTF8("Supprimer le repère")));}
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({p.x,p.y,1,1}),[s=safe(),marker,at](int choice){if(!s||!choice)return;
      if(choice==3){s->command(object({{"kind","delete_marker"},{"id",marker["id"]}}));return;}
      s->markerDialog(choice==2?marker:var(),at);
    });return;
  }
  ActionMenu m(this);
  auto ids = roll.selected;
  double a = roll.duration(), b = 0;
  bool muted = true;
  for (auto &n : roll.notes)
    if (ids.count(n.id)) {
      a = std::min(a, n.start);
      b = std::max(b, n.end);
      muted &= roll.manuallyMuted.count(n.id)>0;
    }
  String kind = target["kind"].toString();
  if(kind=="overlap"){
    auto stacked=idSet(target["ids"]);
    m.addSectionHeader(tr(String::fromUTF8("Notes superposées : "))+String(int(stacked.size())));
    std::vector<Note> choices;
    for(const auto& n:roll.notes)if(stacked.count(n.id))choices.push_back(n);
    std::sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){return a.start!=b.start?a.start<b.start:a.id<b.id;});
    for(size_t i=0;i<choices.size();++i){const auto& n=choices[i];int base=100+int(i)*3;
      m.addSectionHeader(pitchName(n.pitch)+" · "+String(n.start,3)+String::fromUTF8(" → ")+String(n.end,3)+" s · "+n.label+" · #"+String((juce::int64)n.id));
      m.addItem(base,tr(String::fromUTF8("Sélectionner cette note")));
      m.addItem(base+1,tr(String::fromUTF8("Écouter cette note")));
      m.addItem(base+2,tr(String::fromUTF8("Supprimer cette note · Suppr")));
    }
    auto s=safe();m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({p.x,p.y,1,1}),[s,choices](int id){
      if(!s||s->locked||id<100)return;size_t at=size_t((id-100)/3);if(at>=choices.size())return;
      const auto& note=choices[at];bool exists=false;for(const auto& item:items(s->result["editing"]["notes"]))if((juce::int64)item["id"]==note.id)exists=true;if(!exists)return;
      int action=(id-100)%3;
      if(action==2){s->removeNotes({note.id});return;}
      if(action==1){s->control("pause",0,[s,note](var){if(s)s->audition(note);});return;}
      if(s->partId>=0&&s->partId!=note.part)s->selectPart(note.part);
      s->roll.selectIds({note.id});s->roll.focused={note.id};s->roll.repaint();
    });return;
  }
  if(kind=="guided"){
    auto item=target["item"];m.addSectionHeader(tr(String::fromUTF8("Note guidée proposée · à vérifier")));m.addItem(1,tr(String::fromUTF8("Écouter le passage audio")));m.addItem(2,tr(String::fromUTF8("Écouter le MIDI actuel")));m.addItem(3,tr(String::fromUTF8("Écouter la hauteur estimée")));m.addItem(4,tr(String::fromUTF8("Ajouter cette note")));m.addItem(5,tr(String::fromUTF8("Ignorer cette proposition")));auto s=safe();m.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({p.x,p.y,1,1}),[s,item](int id){if(!s)return;if(id==1||id==2)s->listen(number(item,"start"),number(item,"end"),id-1);if(id==3){Note n;n.pitch=(int)item["pitch"];n.velocity=83;for(auto& p:items(s->result["midi_parts"]))if(s->partId>=0&&Note::parse(object({{"part",p}})).part==s->partId)n.program=(int)p["program"];s->control("pause",0,[s,n](var){if(s)s->audition(n);});}if(id==4)s->acceptGuided({item["id"].toString()});if(id==5)s->ignoreGuided({item["id"].toString()});});return;
  }
  if (kind.isNotEmpty()) {
    auto item = target["item"];
    if (kind == "extra") {
      for (auto &e : items(verification["existing"]))
        if (e["id"] == target["id"])
          item = e;
    }
    target = put(target, "item", item);
    a = number(item, "start");
    b = number(item, "end");
    m.addSectionHeader(
        kind == "missing" ? tr(juce::String::fromUTF8("Note manquante proposée"))
        : kind == "split" ? tr(String::fromUTF8("Réattaque possible · à vérifier"))
        : kind == "extend" ? tr(String::fromUTF8("Prolongation possible · à vérifier"))
        : kind == "hold"  ? tr(juce::String::fromUTF8("Tenue proposée · à vérifier"))
                          : tr(String::fromUTF8("Note probablement en trop")));
    m.addItem(1, tr(juce::String::fromUTF8("Écouter le passage audio")),
              !(bool)audio["midi_only"]);
    m.addItem(2, tr(juce::String::fromUTF8("Écouter le passage MIDI")));
    if (kind == "missing")
      m.addItem(10, tr(String::fromUTF8("Écouter la hauteur seule")));
    if (kind != "missing")
      m.addItem(3, muted ? tr(juce::String::fromUTF8("Réactiver · M"))
                         : tr(juce::String::fromUTF8("Rendre muet · M")));
    m.addItem(4, kind == "split" ? tr(String::fromUTF8("Scinder à la réattaque proposée"))
                 : kind == "extend" ? tr(String::fromUTF8("Prolonger jusqu’à la fin proposée"))
                 : kind == "missing" ? tr(String::fromUTF8("Ajouter cette note"))
                 : kind == "hold"
                     ? tr(juce::String::fromUTF8("Fusionner cette tenue · F"))
                     : tr(juce::String::fromUTF8("Supprimer cette note · Suppr")));
    m.addItem(5, kind == "extra" ? tr(String::fromUTF8("Conserver cette note"))
                                 : tr(String::fromUTF8("Ignorer cette proposition")));
  } else {
    if (ids.empty())
      return;
    m.addSectionHeader(String((int)ids.size()) +
                       tr(juce::String::fromUTF8(" notes sélectionnées")));
    m.addItem(1, tr(juce::String::fromUTF8("Écouter le passage audio")),
              !(bool)audio["midi_only"]);
    m.addItem(2, tr(juce::String::fromUTF8("Écouter le passage MIDI")));
    m.addItem(3,
              muted ? tr(juce::String::fromUTF8("Réactiver la sélection · M"))
                    : tr(juce::String::fromUTF8("Rendre la sélection muette · M")));
    m.addSeparator();
    m.addItem(6, tr(juce::String::fromUTF8("Déplacer vers une nouvelle partie…")));
    m.addItem(
        7, tr(juce::String::fromUTF8("Réaffecter l’instrument de cette partie…")));
    juce::PopupMenu transfer;
    for (auto &part : parts)
      transfer.addItem(part.first + 1000, part.second);
    m.addSubMenu(tr(juce::String::fromUTF8("Déplacer la sélection vers…")),
                 transfer);
    m.addItem(8, tr(juce::String::fromUTF8("Fusionner les notes · F")),
              canMerge(ids));
    m.addItem(9, tr(juce::String::fromUTF8("Supprimer la sélection · Suppr")));
  }
  if (kind != "missing" && ids.size() >= 2) {
    m.addSeparator();
    m.addItem(11, tr(String::fromUTF8("Lier · L")));
    m.addItem(12, tr(String::fromUTF8("Délier")));
    m.addItem(13, tr(String::fromUTF8("Chevauchement : ")) + String(linkOverlapMs,0) + String::fromUTF8(" ms…"));
    m.shortcut('L', 11);
  }
  if (kind != "missing")
    m.shortcut('M', 3);
  if (kind == "hold")
    m.shortcut('F', 4);
  else if (kind == "extra")
    m.shortcut(juce::KeyPress::deleteKey, 4);
  else if (kind.isEmpty()) {
    m.shortcut('F', 8);
    m.shortcut(juce::KeyPress::deleteKey, 9);
  }
  auto s = safe();
  m.showMenuAsync(
      juce::PopupMenu::Options().withTargetScreenArea({p.x, p.y, 1, 1}),
      [s, ids, a, b, target, kind](int id) {
        if (!s)
          return;
        if (id == 1 || id == 2) {
          s->listen(a, b, id - 1);
          return;
        }
        if (id == 11 || id == 12) {s->linkNotes(ids,id==12);return;}
        if (id == 13) {s->linkOptions();return;}
        if (id == 10) {
          Note n;
          n.pitch = (int)target["item"]["pitch"];
          n.velocity = 80;
          s->control("pause", 0, [s, n](var) { if (s) s->audition(n); });
          return;
        }
        if (id == 3) {
          s->command(object({{"kind", "toggle_mute"}, {"ids", idsVar(ids)}}));
          return;
        }
        if (id == 4) {
          s->applyDiagnostic(kind, target["item"]);
          return;
        }
        if (id == 5) {
          if (kind == "extra")
            s->kept.insert((juce::int64)target["item"]["id"]);
          else
            s->ignored.insert(target["item"]["id"].toString());
          s->updateDiagnostics();
          s->rebuildSide();
          return;
        }
        if (id == 6)
          s->partDialog(true);
        if (id == 7)
          s->partDialog(false);
        if (id == 8)
          s->mergeNotes(ids);
        if (id == 9)
          s->removeNotes(ids);
        if (id >= 1000)
          s->command(object({{"kind", "transfer"},
                             {"ids", idsVar(ids)},
                             {"program", id - 1000}}),
                     [s, id](var) {
                       if (s)
                         s->selectPart(id - 1000);
                     });
      });
}
void App::listen(double a, double b, int which) {
  if (locked || !notesReady)
    return;
  control("pause", 0, [this, a, b, which](var) {
    mode = which;
    control("seek", std::max(0., a - .15), [this, b](var) {
      listenEnd = std::min(
          number(result, "duration", number(audio, "duration")), b + .2);
      control("play");
    });
  });
}
void App::locate(const var &item) {
  roll.reveal(number(item, "start"), number(item, "end"), (int)item["pitch"]);
  control("pause");
  control("seek", std::max(0., number(item, "start") - .15));
}


void App::linkNotes(std::set<std::int64_t> ids, bool unlink) {
  if (locked || ids.size()<2) return;
  command(unlink ? object({{"kind","unlink"},{"ids",idsVar(ids)}})
                 : object({{"kind","link"},{"ids",idsVar(ids)},{"overlap_ms",linkOverlapMs}}));
}
void App::linkOptions() {
  auto f=juce::Component::SafePointer<Form>(new Form);
  f->text(tr(String::fromUTF8("Chevauchement (ms, de 0 à 500)")),"overlap",linkOverlapMs);
  f->label(tr(String::fromUTF8("Lier prolonge les notes sélectionnées sans déplacer les attaques. Délier ramène les fins à l’attaque suivante sélectionnée. Les accords et les parties restent distincts.")));
  f->onApply=[s=safe(),f](var v) {
    auto amount=decimal(v["overlap"]);
    if (!std::isfinite(amount) || amount<0 || amount>500) {
      f->setHeading(tr(String::fromUTF8("Saisissez une durée de 0 à 500 ms")));return;
    }
    if(s){s->linkOverlapMs=amount;s->edited();}
    if(f)f->close();
  };
  f->show(tr(String::fromUTF8("Lier")));
}

namespace {
String rhythmName(int value,int variant) {
  const String names[]={tr(String::fromUTF8("Ronde")),tr(String::fromUTF8("Blanche")),tr(String::fromUTF8("Noire")),tr(String::fromUTF8("Croche")),tr(String::fromUTF8("Double-croche")),tr(String::fromUTF8("Triple-croche"))};
  return names[value]+(variant==1?tr(String::fromUTF8(" · pointée")):variant==2?tr(String::fromUTF8(" · triolet")):String());
}
}
void App::loadGridUi(const var& value) {
  auto mode=value["mode"].toString();roll.rhythmicGrid.mode=mode=="seconds"?1:mode=="none"?2:0;
  const String subdivisions[]={"1/4","1/8","1/16","1/8T","1/16T"};roll.rhythmicGrid.subdivision=1;
  for(int i=0;i<5;++i)if(value["subdivision"].toString()==subdivisions[i])roll.rhythmicGrid.subdivision=i;
  roll.rhythmicGrid.origin=number(value,"offset",0);
  gridMode.setSelectedId(roll.rhythmicGrid.mode+1,juce::dontSendNotification);
  gridSubdivision.setSelectedId(roll.rhythmicGrid.subdivision+1,juce::dontSendNotification);
  gridSubdivision.setEnabled(!locked&&roll.rhythmicGrid.mode==0);roll.repaint();
}
void App::refreshTempo() {
  auto id=audio["id"];if(id.isVoid())return;auto version=++tempoRefreshVersion;
  invoke("project_tempo",object({{"audioId",id}}),[this,id,version](var info){
    if(audio["id"]!=id||version!=tempoRefreshVersion)return;
    auto text=String((double)info["bpm"],3).trimCharactersAtEnd("0").trimCharactersAtEnd(".")+" BPM";
    if((bool)info["variable"]&&!(bool)info["fixed"])text+=" ~";
    roll.rhythmicGrid.tempos.clear();roll.rhythmicGrid.meters.clear();
    for(auto& point:items(info["tempo_map"]))roll.rhythmicGrid.tempos.push_back({number(point,"beat"),number(point,"seconds"),number(point,"quarter_seconds",.5)});
    for(auto& meter:items(info["meter_map"]))roll.rhythmicGrid.meters.push_back({number(meter,"beat"),(int)meter["numerator"],(int)meter["denominator"]});
    if(roll.rhythmicGrid.tempos.empty())roll.rhythmicGrid.tempos={{0,0,.5}};
    if(roll.rhythmicGrid.meters.empty())roll.rhythmicGrid.meters={{0,4,4}};
    roll.repaint();if(!tempoPreview)syncClick();
    String tip=tr(String::fromUTF8("Tempo / calculette rythmique"));
    if(hasMidiTempoEstimate()){
      auto estimate=String(number(midiTempoEstimate,"bpm"),1).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
      if(midiEstimatePending)text+=String::fromUTF8(" · ≈")+estimate;
      tip+="\n"+localisation::format(String::fromUTF8("Dernière prise : environ {0} BPM · À confirmer"),{estimate});
    }else if(!midiTempoEstimate.isVoid()&&midiEstimateAudio==id)tip+="\n"+midiTempoUnavailable();
    buttonMap["tempo"]->setButtonText(text);buttonMap["tempo"]->setTooltip(tip);resized();
  });
}
void App::tempoOptions() {
  if(locked||audio.isVoid())return;
  invoke("project_tempo",object({{"audioId",audio["id"]}}),[this](var info){tempoForm(info)->show(tr(String::fromUTF8("Tempo / calculette rythmique")));});
}
Form* App::tempoForm(var info) {
    auto f=juce::Component::SafePointer<Form>(new Form);
    f->onDismiss=[s=safe()]{if(s)s->stopTempoPreview();};
    f->label(localisation::format("BPM du projet : {0}",{String(number(info,"bpm"),1)}));
    auto* estimateButton=f->button(tr(String::fromUTF8("Estimer depuis les notes")),[]{});estimateButton->setComponentID("estimateNotesTempo");
    estimateButton->setEnabled(!items(result["editing"]["notes"]).empty());
    f->label(tr(String::fromUTF8("Estimation sur la voix active, ou toutes les voix si Toutes les parties est sélectionné. Les notes muettes individuellement sont ignorées.")));
    estimateButton->onClick=[s=safe(),f,info,estimateButton]{
     if(!s||!f||s->locked)return;s->stopTempoPreview();auto request=s->args();auto previous=f->state();int target=s->partId;auto sourceAudio=s->audio["id"];auto active=std::make_shared<bool>(true);auto onDismiss=f->onDismiss;f->onDismiss=[active,onDismiss]{*active=false;if(onDismiss)onDismiss();};auto analysis=s->result["analysis_id"],revision=s->result["editing"]["revision"];estimateButton->setEnabled(false);f->setApplyEnabled(false);f->setHeading(tr(String::fromUTF8("Estimation du tempo…")));
     s->invoke("estimate_notes_tempo",request,[s,f,info,previous,target,sourceAudio,analysis,revision,estimateButton,active](var estimate){
      if(!s||!f||!*active)return;
      if(s->audio["id"]!=sourceAudio||s->partId!=target||s->result["analysis_id"]!=analysis||s->result["editing"]["revision"]!=revision){estimateButton->setEnabled(true);f->setApplyEnabled(true);f->setHeading(tr(String::fromUTF8("La transcription a changé. Rechargez la sélection.")));return;}
      s->midiTempoEstimate=estimate;s->midiEstimateAudio=s->audio["id"];s->midiEstimatePending=number(estimate,"bpm")>0;s->edited();s->refreshTempo();
      f->close();juce::MessageManager::callAsync([s,info,previous]{if(!s||s->closing)return;auto* next=s->tempoForm(info);for(auto key:{"bpm","ms","value","variant"})if(previous.hasProperty(key)){
       if(next->comboFields.count(key))next->comboFields[key]->setSelectedId(int(previous[key])+1,juce::sendNotificationSync);else next->setValue(key,previous[key]);
      }next->comboFields["mode"]->setSelectedId(previous["mode"].toString()=="source"?2:1,juce::sendNotificationSync);next->show(tr(String::fromUTF8("Tempo / calculette rythmique")));});
     },true,[f,estimateButton](String error){if(f){estimateButton->setEnabled(true);f->setApplyEnabled(true);f->setHeading(error);}});
    };
    addMidiTempoEstimate(f,info);
    std::vector<std::pair<String,var>> modes{{tr(String::fromUTF8("Tempo fixe du projet")),"fixed"}};
    if((bool)info["has_midi"])modes.emplace_back(tr(String::fromUTF8("Conserver les tempos du MIDI importé")),"source");
    f->combo(tr(String::fromUTF8("Tempo à l’export")),"mode",modes,(bool)info["has_midi"]&&!(bool)info["fixed"]?var("source"):var("fixed"));
    f->text(tr(String::fromUTF8("BPM retenu (10 à 600)")),"bpm",String((double)info["bpm"],3));
    auto edit=f->textFields["bpm"]->onTextChange;
    f->textFields["bpm"]->onTextChange=[f,edit]{edit();if(f)f->comboFields["mode"]->setSelectedId(1,juce::sendNotificationSync);};
    if((bool)info["variable"])f->label(tr(String::fromUTF8("Ce MIDI contient des variations de tempo. Le mode fixe les remplace ; le mode source les conserve.")));
    f->label(tr(String::fromUTF8("Changer le BPM conserve les débuts et durées en secondes. Aucune quantification automatique.")));
    f->combo(tr(String::fromUTF8("Valeur de note")),"value",{{rhythmName(0,0),0},{rhythmName(1,0),1},{rhythmName(2,0),2},{rhythmName(3,0),3},{rhythmName(4,0),4},{rhythmName(5,0),5}},2);
    f->combo(tr(String::fromUTF8("Rythme")),"variant",{{tr(String::fromUTF8("Simple")),0},{tr(String::fromUTF8("Pointée")),1},{tr(String::fromUTF8("Triolet (3 dans le temps de 2)")),2}},0);
    f->live([f]{if(!f)return String();try{auto v=f->state();auto ms=phthongos::midi::rhythmMilliseconds(decimal(v["bpm"]),(int)v["value"],(int)v["variant"]);return rhythmName((int)v["value"],(int)v["variant"])+" = "+String(ms,2)+" ms";}catch(const std::exception& e){return localisation::error(String::fromUTF8(e.what()));}});
    f->text(tr(String::fromUTF8("Durée à comparer (ms)")),"ms",500);
    f->live([f]{if(!f)return String();try{auto v=f->state();auto m=phthongos::midi::nearestRhythm(decimal(v["bpm"]),decimal(v["ms"]));return tr(String::fromUTF8("Valeur la plus proche : "))+rhythmName(m.value,m.variant)+" ("+String(m.milliseconds,2)+" ms)\n"+tr(String::fromUTF8("Écart : "))+(m.difference>=0?"+":"")+String(m.difference,2)+" ms";}catch(const std::exception& e){return localisation::error(String::fromUTF8(e.what()));}});
    f->label(tr(String::fromUTF8("La comparaison utilise le BPM affiché et reste indicative. Les notes ne sont pas modifiées.")));
    auto id=audio["id"];
    f->onApply=[s=safe(),f,id](var v){
      if(!s||!f)return;
      try{double bpm=0;if(v["mode"].toString()=="fixed"){bpm=decimal(v["bpm"]);phthongos::midi::validateTempo(bpm);}
        s->invoke("set_project_tempo",object({{"audioId",id},{"bpm",bpm}}),[s,f,infoBpm=bpm](var){if(s){s->midiEstimatePending=false;s->edited();s->refreshTempo();s->status.setText(infoBpm?tr(String::fromUTF8("Tempo enregistré : "))+String(infoBpm,3)+" BPM":tr(String::fromUTF8("Tempos du MIDI source conservés.")),juce::dontSendNotification);}if(f)f->close();},false,[f](String e){if(f)f->setHeading(e);});
      }catch(const std::exception& e){f->setHeading(localisation::error(String::fromUTF8(e.what())));}
    };
    f->setHeading(tr(String::fromUTF8("Tempo / calculette rythmique")));
    return f.getComponent();
}
