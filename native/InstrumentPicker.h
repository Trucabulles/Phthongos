#pragma once
#include "Bridge.h"
class InstrumentPicker : public juce::TextButton {
  std::vector<std::pair<String, var>> choices;
  var value;
  std::function<void(var)> change;
  struct Popup : juce::Component,
                 juce::ListBoxModel,
                 juce::Timer,
                 juce::KeyListener {
    juce::ListBox list;
    juce::Component::SafePointer<InstrumentPicker> owner;
    std::uint32_t leftAt = 0, lastKeyAt = 0;
    String search;
    Popup(InstrumentPicker *p, int height) : list({}, this), owner(p) {
      addAndMakeVisible(list);
      list.setRowHeight(36);
      list.setMouseMoveSelectsRows(true);
      list.setColour(juce::ListBox::backgroundColourId,
                     juce::Colour(0xfffffdf6));
      list.addKeyListener(this);
      list.setWantsKeyboardFocus(true);
      setSize(std::max(200, p->getWidth()), height);
      for (int i = 0; i < (int)p->choices.size(); ++i)
        if (p->choices[(size_t)i].second == p->value)
          list.selectRow(i);
      startTimer(30);
    }
    int getNumRows() override { return owner ? (int)owner->choices.size() : 0; }
    void paintListBoxItem(int row, juce::Graphics &g, int width, int height,
                          bool selected) override {
      if (!owner || row < 0 || row >= (int)owner->choices.size())
        return;
      if (selected) {
        g.setColour(juce::Colour(0xffe9e0d1));
        g.fillAll();
      }
      g.setColour(juce::Colour(0xff254962));
      g.setFont(juce::FontOptions(14));
      g.drawText(owner->choices[(size_t)row].first, 10, 0, width - 20, height,
                 juce::Justification::centredLeft, true);
    }
    void resized() override { list.setBounds(getLocalBounds()); }
    void choose(int row) {
      if (!owner || row < 0 || row >= (int)owner->choices.size())
        return;
      auto p = owner;
      auto selected = p->choices[(size_t)row];
      p->value = selected.second;
      p->setButtonText(selected.first + juce::String::fromUTF8(" ▾"));
      if (p->change)
        p->change(p->value);
      if (auto *c = findParentComponentOfClass<juce::CallOutBox>())
        c->dismiss();
      p->grabKeyboardFocus();
    }
    void listBoxItemClicked(int row, const juce::MouseEvent &) override {
      choose(row);
    }
    bool keyPressed(const juce::KeyPress &k, juce::Component *) override {
      if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey) {
        choose(list.getSelectedRow());
        return true;
      }
      if (!owner)
        return false;
      auto c = k.getTextCharacter();
      if (c > 32 && !k.getModifiers().isCtrlDown() &&
          !k.getModifiers().isAltDown()) {
        auto now = juce::Time::getMillisecondCounter();
        if (now - lastKeyAt > 700)
          search = String();
        lastKeyAt = now;
        search += String::charToString(c).toLowerCase();
        int start = list.getSelectedRow() + (search.length() == 1 ? 1 : 0);
        for (int offset = 0; offset < (int)owner->choices.size(); ++offset) {
          int index =
              (std::max(0, start) + offset) % (int)owner->choices.size();
          auto &p = owner->choices[(size_t)index];
          if (p.first.toLowerCase().startsWith(search) ||
              String((int)p.second + 1).startsWith(search)) {
            list.selectRow(index);
            list.scrollToEnsureRowIsOnscreen(index);
            return true;
          }
        }
      }
      return false;
    }
    void timerCallback() override {
      auto mouse = juce::Desktop::getInstance()
                       .getMainMouseSource()
                       .getScreenPosition()
                       .roundToInt();
      if (getScreenBounds().expanded(4).contains(mouse) ||
          (owner && owner->getScreenBounds().expanded(4).contains(mouse))) {
        leftAt = 0;
        return;
      }
      auto now = juce::Time::getMillisecondCounter();
      if (leftAt == 0)
        leftAt = now;
      else if (now - leftAt >= 180) {
        stopTimer();
        if (auto *c = findParentComponentOfClass<juce::CallOutBox>())
          c->dismiss();
      }
    }
  };

public:
  bool preserveChoiceNames=false;
  std::function<String(var)> displayName;
  void refreshLanguage(localisation::Language previous) {
    for(auto& choice:choices)if(displayName)choice.first=displayName(choice.second);else if(!preserveChoiceNames||choice.second.toString()=="new")choice.first=localisation::retranslate(choice.first,previous);
    setSelectedValue(value);repaint();
  }
  void setSelectedValue(var selected) {
    value=selected;
    for(auto& choice:choices)if(choice.second==value){setButtonText(choice.first+String::fromUTF8(" ▾"));break;}
  }
  InstrumentPicker(std::vector<std::pair<String, var>> c, var v,
                   std::function<void(var)> fn)
      : choices(std::move(c)), value(v), change(std::move(fn)) {
    for (auto &p : choices)
      if (p.second == value)
        setButtonText(p.first + juce::String::fromUTF8(" ▾"));
    onClick = [this] {
      auto *parent = getTopLevelComponent();
      auto area = parent->getLocalArea(this, getLocalBounds());
      int height = std::min(288, std::max(36, parent->getHeight() - 32));
      auto &popup = juce::CallOutBox::launchAsynchronously(
          std::make_unique<Popup>(this, height), area, parent);
      popup.setArrowSize(0);
    };
  }
};
