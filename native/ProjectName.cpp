#include "App.h"

namespace {
bool validProjectName(const String& name) {
  if(name.isEmpty()||name.length()>120||name.containsAnyOf("\\/:*?\"<>|\r\n")||name.endsWithChar('.'))return false;
  for(auto c:name)if(c<32)return false;
  auto stem=name.upToFirstOccurrenceOf(".",false,false).toUpperCase();
  if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL")return false;
  for(int i=1;i<=9;++i)if(stem=="COM"+String(i)||stem=="LPT"+String(i))return false;
  return true;
}
}

String App::projectDisplayName() const {
  if(projectName.isNotEmpty())return projectName;
  if(projectSaveBase.isNotEmpty())return projectSaveBase;
  auto name=projectPath.isNotEmpty()?juce::File(projectPath).getFileNameWithoutExtension()
    :juce::File(audio["name"].toString()).getFileNameWithoutExtension();
  return name.isEmpty()?String("Phthongos"):name;
}
String App::midiExportName() const {
  auto name=juce::File::createLegalFileName(projectDisplayName()).substring(0,120).trim().trimCharactersAtEnd(" .");
  if(!validProjectName(name))name="Phthongos";
  return name;
}
void App::renameProject() {
  if(locked||audio.isVoid()||midiRecording||midiRecordPending||midiCounting)return;
  projectNameForm()->show(tr(String::fromUTF8("Renommer le projet…")));
}
Form* App::projectNameForm() {
  auto f=juce::Component::SafePointer<Form>(new Form);
  f->setHeading(tr(String::fromUTF8("Renommer le projet…")));
  f->setSize(600,320);
  f->actions(tr(String::fromUTF8("Renommer")),tr(String::fromUTF8("Annuler")));
  f->text(tr(String::fromUTF8("Nom du projet")),"name",projectDisplayName());
  f->label(tr(String::fromUTF8("Ce nom sert aux exports MIDI. Les fichiers existants ne sont pas renommés.")));
  auto* editor=f->textFields["name"];editor->setInputRestrictions(120);
  auto changed=editor->onTextChange;
  editor->onTextChange=[f,editor,changed]{if(changed)changed();if(f)f->setApplyEnabled(validProjectName(editor->getText().trim()));};
  f->setApplyEnabled(validProjectName(editor->getText().trim()));
  f->onApply=[s=safe(),f](var state){
    if(!s||s->locked||s->midiRecording||s->midiRecordPending||s->midiCounting)return;
    auto name=state["name"].toString().trim();
    if(!validProjectName(name)){s->status.setText(tr(String::fromUTF8("Nom de projet invalide.")),juce::dontSendNotification);return;}
    if(name!=s->projectDisplayName()){s->projectName=name;s->dirty=true;s->refresh();s->resized();}
    f->close();
  };
  return f.getComponent();
}
