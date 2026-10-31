#include "App.h"
class Application : public juce::JUCEApplication {
  struct Window : juce::DocumentWindow {
    bool replacing=false,wasFullScreen=false;
    void installContent(juce::File cache,bool resize) {
      auto* app=new App(cache);
      app->newProjectRequested=[s=juce::Component::SafePointer<Window>(this),cache]{
        if(!s||s->replacing)return;s->replacing=true;
        juce::MessageManager::callAsync([s,cache]{if(!s)return;s->clearContentComponent();s->installContent(cache,false);static_cast<App*>(s->getContentComponent())->startEmptyProject();s->replacing=false;s->toFront(true);});
      };
      setContentOwned(app,resize);
    }
    Window(juce::File cache = {})
        : DocumentWindow(juce::String::fromUTF8("Phthongos"),
                         juce::Colour(0xfff9faf4), allButtons) {
      setUsingNativeTitleBar(false);
      setTitleBarHeight(0);
      installContent(cache,true);
      setResizable(true, false);
      auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
      setResizeLimits(std::min(1160, area.getWidth()), std::min(800, area.getHeight()), 8192, 8192);
      centreWithSize(std::min(1400, area.getWidth()), std::min(900, area.getHeight()));
      setVisible(true);
    }
    void recoverPointer(){
      if(!isActiveWindow())return;
      restorePointerAfterInterruption(true);
      juce::Timer::callAfterDelay(60,[safe=juce::Component::SafePointer<Window>(this)]{
        if(safe&&safe->isActiveWindow())restorePointerAfterInterruption(true);
      });
    }
    void resized() override {
      juce::DocumentWindow::resized();
      bool full=isFullScreen();if(full!=wasFullScreen){wasFullScreen=full;recoverPointer();}
    }
    void activeWindowStatusChanged() override {
      juce::DocumentWindow::activeWindowStatusChanged();
      recoverPointer();
    }
    void closeButtonPressed() override {
      static_cast<App *>(getContentComponent())->requestClose([] {
        juce::JUCEApplication::getInstance()->quit();
      });
    }
  };
  Theme theme;
  std::unique_ptr<Window> window;
  std::unique_ptr<App> flowTest;

public:
  const juce::String getApplicationName() override { return "Phthongos"; }
  const juce::String getApplicationVersion() override { return "0.1.1"; }
  bool moreThanOneInstanceAllowed() override { return true; }
  void initialise(const juce::String &rawCommand) override {
    phthongos::diagnostics::start(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("data/diagnostics"));
    auto parameters=getCommandLineParameterArray();
    localisation::initialise(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory());
    const int languageIndex=parameters.indexOf("--language");
    if(languageIndex>=0) {
      if(languageIndex+1>=parameters.size()){setApplicationReturnValue(2);quit();return;}
      auto selected=parameters[languageIndex+1];
      if(selected!="fr"&&selected!="en"){setApplicationReturnValue(2);quit();return;}
      localisation::setLanguage(selected=="en"?localisation::Language::english:localisation::Language::french);
      parameters.removeRange(languageIndex,2);
    }
    auto command=parameters.joinIntoString(" ");
    juce::ignoreUnused(rawCommand);
    juce::LookAndFeel::setDefaultLookAndFeel(&theme);
    if(parameters.size()==4&&parameters[0]=="--instance-test"){
      auto output=juce::File(parameters[3]);window=std::make_unique<Window>(output.getParentDirectory().getChildFile("shared-cache/audio"));
      static_cast<App*>(window->getContentComponent())->runInstanceTests(juce::File(parameters[1]),juce::File(parameters[2]),output,[this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command=="--diagnostics-test"){setApplicationReturnValue(App::runDiagnosticsTests()?0:1);quit();return;}
    if(command=="--input-queue-test"){setApplicationReturnValue(App::runInputQueueTests()?0:1);quit();return;}
    if(command=="--localisation-test"){setApplicationReturnValue(App::runLocalisationTests()?0:1);quit();return;}
    if(command=="--wav-bank-test"){
      flowTest=std::make_unique<App>();flowTest->runWavExportTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();},true);return;
    }
    if(command=="--wav-export-test"){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-wav-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);flowTest->runWavExportTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command.startsWith("--guided-test ")){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-guided-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);
      flowTest->runGuidedTests(juce::File(command.fromFirstOccurrenceOf(" ",false,false).unquoted()),[this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command.startsWith("--save-close-test ")){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-save-close-cache-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);
      flowTest->runSaveCloseTests(juce::File(command.fromFirstOccurrenceOf(" ",false,false).unquoted()),[this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command.startsWith("--project-compatibility-test ")){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-compat-cache-"+juce::Uuid().toString()).getChildFile("audio");
      App test(cache);
      setApplicationReturnValue(test.runProjectCompatibilityTest(juce::File(command.fromFirstOccurrenceOf(" ",false,false).unquoted()))?0:1);
      quit();return;
    }
    if(command.startsWith("--project-test ")){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-project-test-"+juce::Uuid().toString()).getChildFile("audio");
      App test(cache);
      setApplicationReturnValue(test.runProjectTransitionTest(juce::File(command.fromFirstOccurrenceOf(" ",false,false).unquoted()))?0:1);
      quit();return;
    }
    if(command=="--midi-input-test"||command=="--stacked-midi-input-test"){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-midi-input-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);flowTest->runMidiInputTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command=="--empty-midi-test"){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-empty-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);flowTest->runEmptyMidiTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command=="--optional-resources-test"){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-optional-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);flowTest->runOptionalResourceTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command=="--render-test"){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-render-"+juce::Uuid().toString()).getChildFile("audio");
      window=std::make_unique<Window>(cache);static_cast<App*>(window->getContentComponent())->runRenderTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command=="--basic-pitch-test"){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-basic-pitch-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);flowTest->runBasicPitchTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command=="--file-transfer-test"){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-file-transfer-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);flowTest->runFileTransferTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command=="--stacked-test"){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-stacked-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);flowTest->runStackedTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command=="--voice-layer-test"){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-layer-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);flowTest->runVoiceLayerTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command=="--save-version-test"){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-version-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);flowTest->runVersionSaveTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if(command=="--tone-test"){
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-tone-"+juce::Uuid().toString()).getChildFile("audio");
      flowTest=std::make_unique<App>(cache);flowTest->runToneFlowTests([this](bool ok){setApplicationReturnValue(ok?0:1);quit();});return;
    }
    if (command == "--flow-test") {
      auto cache =
          juce::File::getSpecialLocation(juce::File::tempDirectory)
              .getChildFile("phthongos-flow-" + juce::Uuid().toString())
              .getChildFile("audio");
      flowTest = std::make_unique<App>(cache);
      flowTest->runFlowTests([this](bool ok) {
        setApplicationReturnValue(ok ? 0 : 1);
        quit();
      });
      return;
    }
    if (command.contains("--self-test")) {
      auto cache=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("phthongos-self-test-"+juce::Uuid().toString()).getChildFile("audio");
      App test(cache);
      setApplicationReturnValue(test.runSelfTests() ? 0 : 1);
      quit();
      return;
    }
    window = std::make_unique<Window>();
    if (command.startsWith("--validation-audio "))
      static_cast<App *>(window->getContentComponent())
          ->openAudioPath(
              command.fromFirstOccurrenceOf(" ", false, false).unquoted());
    else static_cast<App*>(window->getContentComponent())->startEmptyProject();
  }
  void shutdown() override {
    phthongos::diagnostics::suspendGuiHeartbeat();
    {phthongos::diagnostics::Span trace("application/shutdown");
    window.reset();
    flowTest.reset();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);}
    phthongos::diagnostics::stop();
  }
  void systemRequestedQuit() override {
    if (window)
      window->closeButtonPressed();
  }
};
START_JUCE_APPLICATION(Application)
