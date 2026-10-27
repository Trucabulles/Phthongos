#include "App.h"
namespace {
var put(var a, const String &key, var value) {
  a = a.clone();
  a.getDynamicObject()->setProperty(key, value);
  return a;
}
bool validMapping(const var &v) {
  return number(v, "min") >= 1 && number(v, "max") <= 127 &&
         number(v, "min") <= number(v, "level") &&
         number(v, "level") <= number(v, "max") && number(v, "contrast") >= 0 &&
         number(v, "contrast") <= 250;
}
bool geometry(const var &e, const var &notes) {
  for (auto &n : items(notes))
    if (n["id"] == e["id"] && !(bool)n["muted"] &&
        (int)n["pitch_midi"] == (int)e["pitch"] &&
        std::abs(number(n, "start_time_s") - number(e, "start")) < 1e-5 &&
        std::abs(number(n, "end_time_s") - number(e, "end")) < 1e-5)
      return true;
  return false;
}
} // namespace
void App::rebuildSide() {
  if (sidePending)
    return;
  sidePending = true;
  auto s = safe();
  juce::MessageManager::callAsync([s] {
    if (s) {
      s->sidePending = false;
      s->buildSide();
    }
  });
}
void App::buildSide() {
  int scroll = sideView.getViewPositionY();
  side.clear();
  auto heading = [this](const String &key, const String &name) {
    bool open = openSections.count(key) > 0;
    side.heading(
        (open ? juce::String::fromUTF8("▾ ") : juce::String::fromUTF8("▸ ")) +
            name,
        [this, key] {
          if (openSections.count(key))
            openSections.erase(key);
          else {
            openSections.insert(key);
            if (key == "dynamics")
              restoreDynamics();
          }
          rebuildSide();
        });
    return open;
  };
  if(heading("guided",tr(String::fromUTF8("Masque / MIDI guidé"))))buildGuidedPanel();
  roll.refreshOverlaps();
  if(heading("overlaps",tr(String::fromUTF8("Chevauchements de même hauteur"))) ) {
    side.label(tr(String::fromUTF8("Notes des parties visibles. Cliquez pour les localiser ; clic droit sur la zone hachurée pour accéder à chaque note. Aucune suppression automatique.")),85);
    if(roll.overlaps.empty())side.label(tr(String::fromUTF8("Aucun chevauchement de même hauteur.")));
    int pages=std::max(1,(int(roll.overlaps.size())+49)/50);
    overlapsPage=juce::jlimit(0,pages-1,overlapsPage);
    for(int index=overlapsPage*50;index<std::min(int(roll.overlaps.size()),(overlapsPage+1)*50);++index){
      const auto& group=roll.overlaps[size_t(index)];
      auto key="overlap:"+String((juce::int64)*group.ids.begin());
      side.diagnosticRow(Roll::noteLabel(group.pitch)+String::fromUTF8(" · ")+String(group.start,3)+String::fromUTF8(" → ")+String(group.end,3)+" s",
        String(int(group.ids.size()))+tr(String::fromUTF8(" notes superposées")),diagnosticActive==key,
        [this,group,key]{if(locked)return;
          bool other=false;for(const auto& n:roll.notes)if(group.ids.count(n.id)&&!n.editable)other=true;
          if(other)selectPart(-1);
          diagnosticActive=key;roll.diagnosticGhost.clear();
          roll.selectIds(group.ids);roll.focused=group.ids;
          locate(object({{"start",group.start},{"end",group.end},{"pitch",group.pitch}}));rebuildSide();});
    }
    if(pages>1){
      side.label(String(overlapsPage+1)+" / "+String(pages),24);
      side.button(tr(String::fromUTF8("Précédentes")),[this]{--overlapsPage;rebuildSide();},overlapsPage>0);
      side.button(tr(String::fromUTF8("Suivantes")),[this]{++overlapsPage;rebuildSide();},overlapsPage+1<pages);
    }
  }
  if (heading("verification",
              tr(juce::String::fromUTF8("Vérifier le MIDI avec l’audio")))) {
    if ((bool)audio["midi_only"])
      side.label(tr(String::fromUTF8("Associez un audio source depuis le menu Fichier.")));
    else {
      side.label(
          tr(String::fromUTF8("L’analyse fournit des indices acoustiques qui peuvent être erronés. Écoutez et vérifiez chaque proposition avant de l’appliquer. Aucune correction automatique.")),
          105);
      side.combo("Zone",
                 {{tr(String::fromUTF8("Intervalle audio")), "interval"},
                  {tr(String::fromUTF8("Notes sélectionnées")), "selection"},
                  {tr(String::fromUTF8("Plage temporelle du roll")), "time"},
                  {tr(String::fromUTF8("Tout l’audio")), "all"}},
                 verifyScope, [this](var v) {
                   verifyScope = v.toString();
                   rebuildSide();
                 });
      side.toggle(tr(String::fromUTF8("Tout analyser")), verifyScope == "all", [this](bool on) {
        verifyScope = on ? "all" : "interval";
        rebuildSide();
      });
      if (verifyScope == "interval" || verifyScope == "time") {
        double begin = verifyScope == "time" ? number(roll.selection, "start")
                                             : rangeStart;
        double finish = verifyScope == "time"
                            ? number(roll.selection, "end")
                            : std::min(rangeEnd, number(audio, "duration"));
        side.number(tr(String::fromUTF8("Début (s)")), begin, 0,
                    number(audio, "duration"), .01, [this, finish](double v) {
                      rangeStart = v;
                      rangeEnd = finish;
                      verifyScope = "interval";
                    });
        side.number(tr(String::fromUTF8("Fin (s)")), finish, 0, number(audio, "duration"), .01,
                    [this, begin](double v) {
                      rangeStart = begin;
                      rangeEnd = v;
                      verifyScope = "interval";
                    });
      }
      side.button(
          tr(juce::String::fromUTF8(
              "Vérifier les notes et rechercher les manques")),
          [this] { verify(); }, !locked);
      if (locked)
        side.button(tr(juce::String::fromUTF8("Annuler la vérification")),
                    [this] { invoke("cancel_midi_verification", object({})); });
      if (verificationStale && !verification.isVoid())
        side.label(tr(juce::String::fromUTF8(
            "Les notes ou le registre ont changé. Relancez la vérification ; le spectre "
            "compatible sera réutilisé.")));
      if (!verification.isVoid() && !verificationStale) {
        side.label(
            String((int)roll.extra.size()) +
                tr(juce::String::fromUTF8(" notes probablement en trop · ")) +
                String((int)roll.ghosts.size()) +
                tr(juce::String::fromUTF8(" manques · ")) +
                String((int)roll.holds.size()) + tr(String::fromUTF8(" tenues · ")) + String((int)roll.timing.size()) + tr(String::fromUTF8(" durées/réattaques\nSpectre ")) +
                ((bool)verification["cached"]
                     ? tr(juce::String::fromUTF8("réutilisé"))
                     : tr(juce::String::fromUTF8("calculé"))),
            62);
        side.button(tr(String::fromUTF8("Appliquer toutes les propositions…")), [this] { applyDiagnosticBatch("all"); },
                    !locked && (!roll.extra.empty() || !roll.ghosts.empty() || !roll.holds.empty() || !roll.timing.empty()));
        side.label(tr(String::fromUTF8("Les commandes par lot portent sur toutes les pages de la catégorie.")),48);
        auto pageItems=[this](const String& key,const std::vector<var>& values){
          int pages=std::max(1,(int(values.size())+49)/50);auto& page=diagnosticPages[key];page=juce::jlimit(0,pages-1,page);
          if(pages>1){
            side.label(String(page+1)+" / "+String(pages)+String::fromUTF8(" · ")+String(int(values.size()))+" "+tr(String::fromUTF8("propositions")),24);
            side.button(tr(String::fromUTF8("Précédentes")),[this,key]{--diagnosticPages[key];rebuildSide();},!locked&&page>0);
            side.widgets.getLast()->setComponentID("diagnosticPrevious-"+key);
            side.button(tr(String::fromUTF8("Suivantes")),[this,key]{++diagnosticPages[key];rebuildSide();},!locked&&page+1<pages);
            side.widgets.getLast()->setComponentID("diagnosticNext-"+key);
          }
          auto first=values.begin()+std::min(values.size(),size_t(page*50));auto last=values.begin()+std::min(values.size(),size_t((page+1)*50));
          return std::vector<var>(first,last);
        };
        auto entry = [this](const String& kind, const var& item, const String& detail) {
          auto key = kind + ":" + item["id"].toString();
          side.diagnosticRow(
              Roll::noteLabel((int)item["pitch"]) + String::fromUTF8(" · ") +
                  timeName(number(item, "start")),
              detail, key == diagnosticActive,
              [this, kind, item] { navigateDiagnostic(kind, item); });
        };
        if (heading("timing", tr(String::fromUTF8("Durées / réattaques · à vérifier")))) {
          side.button(tr(String::fromUTF8("Appliquer les durées / réattaques…")), [this] { applyDiagnosticBatch("timing"); }, !locked && !roll.timing.empty());
          for(auto& t:pageItems("timing",roll.timing)) {
            String action=t["kind"].toString()=="split"
              ? tr(String::fromUTF8("Scinder vers ")) + timeName(number(t,"at"))
              : tr(String::fromUTF8("Prolonger jusqu’à ")) + timeName(number(t,"proposed_end"));
            entry(t["kind"].toString(),t,action+tr(String::fromUTF8(" · à vérifier")));
          }
          if(roll.timing.empty())side.label(tr(String::fromUTF8("Aucune proposition de durée ou réattaque.")));
        }
        if (heading("holds", tr(String::fromUTF8("Tenues fragmentées")))) {
          side.button(tr(String::fromUTF8("Fusionner les tenues proposées…")), [this] { applyDiagnosticBatch("holds"); }, !locked && !roll.holds.empty());
          for (auto& h : pageItems("holds",roll.holds))
            entry("hold", h, String((int)items(h["ids"]).size()) +
                tr(String::fromUTF8(" fragments · à vérifier · durée ")) +
                String(number(h, "end") - number(h, "start"), 3) + " s");
          if (roll.holds.empty())
            side.label(tr(String::fromUTF8("Aucune tenue proposée.")), 28);
        }
        if (heading("missing", tr(String::fromUTF8("Notes manquantes")))) {
          side.button(tr(String::fromUTF8("Ajouter toutes les notes proposées…")), [this] { applyDiagnosticBatch("missing"); }, !locked && !roll.ghosts.empty());
          for (auto& c : pageItems("missing",roll.ghosts))
            entry("missing", c, tr(String::fromUTF8("Durée ")) +
                String(number(c, "end") - number(c, "start"), 3) +
                tr(String::fromUTF8(" s · à vérifier")));
          if (roll.ghosts.empty())
            side.label(tr(String::fromUTF8("Aucune proposition manquante à examiner.")));
        }
        if (heading("extra", tr(String::fromUTF8("Notes probablement en trop")))) {
          side.button(tr(String::fromUTF8("Supprimer les notes proposées…")), [this] { applyDiagnosticBatch("extra"); }, !locked && !roll.extra.empty());
          std::vector<var> extras;for(const auto& e:items(verification["existing"]))if(roll.extra.count((juce::int64)e["id"]))extras.push_back(e);
          for (auto& e : pageItems("extra",extras))
              entry("extra", e, tr(String::fromUTF8("Durée ")) +
                  String(number(e, "end") - number(e, "start"), 3) +
                  tr(String::fromUTF8(" s · à vérifier")));
          if (roll.extra.empty())
            side.label(tr(String::fromUTF8("Aucune note probablement en trop.")));
        }
        side.button(tr(String::fromUTF8("Limites et contexte")), [this] {
          auto f = juce::Component::SafePointer<Form>(new Form);
          for (auto &w : items(verification["warnings"]))
            f->label(w.toString());
          f->label(tr(juce::String::fromUTF8(
              "Les indices ne sont pas des probabilités. Les diagnostics ne "
              "changent jamais automatiquement le MIDI.")));
          f->onApply = [f](var) {
            if (f != nullptr)
              f->close();
          };
          f->show(tr(juce::String::fromUTF8("Limites de l’analyse")));
        });
        side.button(
            tr(juce::String::fromUTF8("Réafficher les diagnostics écartés")),
            [this] {
              ignored.clear();
              kept.clear();
              updateDiagnostics();
              rebuildSide();
            });
      }
    }
  }
  if (heading("dynamics", tr(juce::String::fromUTF8("Dynamique depuis l’audio")))) {
    if ((bool)audio["midi_only"])
      side.label(tr(String::fromUTF8("Associez un audio source depuis le menu Fichier.")));
    else {
      side.label(tr(String::fromUTF8("Estimation relative · V1 piano. Voix verrouillées et percussions exclues.")),60);
      side.combo(tr(String::fromUTF8("Portée")),
                 {{tr(String::fromUTF8("Toute la partie à traiter")), 0},
                  {tr(String::fromUTF8("Notes sélectionnées")), 1}},
                 scope, [this](var v) {
                   scope = (int)v;
                   dynamics = var();
                   restoreDynamics();
                   rebuildSide();
                 });
      side.button(
          dynamics.isVoid() ? tr(juce::String::fromUTF8("Estimer depuis l’audio"))
                            : tr(String::fromUTF8("Recalculer depuis l’audio")),
          [this] { estimateDynamics(!dynamics.isVoid()); }, !locked);
      side.slider(tr(String::fromUTF8("Niveau moyen")), number(dynMapping, "level", 80), 1, 127, 1,
                  [this](double v) {
                    dynMapping = put(dynMapping, "level", v);
                    hasDynDraft = true;
                    if (!manualRange)
                      suggestRange();
                    dirty = true;
                  });
      side.slider(tr(String::fromUTF8("Contraste (%)")), number(dynMapping, "contrast", 100), 0, 250,
                  5, [this](double v) {
                    dynMapping = put(dynMapping, "contrast", v);
                    hasDynDraft = true;
                    if (!manualRange)
                      suggestRange();
                    dirty = true;
                  });
      side.label(
          tr(String::fromUTF8("Les réglages réutilisent les mesures. Recalculer renouvelle les mesures sans changer les notes. Appliquer remplace les vélocités du périmètre choisi.")),80);
      side.label(manualRange
                     ? tr(String::fromUTF8(
                           "Plage manuelle conservée lors des estimations."))
                     : tr(String::fromUTF8(
                           "Min/Max proposés depuis l’audio si les mesures "
                           "sont suffisantes ; ajustables librement.")),
                 48);
      side.numberPair(
          "Min", number(dynMapping, "min"), "Max", number(dynMapping, "max"), 1,
          127,
          [this](double v) {
            dynMapping = put(dynMapping, "min", int(std::lround(v)));
            manualRange = hasDynDraft = dirty = true;
            rebuildSide();
          },
          [this](double v) {
            dynMapping = put(dynMapping, "max", int(std::lround(v)));
            manualRange = hasDynDraft = dirty = true;
            rebuildSide();
          });
      if (!validMapping(dynMapping))
        side.label(tr(String::fromUTF8(
            "Choisissez un niveau dans la plage Min–Max (1–127).")));
      if (!dynamics.isVoid() && validMapping(dynMapping)) {
        side.label(localisation::count("notes_measured",int(items(dynamics["estimates"]).size())) + String::fromUTF8(" · ") +
                   ((bool)dynamics["cached"]
                        ? tr(juce::String::fromUTF8("mesures réutilisées"))
                        : tr(juce::String::fromUTF8("calculées"))));
        std::vector<int> velocities;
        for (auto &e : items(dynamics["estimates"]))
          velocities.push_back(velocity(number(e, "score")));
        side.drawing(100, [velocities](juce::Graphics &g, int w, int h) {
          int bins[8] = {};
          for (auto v : velocities)
            ++bins[juce::jlimit(0, 7, (v - 1) / 16)];
          for (int i = 0; i < 8; ++i) {
            float height = velocities.empty()
                               ? 0.f
                               : 80.f * bins[i] / (float)velocities.size();
            g.setColour(juce::Colour(0xffad793c));
            g.fillRect(i * w / 8.f, (float)h - 20 - height, w / 8.f - 4,
                       height);
            g.setColour(juce::Colour(0xff617889));
            g.setFont(juce::FontOptions(10));
            g.drawText(String(i * 16 + 1), i * w / 8, h - 18, w / 8, 16,
                       juce::Justification::centred);
          }
        });
        if (heading("dynamicsCompare", tr(String::fromUTF8("Comparer initiale / actuelle / audio"))))
          compareDynamics();
        side.button(
            tr(juce::String::fromUTF8("Appliquer les vélocités")),
            [this] { applyDynamics(); }, !locked && !items(dynamics["estimates"]).empty());
      }
      side.button(
          tr(String::fromUTF8("Retirer toute la dynamique audio")),
          [this] { command(object({{"kind", "reset_dynamics"}})); }, !locked);
    }
  }
  if (heading("short", tr(juce::String::fromUTF8("Notes très courtes")))) {
    side.combo(tr(String::fromUTF8("Portée")),
               {{tr(String::fromUTF8("Tout le morceau")), "all"},
                {tr(String::fromUTF8("Notes sélectionnées")), "selection"}},
               shortState["scope"],
               [this](var v) { shortState = put(shortState, "scope", v); });
    side.number(
        tr(String::fromUTF8("Durée maximale (ms)")),
        number(shortState, "threshold_ms", 100), 1, 1000, 1,
        [this](double v) { shortState = put(shortState, "threshold_ms", v); });
    side.button(
        tr(juce::String::fromUTF8("Repérer les notes courtes")),
        [this] { findShort(); }, !locked);
    if (!shortReport.isVoid()) {
      auto hits = items(shortReport["notes"]);
      side.label(localisation::count("notes_flagged",int(hits.size())));
      side.button(tr(juce::String::fromUTF8("Sélectionner les notes courtes")),
                  [this, hits] {
                    std::set<std::int64_t> ids;
                    for (auto &h : hits)
                      ids.insert((juce::int64)h["id"]);
                    roll.selectIds(ids);
                  });
      if (!hits.empty()) {
        shortIndex = juce::jlimit(0, (int)hits.size() - 1, shortIndex);
        auto h = hits[(size_t)shortIndex];
        for (auto &n : roll.notes)
          if (n.id == (juce::int64)h["id"]) {
            side.label(pitchName(n.pitch) + juce::String::fromUTF8(" · ") +
                       String(number(h, "duration_ms"), 1) + " ms");
            side.button(tr(String::fromUTF8("Voir cette note")), [this, n] {
              roll.reveal(n.start, n.end, n.pitch);
              control("seek", std::max(0., n.start - .1));
              roll.selectIds({n.id});
            });
          }
        side.button(
            tr(juce::String::fromUTF8("Précédente")),
            [this] {
              --shortIndex;
              rebuildSide();
            },
            shortIndex > 0);
        side.button(
            tr(String::fromUTF8("Suivante")),
            [this] {
              ++shortIndex;
              rebuildSide();
            },
            shortIndex + 1 < (int)hits.size());
      }
      side.button(tr(juce::String::fromUTF8("Masquer le repérage")), [this] {
        shortReport = var();
        roll.shortIds.clear();
        shortState = put(shortState, "enabled", false);
        roll.repaint();
        rebuildSide();
      });
    }
  }
  if (heading("clean",
              tr(juce::String::fromUTF8("Séries suspectes / nettoyage")))) {
    side.combo(tr(String::fromUTF8("Portée")),
               {{tr(String::fromUTF8("Tout le morceau")), "all"},
                {tr(String::fromUTF8("Notes sélectionnées")), "notes"},
                {tr(String::fromUTF8("Zone temps × hauteurs")), "region"}},
               cleanScope, [this](var v) {
                 cleanScope = v.toString();
                 rebuildSide();
               });
    if (cleanScope == "region") {
      side.number(tr(String::fromUTF8("Début (s)")), rangeStart, 0, roll.duration(),
                  .001, [this](double v) { rangeStart = v; });
      side.number(tr(String::fromUTF8("Fin (s)")), rangeEnd, 0, roll.duration(), .001,
                  [this](double v) { rangeEnd = v; });
      side.number(tr(String::fromUTF8("Hauteur min.")), number(cleanSettings, "regionLow", 0), 0, 127,
                  1, [this](double v) {
                    cleanSettings = put(cleanSettings, "regionLow", v);
                  });
      side.number(tr(String::fromUTF8("Hauteur max.")), number(cleanSettings, "regionHigh", 127), 0,
                  127, 1, [this](double v) {
                    cleanSettings = put(cleanSettings, "regionHigh", v);
                  });
    }
    if (!(bool)audio["midi_only"])
      side.toggle(tr(String::fromUTF8("Vérifier dans l’audio · essai")), acoustic,
                  [this](bool v) { acoustic = v; });
    if (heading("cleanAdvanced", tr(String::fromUTF8("Réglages avancés")))) {
      side.number(tr(String::fromUTF8("Coupure maximale (ms)")),
                  number(cleanSettings, "gap_ms", 30), 0, 250, 1,
                  [this](double v) {
                    cleanSettings = put(cleanSettings, "gap_ms", v);
                  });
      side.number(tr(String::fromUTF8("Écart de vélocité maximal")),
                  number(cleanSettings, "velocity_delta", 18), 0, 126, 1,
                  [this](double v) {
                    cleanSettings = put(cleanSettings, "velocity_delta", v);
                  });
    }
    side.button(
        tr(juce::String::fromUTF8("Rechercher les séries")),
        [this] { searchClean(); }, !locked);
    auto allRuns = items(clean["runs"]);
    std::vector<var> runs;
    for (auto &r : allRuns) {
      auto ids = items(r["ids"]);
      bool saved = !ids.empty() && cleanKept.count((juce::int64)ids[0]);
      if (cleanFilter == "kept"
              ? saved
              : !saved && ((cleanFilter == "proposed") == (bool)r["proposed"]))
        runs.push_back(r);
    }
    if (!clean.isVoid()) {
      for (auto filter : std::vector<std::pair<String, String>>{
               {"proposed", tr(juce::String::fromUTF8("Proposées"))},
               {"review", tr(juce::String::fromUTF8("À vérifier"))},
               {"kept", tr(juce::String::fromUTF8("Conservées"))}})
        side.button(filter.second, [this, filter] {
          cleanFilter = filter.first;
          cleanIndex = 0;
          rebuildSide();
        });
      side.label(String((int)runs.size()) +
                 tr(juce::String::fromUTF8(
                     " séries restantes. Écouter avant de fusionner.")));
      if (!runs.empty()) {
        cleanIndex = juce::jlimit(0, (int)runs.size() - 1, cleanIndex);
        auto run = runs[(size_t)cleanIndex];
        side.label(
            pitchName((int)run["pitch"]) + juce::String::fromUTF8(" · ") +
            String(number(run, "start"), 3) + juce::String::fromUTF8(" s · ") +
            String((int)items(run["ids"]).size()) + " fragments\n" +
            ((bool)run["proposed"] ? tr(juce::String::fromUTF8("Proposée"))
                                   : tr(juce::String::fromUTF8("À vérifier"))));
        auto fragments = items(result["editing"]["notes"]);
        side.label(tr(juce::String::fromUTF8("Avant · ")) +
                       localisation::count("notes_split",int(items(run["ids"]).size())),
                   24);
        side.drawing((bool)run["proposed"] ? 54 : 24,
                     [run, fragments](juce::Graphics &g, int w, int) {
                       double a = number(run, "start"),
                              span = std::max(.001, number(run, "end") - a);
                       auto ids = idSet(run["ids"]);
                       g.setColour(juce::Colour(0xffb44735));
                       for (auto &n : fragments)
                         if (ids.count((juce::int64)n["id"]))
                           g.fillRect(
                               (float)(4 + (number(n, "start_time_s") - a) /
                                               span * (w - 8)),
                               6.f,
                               (float)std::max(1., (number(n, "end_time_s") -
                                                    number(n, "start_time_s")) /
                                                       span * (w - 8)),
                               12.f);
                       if ((bool)run["proposed"]) {
                         g.setColour(juce::Colour(0xff216375));
                         g.fillRect(4.f, 34.f, (float)w - 8, 12.f);
                       }
                     });
        if ((bool)run["proposed"])
          side.label(
              tr(juce::String::fromUTF8("Aperçu après fusion · une note continue")),
              24);
        side.button(tr(juce::String::fromUTF8("Voir cette série")), [this, run] {
          locate(run);
          roll.focused = idSet(run["ids"]);
          roll.repaint();
        });
        side.button(
            tr(juce::String::fromUTF8("Écouter l’audio")),
            [this, run] {
              listen(number(run, "start"), number(run, "end"), 0);
            },
            !(bool)audio["midi_only"]);
        side.button(tr(juce::String::fromUTF8("Écouter le MIDI actuel")),
                    [this, run] {
                      listen(number(run, "start"), number(run, "end"), 1);
                    });
        side.button(tr(juce::String::fromUTF8("Arrêter l’écoute")), [this] {
          listenEnd = -1;
          control("pause");
        });
        side.button(
            tr(juce::String::fromUTF8("Fusionner cette série")),
            [this, run] { applyClean({run}); }, !locked);
        side.button(tr(juce::String::fromUTF8("Conserver la séparation")),
                    [this, run] {
                      auto ids = items(run["ids"]);
                      if (!ids.empty())
                        cleanKept.insert((juce::int64)ids[0]);
                      rebuildSide();
                    });
        if (cleanFilter == "kept")
          side.button(tr(juce::String::fromUTF8("Réexaminer cette série")),
                      [this, run] {
                        auto ids = items(run["ids"]);
                        if (!ids.empty())
                          cleanKept.erase((juce::int64)ids[0]);
                        rebuildSide();
                      });
        side.button(
            tr(juce::String::fromUTF8("Précédente")),
            [this] {
              --cleanIndex;
              rebuildSide();
            },
            cleanIndex > 0);
        side.button(
            tr(String::fromUTF8("Suivante")),
            [this] {
              ++cleanIndex;
              rebuildSide();
            },
            cleanIndex + 1 < (int)runs.size());
        side.button(tr(String::fromUTF8("Indices techniques")), [run] {
          auto f = juce::Component::SafePointer<Form>(new Form);
          for (auto &b : items(run["boundaries"]))
            f->label(String(number(b, "start"), 3) +
                     tr(juce::String::fromUTF8(" s · coupure ")) +
                     String(number(b, "gap_ms"), 1) +
                     juce::String::fromUTF8(" ms · ") + b["reason"].toString() +
                     "\n" + b["acoustic"]["reason"].toString());
          f->onApply = [f](var) {
            if (f != nullptr)
              f->close();
          };
          f->show(tr(juce::String::fromUTF8("Frontières de la série")));
        });
      }
      side.button(
          tr(juce::String::fromUTF8("Fusionner les séries proposées restantes")),
          [this] {
            std::vector<var> r;
            for (auto &x : items(clean["runs"]))
              if ((bool)x["proposed"] && !items(x["ids"]).empty() &&
                  !cleanKept.count((juce::int64)items(x["ids"])[0]))
                r.push_back(x);
            applyClean(r);
          },
          !locked);
      side.button(
          tr(juce::String::fromUTF8("Diagnostic de la recherche initiale")),
          [this] {
            auto f = juce::Component::SafePointer<Form>(new Form);
            auto d = clean["diagnostics"];
            for (auto row : std::vector<std::pair<String, String>>{
                     {"scope_notes",
                      tr(juce::String::fromUTF8("Notes dans la portée"))},
                     {"examined_pairs",
                      tr(juce::String::fromUTF8("Paires examinées"))},
                     {"out_of_scope", tr(juce::String::fromUTF8("Hors portée"))},
                     {"overlap", tr(String::fromUTF8("Chevauchements"))},
                     {"gap_too_large", tr(String::fromUTF8("Coupures trop longues"))},
                     {"protected",
                      tr(juce::String::fromUTF8("Protection manuelle / muet"))},
                     {"velocity", tr(juce::String::fromUTF8("Écart de vélocité"))},
                     {"review",
                      tr(juce::String::fromUTF8("Frontières à vérifier"))},
                     {"proposed",
                      tr(juce::String::fromUTF8("Frontières proposées"))},
                     {"short_boundaries",
                      tr(juce::String::fromUTF8("Frontières rapprochées"))},
                     {"percussion_notes", tr(String::fromUTF8("Percussions exclues"))},
                     {"runs", tr(juce::String::fromUTF8("Séries"))}})
              f->label(
                  row.second + " : " +
                  d.getProperty(juce::Identifier(row.first), var()).toString());
            f->onApply = [f](var) {
              if (f != nullptr)
                f->close();
            };
            f->show("Diagnostic");
          });
      side.button(tr(String::fromUTF8("Fermer les propositions")), [this] {
        clean = var();
        roll.suspect.clear();
        roll.focused.clear();
        roll.repaint();
        rebuildSide();
      });
    }
    side.button(
        tr(juce::String::fromUTF8("Édition manuelle…")), [this] { manualDialog(); },
        !locked);
    side.button(
        tr(juce::String::fromUTF8("Réinitialiser le nettoyage")),
        [this] { command(object({{"kind", "reset_clean"}})); }, !locked);
  }
  side.finish();
  sideView.setViewPosition(0, scroll);
}
void App::restoreDynamics() {
  if (audio.isVoid() || result.isVoid() || (bool)audio["midi_only"])
    return;
  invoke("cached_dynamics",
         put(args(), "ids", scope == 1 ? idsVar(roll.selected) : var()),
         [this](var report) {
           if (!report.isVoid()) {
             dynamics = report;
             if (!hasDynDraft) {
               if (!result["editing"]["dynamics_mapping"].isVoid()) {
                 dynMapping = result["editing"]["dynamics_mapping"];
                 manualRange = true;
               } else
                 suggestRange();
               hasDynDraft = true;
             }
             rebuildSide();
           }
         });
}
void App::estimateDynamics(bool force) {
  audioCalculation("dynamics", [this,force] {
    setBusy(true, tr(juce::String::fromUTF8("Calcul de la dynamique…")));
    control("pause");
    invoke(
        "estimate_dynamics",
        put(put(args(), "ids", scope == 1 ? idsVar(roll.selected) : var()),"force",force),
        [this](var report) {
          dynamics = report;
          if (!hasDynDraft) {
            suggestRange();
            hasDynDraft = true;
          }
          setBusy(false, tr(String::fromUTF8("Mesures de dynamique disponibles.")));
          rebuildSide();
        },
        true, [this](String) { setBusy(false); });
  });
}
int App::velocity(double score) const {
  return juce::jlimit(
      (int)dynMapping["min"], (int)dynMapping["max"],
      (int)std::floor(number(dynMapping, "level") +
                      (score - .5) * 80 * number(dynMapping, "contrast") / 100 +
                      .5));
}
void App::suggestRange() {
  if (manualRange)
    return;
  dynMapping = put(
      put(dynMapping, "min",
          (int)std::min(number(dynMapping, "min"),
                        number(dynMapping, "level"))),
      "max",
      (int)std::max(number(dynMapping, "max"), number(dynMapping, "level")));
  std::vector<double> energies, scores;
  for (auto &e : items(dynamics["estimates"]))
    if ((bool)e["usable"] && (int)e["note"]["program"] != 128 &&
        number(e, "harmonic") > 1e-10 && number(e, "attack") >= 0 &&
        std::isfinite(number(e, "score"))) {
      energies.push_back(
          std::log(.7 * number(e, "harmonic") + .3 * number(e, "attack")));
      scores.push_back(number(e, "score"));
    }
  if (energies.size() < 12)
    return;
  std::sort(energies.begin(), energies.end());
  std::sort(scores.begin(), scores.end());
  auto n = energies.size(), trim = std::max((size_t)1, (size_t)(n * .02));
  double median = n % 2 ? energies[(n - 1) / 2]
                        : (energies[n / 2 - 1] + energies[n / 2]) / 2,
         margin = 3 * std::log(10.) / 10, level = number(dynMapping, "level"),
         contrast = number(dynMapping, "contrast");
  auto acoustic = [&](double e) { return level * std::exp((e - median) / 4); };
  auto midi = [&](double s) { return level + (s - .5) * 80 * contrast / 100; };
  dynMapping = put(
      put(dynMapping, "min",
          (int)std::max(
              1., std::floor(std::min({level, acoustic(energies[trim] - margin),
                                       midi(scores[trim])})))),
      "max",
      (int)std::min(127., std::ceil(std::max(
                              {level, acoustic(energies[n - 1 - trim] + margin),
                               midi(scores[n - 1 - trim])}))));
}
void App::applyDynamics() {
  if (locked || dynamics.isVoid() || !validMapping(dynMapping))
    return;
  std::set<std::int64_t> ids;
  for (auto &e : items(dynamics["estimates"]))
    ids.insert((juce::int64)e["note"]["id"]);
  setBusy(true);
  control("pause");
  invoke(
      "apply_dynamics",
      put(put(args(), "ids", idsVar(ids)), "mapping", dynMapping),
      [this](var v) {
        updateResult(v);
        setBusy(false, tr(juce::String::fromUTF8(
                           "Dynamique appliquée · Ctrl+Z pour comparer.")));
        restoreDynamics();
      },
      false, [this](String) { setBusy(false); });
}
void App::compareDynamics() {
  struct Comparison {
    int delta;
    double start;
    std::int64_t id;
    String text;
  };
  std::vector<Comparison> rows;
  for (auto &e : items(dynamics["estimates"])) {
    auto n = e["note"];
    int proposed = velocity(number(e, "score"));
    auto initial = dynamics["reference_velocities"].getProperty(
        juce::Identifier(n["id"].toString()), var());
    var current;
    for (auto &v : items(result["editing"]["notes"]))
      if (v["id"] == n["id"])
        current = v["velocity_midi"];
    int delta = initial.isVoid() ? 0 : proposed - (int)initial;
    rows.push_back(
        {delta, number(n, "start"), (juce::int64)n["id"],
         pitchName((int)n["pitch"]) + String::fromUTF8(" · ") +
             String(number(n, "start"), 2) + " s     " +
             (initial.isVoid() ? String::fromUTF8("—") : initial.toString()) +
             " / " +
             (current.isVoid() ? String::fromUTF8("—") : current.toString()) +
             " / " + String(proposed) + " / " +
             (initial.isVoid()
                  ? String::fromUTF8("—")
                  : (delta > 0 ? String("+") : String()) + String(delta))});
  }
  std::sort(rows.begin(), rows.end(), [](auto &a, auto &b) {
    if (std::abs(a.delta) != std::abs(b.delta))
      return std::abs(a.delta) > std::abs(b.delta);
    if (a.start != b.start)
      return a.start < b.start;
    return a.id < b.id;
  });
  side.label(tr(String::fromUTF8(
                 "Note · position     Initiale / Actuelle / Audio / Écart")),
             42);
  int pages = std::max(1, ((int)rows.size() + 49) / 50);
  dynamicsPage = juce::jlimit(0, pages - 1, dynamicsPage);
  for (int i = dynamicsPage * 50;
       i < std::min((dynamicsPage + 1) * 50, (int)rows.size()); ++i)
    side.label(rows[(size_t)i].text, 34);
  if (pages > 1) {
    side.label(String(dynamicsPage + 1) + " / " + String(pages), 24);
    side.button(
        tr(String::fromUTF8("Précédentes")),
        [this] {
          --dynamicsPage;
          rebuildSide();
        },
        dynamicsPage > 0);
    side.button(
        tr(String::fromUTF8("Suivantes")),
        [this] {
          ++dynamicsPage;
          rebuildSide();
        },
        dynamicsPage + 1 < pages);
  }
}
void App::verificationOptions() {
  auto f = juce::Component::SafePointer<Form>(new Form);
  f->combo("Zone", "scope",
           {{tr(String::fromUTF8("Intervalle audio")), "interval"},
            {tr(juce::String::fromUTF8("Notes sélectionnées")), "selection"},
            {tr(String::fromUTF8("Plage temporelle du roll")), "time"},
            {tr(juce::String::fromUTF8("Tout l’audio")), "all"}},
           verifyScope);
  f->toggle(tr(String::fromUTF8("Tout analyser")), "all", verifyScope == "all");
  f->text(tr(juce::String::fromUTF8("Début (s)")), "start", rangeStart);
  f->text(tr(String::fromUTF8("Fin (s)")), "end", rangeEnd);
  f->onApply = [this, f](var v) {
    verifyScope = (bool)v["all"] ? String("all") : v["scope"].toString();
    rangeStart = decimal(v["start"]);
    rangeEnd = decimal(v["end"]);
    if (f != nullptr)
      f->close();
    rebuildSide();
  };
  f->show(tr(juce::String::fromUTF8("Zone de vérification")));
}
void App::verify() {
  if (locked)
    return;
  if(result.isVoid()){ensureGuidedEditor([s=safe()]{if(s)s->verify();});return;}
  double a = rangeStart, b = std::min(rangeEnd, number(audio, "duration"));
  var ids;
  if ((bool)roll.selection["temporal"])
    verifyScope = "time";
  if (verifyScope == "all") {
    a = 0;
    b = number(audio, "duration");
  }
  if (verifyScope == "time") {
    a = number(roll.selection, "start");
    b = number(roll.selection, "end");
  }
  if (verifyScope == "selection") {
    ids = idsVar(roll.selected);
    a = number(audio, "duration");
    b = 0;
    for (auto &n : roll.notes)
      if (roll.selected.count(n.id)) {
        a = std::min(a, n.start);
        b = std::max(b, n.end);
      }
  }
  if (!std::isfinite(a) || !std::isfinite(b) || a < 0 || b <= a ||
      b > number(audio, "duration")) {
    status.setText(
        tr(juce::String::fromUTF8("Choisissez un intervalle valide dans l’audio.")),
        juce::dontSendNotification);
    return;
  }
  audioCalculation("verification", [this, a, b, ids] {
    setBusy(true, tr(juce::String::fromUTF8("Vérification MIDI / audio…")));
    invoke(
        "verify_midi_audio",
        put(put(put(put(args(), "start", a), "end", b), "ids", ids),"pitchRange",roll.bandEnabled?array({roll.bandLow,roll.bandHigh}):var()),
        [this](var r) {
          verification = r;diagnosticPages.clear();
          diagnosticActive.clear();
          roll.diagnosticGhost.clear();
          verificationStale = false;
          ignored.clear();
          kept.clear();
          updateDiagnostics();
          setBusy(false, tr(juce::String::fromUTF8(
                             "Vérification terminée · aucune note modifiée.")));
          rebuildSide();
        },
        true, [this](String) { setBusy(false); });
  });
}
void App::updateDiagnostics() {
  roll.ghosts.clear();
  roll.holds.clear();
  roll.timing.clear();
  roll.extra.clear();
  if (verification.isVoid() || verificationStale)
    return;
  auto notes = result["editing"]["notes"];
  for (auto &e : items(verification["existing"]))
    if (e["presence"].toString() == "probably_absent" &&
        !kept.count((juce::int64)e["id"]) && geometry(e, notes))
      roll.extra.insert((juce::int64)e["id"]);
  for (auto &c : items(verification["missing"])) {
    if (ignored.count(c["id"].toString()))
      continue;
    bool exists = false;
    for (auto &n : items(notes))
      if (!(bool)n["muted"] && (int)n["pitch_midi"] == (int)c["pitch"] &&
          number(n, "start_time_s") <= number(c, "start") + .06 &&
          number(n, "end_time_s") >= number(c, "start") - .06) {
        exists = true;
        break;
      }
    if (!exists)
      roll.ghosts.push_back(c);
  }
  for(auto& t:items(verification["timing"])) {
    if(ignored.count(t["id"].toString()))continue;
    for(auto& n:items(notes))
      if(n["id"]==t["note_id"] && !(bool)n["muted"] &&
         (int)n["pitch_midi"]==(int)t["pitch"] &&
         std::abs(number(n,"start_time_s")-number(t,"start"))<1e-5 &&
         std::abs(number(n,"end_time_s")-number(t,"end"))<1e-5) {
        roll.timing.push_back(t);break;
      }
  }
  for (auto &h : items(verification["fragmented"])) {
    if (ignored.count(h["id"].toString()))
      continue;
    bool valid = canMerge(idSet(h["ids"]));
    for (auto &id : items(h["ids"])) {
      bool found = false;
      for (auto &e : items(verification["existing"]))
        if (e["id"] == id && geometry(e, notes)) {
          found = true;
          break;
        }
      valid &= found;
    }
    if (valid)
      roll.holds.push_back(h);
  }
  roll.repaint();
}
void App::applyDiagnostic(const String &kind, const var &item) {
  if (locked)
    return;
  if(kind=="split" || kind=="extend") {
    auto action=kind=="split"
      ? object({{"kind","split"},{"id",item["note_id"]},{"at",item["at"]}})
      : object({{"kind","extend"},{"id",item["note_id"]},{"end",item["proposed_end"]}});
    command(action,[this,item](var){ignored.insert(item["id"].toString());updateDiagnostics();rebuildSide();},true);
  }
  else if (kind == "extra")
    command(
        object({{"kind", "delete"}, {"ids", array({item["id"]})}}),
        [this, item](var) {
          kept.insert((juce::int64)item["id"]);
          updateDiagnostics();
          rebuildSide();
        },
        true);
  else if (kind == "hold")
    command(
        object({{"kind", "merge"}, {"ids", item["ids"]}}),
        [this, item](var) {
          ignored.insert(item["id"].toString());
          updateDiagnostics();
          rebuildSide();
        },
        true);
  else if(kind=="missing") {
    var part;
    for (auto &n : roll.notes)
      if (partId < 0 || n.part == partId) {
        part = n.source["part"];
        break;
      }
    command(
        object({{"kind", "add"},
                {"pitch", item["pitch"]},
                {"start", item["start"]},
                {"end", item["end"]},
                {"velocity", 80},
                {"part", part}}),
        [this, item](var) {
          ignored.insert(item["id"].toString());
          updateDiagnostics();
          rebuildSide();
        },
        true);
  }
}
var App::diagnosticDestinations() const {
  auto locks=idSet(result["editing"]["locked_parts"]);std::set<int> seen;std::vector<var> choices;
  for(const auto& p:items(result["midi_parts"])){
    int key=p["identity"].isVoid()?(int)p["program"]:(int)p["identity"];seen.insert(key);if(!locks.count(key))choices.push_back(p);
  }
  for(const auto& value:items(result["editing"]["notes"])){
    auto n=Note::parse(value);if(!seen.insert(n.part).second||locks.count(n.part))continue;
    auto p=value["part"];if(!p.isObject())p=object({{"program",0},{"is_drum",false},{"label",tr(String::fromUTF8("Piano"))}});
    choices.push_back(p);
  }
  return array(choices);
}
var App::diagnosticBatchPlan(const String& section) {
  std::vector<var> actions;std::set<std::int64_t> used;
  auto locks=idSet(result["editing"]["locked_parts"]);
  std::map<std::int64_t,Note> notes;
  for(const auto& value:items(result["editing"]["notes"])) {auto n=Note::parse(value);notes.emplace(n.id,n);}
  int missing=0,holds=0,timing=0,extra=0,skipped=0;
  auto append=[&](var action,const std::set<std::int64_t>& ids,int& count){
    for(auto id:ids){auto n=notes.find(id);if(n==notes.end()||locks.count(n->second.part)||used.count(id)){++skipped;return;}}
    used.insert(ids.begin(),ids.end());actions.push_back(action);++count;
  };
  // Keep whole holds first. A fragment cannot also be split, extended or deleted.
  if(section=="all"||section=="holds")for(const auto& h:roll.holds)
    append(object({{"kind","merge"},{"ids",h["ids"]}}),idSet(h["ids"]),holds);
  if(section=="all"||section=="timing")for(const auto& t:roll.timing){
    auto kind=t["kind"].toString();if(kind!="split"&&kind!="extend"){++skipped;continue;}
    auto action=kind=="split" ? object({{"kind","split"},{"id",t["note_id"]},{"at",t["at"]}})
                              : object({{"kind","extend"},{"id",t["note_id"]},{"end",t["proposed_end"]}});
    append(action,{(juce::int64)t["note_id"]},timing);
  }
  if(section=="all"||section=="extra")for(const auto& e:items(verification["existing"]))
    if(roll.extra.count((juce::int64)e["id"]))append(object({{"kind","delete"},{"ids",array({e["id"]})}}),{(juce::int64)e["id"]},extra);
  auto coverageActions=actions;std::set<std::tuple<int,juce::int64,juce::int64>> additions;
  if(section=="all"||section=="missing")for(const auto& c:roll.ghosts){
    // Do not add a ghost already covered by a planned hold or prolongation.
    bool covered=false;
    for(const auto& a:coverageActions){
      if(a["kind"].toString()=="extend"){
        auto n=notes.find((juce::int64)a["id"]);
        covered|=n!=notes.end()&&n->second.pitch==(int)c["pitch"]&&n->second.start<=number(c,"start")+.06&&number(a,"end")>=number(c,"end")-.06;
      } else if(a["kind"].toString()=="merge"){
        double start=1200,end=0;int pitch=-1;
        for(auto id:idSet(a["ids"])){auto n=notes.find(id);if(n!=notes.end()){start=std::min(start,double(n->second.start));end=std::max(end,double(n->second.end));pitch=n->second.pitch;}}
        covered|=pitch==(int)c["pitch"]&&start<=number(c,"start")+.06&&end>=number(c,"end")-.06;
      }
    }
    auto geometry=std::make_tuple((int)c["pitch"],juce::int64(std::llround(number(c,"start")*1000)),juce::int64(std::llround(number(c,"end")*1000)));
    if(covered||!additions.insert(geometry).second){++skipped;continue;}
    append(object({{"kind","add"},{"pitch",c["pitch"]},{"start",c["start"]},{"end",c["end"]},{"velocity",80}}),{},missing);
  }
  return object({{"actions",array(actions)},{"missing",missing},{"holds",holds},{"timing",timing},{"extra",extra},{"skipped",skipped}});
}
void App::applyDiagnosticBatch(const String& section) {
  if(locked||verificationStale||verification.isVoid())return;
  auto plan=diagnosticBatchPlan(section),request=args();
  auto f=juce::Component::SafePointer<Form>(new Form);
  f->setComponentID("diagnosticBatchConfirmation");
  f->label(tr(String::fromUTF8("Propositions de la dernière vérification, dans la zone analysée. Vérifiez-les à l’écoute avant de les appliquer.")));
  for(const auto& row:std::vector<std::pair<const char*,String>>{{"missing",tr(String::fromUTF8("Notes à ajouter : "))},{"holds",tr(String::fromUTF8("Tenues à fusionner : "))},{"timing",tr(String::fromUTF8("Durées / réattaques à appliquer : "))},{"extra",tr(String::fromUTF8("Notes à supprimer : "))}})
    f->label(row.second+plan[row.first].toString(),false);
  if((int)plan["skipped"]>0)f->label(tr(String::fromUTF8("Propositions exclues (verrouillage, conflit ou doublon) : "))+plan["skipped"].toString(),false);
  if((int)plan["missing"]>0){
    std::vector<std::pair<String,var>> choices;var selected;
    for(const auto& p:items(diagnosticDestinations())){
      int key=p["identity"].isVoid()?(int)p["program"]:(int)p["identity"];
      choices.emplace_back(displayPartLabel(p),p);
      if(selected.isVoid()||key==partId)selected=p;
    }
    choices.emplace_back(tr(String::fromUTF8("Nouvelle voix…")),"new");if(selected.isVoid())selected="new";
    f->combo(tr(String::fromUTF8("Voix de destination des notes ajoutées")),"destination",choices,selected);
    f->label(tr(String::fromUTF8("L’analyse spectrale n’attribue pas automatiquement les notes manquantes à un instrument.")));
  }
  f->label(tr(String::fromUTF8("Une seule annulation (Ctrl+Z) restaure tout le lot. Les voix verrouillées sont conservées. En cas de conflit, les tenues sont prioritaires sur les durées, puis les suppressions.")));
  f->setApplyEnabled(plan["actions"].size()>0);
  f->onApply=[this,f,plan,request](var values){
    if(!f||locked)return;
    if(verificationStale||audio["id"]!=request["audioId"]||result["analysis_id"]!=request["analysisId"]||result["editing"]["revision"]!=request["revision"]){
      f->close();juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,tr(String::fromUTF8("Appliquer les propositions")),tr(String::fromUTF8("La vérification a changé. Ouvrez à nouveau le récapitulatif.")));return;
    }
    auto actions=plan["actions"].clone();
    bool create=values["destination"].toString()=="new";
    for(auto& action:*actions.getArray())if(action["kind"].toString()=="add")action.getDynamicObject()->setProperty("part",create?var():values["destination"]);
    f->close();
    command(object({{"kind","diagnostic_batch"},{"actions",actions},{"new_voice",create?var(tr(String::fromUTF8("Propositions spectrales"))):var()}}));
  };
  f->show(tr(String::fromUTF8("Appliquer les propositions")));
}
void App::cleanOptions() {
  auto f = juce::Component::SafePointer<Form>(new Form);
  f->combo(tr(juce::String::fromUTF8("Portée")), "scope",
           {{tr(String::fromUTF8("Tout le morceau")), "all"},
            {tr(juce::String::fromUTF8("Notes sélectionnées")), "notes"},
            {tr(juce::String::fromUTF8("Zone temps × hauteurs")), "region"}},
           cleanScope);
  f->text(tr(juce::String::fromUTF8("Début (s)")), "start",
          number(roll.selection, "start", rangeStart));
  f->text(tr(String::fromUTF8("Fin (s)")), "end", number(roll.selection, "end", rangeEnd));
  f->text(tr(String::fromUTF8("Hauteur minimale")), "low", number(roll.selection, "low", 0));
  f->text(tr(String::fromUTF8("Hauteur maximale")), "high", number(roll.selection, "high", 127));
  f->text(tr(String::fromUTF8("Coupure maximale entre fragments (ms)")), "gap_ms",
          cleanSettings["gap_ms"]);
  f->text(tr(juce::String::fromUTF8("Écart de vélocité maximal")), "velocity_delta",
          cleanSettings["velocity_delta"]);
  f->toggle(tr(juce::String::fromUTF8("Vérifier dans l’audio · essai")), "acoustic",
            acoustic);
  f->onApply = [this, f](var v) {
    cleanScope = v["scope"].toString();
    rangeStart = decimal(v["start"]);
    rangeEnd = decimal(v["end"]);
    cleanSettings =
        object({{"gap_ms", decimal(v["gap_ms"])},
                {"velocity_delta", (int)decimal(v["velocity_delta"])},
                {"sustain_min", .3},
                {"regionLow", (int)decimal(v["low"])},
                {"regionHigh", (int)decimal(v["high"])}});
    acoustic = (bool)v["acoustic"];
    clean = var();
    if (f != nullptr)
      f->close();
    rebuildSide();
  };
  f->show(tr(juce::String::fromUTF8("Recherche de séries")));
}
void App::searchClean() {
  if (locked)
    return;
  var region =
      cleanScope == "all" ? object({{"kind", "all"}})
      : cleanScope == "notes"
          ? object({{"kind", "notes"}, {"ids", idsVar(roll.selected)}})
          : object({{"kind", "region"},
                    {"start", rangeStart},
                    {"end", rangeEnd},
                    {"low", (int)number(cleanSettings, "regionLow", 0)},
                    {"high", (int)number(cleanSettings, "regionHigh", 127)}});
  if (partId >= 0)
    region = object({{"kind", "part"}, {"program", partId}, {"scope", region}});
  if(acoustic && roll.bandEnabled) {
    std::set<std::int64_t> eligible;
    for(auto& value:items(result["editing"]["notes"])) {
      auto n=Note::parse(value);
      if((partId<0||n.part==partId) && n.pitch>=roll.bandLow && n.pitch<=roll.bandHigh &&
        (cleanScope!="notes"||roll.selected.count(n.id)) &&
        (cleanScope!="region"||(n.start<rangeEnd && n.end>rangeStart && n.pitch>=number(cleanSettings,"regionLow",0) && n.pitch<=number(cleanSettings,"regionHigh",127))))eligible.insert(n.id);
    }
    if(eligible.empty()){status.setText(tr(String::fromUTF8("Aucune note dans ce registre pour la recherche de séries.")),juce::dontSendNotification);return;}
    region=object({{"kind","notes"},{"ids",idsVar(eligible)}});
  }
  auto run = [this, region] {
    setBusy(true, tr(juce::String::fromUTF8("Recherche des séries…")));
    control("pause");
    invoke(
        "preview_clean",
        put(put(put(args(), "scope", region), "settings", cleanSettings),
            "acoustic", acoustic),
        [this](var v) {
          clean = v;
          cleanKept.clear();
          cleanFilter = "review";
          for (auto &run : items(v["runs"]))
            if ((bool)run["proposed"])
              cleanFilter = "proposed";
          cleanIndex = 0;
          roll.suspect.clear();
          for (auto &r : items(clean["runs"]))
            for (auto &id : items(r["ids"]))
              roll.suspect.insert((juce::int64)id);
          setBusy(false, tr(String::fromUTF8("Recherche des séries terminée.")));
          rebuildSide();
        },
        true, [this](String) { setBusy(false); });
  };
  if (acoustic)
    audioCalculation("cleanup", run);
  else
    run();
}
void App::applyClean(const std::vector<var> &runs) {
  if (locked || runs.empty())
    return;
  std::vector<var> ids;
  for (auto &r : runs) {
    auto a = items(r["ids"]);
    if (!a.empty())
      ids.push_back(a[0]);
  }
  setBusy(true);
  control("pause");
  invoke(
      "apply_clean", put(args(), "runIds", array(ids)),
      [this](var receipt) {
        clean = receipt["preview"];
        updateResult(receipt["view"]);
        roll.suspect.clear();
        for (auto &r : items(clean["runs"]))
          for (auto &id : items(r["ids"]))
            roll.suspect.insert((juce::int64)id);
        setBusy(false, String(number(receipt, "merged_runs"), 0) +
                           tr(juce::String::fromUTF8(
                               " séries fusionnées · Ctrl+Z pour annuler.")));
        rebuildSide();
      },
      false, [this](String) { setBusy(false); });
}
std::vector<App::ToneVoice> App::toneVoices() const {
  std::vector<ToneVoice> groups;if(!(bool)tone["enabled"]||toneReport.isVoid())return groups;
  auto outside=idSet(toneReport["out_ids"]);
  for(const auto& value:items(result["editing"]["notes"])){
    auto n=Note::parse(value);if(!outside.count(n.id)||(partId>=0&&n.part!=partId))continue;
    auto at=std::find_if(groups.begin(),groups.end(),[&](const auto& g){return g.key==n.part;});
    if(at==groups.end()){groups.push_back({n.part,n.label,{}});at=std::prev(groups.end());}at->notes.push_back(n);
  }
  std::sort(groups.begin(),groups.end(),[](const auto& a,const auto& b){return a.key<b.key;});
  for(auto& g:groups)std::sort(g.notes.begin(),g.notes.end(),[](const auto& a,const auto& b){return std::tie(a.start,a.pitch,a.id)<std::tie(b.start,b.pitch,b.id);});
  return groups;
}
void App::selectToneNotes(int voice) {
  if(locked||result.isVoid())return;
  if(!(bool)tone["enabled"]||toneReport.isVoid()){
    analyzeTone(false,[s=safe(),voice]{if(s)s->selectToneNotes(voice);});return;
  }
  std::set<std::int64_t> ids;auto locks=idSet(result["editing"]["locked_parts"]);
  for(const auto& g:toneVoices())if((voice<0||voice==g.key)&&!locks.count(g.key))for(const auto& n:g.notes)ids.insert(n.id);
  roll.selectIds(ids);refresh();
}
void App::focusToneNote(std::int64_t id) {
  if(locked)return;
  for(const auto& g:toneVoices())for(const auto& n:g.notes)if(n.id==id){
    roll.selectIds({id});roll.focused={id};roll.diagnosticGhost.clear();diagnosticActive="tone:"+String(id);
    locate(object({{"start",n.start},{"end",n.end},{"pitch",n.pitch}}));return;}
}
Form* App::toneDetailsForm(int page) {
  auto f=juce::Component::SafePointer<Form>(new Form);f->setHeading(tr(String::fromUTF8("Détail par voix")));f->actions(tr(String::fromUTF8("Fermer")),{});f->onApply=[f](var){if(f)f->close();};
  auto groups=toneVoices();int total=0;for(const auto& g:groups)total+=int(g.notes.size());
  int pages=std::max(1,(total+49)/50);page=juce::jlimit(0,pages-1,page);
  f->label(localisation::count("out_of_scale",total)+String::fromUTF8(" · ")+(tone["scope"].toString()=="selection"?tr(String::fromUTF8("Notes sélectionnées")):tr(String::fromUTF8("Tout le morceau"))));
  f->label(tr(String::fromUTF8("Cliquez sur une note pour la localiser. Les voix verrouillées restent consultables et sont exclues des sélections.")));
  auto* panel=new SidePanel;panel->width=460;auto locks=idSet(result["editing"]["locked_parts"]);int index=0;
  for(const auto& g:groups){int first=index;index+=int(g.notes.size());if(index<=page*50||first>=(page+1)*50)continue;
    auto title=g.name+String::fromUTF8(" · ")+localisation::count("out_of_scale",int(g.notes.size()));if(locks.count(g.key))title+=String::fromUTF8(" · ")+tr(String::fromUTF8("Verrouillée"));
    panel->label(title,32);
    panel->button(tr(String::fromUTF8("Sélectionner les notes hors gamme")),[s=safe(),f,key=g.key]{if(!s)return;if(f)f->close();s->selectToneNotes(key);},!locked&&!locks.count(g.key));
    for(int i=std::max(0,page*50-first);i<std::min(int(g.notes.size()),(page+1)*50-first);++i){const auto& n=g.notes[size_t(i)];
      auto detail=String(n.start,3)+String::fromUTF8(" → ")+String(n.end,3)+" s"+String::fromUTF8(" · ")+tr(String::fromUTF8("Durée "))+String(int(std::llround((n.end-n.start)*1000)))+" ms";
      panel->diagnosticRow(Roll::noteLabel(n.pitch),detail,roll.focused.count(n.id)>0,[s=safe(),f,id=n.id]{if(!s)return;if(f)f->close();s->focusToneNote(id);});
    }
  }
  if(!total)panel->label(tr(String::fromUTF8("Aucune note hors gamme dans cette portée.")));
  if(pages>1){panel->label(String(page+1)+" / "+String(pages),24);
    auto navigate=[s=safe(),f](int next){if(!s)return;if(f)f->close();juce::MessageManager::callAsync([s,next]{if(s)s->toneDetailsForm(next)->show(tr(String::fromUTF8("Détail par voix")));});};
    panel->button(tr(String::fromUTF8("Précédentes")),[navigate,page]{navigate(page-1);},page>0);
    panel->button(tr(String::fromUTF8("Suivantes")),[navigate,page]{navigate(page+1);},page+1<pages);}
  panel->setSize(460,panel->yy);f->custom(panel,panel->yy);return f.getComponent();
}
void App::toneOptions() {
  if (!buttonMap.count("toneSuggest")) {
    for (auto *c : {&toneRoot, &toneMode, &toneScope})
      addChildComponent(c);
    addChildComponent(toneCaption);
    addChildComponent(toneHelp);
    const char* roots[]={"Do / C","Do♯ / Ré♭","Ré / D","Ré♯ / Mi♭","Mi / E","Fa / F","Fa♯ / Sol♭","Sol / G","Sol♯ / La♭","La / A","La♯ / Si♭","Si / B"};
    for (int i = 0; i < 12; ++i) toneRoot.addItem(tr(String::fromUTF8(roots[i])),i+1);
    const char *names[] = {"Majeure",
                           "Mineure naturelle",
                           "Mineure harmonique",
                           "Mineure mélodique",
                           "Dorienne",
                           "Phrygienne",
                           "Lydienne",
                           "Mixolydienne",
                           "Locrienne",
                           "Pentatonique majeure",
                           "Pentatonique mineure",
                           "Chromatique"};
    for (int i = 0; i < 12; ++i)
      toneMode.addItem(tr(String::fromUTF8(names[i])), i + 1);
    toneScope.addItem(tr(String::fromUTF8("Tout le morceau")), 1);
    toneScope.addItem(tr(String::fromUTF8("Notes sélectionnées")), 2);
    auto changed = [this] {
      ++toneVersion;
      if (tone["scope"].toString() != (toneScope.getSelectedId() == 2
                                           ? String("selection")
                                           : String("all"))) {
        toneReport = var();
        tone = put(tone, "ids", var());
      }
      const char *modes[] = {"major",
                             "minor",
                             "harmonic_minor",
                             "melodic_minor",
                             "dorian",
                             "phrygian",
                             "lydian",
                             "mixolydian",
                             "locrian",
                             "major_pentatonic",
                             "minor_pentatonic",
                             "chromatic"};
      tone = put(
          put(put(tone, "scale",
                  object(
                      {{"tonic", toneRoot.getSelectedId() - 1},
                       {"mode", modes[juce::jlimit(
                                    0, 11, toneMode.getSelectedId() - 1)]}})),
              "scope", toneScope.getSelectedId() == 2 ? "selection" : "all"),
          "enabled", false);
      if(guidedUseScale)invalidateGuided();
      roll.tonal.clear();
      toneOptions();roll.repaint();
      edited();
    };
    toneRoot.onChange = changed;
    toneMode.onChange = changed;
    toneScope.onChange = changed;
    button("toneAdjust", tr(String::fromUTF8("Régler la gamme / portée")),[this]{toneExpanded=!toneExpanded;toneOptions();resized();});
    button("toneSuggest", tr(String::fromUTF8("Suggérer une tonalité")),
           [this] { analyzeTone(true); });
    button("toneShow", tr(String::fromUTF8("Afficher les notes hors gamme")),
           [this] { analyzeTone(false); });
    button("toneSelect", tr(String::fromUTF8("Sélectionner les notes hors gamme")),
           [this] { selectToneNotes(); });
    button("toneDetails",tr(String::fromUTF8("Détail par voix…")),[this]{toneDetailsForm()->show(tr(String::fromUTF8("Détail par voix")));});
    button("toneHide", tr(String::fromUTF8("Masquer le repérage")), [this] {
      ++toneVersion;
      roll.tonal.clear();
      tone = put(tone, "enabled", false);
      toneOptions();roll.repaint();
      edited();
    });
    toneCaption.setFont(juce::FontOptions(16, juce::Font::bold));toneCaption.setMinimumHorizontalScale(1);
    toneHelp.setFont(juce::FontOptions(15));
  }
  const char *modes[] = {"major",
                         "minor",
                         "harmonic_minor",
                         "melodic_minor",
                         "dorian",
                         "phrygian",
                         "lydian",
                         "mixolydian",
                         "locrian",
                         "major_pentatonic",
                         "minor_pentatonic",
                         "chromatic"};
  toneRoot.setSelectedId((int)tone["scale"]["tonic"] + 1,
                         juce::dontSendNotification);
  for (int i = 0; i < 12; ++i)
    if (tone["scale"]["mode"].toString() == modes[i])
      toneMode.setSelectedId(i + 1, juce::dontSendNotification);
  toneScope.setSelectedId(tone["scope"].toString() == "selection" ? 2 : 1,
                          juce::dontSendNotification);
  String caption=tr(String::fromUTF8("Tonalité / gamme"));
  if((bool)tone["enabled"]){auto voices=toneVoices();int total=0;for(const auto& g:voices)total+=int(g.notes.size());
    caption+=String::fromUTF8(" · ")+toneRoot.getText()+" "+toneMode.getText()+String::fromUTF8(" · ")+(toneReport.isVoid()?tr(String::fromUTF8("Diagnostic à recalculer")):localisation::count("out_of_scale",total));
    if(partId>=0){for(const auto& p:parts)if(p.first==partId){caption+=String::fromUTF8(" · ")+p.second;break;}}
    else for(const auto& g:voices)caption+=String::fromUTF8(" · ")+g.name+" : "+String(int(g.notes.size()));
  }
  toneCaption.setText(caption,juce::dontSendNotification);toneCaption.setTooltip(caption);
  toneHelp.setText(
      tr(String::fromUTF8(
          "Magenta : hors gamme choisie, pas forcément une fausse note. Aucun "
          "déplacement, suppression ou nettoyage automatique.")),
      juce::dontSendNotification);
  for (juce::Component *c : std::initializer_list<juce::Component *>{
           &toneRoot, &toneMode, &toneScope, &toneCaption, &toneHelp})
    c->setVisible(tonalityOpen && (c==&toneCaption || c==&toneHelp || toneExpanded));
  for (auto k : {"toneSuggest", "toneShow", "toneSelect", "toneHide"})
    buttonMap[k]->setVisible(tonalityOpen && (toneExpanded || String(k)=="toneSelect" || String(k)=="toneHide"));
  buttonMap["toneAdjust"]->setVisible(tonalityOpen);
  buttonMap["toneDetails"]->setVisible(tonalityOpen);buttonMap["toneDetails"]->setEnabled(!locked&&(bool)tone["enabled"]&&!toneReport.isVoid());
  buttonMap["toneAdjust"]->setButtonText((toneExpanded?tr(String::fromUTF8("Réduire les réglages")):tr(String::fromUTF8("Régler la gamme / portée"))));
  for (auto *b : toneSuggestions) b->setVisible(tonalityOpen && toneExpanded);
  // These controls are created lazily and can also be restored from a project.
  // Always reserve their space when changing their visibility.
  resized();
  repaint();
}
void App::analyzeTone(bool suggest,std::function<void()> done) {
  if (result.isVoid())
    return;
  var examined;
  if (tone["scope"].toString() == "selection") {
    if (suggest)
      examined = idsVar(roll.selected);
    else {
      auto remembered =
          tone["ids"];
      if (remembered.isArray()) {
        std::set<std::int64_t> alive, valid = idSet(remembered);
        for (auto &n : items(result["editing"]["notes"]))
          if (valid.count((juce::int64)n["id"]))
            alive.insert((juce::int64)n["id"]);
        examined = idsVar(alive);
      } else
        examined = idsVar(roll.selected);
    }
  }
  const int version=++toneVersion;auto requested=args();const int requestedPart=partId;
  invoke("tonal_analysis",
         put(put(requested,"ids",examined),"scale",suggest?var():tone["scale"]),
         [this,suggest,version,requested,requestedPart,examined,done](var r) {
           if(version!=toneVersion||partId!=requestedPart||audio["id"]!=requested["audioId"]||result["analysis_id"]!=requested["analysisId"]||result["editing"]["revision"]!=requested["revision"])return;
           toneReport = r;
           tone = put(tone, "scale", r["scale"]);
           tone = put(tone,"ids",examined.isArray()?examined:r["examined_ids"]);
           if (suggest) {
             tone=put(tone,"enabled",false);roll.tonal.clear();roll.repaint();
             toneSuggestions.clear();
             for (auto &candidate : items(r["suggestions"])) {
               auto *b = toneSuggestions.add(new juce::TextButton(
                   pitchName((int)candidate["scale"]["tonic"] + 60)
                       .dropLastCharacters(1) +
                   String::fromUTF8(" · ") +
                   candidate["scale"]["mode"].toString()));
               addAndMakeVisible(b);
               b->onClick = [this, candidate] {
                 ++toneVersion;
                 tone = put(put(tone, "scale", candidate["scale"]), "enabled",
                            false);
                 roll.tonal.clear();
                 toneOptions();
                 edited();
               };
             }
             toneOptions();
             resized();
           } else {
             roll.tonal = idSet(r["out_ids"]);
             tone = put(tone, "enabled", true);
             toneExpanded=false;
             toneOptions();resized();
             roll.repaint();
           }
           if(suggest)dirty = true;
           else if(done)done();
         });
}
void App::findShort(bool reuseScope) {
  if (result.isVoid())
    return;
  auto ids =
      shortState["scope"].toString() == "selection"
          ? (reuseScope && shortState["ids"].isArray() ? shortState["ids"]
                                                       : idsVar(roll.selected))
          : var();
  shortState = put(put(shortState, "ids", ids), "enabled", true);
  invoke(
      "short_note_analysis",
      put(put(args(), "ids", ids), "thresholdMs", shortState["threshold_ms"]),
      [this](var r) {
        shortReport = r;
        roll.shortIds.clear();
        for (auto &n : items(r["notes"]))
          roll.shortIds.insert((juce::int64)n["id"]);
        roll.repaint();
        rebuildSide();
      });
}


// Navigation only: playback is paused/repositioned, MIDI and undo history stay intact.
void App::navigateDiagnostic(const String& kind, const var& item) {
  if (locked || verificationStale) return;
  diagnosticActive = kind + ":" + item["id"].toString();
  roll.diagnosticGhost = (kind == "missing" || kind=="split" || kind=="extend") ? item["id"].toString() : String();
  auto ids = kind == "hold" ? idSet(item["ids"]) : std::set<std::int64_t>{};
  if (kind == "extra") ids.insert((juce::int64)item["id"]);
  if(kind=="split" || kind=="extend")ids.insert((juce::int64)item["note_id"]);
  if (!ids.empty() && partId >= 0) {
    bool visible = false;
    for (auto& n : roll.notes) if (ids.count(n.id)) visible = true;
    if (!visible) selectPart(-1);
  }
  roll.selectIds(ids);
  roll.focused = ids;
  locate(item);
  roll.repaint();
  rebuildSide();
}
