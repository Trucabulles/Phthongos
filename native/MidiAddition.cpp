#include "App.h"
namespace {
var put(var args,const String& key,var value) {args=args.clone();args.getDynamicObject()->setProperty(key,value);return args;}
class ImportPreview : public juce::Component,private juce::Timer {
  std::vector<Note> current,incoming;
  juce::Component::SafePointer<Form> form;
  double baseDuration,first=1200,last=0;
public:
  ImportPreview(const std::vector<Note>& existing,const var& data,Form* owner,double duration)
    :current(existing),form(owner),baseDuration(duration) {
    for(auto& value:items(data["notes"])) {
      auto n=Note::parse(value); first=std::min(first,n.start);last=std::max(last,n.end);incoming.push_back(n);
    }
    setName(tr(String::fromUTF8("Aperçu de l’ajout MIDI : existant bleu, ajout bronze")));startTimer(150);
  }
  void timerCallback() override { repaint(); }
  void paint(juce::Graphics& g) override {
    double offset=form?decimal(form->state()["offset"]):0;
    bool valid=std::isfinite(offset) && first+offset>=0 && last+offset<=1200;
    if(form) form->setApplyEnabled(valid && !(bool)form->state()["applying"]);
    g.fillAll(juce::Colour(0xfff1ebdf));g.setColour(juce::Colour(0xff254962));g.setFont(juce::FontOptions(16));
    g.drawText(tr(String::fromUTF8("Existant : bleu · Ajout : bronze")),10,5,getWidth()-20,25,juce::Justification::left);
    if(!valid){g.setColour(juce::Colour(0xffa44737));g.drawFittedText(tr(String::fromUTF8("Décalage invalide : toutes les notes doivent rester entre 0 et 20 minutes.")),getLocalBounds().reduced(12).withTrimmedTop(34),juce::Justification::centred,3);return;}
    double end=std::max({1.,baseDuration,last+offset});int low=127,high=0;
    for(auto& n:current){low=std::min(low,n.pitch);high=std::max(high,n.pitch);}
    for(auto& n:incoming){low=std::min(low,n.pitch);high=std::max(high,n.pitch);}
    low=std::max(0,low-2);high=std::min(127,high+2);
    auto area=getLocalBounds().toFloat().withTrimmedTop(36).withTrimmedBottom(25).reduced(10,0);
    auto draw=[&](const std::vector<Note>& notes,double shift,juce::Colour colour) {
      g.setColour(colour);size_t stride=std::max(size_t(1),notes.size()/12000);
      for(size_t i=0;i<notes.size();i+=stride) {
        auto& n=notes[i];float x=area.getX()+(float)((n.start+shift)/end)*area.getWidth();
        float y=area.getY()+(float)(high-n.pitch)/(high-low+1)*area.getHeight();
        g.fillRect(x,y,std::max(1.f,(float)((n.end-n.start)/end)*area.getWidth()),std::max(2.f,area.getHeight()/(high-low+1)-1));
      }
    };
    draw(current,0,juce::Colour(0x6034697b));draw(incoming,offset,juce::Colour(0xffad793c));
    g.setColour(juce::Colour(0xff254962));
    g.drawText("0:00",10,getHeight()-24,60,24,juce::Justification::left);
    g.drawText(tr(String::fromUTF8("Ajout : "))+timeName(first+offset)+String::fromUTF8(" → ")+timeName(last+offset),75,getHeight()-24,getWidth()-150,24,juce::Justification::centred);
    g.drawText(timeName(end),getWidth()-80,getHeight()-24,70,24,juce::Justification::right);
  }
};
}
void App::appendMidi(const String& requested) {
  if(locked || result.isVoid())return;
  auto open=[s=safe()](String path){
    if(!s || s->locked || s->result.isVoid())return;
    auto base=s->args();s->setBusy(true,tr(String::fromUTF8("Lecture du MIDI à ajouter…")));
    s->control("pause");
    s->invoke("preview_midi_addition",put(base,"path",path),[s,base](var preview){
      if(!s)return;s->setBusy(false);s->showMidiAddition(preview,base);
    },true,[s](String){if(s)s->setBusy(false);});
  };
  if(requested.isNotEmpty())open(requested);
  else choose(tr(String::fromUTF8("Ajouter un MIDI au projet")),false,"*.mid;*.midi",open);
}
void App::showMidiAddition(var preview,var baseArgs) {
  midiAdditionForm(preview,baseArgs)->show(tr(String::fromUTF8("Ajouter un MIDI · voix")));
}
Form* App::midiAdditionForm(var preview,var baseArgs) {
  juce::Component::SafePointer<Form> f=new Form;
  f->setHeading(tr(String::fromUTF8("Ajouter un MIDI · voix")));
  f->actions(tr(String::fromUTF8("Ajouter")),tr(String::fromUTF8("Annuler")));
  f->label(preview["name"].toString()+String::fromUTF8(" · ")+localisation::count("notes",int(items(preview["notes"]).size()))+". "+tr(String::fromUTF8("Les notes actuelles et l’audio restent en place.")));
  f->text(tr(String::fromUTF8("Décalage en secondes (positif ou négatif)")),"offset",0);
  f->live([f]{return f && f->state()["importError"].toString().isNotEmpty()?f->state()["importError"].toString():tr(String::fromUTF8("Affectez chaque partie à une voix existante ou nouvelle."));});
  std::vector<Note> existing;for(auto& n:items(result["editing"]["notes"]))existing.push_back(Note::parse(n));
  f->custom(new ImportPreview(existing,preview,f,number(result,"duration")),200);
  auto sourceParts=items(preview["parts"]);int index=0;
  auto programNames=juce::JSON::parse(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getChildFile("player-assets/gmPrograms.json"));
  for(auto& part:sourceParts) {
    String suffix(index++),target="target"+suffix,name="name"+suffix,program="program"+suffix;
    std::vector<std::pair<String,var>> choices{{tr(String::fromUTF8("Créer une nouvelle voix")),"new"}};
    for(auto& p:parts)choices.emplace_back(p.second,p.first);
    f->compactChoice(displayPartLabel(part["part"])+" ("+localisation::count("notes",(int)part["count"])+")",target,choices,"new",[f,name,program](var destination){
      if(!f)return;bool create=destination.toString()=="new";
      f->textFields[name]->setEnabled(create);f->compactFields[program]->setEnabled(create);
    });
    f->text(tr(String::fromUTF8("Nom de la nouvelle voix")),name,part["part"]["label"]);
    std::vector<std::pair<String,var>> instruments;
    int original=(int)part["part"]["program"];
    for(int p=0;p<=128;++p) {
      if(p!=original && ((p>=96&&p<=103)||(p>=120&&p<=127)))continue;
      auto names=items(programNames);String label=p<(int)names.size()?names[p].toString():tr(String::fromUTF8("Percussions"));
      instruments.emplace_back(tr(label),p);
    }
    f->compactChoice(tr(String::fromUTF8("Instrument de la nouvelle voix")),program,instruments,original);
  }
  f->onApply=[s=safe(),f,preview,baseArgs,sourceParts](var values){
    if(!s || !f || s->locked)return;
    std::vector<var> mappings;int index=0;
    for(auto& part:sourceParts) {
      String suffix(index++);auto target=values[juce::Identifier("target"+suffix)];
      mappings.push_back(object({{"source",part["source"]},{"target",target.toString()=="new"?var():target},
        {"name",values[juce::Identifier("name"+suffix)]},{"program",values[juce::Identifier("program"+suffix)]}}));
    }
    double offset=decimal(values["offset"]);
    if(!std::isfinite(offset))return;
    auto params=put(put(put(baseArgs,"token",preview["token"]),"offset",offset),"mappings",array(mappings));
    s->setBusy(true,tr(String::fromUTF8("Ajout des voix MIDI…")));f->setValue("applying",true);f->setValue("importError","");f->setApplyEnabled(false);
    s->invoke("apply_midi_addition",params,[s,f](var result){
      if(!s)return;if(f)f->close();s->roll.clearSelection();s->verificationStale=true;
      s->partId=-1;
      s->updateResult(result);s->setBusy(false,tr(String::fromUTF8("MIDI ajouté · Ctrl+Z pour annuler l’ensemble de l’import.")));
    },true,[s,f](String error){
      if(s)s->setBusy(false);if(f){f->setValue("applying",false);f->setValue("importError",error);f->setApplyEnabled(true);}
    });
  };
  return f.getComponent();
}

namespace {
class VoiceSwatches : public juce::Component {
  class Chip : public juce::Button {
    unsigned index;
  public:
    Chip(unsigned i):juce::Button(tr(String::fromUTF8(voicePalette::names[i]))),index(i){setTooltip(getName());setWantsKeyboardFocus(true);setClickingTogglesState(false);}
    void paintButton(juce::Graphics& g,bool over,bool down) override {
      auto r=getLocalBounds().toFloat().reduced(2);auto colour=juce::Colour(voicePalette::colours[index]);
      g.setColour(over||down?colour.brighter(.15f):colour);g.fillRoundedRectangle(r,5);

      if(getToggleState()||hasKeyboardFocus(false)){g.setColour(juce::Colour(0xff254962));g.drawRoundedRectangle(r.expanded(1),6,2);g.setColour(juce::Colours::white);g.drawRoundedRectangle(r.reduced(2),3,2);}
    }
  };
  std::vector<std::unique_ptr<Chip>> chips;
public:
  VoiceSwatches(unsigned selected,std::function<void(unsigned)> changed){
    setName(tr(String::fromUTF8("Couleur de la voix")));
    for(unsigned i=0;i<12;++i){auto chip=std::make_unique<Chip>(i);chip->setToggleState(i==selected,juce::dontSendNotification);chip->onClick=[this,i,changed]{for(unsigned j=0;j<chips.size();++j)chips[j]->setToggleState(i==j,juce::dontSendNotification);changed(i);};addAndMakeVisible(*chip);chips.push_back(std::move(chip));}
  }
  void resized() override {for(unsigned i=0;i<chips.size();++i)chips[i]->setBounds(int(i%6)*getWidth()/6,int(i/6)*44,getWidth()/6-2,40);}
};
}
void App::showVoiceForm(bool create){
  if(locked||audio.isVoid())return;
  if(create&&result.isVoid()){ensureGuidedEditor([this]{showVoiceForm(true);});return;}
  if(!create&&(partId<0||idSet(result["editing"]["locked_parts"]).count(partId)))return;
  voiceForm(create)->show((create?tr(String::fromUTF8("Ajouter une voix vide")):tr(String::fromUTF8("Couleur de la voix"))));
}
Form* App::voiceForm(bool create){
  auto f=juce::Component::SafePointer<Form>(new Form);auto key=partId;
  f->actions((create?tr(String::fromUTF8("Ajouter")):tr(String::fromUTF8("Enregistrer"))),tr(String::fromUTF8("Annuler")));
  int program=0;for(const auto& p:items(result["midi_parts"]))if((p["identity"].isVoid()?(int)p["program"]:(int)p["identity"])==key)program=(int)p["program"];
  unsigned colour=roll.voiceColourIndex(key,program);
  if(create){
    std::set<unsigned> used;for(const auto& p:items(result["midi_parts"]))used.insert(roll.voiceColourIndex(p["identity"].isVoid()?(int)p["program"]:(int)p["identity"],(int)p["program"]));
    colour=0;while(colour<11&&used.count(colour))++colour;
    f->text(tr(String::fromUTF8("Nom de la nouvelle voix")),"name",tr(String::fromUTF8("Nouvelle voix")));
    auto names=items(juce::JSON::parse(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("player-assets/gmPrograms.json")));
    std::vector<std::pair<String,var>> instruments;for(int p=0;p<=128;++p){if((p>=96&&p<=103)||(p>=120&&p<=127))continue;instruments.emplace_back((p<(int)names.size()?tr(names[p].toString()):tr(String::fromUTF8("Percussions"))),p);}
    f->compactChoice(tr(String::fromUTF8("Instrument de la nouvelle voix")),"program",instruments,0);
    f->label(tr(String::fromUTF8("Voix vide : dessinez, collez des notes ou importez un MIDI dans cette voix. Les autres voix restent intactes.")));
  }else f->label(part.getText());
  f->label(tr(String::fromUTF8("Couleur de la voix")));f->setValue("colour",int(colour));
  f->custom(new VoiceSwatches(colour,[f](unsigned choice){if(f)f->setValue("colour",int(choice));}),88);
  f->label(tr(String::fromUTF8("Ces 12 couleurs sont réservées aux voix. Les diagnostics, la lecture et le verrouillage gardent leurs propres indications.")));
  f->onApply=[s=safe(),f,create,key](var values){
    if(!s||!f||s->locked)return;
    if(create&&values["name"].toString().trim().isEmpty()){f->setHeading(tr(String::fromUTF8("Nom de partie invalide.")));return;}
    std::set<int> before;for(const auto& p:s->parts)before.insert(p.first);
    auto action=create?object({{"kind","create_empty_part"},{"name",values["name"]},{"program",values["program"]},{"colour",values["colour"]}}):object({{"kind","set_part_colour"},{"part",key},{"colour",values["colour"]}});
    s->command(action,[s,f,create,before,key](var){if(!s)return;if(f)f->close();int target=key;if(create)for(const auto& p:s->parts)if(!before.count(p.first)){target=p.first;break;}s->selectPart(target);});
  };
  return f.getComponent();
}
