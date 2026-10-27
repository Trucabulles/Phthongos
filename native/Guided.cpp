#include "App.h"
#include "../core/project/DocumentJson.h"
namespace {
var addField(var value,const char* key,var field){value=value.clone();value.getDynamicObject()->setProperty(key,field);return value;}
var strings(const std::set<String>& values){juce::Array<var> out;for(auto& v:values)out.add(v);return var(out);}
}
void App::invalidateGuided(){guidedReport=var();guidedSelected.clear();roll.guidedGhosts.clear();roll.guidedCurve=var();if(roll.diagnosticGhost.startsWith("guided-"))roll.diagnosticGhost={};}
void App::updateMaskView(){
  roll.spectralMask.clear();unsigned key=partId<0?phthongos::allMaskParts:unsigned(partId);auto all=result["editing"]["spectral_masks"];auto value=all[juce::Identifier(String(key))];
  for(auto& stroke:items(value))roll.spectralMask.push_back(std::make_shared<const phthongos::MaskStroke>(phthongos::project::readMaskStroke(stroke)));
  roll.maskRevision=std::uint64_t((juce::int64)result["editing"]["revision"])^(std::uint64_t(key)<<32)^(std::uint64_t((juce::int64)audio["id"])*0x9e3779b97f4a7c15ULL);roll.repaint();
}
void App::ensureGuidedEditor(std::function<void()> done){
  if(locked||audio.isVoid()||(bool)audio["midi_only"])return;if(!result.isVoid()){done();return;}
  setBusy(true,tr(String::fromUTF8("Préparation de l’édition guidée…")));
  invoke("create_blank_midi",object({{"audioId",audio["id"]}}),[this,done](var v){referenceAudio=true;mode=2;updateResult(v);syncNotes(false,[this,done](var){setBusy(false);done();});},false,[this](String e){setBusy(false,e);});
}
void App::setMaskTool(int mode){
  if(locked)return;roll.maskMode=roll.maskMode==mode?0:mode;roll.tool=0;roll.maskVisible=true;
  if(roll.maskMode){displayMode.setSelectedId(3,juce::sendNotificationSync);audioLayer.setSelectedId(2,juce::sendNotificationSync);}
  roll.setTooltip({});
  roll.updateToolCursor();rebuildSide();refresh();
}
void App::commitMask(const phthongos::MaskStroke& stroke){auto encoded=phthongos::project::writeMaskStroke(stroke);auto key=partId<0?int(phthongos::allMaskParts):partId;
  ensureGuidedEditor([this,encoded,key]{command(object({{"kind","mask_stroke"},{"part",key},{"stroke",encoded}}));});
}
std::vector<var> App::visibleGuidedNotes() const {
  std::vector<var> result;for(auto& note:items(guidedReport["notes"])){
    auto category=note["category"].toString();if(category.isEmpty())category="review";
    if(guidedCategories.count(category))result.push_back(note);
  }return result;
}
void App::updateGuidedView(){
  auto visible=visibleGuidedNotes();roll.guidedGhosts=guidedShowProposals?visible:std::vector<var>{};
  roll.guidedCurve=guidedShowCurve?guidedReport["curve"]:var();
  guidedPage=std::max(0,std::min(guidedPage,std::max(0,(int(visible.size())-1)/40)));roll.repaint();
}
void App::installGuided(var report){guidedReport=std::move(report);updateGuidedView();rebuildSide();}
void App::focusGuided(const String& id){
  guidedShowProposals=true;updateGuidedView();
  if(juce::ModifierKeys::getCurrentModifiers().isCtrlDown()){if(guidedSelected.count(id))guidedSelected.erase(id);else guidedSelected.insert(id);}else{guidedSelected.clear();guidedSelected.insert(id);}
  for(auto& note:items(guidedReport["notes"]))if(note["id"].toString()==id){locate(note);roll.diagnosticGhost=id;break;}roll.repaint();rebuildSide();
}
void App::analyzeGuided(){
  ensureGuidedEditor([this]{
    if(guidedUseMask&&roll.spectralMask.empty()){status.setText(tr(String::fromUTF8("Peignez une zone ou désactivez « Analyser uniquement la sélection peinte ».")),juce::dontSendNotification);return;}
    auto request=args();request=addField(request,"useScale",guidedUseScale);request=addField(request,"scale",tone["scale"]);request=addField(request,"low",roll.bandEnabled?roll.bandLow:0);request=addField(request,"high",roll.bandEnabled?roll.bandHigh:127);request=addField(request,"useMask",guidedUseMask);request=addField(request,"maskExtent",guidedZone=="mask");request=addField(request,"start",guidedZone=="visible"?roll.start():0.);request=addField(request,"end",std::min(number(audio,"duration"),guidedZone=="visible"?roll.start()+roll.span():number(audio,"duration")));
    invalidateGuided();guidedShowProposals=true;guidedRunning=true;reportText="guided";setBusy(true,tr(String::fromUTF8("Analyse guidée du signal…")));rebuildSide();
    invoke("guided_analyze",request,[this](var report){guidedRunning=false;installGuided(report);setBusy(false,localisation::count("guided_ready",int(report["notes"].size())));},true,[this](String error){guidedRunning=false;setBusy(false,error);rebuildSide();});
  });
}
void App::acceptGuided(const std::set<String>& ids){if(locked||guidedReport.isVoid()||ids.empty())return;auto request=addField(addField(args(),"token",guidedReport["token"]),"ids",strings(ids));std::set<std::int64_t> previous;for(auto& n:items(result["editing"]["notes"]))previous.insert((juce::int64)n["id"]);
  setBusy(true,tr(String::fromUTF8("Ajout des propositions choisies…")));control("pause",0,[this,request,previous](var){invoke("guided_accept",request,[this,previous](var response){updateResult(response["result"]);installGuided(response["report"]);guidedShowCurve=false;guidedShowProposals=false;roll.maskVisible=false;roll.maskMode=0;roll.tool=0;roll.setMouseCursor(juce::MouseCursor::NormalCursor);updateGuidedView();std::set<std::int64_t> added;for(auto& n:items(result["editing"]["notes"]))if(!previous.count((juce::int64)n["id"]))added.insert((juce::int64)n["id"]);roll.selectIds(added);setBusy(false,localisation::count("notes_added",int(added.size()))+(number(response,"duplicates")?tr(String::fromUTF8(" · doublons déjà présents ignorés")):String()));},false,[this](String error){setBusy(false,error);invalidateGuided();rebuildSide();});});
}
void App::ignoreGuided(const std::set<String>& ids){if(locked||guidedReport.isVoid()||ids.empty())return;auto request=addField(addField(args(),"token",guidedReport["token"]),"ids",strings(ids));invoke("guided_ignore",request,[this](var response){guidedSelected.clear();installGuided(response["report"]);},false,[this](String error){status.setText(error,juce::dontSendNotification);invalidateGuided();rebuildSide();});}
void App::buildGuidedPanel(){
  bool hasAudio=!audio.isVoid()&&!(bool)audio["midi_only"];
  side.label(tr(String::fromUTF8("Instrument isolé ou stem. Suivi d’une ligne dominante : les accords peuvent être incomplets. Les catégories sont des indices, pas des certitudes. Écoutez avant d’ajouter les notes.")),82);
  if(!hasAudio){side.label(tr(String::fromUTF8("Ouvrez ou associez un audio pour utiliser le masque.")),48);return;}
  if(guidedRunning){side.label(tr(String::fromUTF8("Analyse en cours · les notes MIDI restent inchangées.")),42);side.button(tr(String::fromUTF8("Annuler l’analyse guidée")),[this]{invoke("guided_cancel",object({}));});return;}
  side.label(partId<0?tr(String::fromUTF8("Masque commun · ajouts dans une nouvelle voix")):tr(String::fromUTF8("Masque et ajouts : partie sélectionnée")),35);
  side.button(roll.maskMode==1?tr(String::fromUTF8("Terminer le pinceau")):tr(String::fromUTF8("Peindre la zone à examiner")),[this]{setMaskTool(1);},!locked);
  side.button(roll.maskMode==2?tr(String::fromUTF8("Terminer la gomme")):tr(String::fromUTF8("Gommer le masque")),[this]{setMaskTool(2);},!locked&&!roll.spectralMask.empty());
  side.slider(tr(String::fromUTF8("Rayon du pinceau (demi-tons)")),roll.brushSemitones,.25,6,.25,[this](double v){roll.brushSemitones=v;edited();});
  side.toggle(tr(String::fromUTF8("Afficher le masque de sélection")),roll.maskVisible,[this](bool on){roll.maskVisible=on;roll.repaint();edited();});
  side.button(tr(String::fromUTF8("Effacer le masque · annulable")),[this]{command(object({{"kind","clear_mask"},{"part",partId<0?int(phthongos::allMaskParts):partId}}));},!locked&&!roll.spectralMask.empty());
  side.label(tr(String::fromUTF8("Un trait = une annulation (Ctrl+Z). Choisissez la flèche pour revenir à l’édition des notes.")),52);
  side.toggle(tr(String::fromUTF8("Analyser uniquement la sélection peinte")),guidedUseMask,[this](bool on){guidedUseMask=on;invalidateGuided();rebuildSide();edited();});
  side.combo("Zone",{{tr(String::fromUTF8("Étendue du masque")),"mask"},{tr(String::fromUTF8("Vue visible")),"visible"},{tr(String::fromUTF8("Tout l’audio")),"all"}},guidedZone,[this](var v){guidedZone=v.toString();invalidateGuided();edited();});
  side.label(roll.bandEnabled?tr(String::fromUTF8("Registre : "))+Roll::noteLabel(roll.bandLow)+String::fromUTF8(" → ")+Roll::noteLabel(roll.bandHigh):tr(String::fromUTF8("Registre : toutes les hauteurs (réglable par les bornes Grave/Aiguë)")),52);
  side.toggle(tr(String::fromUTF8("Favoriser la gamme choisie · facultatif")),guidedUseScale,[this](bool on){guidedUseScale=on;invalidateGuided();rebuildSide();edited();});
  if(guidedUseScale){toneOptions();side.label(tr(String::fromUTF8("Gamme : "))+toneRoot.getText()+" · "+toneMode.getText(),36);}
  side.button(tr(String::fromUTF8("Choisir la gamme…")),[this]{tonalityOpen=true;toneOptions();resized();edited();},!locked);
  side.button(tr(String::fromUTF8("Analyser et proposer des notes")),[this]{analyzeGuided();},!locked);
  if(guidedRunning)side.button(tr(String::fromUTF8("Annuler l’analyse guidée")),[this]{invoke("guided_cancel",object({}));},true);
  side.toggle(tr(String::fromUTF8("Afficher la courbe de hauteur estimée")),guidedShowCurve,[this](bool on){guidedShowCurve=on;updateGuidedView();edited();});
  if(guidedReport.isVoid())return;
  side.toggle(tr(String::fromUTF8("Afficher les propositions dans le roll")),guidedShowProposals,[this](bool on){guidedShowProposals=on;updateGuidedView();edited();});
  side.label(tr(String::fromUTF8("Les moins plausibles sont masquées par défaut et restent consultables. Aucun pourcentage de certitude.")),54);
  const std::array<std::pair<String,String>,3> categories={{{"probable",tr(String::fromUTF8("Probables"))},{"review",tr(String::fromUTF8("À vérifier"))},{"unlikely",tr(String::fromUTF8("Peu plausibles"))}}};
  for(const auto& category:categories){int count=0;std::set<String> ids;for(auto& n:items(guidedReport["notes"]))if(n["category"].toString()==category.first){++count;ids.insert(n["id"].toString());}
    side.toggle(category.second+" ("+String(count)+")",guidedCategories.count(category.first)>0,[this,key=category.first](bool on){if(on)guidedCategories.insert(key);else guidedCategories.erase(key);guidedSelected.clear();guidedPage=0;updateGuidedView();rebuildSide();edited();});
    side.button(localisation::format("guided_add_category",{category.second,String(count)}),[this,ids]{acceptGuided(ids);},!locked&&!ids.empty());
  }
  auto proposals=visibleGuidedNotes();side.label(localisation::count("proposal_count",int(proposals.size()))+tr(String::fromUTF8(" · clic pour localiser, Ctrl+clic pour grouper. Actions d’écoute et d’ajout aussi au clic droit sur une note fantôme.")),72);
  side.button(localisation::format("add_selection",{String(int(guidedSelected.size()))}),[this]{acceptGuided(guidedSelected);},!locked&&!guidedSelected.empty());
  side.button(localisation::format("guided_add_visible",{String(int(proposals.size()))}),[this]{std::set<String> ids;for(auto& n:visibleGuidedNotes())ids.insert(n["id"].toString());acceptGuided(ids);},!locked&&!proposals.empty());
  for(int i=guidedPage*40;i<std::min((int)proposals.size(),(guidedPage+1)*40);++i){auto item=proposals[std::size_t(i)];auto id=item["id"].toString();side.diagnosticRow(Roll::noteLabel((int)item["pitch"])+" · "+timeName(number(item,"start")),tr(String::fromUTF8("Durée "))+String(number(item,"end")-number(item,"start"),3)+tr(String::fromUTF8(" s · à vérifier")),guidedSelected.count(id)>0,[this,id]{focusGuided(id);});}
  if(guidedPage>0)side.button(tr(String::fromUTF8("Propositions précédentes")),[this]{--guidedPage;rebuildSide();});if((guidedPage+1)*40<int(proposals.size()))side.button(tr(String::fromUTF8("Propositions suivantes")),[this]{++guidedPage;rebuildSide();});
}
