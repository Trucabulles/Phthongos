#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace fileTransfer {
enum class Kind { none, audio, midi };
inline Kind kind(const juce::StringArray& paths) {
  if(paths.size()!=1)return Kind::none;
  juce::File file(paths[0]);if(!file.existsAsFile())return Kind::none;
  if(file.hasFileExtension("mid;midi"))return Kind::midi;
  return file.hasFileExtension("wav;mp3;flac;m4a")?Kind::audio:Kind::none;
}
// A prepared MIDI is retained after drop: some hosts read the path later.
inline juce::File stagingFile(const juce::String& name) {
  auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Phthongos-midi-drag");
  if(root.createDirectory().failed())return {};
  for(auto folder:root.findChildFiles(juce::File::findDirectories,false,"drag-*")) {
    if(folder.isSymbolicLink()||folder.getParentDirectory()!=root)continue;
    if(juce::Time::getCurrentTime().toMilliseconds()-folder.getLastModificationTime().toMilliseconds()<3*24*60*60*1000LL)continue;
    for(auto file:folder.findChildFiles(juce::File::findFiles,false,"*.mid"))if(!file.isSymbolicLink())file.deleteFile();
    folder.deleteFile(); // Only removes an empty directory; never recurse into foreign files.
  }
  auto folder=root.getChildFile("drag-"+juce::Uuid().toString());if(folder.createDirectory().failed())return {};
  auto label=juce::File::createLegalFileName(name).substring(0,100).trim();
  return folder.getChildFile((label.isEmpty()?juce::String("Phthongos"):label)+".mid");
}
// All files for a native drag must share a private directory. The batch exporter
// creates it atomically, so return a path which does not exist yet.
inline juce::File stagingFolder() {
  auto sample=stagingFile("Phthongos");
  if(sample==juce::File())return {};
  auto folder=sample.getParentDirectory();
  return folder.deleteFile()?folder:juce::File();
}
juce::Image dragImage(const juce::StringArray& files);
bool performExternalDrag(const juce::StringArray&,juce::Component*,std::function<void()>);
bool nativeDragTests(const juce::File& directory);
}

class MidiDragButton : public juce::TextButton {
public:
  using Ready=std::function<void(juce::StringArray)>;
  std::function<void(Ready)> prepare;
  std::function<void()> failed;
  std::function<bool(const juce::StringArray&,juce::Component*,std::function<void()>)> externalDrag=fileTransfer::performExternalDrag;
  using juce::TextButton::TextButton;
  void mouseDown(const juce::MouseEvent& e) override {
    ++generation;gesture=false;requested=false;started=false;active=e.mods.isLeftButtonDown();files.clear();
    juce::TextButton::mouseDown(e);
  }
  void mouseDrag(const juce::MouseEvent& e) override {
    juce::TextButton::mouseDrag(e);
    if(!active||!isEnabled()||!e.mods.isLeftButtonDown()||!prepare||started)return;
    if(e.getDistanceFromDragStart()<6&&!gesture)return;
    gesture=true;
    if(!requested) {
      requested=true;auto token=generation;auto safe=juce::Component::SafePointer<MidiDragButton>(this);
      juce::Component::beginDragAutoRepeat(40);
      prepare([safe,token](juce::StringArray ready){if(safe&&safe->active&&safe->generation==token)safe->files=std::move(ready);});
    }
    if(files.isEmpty())return;
    for(const auto& path:files)if(!juce::File(path).existsAsFile())return;
    started=true;setState(juce::Button::buttonNormal);juce::Component::beginDragAutoRepeat(0);
    auto safe=juce::Component::SafePointer<MidiDragButton>(this);
    auto launch=externalDrag; // OLE pumps messages; the source can be destroyed while dragging.
    if(!launch(files,this,[safe]{if(safe){safe->active=false;safe->setState(juce::Button::buttonNormal);}})&&safe) {
      safe->active=false;if(safe->failed)safe->failed();
    }
  }
  void mouseUp(const juce::MouseEvent& e) override {
    active=false;++generation;juce::Component::beginDragAutoRepeat(0);
    if(gesture)setState(juce::Button::buttonNormal); // Suppress the normal click after any attempted drag.
    juce::TextButton::mouseUp(e);
  }
  bool cancelPendingDrag() {
    if(!active||!gesture||started)return false;
    active=false;++generation;juce::Component::beginDragAutoRepeat(0);setState(juce::Button::buttonNormal);return true;
  }
private:
  juce::StringArray files;unsigned generation=0;bool gesture=false,requested=false,started=false,active=false;
};
