#pragma once
#include <juce_core/juce_core.h>
namespace localisation {
enum class Language { french, english };
Language language();
void setLanguage(Language);
juce::String code();
void initialise(const juce::File& applicationFolder);
void saveChoice(const juce::File& applicationFolder,const juce::String& code);
juce::String savedChoice(const juce::File& applicationFolder);
juce::String text(const juce::String& source);
juce::String retranslate(const juce::String& displayed,Language previous);
juce::String error(const juce::String& source);
juce::String format(const juce::String& source,std::initializer_list<juce::String> values);
juce::String count(const juce::String& key,int number);
juce::var report(juce::var);
int catalogSize();
}
inline juce::String tr(const juce::String& source){return localisation::text(source);}
