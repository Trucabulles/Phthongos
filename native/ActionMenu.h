#pragma once
#include "Bridge.h"
// A native, bounded context menu with the same immediate F / M / Delete actions
// as the reference UI. JUCE PopupMenu only provides prefix navigation here.
class ActionMenu {
  struct Row {
    int id;
    String text;
    bool enabled;
  };
  juce::Component *parent;
  std::vector<Row> rows;
  std::map<int, int> keys;
  struct Content : juce::Component, juce::ListBoxModel, juce::KeyListener {
    std::vector<Row> rows;
    std::map<int, int> keys;
    std::function<void(int)> callback;
    juce::ListBox list;
    Content(std::vector<Row> r, std::map<int, int> k,
            std::function<void(int)> fn, int width, int height)
        : rows(std::move(r)), keys(std::move(k)), callback(std::move(fn)),
          list({}, this) {
      addAndMakeVisible(list);
      list.setRowHeight(30);
      list.setMouseMoveSelectsRows(true);
      list.setColour(juce::ListBox::backgroundColourId,
                     juce::Colour(0xfff9faf4));
      list.addKeyListener(this);
      setSize(width, height);
      for (int i = 0; i < (int)rows.size(); ++i)
        if (rows[(size_t)i].id > 0 && rows[(size_t)i].enabled) {
          list.selectRow(i);
          break;
        }
    }
    void resized() override { list.setBounds(getLocalBounds()); }
    int getNumRows() override { return (int)rows.size(); }
    String getNameForRow(int row) override {
      return row >= 0 && row < (int)rows.size() ? rows[(size_t)row].text
                                                : String();
    }
    void paintListBoxItem(int i, juce::Graphics &g, int w, int h,
                          bool selected) override {
      if (i < 0 || i >= (int)rows.size())
        return;
      auto &r = rows[(size_t)i];
      if (selected && r.id > 0 && r.enabled) {
        g.setColour(juce::Colour(0xffe9e0d1));
        g.fillAll();
      }
      g.setColour(juce::Colour(r.enabled ? 0xff254962 : 0xff9da9ac));
      g.setFont(
          juce::FontOptions(13, r.id ? juce::Font::plain : juce::Font::bold));
      g.drawText(r.text, 10, 0, w - 20, h, juce::Justification::centredLeft,
                 true);
    }
    void choose(int id) {
      auto found = std::find_if(rows.begin(), rows.end(), [id](auto &r) {
        return r.id == id && r.enabled;
      });
      if (found == rows.end())
        return;
      auto fn = callback;
      if (auto *c = findParentComponentOfClass<juce::CallOutBox>())
        c->dismiss();
      fn(id);
    }
    void listBoxItemClicked(int i, const juce::MouseEvent &) override {
      if (i >= 0 && i < (int)rows.size())
        choose(rows[(size_t)i].id);
    }
    bool keyPressed(const juce::KeyPress &k, juce::Component *) override {
      if (k == juce::KeyPress::escapeKey) {
        if (auto *c = findParentComponentOfClass<juce::CallOutBox>())
          c->dismiss();
        return true;
      }
      auto it = keys.find(k.getKeyCode());
      if (it == keys.end())
        it = keys.find(
            (int)juce::CharacterFunctions::toUpperCase(k.getTextCharacter()));
      if (it != keys.end() && !k.getModifiers().isAnyModifierKeyDown()) {
        choose(it->second);
        return true;
      }
      if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey) {
        int i = list.getSelectedRow();
        if (i >= 0)
          choose(rows[(size_t)i].id);
        return true;
      }
      if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey) {
        int at = list.getSelectedRow(),
            step = k == juce::KeyPress::upKey ? -1 : 1;
        for (int i = 0; i < (int)rows.size(); ++i) {
          at = (at + step + (int)rows.size()) % (int)rows.size();
          if (rows[(size_t)at].id > 0 && rows[(size_t)at].enabled) {
            list.selectRow(at);
            list.scrollToEnsureRowIsOnscreen(at);
            break;
          }
        }
        return true;
      }
      return false;
    }
  };

public:
  ActionMenu(juce::Component *p) : parent(p) {}
  void addSectionHeader(const String &s) { rows.push_back({0, s, true}); }
  void addSeparator() { rows.push_back({0, "", false}); }
  void addItem(int id, const String &s, bool enabled = true) {
    rows.push_back({id, s, enabled});
  }
  void addSubMenu(const String &s, const juce::PopupMenu &m) {
    addSectionHeader(s);
    for (juce::PopupMenu::MenuItemIterator it(m); it.next();)
      if (it.getItem().itemID > 0)
        addItem(it.getItem().itemID, it.getItem().text, it.getItem().isEnabled);
  }
  void shortcut(int key, int id) { keys[key] = id; }
  void showMenuAsync(const juce::PopupMenu::Options &options,
                     std::function<void(int)> fn) {
    auto screen = options.getTargetScreenArea();
    auto area = parent->getLocalArea(nullptr, screen);
    int height = std::min((int)rows.size() * 30,
                          std::min(480, parent->getHeight() - 16));
    auto content = std::make_unique<Content>(
        rows, keys, std::move(fn), std::min(360, parent->getWidth() - 16),
        height);
    auto &box = juce::CallOutBox::launchAsynchronously(std::move(content), area,
                                                       parent);
    box.setArrowSize(0);
  }
};
