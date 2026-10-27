#pragma once
#include "ReferenceIcons.h"
#include "Localisation.h"
#include "../core/services/Diagnostics.h"
#include "../core/services/Desktop.h"
#include "../core/services/DocumentHost.h"
#include "../core/services/Spectrogram.h"
#include "../core/services/Acoustic.h"
#include "../core/services/Verification.h"
#include <atomic>
#include <mutex>
#include "../core/audio/Resample.h"
#include "../core/project/DocumentJson.h"
#include "../core/midi/Midi.h"
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
using juce::String;
using juce::var;
inline var object(std::initializer_list<std::pair<String, var>> fields) {
  auto *d = new juce::DynamicObject;
  for (auto &f : fields)
    d->setProperty(f.first, f.second);
  return var(d);
}
inline var array(const std::vector<var> &values) {
  juce::Array<var> a;
  for (auto &v : values)
    a.add(v);
  return var(a);
}
inline var idsVar(const std::set<std::int64_t> &ids) {
  std::vector<var> a;
  for (auto id : ids)
    a.emplace_back((juce::int64)id);
  return array(a);
}
inline std::set<std::int64_t> idSet(const var &v) {
  std::set<std::int64_t> r;
  if (auto *a = v.getArray())
    for (auto &i : *a)
      r.insert((juce::int64)i);
  return r;
}
inline double number(const var &v, const char *k, double fallback = 0) {
  return v.hasProperty(k) ? (double)v[k] : fallback;
}
inline std::vector<var> items(const var &v) {
  std::vector<var> r;
  if (auto *a = v.getArray())
    for (auto &x : *a)
      r.push_back(x);
  return r;
}
inline String pitchName(int p) {
  const char *ns[] = {"C",  "C♯", "D",  "D♯", "E",  "F",
                      "F♯", "G",  "G♯", "A",  "A♯", "B"};
  return String::fromUTF8(ns[juce::jlimit(0, 127, p) % 12]) +
         String(p / 12 - 1);
}
inline String timeName(double t) {
  return String((int)(t / 60)) + ":" +
         String(std::fmod(t, 60), 2).paddedLeft('0', 5);
}
class Bridge {
  phthongos::services::Desktop core;
  juce::ThreadPool jobs{1}, longJobs{1};
public:
  std::function<void(const String&)> requestObserver; // Optional request tracing for regression checks.
  Bridge(const juce::File& runtime,const juce::File& cache):core(runtime,cache){}
  ~Bridge(){phthongos::diagnostics::Span trace("bridge/shutdown");jobs.removeAllJobs(true,-1);longJobs.removeAllJobs(true,-1);}
  var call(const String& cmd,const var& args){
    try{return object({{"ok",true},{"value",localisation::language()==localisation::Language::french?core.request(cmd,args):localisation::report(core.request(cmd,args))}});}
    catch(const std::exception& e){phthongos::diagnostics::event("request_failed",cmd+": "+String::fromUTF8(e.what()));return object({{"ok",false},{"error",localisation::error(String::fromUTF8(e.what()))}});}
    catch(...){return object({{"ok",false},{"error",tr(String::fromUTF8("Erreur native inconnue."))}});}
  }
  void request(const String &cmd, const var &args,
               std::function<void(var)> callback, bool heavy = false) {
    if(requestObserver)requestObserver(cmd);
    bool verbose=cmd!="player_snapshot"&&cmd!="host_events"&&cmd!="soundbank_status"&&cmd!="muscriptor_status";
    auto trace=std::make_shared<phthongos::diagnostics::Span>("request/"+cmd,verbose);trace->phase("queued");
    auto work = [this, cmd, args, trace, callback = std::move(callback)] {
      trace->phase("running");
      auto response = call(cmd, args);
      trace->phase("await_gui_callback");
      juce::MessageManager::callAsync(
          [callback, response, trace] { trace->phase("gui_callback");callback(response); });
    };
    if (heavy)
      longJobs.addJob(std::move(work));
    else
      jobs.addJob(std::move(work));
  }
};
inline String displayPartLabel(const var& part) {
  auto label=part["label"].toString();
  // Imported track names may already equal the instrument name (including
  // repeated exports). Clean the presentation without rewriting project data.
  auto separator=String::fromUTF8(" · ");
  if(label.contains(separator)) {
    auto first=label.upToFirstOccurrenceOf(separator,false,false).trim(),tail=label;
    bool identical=first.isNotEmpty();
    while(identical&&tail.contains(separator)) {
      identical=tail.upToFirstOccurrenceOf(separator,false,false).trim()==first;
      tail=tail.fromFirstOccurrenceOf(separator,false,false);
    }
    if(identical&&tail.trim()==first)label=first;
  }
  if(part["identity"].isVoid()||part["identity"].isUndefined()) {
    static auto names=juce::JSON::parse(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("player-assets/gmPrograms.json"));
    int program=(int)part["program"];
    if(program>=0&&program<names.size()&&label==names[program].toString())return tr(label);
  }
  return label;
}
struct Note {
  std::int64_t id = 0;
  int pitch = 60, velocity = 80, program = 0, part = 0, index = -1;
  double start = 0, end = 1;
  bool muted = false, protectedNote = false, editable = true;
  String label;
  var source;
  static Note parse(const var &v) {
    Note n;
    n.source = v;
    n.id = (juce::int64)v["id"];
    n.pitch = (int)v["pitch_midi"];
    n.start = (double)v["start_time_s"];
    n.end = (double)v["end_time_s"];
    n.velocity = (int)v["velocity_midi"];
    n.muted = (bool)v["muted"];
    n.protectedNote = (bool)v["protected"];
    auto p = v["part"];
    n.program = (int)p["program"];
    n.part = p["identity"].isVoid() || p["identity"].isUndefined()
                 ? n.program
                 : (int)p["identity"];
    n.label = displayPartLabel(p);
    return n;
  }
};
struct View {
  double zoom = 1, pan = 0, pitchZoom = 1, pitchPan = 0, audioHeight = 116,
         keyHeight = 56;
  static double clamp(double v, double a, double b) {
    return std::max(a, std::min(b, v));
  }
  void timeZoom(double duration, double requested, double anchor) {
    auto next = clamp(requested, 1, 512);
    auto span = duration / zoom;
    auto time = pan * (duration - span) + anchor * span;
    auto ns = duration / next;
    zoom = next;
    pan = next == 1 ? 0 : clamp((time - anchor * ns) / (duration - ns), 0, 1);
  }
  void proportional(double duration, double factor, double ta, double pa) {
    auto applied = clamp(factor, std::max(1 / zoom, 1 / pitchZoom),
                         std::min(512 / zoom, 64 / pitchZoom));
    auto anchor = clamp(pa, 0, 1);
    auto pitch =
        127 - pitchPan * (128 - 128 / pitchZoom) - anchor * 128 / pitchZoom;
    timeZoom(duration, zoom * applied, clamp(ta, 0, 1));
    pitchZoom *= applied;
    auto top = pitch + anchor * 128 / pitchZoom;
    pitchPan =
        pitchZoom == 1 ? 0 : clamp((127 - top) / (128 - 128 / pitchZoom), 0, 1);
  }
  var json() const {
    return object({{"zoom", zoom},
                   {"pan", pan},
                   {"pitchZoom", pitchZoom},
                   {"pitchPan", pitchPan},
                   {"audioHeight", audioHeight},
                   {"keyHeight", 56}});
  }
  void load(const var &v) {
    zoom = number(v, "zoom", 1);
    pan = number(v, "pan");
    pitchZoom = number(v, "pitchZoom", 1);
    pitchPan = number(v, "pitchPan");
    audioHeight = number(v, "audioHeight", 116);
    // Older projects may contain a stretched keyboard; its height is now fixed.
    keyHeight = 56;
  }
};
struct Theme : juce::LookAndFeel_V4 {
  const juce::Colour paper{0xfff9faf4}, ink{0xff254962}, bronze{0xffad793c},
      border{0xffcbbda5};
  Theme() {
    setColour(juce::ResizableWindow::backgroundColourId, paper);
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xfff1ebdf));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffe4d5bb));
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::TextButton::textColourOnId, ink);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xfffffdf6));
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::outlineColourId, border);
    setColour(juce::ComboBox::arrowColourId, ink);
    setColour(juce::PopupMenu::backgroundColourId, paper);
    setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::PopupMenu::headerTextColourId, ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId,
              juce::Colour(0xffe9e0d1));
    setColour(juce::PopupMenu::highlightedTextColourId, ink);
    setColour(juce::Label::textColourId, ink);
    setColour(juce::Label::textWhenEditingColourId, ink);
    setColour(juce::TextEditor::highlightedTextColourId, ink);
    setColour(juce::ToggleButton::textColourId, ink);
    setColour(juce::ToggleButton::tickColourId, bronze);
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xfffffdf6));
    setColour(juce::TextEditor::textColourId, ink);
    setColour(juce::TextEditor::outlineColourId, border);
    setColour(juce::Slider::thumbColourId, bronze);
    setColour(juce::Slider::trackColourId, bronze);
    setColour(juce::Slider::backgroundColourId, juce::Colour(0xff97a5ad));
    setColour(juce::Slider::textBoxTextColourId, ink);
    setColour(juce::Slider::textBoxBackgroundColourId,
              juce::Colour(0xfffffdf6));
    setColour(juce::Slider::textBoxOutlineColourId, border);
    setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xffb8ad99));
    setColour(juce::AlertWindow::backgroundColourId, paper);
    setColour(juce::AlertWindow::textColourId, ink);
  }
  juce::Label* createSliderTextBox(juce::Slider& slider) override {
    auto* label=juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setColour(juce::Label::textWhenEditingColourId,ink);
    label->setColour(juce::TextEditor::highlightedTextColourId,ink);
    label->setColour(juce::CaretComponent::caretColourId,ink);
    return label;
  }
  juce::Font getLabelFont(juce::Label &l) override {
    auto f = l.getFont();
    f.setHeight(std::max(15.f, f.getHeight()));
    return f;
  }
  juce::Font getTextButtonFont(juce::TextButton &b, int) override {
    return juce::Font(
        juce::FontOptions(b.getComponentID() == "section" ? 18.f : 16.f,
                          b.getComponentID() == "section" ? juce::Font::bold
                                                          : juce::Font::plain));
  }
  juce::Font getComboBoxFont(juce::ComboBox &) override {
    return juce::Font(juce::FontOptions(16));
  }
  void drawComboBox(juce::Graphics &g, int w, int h, bool, int, int, int, int,
                    juce::ComboBox &b) override {
    auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(.5f);
    g.setColour(b.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(r, 4);
    g.setColour(border);
    g.drawRoundedRectangle(r, 4, 1);
    float cx = w - 14.f, cy = h / 2.f;
    juce::Path p;
    p.startNewSubPath(cx - 4, cy - 2.5f);
    p.lineTo(cx, cy + 2.5f);
    p.lineTo(cx + 4, cy - 2.5f);
    g.setColour(ink.withMultipliedAlpha(b.isEnabled() ? 1.f : .4f));
    g.strokePath(p, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded));
  }
  void getIdealPopupMenuItemSizeWithOptions(const String& text,bool separator,int standardHeight,int& width,int& height,const juce::PopupMenu::Options& options)override{
    juce::LookAndFeel_V4::getIdealPopupMenuItemSizeWithOptions(text,separator,standardHeight,width,height,options);
    if(auto* target=options.getTargetComponent();target&&target->getComponentID()=="voiceSelector"&&!separator)width+=22;
  }
  void drawPopupMenuItem(juce::Graphics &g, const juce::Rectangle<int> &area,
                         bool separator, bool active, bool highlighted,
                         bool ticked, bool sub, const String &text,
                         const String &shortcut, const juce::Drawable *icon,
                         const juce::Colour *colour) override {
    if (separator) {
      g.setColour(border);
      g.drawHorizontalLine(area.getCentreY(), (float)area.getX() + 8,
                           (float)area.getRight() - 8);
      return;
    }
    if (highlighted) {
      g.setColour(juce::Colour(0xffe9e0d1));
      g.fillRect(area);
    }
    g.setColour(
        (colour ? *colour : ink).withMultipliedAlpha(active ? 1.f : .4f));
    g.setFont(juce::FontOptions(16));
    auto row = area.reduced(9, 0);
    auto check = row.removeFromLeft(18);
    if (ticked) {
      float x = check.getCentreX() - 4.f, y = (float)check.getCentreY();
      juce::Path p;
      p.startNewSubPath(x, y);
      p.lineTo(x + 3, y + 3);
      p.lineTo(x + 8, y - 4);
      g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
    }
    if(icon){
      auto badge=row.removeFromLeft(22).toFloat().withSizeKeepingCentre(12.f,12.f);juce::Graphics::ScopedSaveState state(g);
      icon->drawWithin(g,badge,juce::RectanglePlacement::centred,active?1.f:.4f);
    }
    if (sub) {
      auto a = row.removeFromRight(12);
      g.drawText(String::fromUTF8("›"), a, juce::Justification::centred);
    }
    if (shortcut.isNotEmpty()) {
      auto a = row.removeFromRight(std::min(100, row.getWidth() / 3));
      g.drawText(shortcut, a, juce::Justification::centredRight);
    }
    g.drawText(text, row, juce::Justification::centredLeft);
  }
  void drawButtonText(juce::Graphics &g, juce::TextButton &b, bool over,
                      bool down) override {
    auto id = b.getComponentID();
    String path;
    if(id=="midiRecord") {
      auto r=juce::Rectangle<float>(13,13).withCentre(b.getLocalBounds().toFloat().getCentre());
      g.setColour((b.getToggleState()?juce::Colours::white:juce::Colour(0xffb44735)).withMultipliedAlpha(b.isEnabled()?1.f:.4f));
      if((bool)b.getProperties()["recordPending"])g.drawEllipse(r,2.f);
      else if(b.getToggleState())g.fillRect(r.reduced(1.5f));else g.fillEllipse(r);
      return;
    }
    if (id == "begin" || id == "end") {
      auto centre=b.getLocalBounds().toFloat().getCentre();
      g.setColour(ink.withMultipliedAlpha(b.isEnabled()?1.f:.38f));
      juce::Path p;
      if(id=="begin") {g.fillRect(centre.x-6,centre.y-6,1.5f,12.f);p.addTriangle(centre.x-3,centre.y,centre.x+5,centre.y-6,centre.x+5,centre.y+6);}
      else {g.fillRect(centre.x+4.5f,centre.y-6,1.5f,12.f);p.addTriangle(centre.x+3,centre.y,centre.x-5,centre.y-6,centre.x-5,centre.y+6);}
      g.fillPath(p);return;
    }
    if(id=="partColour") {
      auto c=juce::Colour((juce::uint32)(juce::int64)b.getProperties()["voiceColour"]);auto r=b.getLocalBounds().toFloat().reduced(7,5);
      g.setColour(c.withMultipliedAlpha(b.isEnabled()?1.f:.38f));g.fillRoundedRectangle(r,3);g.setColour(ink.withAlpha(.7f));g.drawRoundedRectangle(r,3,1);return;
    }
    if(id=="partDelete") {
      auto c=b.getLocalBounds().toFloat().getCentre();
      g.setColour(ink.withMultipliedAlpha(b.isEnabled()?1.f:.38f));
      juce::Path p;p.startNewSubPath(c.x-5,c.y-4);p.lineTo(c.x-4,c.y+7);p.lineTo(c.x+4,c.y+7);p.lineTo(c.x+5,c.y-4);
      p.startNewSubPath(c.x-7,c.y-5);p.lineTo(c.x+7,c.y-5);
      p.startNewSubPath(c.x-3,c.y-5);p.lineTo(c.x-3,c.y-8);p.lineTo(c.x+3,c.y-8);p.lineTo(c.x+3,c.y-5);
      p.startNewSubPath(c.x-2,c.y-2);p.lineTo(c.x-2,c.y+4);p.startNewSubPath(c.x+2,c.y-2);p.lineTo(c.x+2,c.y+4);
      g.strokePath(p,juce::PathStrokeType(1.3f));return;
    }
    if(id=="partLock") {
      auto c=b.getLocalBounds().toFloat().getCentre();
      g.setColour(ink.withMultipliedAlpha(b.isEnabled()?1.f:.38f));
      g.drawRoundedRectangle(c.x-6,c.y-1,12,9,1.5f,1.3f);
      juce::Path shackle;shackle.startNewSubPath(c.x-4,c.y-1);shackle.lineTo(c.x-4,c.y-5);
      shackle.cubicTo(c.x-4,c.y-10,c.x+4,c.y-10,c.x+4,c.y-5);
      shackle.lineTo(c.x+4,c.y+(b.getToggleState()?-1.f:-5.f));
      g.strokePath(shackle,juce::PathStrokeType(1.5f));
      g.fillEllipse(c.x-1,c.y+2,2,2);g.drawLine(c.x,c.y+3,c.x,c.y+5,1.2f);return;
    }
    if(id=="partMute") {
      auto c=b.getLocalBounds().toFloat().getCentre();
      g.setColour(ink.withMultipliedAlpha(b.isEnabled()?1.f:.38f));juce::Path p;
      p.startNewSubPath(c.x-7,c.y-3);p.lineTo(c.x-3,c.y-3);p.lineTo(c.x+2,c.y-7);p.lineTo(c.x+2,c.y+7);p.lineTo(c.x-3,c.y+3);p.lineTo(c.x-7,c.y+3);p.closeSubPath();
      g.strokePath(p,juce::PathStrokeType(1.3f));
      if(b.getToggleState())g.drawLine(c.x-9,c.y-9,c.x+9,c.y+9,1.5f);
      else {g.drawLine(c.x+5,c.y-4,c.x+7,c.y,1.3f);g.drawLine(c.x+7,c.y,c.x+5,c.y+4,1.3f);}return;
    }
    if (id == "help") {
      auto r=b.getLocalBounds().toFloat().reduced(3); g.setColour(ink); g.drawEllipse(r,1.4f);
      g.setFont(juce::FontOptions(19,juce::Font::bold)); g.drawText("?",r,juce::Justification::centred); return;
    }
    if (id == "section") {
      g.setColour(ink);
      g.setFont(getTextButtonFont(b, b.getHeight()));
      g.drawText(b.getButtonText(), 14, 0, b.getWidth() - 28, b.getHeight(),
                 juce::Justification::centredLeft);
      return;
    }
    if (id.startsWith("window")) {
      g.setColour(ink);
      auto r = juce::Rectangle<float>(16, 16).withCentre(
          b.getLocalBounds().toFloat().getCentre());
      if (id == "windowMin")
        g.drawLine(r.getX() + 3, r.getY() + 8.5f, r.getX() + 13,
                   r.getY() + 8.5f, 1.2f);
      if (id == "windowMax")
        g.drawRect(r.withSizeKeepingCentre(9, 9), 1.2f);
      if (id == "windowClose") {
        g.drawLine(r.getX() + 3.5f, r.getY() + 3.5f, r.getX() + 12.5f,
                   r.getY() + 12.5f, 1.2f);
        g.drawLine(r.getX() + 12.5f, r.getY() + 3.5f, r.getX() + 3.5f,
                   r.getY() + 12.5f, 1.2f);
      }
      return;
    }

    if (id == "select")
      path = reference_icons::select;
    if (id == "add")
      path = reference_icons::add;
    if (id == "cut")
      path = "M5 16a3 3 0 1 0 0 6 3 3 0 0 0 0-6Zm14 0a3 3 0 1 0 0 6 3 3 0 0 0 0-6ZM6 17 20 2M18 17 4 2";
    if (id == "eraser")
      path = "M3 15 14 3 22 10 11 22 7 22 3 18ZM8 10 16 17M11 22H22";
    if (id == "hand")
      path = reference_icons::hand;
    if (id == "file")
      path = reference_icons::file;
    if (id == "settings")
      path = reference_icons::settings;
    if (id == "eye")
      path = "M2 12s4-7 10-7 10 7 10 7-4 7-10 7-10-7-10-7Z";
    if (path.isEmpty()) {
      juce::LookAndFeel_V4::drawButtonText(g, b, over, down);
      return;
    }
    auto geometry = juce::Drawable::parseSVGPath(path);
    if (id == "eye") {
      geometry.addEllipse(9, 9, 6, 6);
      if(!b.getToggleState()){geometry.startNewSubPath(2,2);geometry.lineTo(22,22);}
    }
    float size = id == "file" ? 18.f : id == "settings" ? 17.f : 16.f;
    auto centre = b.getLocalBounds().toFloat().getCentre();
    auto bounds = geometry.getBounds();
    float scale = size / 24.f;
    geometry.applyTransform(juce::AffineTransform::scale(scale).translated(
        centre.x - bounds.getCentreX() * scale,
        centre.y - bounds.getCentreY() * scale));
    g.setColour(ink.withMultipliedAlpha(b.isEnabled() || id=="eye" ? 1.f : .38f));
    g.strokePath(geometry,
                 juce::PathStrokeType((id == "settings" ? 1.6f : 1.5f) * scale,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
  }
  void drawTickBox(juce::Graphics &g, juce::Component &b, float x, float y,
                   float w, float h, bool ticked, bool enabled, bool,
                   bool) override {
    // Keep every edge of the stroke inside the control, including at fractional DPI.
    auto size=std::min(16.f,std::min(w,h)-2.f);
    auto r = juce::Rectangle<float>(x, y, w, h).withSizeKeepingCentre(size,size);
    r=r.getIntersection(b.getLocalBounds().toFloat().reduced(1.f));
    g.setColour((ticked ? bronze : juce::Colour(0xfffffdf6))
                    .withMultipliedAlpha(enabled ? 1.f : .38f));
    g.fillRoundedRectangle(r, 2);
    g.setColour(border);
    g.drawRoundedRectangle(r.reduced(.75f), 2, 1.5f);
    if (ticked) {
      g.setColour(juce::Colours::white);
      juce::Path p;
      p.startNewSubPath(r.getX() + 3, r.getY() + 7);
      p.lineTo(r.getX() + 6, r.getY() + 10);
      p.lineTo(r.getX() + 11, r.getY() + 3);
      g.strokePath(p, juce::PathStrokeType(1.8f));
    }
  }
  void drawCallOutBoxBackground(juce::CallOutBox &b, juce::Graphics &g,
                                const juce::Path &p, juce::Image &) override {
    g.setColour(juce::Colour(0xfffffdf6));
    g.fillPath(p);
    g.setColour(border);
    g.strokePath(p, juce::PathStrokeType(1));
  }
  void drawButtonBackground(juce::Graphics &g, juce::Button &b,
                            const juce::Colour &c, bool over,
                            bool down) override {
    if (b.getComponentID()=="help") return;
    if (b.getComponentID().startsWith("window")) {
      if (over || down) {
        g.setColour(b.getComponentID() == "windowClose"
                        ? juce::Colour(0xffdfb8a8)
                        : juce::Colour(0xffe9e0d1));
        g.fillAll();
      }
      return;
    }
    auto r = b.getLocalBounds().toFloat().reduced(.5f);
    g.setColour(down ? c.darker(.1f) : over ? c.brighter(.04f) : c);
    g.fillRoundedRectangle(r, 5);
    g.setColour(border);
    g.drawRoundedRectangle(r, 5, 1);
  }
  juce::Typeface::Ptr getTypefaceForFont(const juce::Font &f) override {
    return juce::LookAndFeel_V4::getTypefaceForFont(juce::Font(
        juce::FontOptions("Segoe UI", f.getHeight(), f.getStyleFlags())));
  }
  void drawDocumentWindowTitleBar(juce::DocumentWindow &w, juce::Graphics &g,
                                  int width, int height, int, int,
                                  const juce::Image *, bool) override {
    g.fillAll(paper);
    g.setColour(ink);
    g.setFont(juce::FontOptions(13));
    g.drawText(w.getName(), 100, 0, width - 200, height,
               juce::Justification::centred);
  }
  struct WindowButton : juce::Button {
    int kind;
    WindowButton(int k)
        : Button(k == juce::DocumentWindow::closeButton ? tr(String::fromUTF8("Fermer"))
                 : k == juce::DocumentWindow::minimiseButton
                     ? tr(String::fromUTF8("Réduire"))
                     : tr(String::fromUTF8("Agrandir"))),
          kind(k) {}
    void paintButton(juce::Graphics &g, bool over, bool down) override {
      if (over || down) {
        g.setColour(juce::Colour(kind == juce::DocumentWindow::closeButton
                                     ? 0xffdfb8a8
                                     : 0xffe9e0d1));
        g.fillAll();
      }
      g.setColour(juce::Colour(0xff254962));
      float x = getWidth() / 2.f, y = getHeight() / 2.f;
      if (kind == juce::DocumentWindow::closeButton) {
        g.drawLine(x - 4, y - 4, x + 4, y + 4, 1.5);
        g.drawLine(x + 4, y - 4, x - 4, y + 4, 1.5);
      } else if (kind == juce::DocumentWindow::minimiseButton)
        g.drawLine(x - 5, y + 2, x + 5, y + 2, 1.5);
      else
        g.drawRect(x - 4, y - 4, 8.f, 8.f, 1.5f);
    }
  };
  juce::Button *createDocumentWindowButton(int kind) override {
    return new WindowButton(kind);
  }
};
