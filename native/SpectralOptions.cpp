#include "App.h"
#include "SpectralPalettes.h"
#include "ActionMenu.h"
void App::spectralBandMenu(juce::Point<int> position,int pitch) {
  if(locked)return;
  ActionMenu menu(this);
  menu.addSectionHeader(Roll::noteLabel(pitch));
  menu.addItem(2,tr(String::fromUTF8("Définir comme note la plus aiguë")));
  menu.addItem(1,tr(String::fromUTF8("Définir comme note la plus grave")));
  menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({position.x,position.y,1,1}),[s=safe(),pitch](int action){
    if(s && !s->locked && (action==1||action==2))s->roll.setBandBoundary(action==2,pitch);
  });
}
void App::recolourSpectrum() {
  int width=(int)spectrumData["columns"],height=(int)spectrumData["rows"];
  auto values=items(spectrumData["values"]);
  if(width<=0||height<=0||values.size()!=(size_t)width*height)return;
  juce::Image image(juce::Image::ARGB,width,height,true);
  juce::Image::BitmapData pixels(image,juce::Image::BitmapData::writeOnly);
  // Stored energy is logarithmic over -90..0 dB. Palette/contrast never rerun FFT.
  double floor=-90.+spectralContrast*.4;
  double gainDb=roll.displayMode==2?20.*std::log10(std::max(.01,roll.visualGain)):0.;
  for(int x=0;x<width;++x)for(int y=0;y<height;++y) {
    double db=(int)values[x*height+y]/255.*90.-90.;
    float energy=(float)View::clamp((db+gainDb-floor)/(-floor),0,1);
    if(spectralStyle==1)energy=std::pow(energy,.8f);
    auto colour=spectral_palettes::colour(spectralPalette,energy);
    if(spectralStyle==0)colour=colour.interpolatedWith(juce::Colour(0xfff1ebdf),.22f);
    pixels.setPixelColour(x,y,colour.withAlpha(energy));
  }
  roll.spectrum=image;roll.repaint();
}
void App::saveSpectralBand() {
  spectralBands.getDynamicObject()->setProperty(juce::Identifier(String(partId)),
    object({{"enabled",roll.bandEnabled},{"low",roll.bandLow},{"high",roll.bandHigh}}));
}
void App::loadSpectralBand() {
  bool previousEnabled=roll.bandEnabled;int previousLow=roll.bandLow,previousHigh=roll.bandHigh;
  auto value=spectralBands[juce::Identifier(String(partId))];
  roll.bandEnabled=(bool)value["enabled"];
  roll.bandLow=juce::jlimit(0,127,(int)number(value,"low",36));
  roll.bandHigh=juce::jlimit(roll.bandLow,127,(int)number(value,"high",96));
  if(previousEnabled!=roll.bandEnabled || previousLow!=roll.bandLow || previousHigh!=roll.bandHigh) {
    verificationStale=true;updateDiagnostics();
  }
  roll.repaint();
}
void App::spectralOptions() {
  auto f=juce::Component::SafePointer<Form>(new Form);
  f->actions(tr(String::fromUTF8("Fermer")),{});
  f->onApply=[f](var){if(f)f->close();};
  f->compactChoice("Palette","palette",{{"Inferno",0},{"Magma",1},{"Viridis",2}},spectralPalette,[s=safe()](var v){
    if(!s)return;s->spectralPalette=(int)v;s->recolourSpectrum();s->edited();
  });
  f->compactChoice(tr(String::fromUTF8("Rendu")),"style",{{tr(String::fromUTF8("Discret")),0},{tr(String::fromUTF8("Contrasté")),1}},spectralStyle,[s=safe()](var v){
    if(!s)return;s->spectralStyle=(int)v;s->recolourSpectrum();s->edited();
  });
  auto* slider=new VisibleCursorSlider;slider->setRange(0,100,1);slider->setValue(spectralContrast);
  slider->setTextBoxStyle(juce::Slider::TextBoxRight,false,55,27);slider->setName(tr(String::fromUTF8("Contraste spectral")));
  f->label(tr(String::fromUTF8("Contraste spectral : remonter le seuil pour atténuer le fond faible.")));f->custom(slider,32);
  slider->onValueChange=[s=safe(),slider]{if(!s)return;s->spectralContrast=slider->getValue();s->recolourSpectrum();s->edited();};
  f->toggle(tr(String::fromUTF8("Limiter les diagnostics au registre ci-dessous")),"band",roll.bandEnabled);
  std::vector<std::pair<String,var>> noteChoices;
  for(int pitch=0;pitch<=127;++pitch)noteChoices.emplace_back(Roll::noteLabel(pitch),pitch);
  auto chooseBoundary=[f,s=safe()](bool high,var value){
    if(!f||!s)return;s->roll.setBandBoundary(high,(int)value);
    f->setValue("low",s->roll.bandLow);f->setValue("high",s->roll.bandHigh);f->setValue("band",true);
  };
  f->compactChoice(tr(String::fromUTF8("Note la plus grave")),"low",noteChoices,roll.bandLow,[chooseBoundary](var value){chooseBoundary(false,value);});
  f->compactChoice(tr(String::fromUTF8("Note la plus aiguë")),"high",noteChoices,roll.bandHigh,[chooseBoundary](var value){chooseBoundary(true,value);});
  for(auto key:{"low","high"})dynamic_cast<InstrumentPicker*>(f->compactFields[key])->displayName=[](var pitch){return Roll::noteLabel((int)pitch);};
  auto applyBand=[f,s=safe()]{
    if(!s||!f)return String();auto v=f->state();double lo=decimal(v["low"]),hi=decimal(v["high"]);
    if(!std::isfinite(lo)||!std::isfinite(hi)||lo<0||hi>127||lo>hi)return tr(String::fromUTF8("La note grave doit se situer sous la note aiguë."));
    bool enabled=(bool)v["band"];
    if(s->roll.bandLow!=(int)lo||s->roll.bandHigh!=(int)hi||s->roll.bandEnabled!=enabled){
      s->roll.bandLow=(int)lo;s->roll.bandHigh=(int)hi;s->roll.bandEnabled=enabled;
      s->saveSpectralBand();s->verificationStale=true;s->updateDiagnostics();s->rebuildSide();s->edited();s->roll.repaint();
    }
    return s->roll.bandLabel((int)lo)+String::fromUTF8(" → ")+s->roll.bandLabel((int)hi);
  };
  f->live(applyBand);
  auto previousToggle=f->toggleFields["band"]->onClick;
  f->toggleFields["band"]->onClick=[previousToggle,applyBand]{previousToggle();applyBand();};
  f->onApply=[f,applyBand](var){applyBand();if(f)f->close();};
  f->button(tr(String::fromUTF8("Adapter la plage à la partie")),[s=safe(),f,applyBand]{
    if(!s||!f)return;int low=127,high=0;bool found=false;
    for(auto& n:items(s->result["editing"]["notes"])) {
      auto note=Note::parse(n);if((s->partId<0||note.part==s->partId)&&note.program!=128){low=std::min(low,note.pitch);high=std::max(high,note.pitch);found=true;}
    }
    if(found){f->setValue("low",std::max(0,low-2));f->setValue("high",std::min(127,high+2));f->setValue("band",true);applyBand();}
  });
  f->label(tr(String::fromUTF8("Clic droit sur l’échelle des notes à gauche pour poser Grave ou Aiguë. Glissez les marqueurs ou les poignées ↕ des barres pour les ajuster. Réglages mémorisés par partie.")));
  f->show(tr(String::fromUTF8("Spectre / registre")));
}
