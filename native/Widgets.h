#pragma once
#include "Bridge.h"
#include "PointerSafety.h"
#include "InstrumentPicker.h"
#include <regex>
// Native owned windows follow their application without becoming global topmost.
void attachDialogOwner(juce::DialogWindow&,juce::Component*);
bool dialogHasOwner(const juce::DialogWindow&,const juce::Component&);
inline void refreshUiLanguage(juce::Component& component,localisation::Language previous) {
  if((bool)component.getProperties()["preserveLanguageText"])return;
  auto translate=[previous](const String& text){return localisation::retranslate(text,previous);};
  component.setName(translate(component.getName()));
  if(auto* picker=dynamic_cast<InstrumentPicker*>(&component))picker->refreshLanguage(previous);
  else if(auto* button=dynamic_cast<juce::Button*>(&component))button->setButtonText(translate(button->getButtonText()));
  else if(auto* label=dynamic_cast<juce::Label*>(&component)){
    if(!label->isEditable())label->setText(translate(label->getText()),juce::dontSendNotification);
    return; // Label's internal editor is not presentation text.
  }else if(auto* combo=dynamic_cast<juce::ComboBox*>(&component)){
    auto selected=combo->getSelectedId();
    for(int i=0;i<combo->getNumItems();++i)combo->changeItemText(combo->getItemId(i),translate(combo->getItemText(i)));
    if(selected)combo->setSelectedId(selected,juce::dontSendNotification);
    return; // Do not translate ComboBox's internal editable label separately.
  }else if(auto* editor=dynamic_cast<juce::TextEditor*>(&component)){
    if(editor->isReadOnly())editor->setText(translate(editor->getText()),false);
    return;
  }
  for(auto* child:component.getChildren())refreshUiLanguage(*child,previous);
  component.repaint();
}
// Listening controls keep exact dB values in the model and saved projects.
// Percentages use a squared-amplitude taper, normalised to each control maximum.
class ListeningVolumeSlider : public VisibleCursorSlider {
public:
  static double percent(double db){return db<=-60?0:100*std::pow(10.,db/40.);}
  static double decibels(double percent){return percent<=0?-60:std::max(-60.,40*std::log10(percent/100.));}
  ListeningVolumeSlider(){setRange(-60,0,0);setSliderStyle(LinearHorizontal);setTextBoxStyle(TextBoxRight,false,58,25);setDoubleClickReturnValue(true,0);}
  String getTextFromValue(double db) override{return String(int(std::lround(100*valueToProportionOfLength(db))))+" %";}
  double getValueFromText(const String& text) override{return proportionOfLengthToValue(text.retainCharacters("0123456789.,").replaceCharacter(',','.').getDoubleValue()/100.);}
  double valueToProportionOfLength(double db) override{return percent(db)/percent(getMaximum());}
  double proportionOfLengthToValue(double proportion) override{return juce::jlimit(getMinimum(),getMaximum(),decibels(std::max(0.,proportion)*percent(getMaximum())));}
};
class ClickOptionsPanel : public juce::Component {
public:
  ListeningVolumeSlider volume;
  juce::Label caption,preCaption,help;
  juce::TextButton counts[3];
  int count=1;
  ClickOptionsPanel(double db,int bars):count(bars){
    for(auto* c:std::initializer_list<juce::Component*>{&volume,&caption,&preCaption,&help,&counts[0],&counts[1],&counts[2]})addAndMakeVisible(c);
    caption.setText(tr(String::fromUTF8("Volume du clic")),juce::dontSendNotification);
    preCaption.setText(tr(String::fromUTF8("Précompte")),juce::dontSendNotification);
    help.setText(tr(String::fromUTF8("Le précompte reste audible même si le clic est désactivé.")),juce::dontSendNotification);help.setFont(juce::FontOptions(13));
    volume.setValue(db,juce::dontSendNotification);volume.setDoubleClickReturnValue(true,-12);volume.setName(caption.getText());
    const String labels[]={tr("Aucun"),tr("1 mesure"),tr("2 mesures")};
    for(int i=0;i<3;++i){counts[i].setButtonText(labels[i]);counts[i].setClickingTogglesState(true);counts[i].setRadioGroupId(701);counts[i].setToggleState(i==count,juce::dontSendNotification);counts[i].onClick=[this,i]{count=i;};}
  }
  void resized() override{
    caption.setBounds(0,0,getWidth(),24);volume.setBounds(0,26,getWidth(),30);
    preCaption.setBounds(0,72,getWidth(),24);int width=(getWidth()-12)/3;
    for(int i=0;i<3;++i)counts[i].setBounds(i*(width+6),102,width,34);
    help.setBounds(0,148,getWidth(),42);
  }
};
class BeatIndicator : public juce::Component {
public:
  int pulses=4,active=-1;
  BeatIndicator(){setInterceptsMouseClicks(false,false);}
  void update(int total,int pulse){if(pulses!=total||active!=pulse){pulses=total;active=pulse;repaint();}}
  void paint(juce::Graphics& g) override{
    int shown=std::min(pulses,8);float step=float(getWidth())/std::max(1,shown);
    for(int i=0;i<shown;++i){bool on=active>=0&&(pulses<=8?active==i:active%8==i);g.setColour(juce::Colour(on?(active==0?0xffad793c:0xff294e64):0xffd4c9b5));float size=on?9.f:6.f;g.fillEllipse(step*(i+.5f)-size/2,getHeight()/2.f-size/2,size,size);}
    if(pulses>8&&active>=0){g.setColour(juce::Colour(0xff294e64));g.setFont(juce::FontOptions(11));g.drawText(String(active+1)+"/"+String(pulses),getLocalBounds(),juce::Justification::centred);}
  }
};
class ConfirmOverlay : public juce::Component {
  juce::Label heading, message;
  juce::TextButton accept, discard, cancel{tr(String::fromUTF8("Annuler"))},
      closeButton{juce::String::fromUTF8("×")};
  juce::Rectangle<int> sheet;
  bool hasDiscard;

public:
  std::function<void(int)> answer;
  ConfirmOverlay(const String &title, const String &text, const String &action,
                 const String &other)
      : hasDiscard(other.isNotEmpty()) {
    setWantsKeyboardFocus(true);
    for (juce::Component *c : std::initializer_list<juce::Component *>{
             &heading, &message, &accept, &discard, &cancel, &closeButton})
      addAndMakeVisible(c);
    heading.setText(title, juce::dontSendNotification);
    heading.setFont(juce::FontOptions(23, juce::Font::bold));
    message.setText(text, juce::dontSendNotification);
    message.setFont(juce::FontOptions(16));
    message.setMinimumHorizontalScale(1);
    accept.setButtonText(action);
    discard.setButtonText(other);
    discard.setVisible(hasDiscard);
    accept.setColour(juce::TextButton::buttonColourId,
                     juce::Colour(0xffad793c));
    accept.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    accept.onClick = [this] {
      if (answer)
        answer(1);
    };
    discard.onClick = [this] {
      if (answer)
        answer(2);
    };
    cancel.onClick = closeButton.onClick = [this] {
      if (answer)
        answer(0);
    };
  }
  void focusCancel() { cancel.grabKeyboardFocus(); }
  bool keyPressed(const juce::KeyPress &k) override {
    if (k == juce::KeyPress::escapeKey) {
      if (answer)
        answer(0);
      return true;
    }
    return false;
  }
  void resized() override {
    sheet = juce::Rectangle<int>(std::min(540, getWidth() - 48), 260)
                .withCentre(getLocalBounds().getCentre());
    heading.setBounds(sheet.getX() + 76, sheet.getY() + 28,
                      sheet.getWidth() - 145, 37);
    closeButton.setBounds(sheet.getRight() - 54, sheet.getY() + 24, 30, 30);
    message.setBounds(sheet.getX() + 28, sheet.getY() + 86,
                      sheet.getWidth() - 56, 91);
    int y = sheet.getBottom() - 65;
    if (hasDiscard) {
      accept.setBounds(sheet.getX() + 28, y, 126, 37);
      discard.setBounds(sheet.getX() + 164, y, sheet.getWidth() - 294, 37);
      cancel.setBounds(sheet.getRight() - 120, y, 92, 37);
    } else {
      cancel.setBounds(sheet.getRight() - 262, y, 108, 37);
      accept.setBounds(sheet.getRight() - 144, y, 116, 37);
    }
  }
  void paint(juce::Graphics &g) override {
    g.fillAll(juce::Colour(0x40253e51));
    g.setColour(juce::Colour(0xfff9faf4));
    g.fillRoundedRectangle(sheet.toFloat(), 16);
    g.setColour(juce::Colour(0xffcbb995));
    g.drawRoundedRectangle(sheet.toFloat().reduced(.5f), 16, 1);
    auto symbol = juce::Rectangle<float>(36, 36).withPosition(
        (float)sheet.getX() + 28, (float)sheet.getY() + 28);
    g.setColour(juce::Colour(0xffe9dfca));
    g.fillEllipse(symbol);
    g.setColour(juce::Colour(0xffcbb995));
    g.drawEllipse(symbol, 1);
    g.setColour(juce::Colour(0xffa87335));
    g.setFont(juce::FontOptions(27));
    g.drawText("!", symbol, juce::Justification::centred);
  }
};
// Same immediate controls and listening-only scope as ReverbControl.tsx.
class ReverbPanel : public juce::Component {
  juce::ToggleButton enabled{tr(juce::String::fromUTF8("Réverbération"))};
  juce::Label caption, value, help;
  VisibleCursorSlider amount;

public:
  ReverbPanel(bool on, double initial,
              std::function<void(bool, double)> change) {
    for (juce::Component *c : std::initializer_list<juce::Component *>{
             &enabled, &caption, &value, &help, &amount})
      addAndMakeVisible(c);
    enabled.setToggleState(on, juce::dontSendNotification);
    amount.setSliderStyle(juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    amount.setRange(0, 100, 1);
    amount.setValue(initial, juce::dontSendNotification);
    amount.setEnabled(on);
    amount.setName(tr(juce::String::fromUTF8("Quantité de réverbération")));
    value.setText(String(initial, 0) + " %", juce::dontSendNotification);
    value.setJustificationType(juce::Justification::centredRight);
    caption.setText(tr(juce::String::fromUTF8("Quantité")),
                    juce::dontSendNotification);
    help.setText(
        tr(juce::String::fromUTF8("Écoute MIDI et préécoute uniquement. Aucun "
                               "effet sur les notes ni sur l’export MIDI.")),
        juce::dontSendNotification);
    help.setFont(juce::FontOptions(11));
    auto update = [this, change] {
      amount.setEnabled(enabled.getToggleState());
      value.setText(String(amount.getValue(), 0) + " %",
                    juce::dontSendNotification);
      change(enabled.getToggleState(), amount.getValue());
    };
    enabled.onClick = update;
    amount.onValueChange = update;
    setSize(254, 160);
  }
  void resized() override {
    enabled.setBounds(8, 8, getWidth() - 16, 28);
    caption.setBounds(10, 44, 140, 24);
    value.setBounds(getWidth() - 70, 44, 58, 24);
    amount.setBounds(8, 70, getWidth() - 16, 26);
    help.setBounds(10, 100, getWidth() - 20, 52);
  }
};
class Form : public juce::Component, private juce::Timer {
  juce::OwnedArray<juce::Component> children;
  juce::Viewport viewport;
  juce::Component body;
  juce::TextButton apply{tr(String::fromUTF8("Appliquer"))}, cancel{tr(String::fromUTF8("Annuler"))};
  juce::Label heading;
  juce::TextButton dismiss{juce::String::fromUTF8("×")};
  var values = object({});
  int yy = 12;
  std::vector<std::pair<juce::Label *, std::function<String()>>> liveLabels;
  void timerCallback() override {
    for (auto &l : liveLabels)
      l.first->setText(l.second(), juce::dontSendNotification);
  }

public:
  template <class T> T *own(T *p) {
    children.add(p);
    return p;
  }
  std::map<String, juce::TextEditor *> textFields;
  std::map<String, juce::ComboBox *> comboFields;
  std::map<String,juce::ToggleButton*> toggleFields;
  std::map<String,juce::Component*> compactFields;
  std::function<void(var)> onApply;
  std::function<void()> onDismiss;
  inline static std::set<Form*> activeForms;
  ~Form() override { dismissCleanup();activeForms.erase(this); }
  void dismissCleanup(){if(onDismiss){auto fn=std::move(onDismiss);fn();}}
  Form() {
    activeForms.insert(this);
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&body, false);
    addAndMakeVisible(apply);
    addAndMakeVisible(cancel);
    addAndMakeVisible(heading);
    addAndMakeVisible(dismiss);
    dismiss.setComponentID("windowClose");
    dismiss.setName(tr(String::fromUTF8("Fermer")));
    heading.setFont(juce::FontOptions(24, juce::Font::bold));
    dismiss.onClick = [this] { close(); };
    apply.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffad793c));
    apply.setColour(juce::TextButton::textColourOffId,
                    juce::Colour(0xfffffdf6));
    apply.onClick = [this] {
      if (onApply)
        onApply(values);
    };
    cancel.onClick = [this] { close(); };
    setSize(600, 460);
  }
  void actions(const String &primary, const String &secondary) {
    apply.setButtonText(primary);
    cancel.setButtonText(secondary);
    cancel.setVisible(secondary.isNotEmpty());
  }
  var state() const { return values; }
  void setHeading(const String& text) { heading.setText(text,juce::dontSendNotification); }
  void setApplyEnabled(bool enabled) { apply.setEnabled(enabled); }
  void custom(juce::Component* component,int height) {
    own(component);body.addAndMakeVisible(component);
    component->setBounds(16,yy,540,height);yy+=height+10;resizeBody();
  }
  void resizeCustom(juce::Component* component,int height){int delta=height-component->getHeight(),bottom=component->getBottom();component->setSize(component->getWidth(),height);for(auto* child:body.getChildren())if(child!=component&&child->getY()>=bottom)child->setTopLeftPosition(child->getX(),child->getY()+delta);yy+=delta;resizeBody();}
  void text(const String &title, const String &key, const var &value) {
    auto *l = own(new juce::Label({}, title));
    body.addAndMakeVisible(l);
    l->setBounds(16, yy, 540, 24);
    yy += 26;
    auto *e = own(new juce::TextEditor);
    body.addAndMakeVisible(e);
    e->setBounds(16, yy, 540, 34);
    e->setText(value.toString());
    textFields[key] = e;
    values.getDynamicObject()->setProperty(key, value);
    e->onTextChange = [this, e, key] {
      values.getDynamicObject()->setProperty(key, e->getText());
    };
    yy += 48;
    resizeBody();
  }
  void toggle(const String &title, const String &key, bool value) {
    auto *b = own(new juce::ToggleButton(title));
    body.addAndMakeVisible(b);
    b->setBounds(16, yy, 540, 30);
    b->setClickingTogglesState(true);
    b->setMouseClickGrabsKeyboardFocus(true);
    b->setToggleState(value, juce::dontSendNotification);
    toggleFields[key]=b;
    values.getDynamicObject()->setProperty(key, value);
    b->onClick = [this, b, key] {
      values.getDynamicObject()->setProperty(key, b->getToggleState());
    };
    yy += 40;
    resizeBody();
  }
  void combo(const String &title, const String &key,
             const std::vector<std::pair<String, var>> &choices,
             const var &value, std::function<void(var)> changed = {}) {
    auto *l = own(new juce::Label({}, title));
    body.addAndMakeVisible(l);
    l->setBounds(16, yy, 540, 24);
    yy += 26;
    auto *b = own(new juce::ComboBox);
    comboFields[key] = b;
    if(key=="output")b->getProperties().set("preserveLanguageText",true);
    body.addAndMakeVisible(b);
    b->setBounds(16, yy, 540, 34);
    int index = 1, selected = 1;
    for (auto &choice : choices) {
      b->addItem(choice.first, index);
      if (choice.second == value)
        selected = index;
      ++index;
    }
    b->setSelectedId(selected, juce::dontSendNotification);
    values.getDynamicObject()->setProperty(
        key, choices.empty() ? var() : choices[(size_t)selected - 1].second);
    b->onChange = [this, b, key, choices, changed] {
      if (b->getSelectedId() > 0) {
        values.getDynamicObject()->setProperty(
            key, choices[(size_t)b->getSelectedId() - 1].second);
        if (changed)
          changed(choices[(size_t)b->getSelectedId() - 1].second);
      }
    };
    yy += 48;
    resizeBody();
  }
  juce::TextButton *button(const String &title, std::function<void()> fn) {
    auto *b = own(new juce::TextButton(title));
    body.addAndMakeVisible(b);
    b->setBounds(16, yy, 540, 34);
    b->onClick = std::move(fn);
    yy += 44;
    resizeBody();
    return b;
  }
  void label(const String &text, bool translate = true) {
    auto *l = own(new juce::Label({}, text));
    l->getProperties().set("preserveLanguageText",!translate);
    body.addAndMakeVisible(l);
    l->setBounds(16, yy, 540, 54);
    l->setMinimumHorizontalScale(1);
    yy += 60;
    resizeBody();
  }
  void live(std::function<String()> fn) {
    auto *l = own(new juce::Label({}, fn()));
    body.addAndMakeVisible(l);
    l->setBounds(16, yy, 540, 54);
    l->setMinimumHorizontalScale(1);
    yy += 60;
    liveLabels.emplace_back(l, std::move(fn));
    startTimer(500);
    resizeBody();
  }
  void setValue(const String &key, const var &v) {
    values.getDynamicObject()->setProperty(key, v);
    if(compactFields.count(key))if(auto* picker=dynamic_cast<InstrumentPicker*>(compactFields[key]))picker->setSelectedValue(v);
    if(toggleFields.count(key))toggleFields[key]->setToggleState((bool)v,juce::dontSendNotification);
    auto it = textFields.find(key);
    if (it != textFields.end())
      it->second->setText(v.toString(), false);
  }
  void instrument(const String &key,
                  const std::vector<std::pair<String, var>> &choices,
                  const var &value,std::function<void(var)> changed={}) {
    values.getDynamicObject()->setProperty(key, value);
    auto *b = own(new InstrumentPicker(choices, value, [this, key,changed](var v) {
      values.getDynamicObject()->setProperty(key, v);
      if(changed)changed(v);
    }));
    compactFields[key]=b;
    if(key.startsWith("target"))b->preserveChoiceNames=true;
    body.addAndMakeVisible(b);
    b->setBounds(16, yy, 540, 36);
    yy += 48;
    resizeBody();
  }
  void compactChoice(const String& title,const String& key,const std::vector<std::pair<String,var>>& choices,const var& value,std::function<void(var)> changed={}) {
    auto* l=own(new juce::Label({},title));body.addAndMakeVisible(l);
    if(key.startsWith("target"))l->getProperties().set("preserveLanguageText",true);
    l->setBounds(16,yy,540,24);yy+=26;instrument(key,choices,value,changed);
  }
  void close() {
    dismissCleanup();
    if (auto *w = findParentComponentOfClass<juce::DialogWindow>())
      w->exitModalState(0);
  }
  void resized() override {
    heading.setBounds(28, 18, getWidth() - 96, 38);
    dismiss.setBounds(getWidth() - 55, 18, 29, 29);
    viewport.setBounds(12, 65, getWidth() - 24, getHeight() - 130);
    cancel.setBounds(getWidth() - 260, getHeight() - 50, 115, 36);
    apply.setBounds(getWidth() - 135, getHeight() - 50, 115, 36);
    resizeBody();
  }
  juce::DialogWindow* createDialog(const String &title,juce::Component* owner=nullptr) {
    if(!owner)owner=juce::TopLevelWindow::getActiveTopLevelWindow();
    if(!owner)for(int i=0;i<juce::TopLevelWindow::getNumTopLevelWindows();++i){auto* candidate=juce::TopLevelWindow::getTopLevelWindow(i);if(candidate->isVisible()&&candidate->getName()=="Phthongos"){owner=candidate;break;}}
    // Size to contents; short forms must not inherit an empty 460px sheet.
    heading.setText(title, juce::dontSendNotification);
    setSize(540, juce::jlimit(220, 720, yy + 138));
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = title;
    options.componentToCentreAround=owner;
    // The content owns the rounded sheet. A rectangular opaque parent would
    // show through its corners and produce a second, differently rounded frame.
    options.dialogBackgroundColour = juce::Colours::transparentBlack;
    options.content.setOwned(this);
    options.useNativeTitleBar = false;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    // Without a native shadow Windows leaves the entire corner geometry to
    // our transparent sheet instead of adding a second system-rounded frame.
    auto* w=options.create();
    w->setTitleBarHeight(0);
    w->setDropShadowEnabled(false);
    attachDialogOwner(*w,owner);
    return w;
  }
  void show(const String& title) {
    createDialog(title)->enterModalState(true,nullptr,true);
  }
  void paint(juce::Graphics &g) override {
    juce::Path sheet;
    sheet.addRoundedRectangle(getLocalBounds().toFloat().reduced(.5f),12);
    g.setColour(juce::Colour(0xfff9faf4));
    g.fillPath(sheet);
    g.setColour(juce::Colour(0xffcbbda5));
    g.strokePath(sheet,juce::PathStrokeType(1));
  }

private:
  void resizeBody() {
    body.setSize(std::max(1, viewport.getWidth() -
                                 (yy + 8 > viewport.getHeight() ? 14 : 0)),
                 yy + 8);
    for (auto *c : children)
      c->setBounds(16, c->getY(), std::max(1, body.getWidth() - 32),
                   c->getHeight());
  }
};
class SidePanel : public juce::Component {
public:
  std::function<void()> committed;
  juce::OwnedArray<juce::Component> widgets;
  int yy = 0;
  int width = 310;
  template <class T> T *own(T *p) {
    widgets.add(p);
    return p;
  }
  struct Drawing : juce::Component {
    std::function<void(juce::Graphics &, int, int)> draw;
    void paint(juce::Graphics &g) override {
      if (draw)
        draw(g, getWidth(), getHeight());
    }
  };
  void drawing(int height, std::function<void(juce::Graphics &, int, int)> fn) {
    auto *d = own(new Drawing);
    d->draw = std::move(fn);
    addAndMakeVisible(d);
    d->setBounds(10, yy, width - 20, height);
    yy += height + 5;
  }
  void clear() {
    widgets.clear();
    yy = 0;
  }
  void label(const String &s, int height = 48) {
    auto *l = own(new juce::Label({}, s));
    addAndMakeVisible(l);
    l->setBounds(10, yy, width - 20, height);
    l->setMinimumHorizontalScale(1);
    l->setFont(juce::FontOptions(12));
    yy += height + 5;
  }
  void button(const String &s, std::function<void()> fn, bool enabled = true) {
    auto *b = own(new juce::TextButton(s));
    addAndMakeVisible(b);
    b->setBounds(10, yy, width - 20, 32);
    b->onClick = std::move(fn);
    b->setEnabled(enabled);
    yy += 38;
  }
  struct DiagnosticRow : juce::Button {
    String title, detail;
    bool active;
    DiagnosticRow(const String& a, const String& b, bool selected)
      : juce::Button(a + " · " + b), title(a), detail(b), active(selected) {
      setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    void paintButton(juce::Graphics& g, bool hover, bool down) override {
      auto r = getLocalBounds().toFloat().reduced(.5f);
      if (active || hover || down) {
        g.setColour(juce::Colour(active ? 0xffe6d6b8 : 0xffefe8dc));
        g.fillRoundedRectangle(r, 4);
      }
      g.setColour(juce::Colour(active ? 0xffa87335 : 0xffd4c5ac));
      g.drawLine(0, (float)getHeight()-1, (float)getWidth(), (float)getHeight()-1);
      if (active) g.fillRect(0, 5, 3, getHeight()-10);
      g.setColour(juce::Colour(0xff254962));
      g.setFont(juce::FontOptions(15, active ? juce::Font::bold : juce::Font::plain));
      g.drawText(title, 10, 3, getWidth()-20, 23, juce::Justification::centredLeft);
      g.setFont(juce::FontOptions(14));
      g.drawText(detail, 10, 26, getWidth()-20, 22, juce::Justification::centredLeft);
    }
  };
  void diagnosticRow(const String& title, const String& detail, bool active,
                     std::function<void()> action) {
    auto* row = own(new DiagnosticRow(title, detail, active));
    addAndMakeVisible(row);
    row->setBounds(10, yy, width-20, 54);
    row->onClick = std::move(action);
    yy += 56;
  }
  void combo(const String &title,
             const std::vector<std::pair<String, var>> &choices,
             const var &value, std::function<void(var)> change) {
    auto *l = own(new juce::Label({}, title));
    addAndMakeVisible(l);
    l->setBounds(10, yy, 65, 30);
    l->setFont(juce::FontOptions(12));
    auto *c = own(new juce::ComboBox);
    addAndMakeVisible(c);
    c->setBounds(78, yy, width - 90, 30);
    int i = 1;
    for (auto &v : choices) {
      c->addItem(v.first, i);
      if (v.second == value)
        c->setSelectedId(i, juce::dontSendNotification);
      ++i;
    }
    c->onChange = [c, choices, change] {
      if (c->getSelectedId() > 0)
        change(choices[(size_t)c->getSelectedId() - 1].second);
    };
    yy += 42;
  }
  void numberPair(const String &a, double av, const String &b, double bv,
                  double minimum, double maximum,
                  std::function<void(double)> ac,
                  std::function<void(double)> bc) {
    int half = (width - 20) / 2;
    auto field = [this, minimum, maximum](const String &name, double v, int x,
                                          int w,
                                          std::function<void(double)> change) {
      auto *l = own(new juce::Label({}, name));
      addAndMakeVisible(l);
      l->setBounds(x, yy, 33, 32);
      auto *n = own(new VisibleCursorSlider);
      addAndMakeVisible(n);
      n->setSliderStyle(juce::Slider::IncDecButtons);
      n->setTextBoxStyle(juce::Slider::TextBoxLeft, false, w - 56, 30);
      n->setRange(minimum, maximum, 1);
      n->setValue(v, juce::dontSendNotification);
      n->setBounds(x + 35, yy, w - 40, 32);
      n->setName(name);
      n->onValueChange = [n, change] { change(n->getValue()); };
    };
    field(a, av, 10, half, ac);
    field(b, bv, 10 + half, half, bc);
    yy += 44;
  }
  void number(const String &title, double value, double low, double high,
              double step, std::function<void(double)> change) {
    auto *l = own(new juce::Label({}, title));
    addAndMakeVisible(l);
    l->setBounds(10, yy, width - 130, 32);
    l->setFont(juce::FontOptions(15));
    auto *n = own(new VisibleCursorSlider);
    addAndMakeVisible(n);
    n->setSliderStyle(juce::Slider::IncDecButtons);
    n->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 74, 30);
    n->setRange(low, high, step);
    n->setValue(value, juce::dontSendNotification);
    n->setBounds(width - 116, yy, 104, 32);
    n->setName(title);
    n->onValueChange = [n, change] { change(n->getValue()); };
    yy += 42;
  }
  void heading(const String &title, std::function<void()> fn) {
    yy += 5;
    auto *b = own(new juce::TextButton(title));
    addAndMakeVisible(b);
    b->setComponentID("section");
    b->setBounds(0, yy, width, 45);
    b->onClick = std::move(fn);
    yy += 54;
  }
  void toggle(const String &s, bool value, std::function<void(bool)> fn) {
    auto *b = own(new juce::ToggleButton(s));
    addAndMakeVisible(b);
    b->setBounds(10, yy, width - 20, 30);
    b->setToggleState(value, juce::dontSendNotification);
    b->onClick = [b, fn] { fn(b->getToggleState()); };
    yy += 36;
  }
  void slider(const String &s, double value, double low, double high,
              double step, std::function<void(double)> fn) {
    auto *caption = own(new juce::Label({}, s));
    addAndMakeVisible(caption);
    caption->setBounds(10, yy, width - 90, 24);
    caption->setFont(juce::FontOptions(12));
    auto *output = own(new juce::Label({}, String(value, step < 1 ? 2 : 0)));
    addAndMakeVisible(output);
    output->setBounds(width - 80, yy, 68, 24);
    output->setJustificationType(juce::Justification::centredRight);
    output->setFont(juce::FontOptions(12));
    yy += 27;
    auto *b = own(new VisibleCursorSlider);
    addAndMakeVisible(b);
    b->setBounds(10, yy, width - 20, 32);
    b->setRange(low, high, step);
    b->setValue(value, juce::dontSendNotification);
    b->setSliderStyle(juce::Slider::LinearHorizontal);
    b->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    b->onValueChange = [b, fn, output, step] {
      output->setText(String(b->getValue(), step < 1 ? 2 : 0),
                      juce::dontSendNotification);
      fn(b->getValue());
    };
    b->onDragEnd = [this] {
      if (committed)
        committed();
    };
    yy += 38;
  }
  void finish() { setSize(width, yy + 10); }
};
inline double decimal(const var &v) {
  auto s = v.toString().trim().replaceCharacter(',', '.');
  static const std::regex pattern(R"(^(?:\d+(?:\.\d*)?|\.\d+)$)");
  if (!std::regex_match(s.toStdString(), pattern))
    return std::numeric_limits<double>::quiet_NaN();
  return s.getDoubleValue();
}
