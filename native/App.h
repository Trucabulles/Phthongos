#pragma once
#include "Roll.h"
#include "Widgets.h"
#include "MidiInput.h"
#include "StackedVoices.h"
#include "FileTransfer.h"
#include "../core/midi/TempoEstimate.h"
class App : public juce::Component,
            public juce::FileDragAndDropTarget,
            private juce::Timer,
            private juce::KeyListener {
public:
  App(juce::File testCache = {});
  std::function<void()> newProjectRequested;
  void runToneFlowTests(std::function<void(bool)>);
  void runVersionSaveTests(std::function<void(bool)>);
  void startEmptyProject(std::function<void(bool)> done={});
  void runEmptyMidiTests(std::function<void(bool)>);
  void runVoiceLayerTests(std::function<void(bool)>);
  void runStackedTests(std::function<void(bool)>);
  void runOptionalResourceTests(std::function<void(bool)>);
  void runBasicPitchTests(std::function<void(bool)>);
  void runFileTransferTests(std::function<void(bool)>);
  void runMidiInputTests(std::function<void(bool)>);
  ~App() override;
  void resized() override;
  void paint(juce::Graphics &) override;
  void paintOverChildren(juce::Graphics&) override;
  bool isInterestedInFileDrag(const juce::StringArray&) override;
  void fileDragEnter(const juce::StringArray&,int,int) override;
  void fileDragExit(const juce::StringArray&) override;
  void filesDropped(const juce::StringArray&,int,int) override;
  void mouseDown(const juce::MouseEvent &) override;
  void mouseDrag(const juce::MouseEvent &) override;
  void mouseDoubleClick(const juce::MouseEvent &) override;
  void requestClose(std::function<void()>);
  bool runSelfTests();
  void runRenderTests(std::function<void(bool)>);
  std::uint64_t uiPaints=0,uiRefreshes=0,playerPolls=0;
  void runWavExportTests(std::function<void(bool)>,bool useBank=false);
  static bool runLocalisationTests();
  static bool runDiagnosticsTests();
  void runGuidedTests(const juce::File&,std::function<void(bool)>);
  bool runProjectTransitionTest(const juce::File&);
  bool runProjectCompatibilityTest(const juce::File&);
  void runSaveCloseTests(const juce::File&,std::function<void(bool)>);
  void openAudioPath(const String &);
  void runInstanceTests(const juce::File&,const juce::File&,const juce::File&,std::function<void(bool)>);
  void runFlowTests(std::function<void(bool)>);
  static bool runInputQueueTests();

private:
  var guidedReport;std::set<String> guidedSelected;bool guidedRunning=false,guidedUseMask=true,guidedShowCurve=true;String guidedZone="mask";int guidedPage=0;bool guidedUseScale=false,guidedShowProposals=true;std::set<String> guidedCategories{"probable","review"};
  std::vector<var> visibleGuidedNotes() const;void updateGuidedView();
  void ensureGuidedEditor(std::function<void()>);
  void setMaskTool(int);void commitMask(const phthongos::MaskStroke&);void updateMaskView();void invalidateGuided();
  void buildGuidedPanel();void analyzeGuided();void installGuided(var);void focusGuided(const String&);void acceptGuided(const std::set<String>&);void ignoreGuided(const std::set<String>&);
  std::unique_ptr<MidiInputController> midiInput;
  juce::ToggleButton midiStep{tr(String::fromUTF8("Saisie pas à pas"))};juce::ComboBox midiLength;
  juce::TextButton midiStepTool{tr(String::fromUTF8("Pas à pas…"))};
  juce::Component::SafePointer<Form> midiStepForm;void midiStepOptions();void advanceMidiStep();
  juce::TextButton midiRecord;
  bool clickEnabled=false,midiCounting=false,tempoPreview=false;double clickDb=-12,previewPosition=0;int preCountBars=1;
  std::map<unsigned,std::pair<int,int>> physicalMidi,countHeld;
  double countPulseSeconds=.5;int countMeter=4;double previewBpm=0;
  BeatIndicator beatIndicator;juce::Label countOverlay;void updateClickVisuals();
  void syncClick(double candidate=0);void clickOptions();void finishMidiCountIn();void previewTempo(double);void stopTempoPreview();
  bool midiRecording=false,midiRecordPending=false;double midiClockTime=0,midiClockPosition=0;
  int midiRecordPart=-1;double midiCaptureStartTime=0;var midiRecordAudio;std::vector<Note> midiRecorded;std::map<unsigned,Note> midiHeld;
  void startMidiRecording();void stopMidiRecording(std::function<void()> done={});
  double midiEventTime(double)const;void recordMidiEvent(const juce::MidiMessage&);void updateMidiPreview();
  void updateMidiClock(const var&);void updateTakeDuration();
  var midiTempoEstimate;var midiEstimateAudio;std::vector<phthongos::midi::TempoOnset> midiTakeStarts;
  bool midiEstimatePending=false;
  bool hasMidiTempoEstimate()const{return !midiTempoEstimate.isVoid()&&midiEstimateAudio==audio["id"]&&number(midiTempoEstimate,"bpm")>0;}
  void estimateMidiTake();void addMidiTempoEstimate(Form*,var);String midiTempoUnavailable()const;
  struct StepChord{var audio;int part;double start,duration;std::map<unsigned,std::pair<int,int>> notes;};
  std::optional<StepChord> midiChord,midiInFlight;std::deque<StepChord> midiChords;std::set<unsigned> midiDown;
  double midiNext=-1;bool midiCommitting=false;unsigned midiGeneration=0;
  void initialiseMidiInput();void handleMidiInput(const juce::MidiMessage&);void resetMidiInput(bool disarm=false);void commitMidiChord();void refreshMidiInput();
  double midiStepBeats()const;void midiSettings(Form*);void queueMidiChord();
  Bridge bridge;
  Roll roll;
  std::unique_ptr<StackedVoices> stacked;bool stackedView=false;
  juce::ComboBox voicePresentation,voiceFraming;juce::TextButton transferVoices;
  double nextPlayerPoll=0;
  void applyPlayerSnapshot(var);
  void initialiseStacked();void refreshStacked();void layoutStacked();
  Theme theme;
  juce::ComponentDragger windowDragger;
  juce::TooltipWindow tooltips{this};
  juce::Component::SafePointer<juce::CallOutBox> helpPopup;
  void positionHelp();
  juce::Viewport sideView;
  SidePanel side;
  juce::Label title, status, device, position, inspector, modelCaption,
      calculation, editorBadge;
  juce::ComboBox toneRoot, toneMode, toneScope;
  juce::Label toneCaption, toneHelp;
  juce::OwnedArray<juce::TextButton> toneSuggestions;
  juce::ComboBox model, part, displayMode, audioLayer, gridMode, gridSubdivision;
  juce::Label voiceSourceLabel;juce::TextButton voiceSourceReload;
  String voiceSourcePath(int)const;void reloadVoiceAudio(int);void refreshVoiceSource();
  int pitchVoiceTarget()const;
  std::unique_ptr<juce::Component> pitchControls;
  bool pitchControlsExpanded=true;
  int pitchBandHeight() const;void refreshPitchBand();
  unsigned pitchControlsGeneration=0;bool pitchControlsPending=false;
  void initialisePitchControls();void syncPitchControls();void pitchControlsChanged(var);
  void applyPitchControls(var);
  VisibleCursorSlider opacity, visualGain;
  juce::Rectangle<int> toolbarBounds, gridCaptionBounds, zoomCaptionBounds, opacityCaptionBounds, gainCaptionBounds, registerCaptionBounds;
  double waveOpacity=.2, waveVisualGain=4.9;
  var spectrumData, spectralBands=object({});
  int spectralPalette=0,spectralStyle=0;
  double spectralContrast=100;
  void spectralOptions();
  void spectralBandMenu(juce::Point<int>,int);
  void recolourSpectrum();
  void saveSpectralBand();
  void loadSpectralBand();
  int spectrogramVersion = 0;
  bool spectrumPending = false;
  String spectrumKey, spectrumWanted, spectrumAudio;
  double spectrumChangedAt=0;
  VisibleCursorSlider zoom;ListeningVolumeSlider audioVolume,midiVolume,voiceVolume;
  var voiceLevels=object({});bool voiceLevelPending=false,voiceLevelDirty=false;
  void syncVoiceLevels();
  juce::ToggleButton follow{tr(String::fromUTF8("Suivre la lecture"))};
  juce::TextButton others{tr(String::fromUTF8("Afficher les autres parties"))};
  juce::OwnedArray<juce::TextButton> buttons;
  std::map<String, juce::TextButton *> buttonMap;
  juce::Image logo;
  std::unique_ptr<juce::Component> initialisationPopup;
  void syncInitialisationPopup();
  var audio, result, playback, engine, bank, verification, clean, dynamics,
      toneReport, shortReport;
  var dynMapping =
          object({{"level", 80}, {"contrast", 100}, {"min", 35}, {"max", 120}}),
      tone = object({{"scale", object({{"tonic", 0}, {"mode", "major"}})},
                     {"scope", "all"},
                     {"ids", var()},
                     {"enabled", false}});
  var shortState = object({{"threshold_ms", 100},
                           {"scope", "all"},
                           {"ids", var()},
                           {"enabled", false}});
  String projectPath, projectSaveBase, projectSaveFolder, defaultProjectFolder, projectName;
  String projectIdentity=juce::Uuid().toString();
  std::vector<var> recentProjects;
  void loadRecentProjects();bool storeRecentProjects();void rememberProject(const String&);
  bool timestampSaves=false;
  bool settingsOpening=false;juce::Component::SafePointer<Form> settingsForm;
  juce::File savePreferencesFile;
  void loadSavePreferences();
  void storeSavePreferences(bool,const String&);
  static juce::File versionedProjectPath(const juce::File&,const String&,juce::Time);
  String projectSaveSuggestion(const var&) const;
  int partId = -1, mode = 0, scope = 0;
  bool toneExpanded = true;
  std::set<std::int64_t> hiddenParts;
  int legacyVisiblePart=-1;
  bool locked = false, dirty = false, polling = false,
       settingsPolling = false, reverbEnabled = false, manualRange = false,
       hasDynDraft = false, correctionOpen = true, tonalityOpen = false;
  double reverbAmount = 50, audioDb = 0, midiDb = 0, listenEnd = -1;
  std::uint64_t notesVersion = 0, detailVersion = 0;
  bool notesReady = false;var preparedAudioId;
  int timerCount = 0, verificationPage = 0, missingIndex = 0, cleanIndex = 0,
      shortIndex = 0, dynamicsPage = 0, overlapsPage = 0;
  double rangeStart = 0, rangeEnd = 6;
  std::map<String,int> diagnosticPages;
  String verifyScope = "interval", cleanScope = "all";
  var cleanSettings =
      object({{"gap_ms", 30}, {"sustain_min", .3}, {"velocity_delta", 18}});
  bool acoustic = false;
  std::set<String> openSections, ignored;
  std::set<std::int64_t> kept, cleanKept;
  String cleanFilter = "proposed";
  bool verificationStale = false;
  std::vector<std::pair<int, String>> parts;
  std::unique_ptr<juce::FileChooser> chooser;
  std::function<juce::File(const String&)> testSaveDestination;
  std::unique_ptr<ConfirmOverlay> confirmation;
  void ask(const String &, const String &, const String &, const String &,
           std::function<void(int)>);
  using Done = std::function<void(var)>;
  void invoke(const String &, var, Done = {}, bool heavy = false,
              std::function<void(String)> failed = {});
  var args() const;
  void updateResult(var, bool prepare = false, bool synchronize = true);
  void rebuildNotes();
  void refresh();
  void rebuildSide();
  void setBusy(bool, const String &message = {});
  void edited();
  void timerCallback() override;
  bool keyPressed(const juce::KeyPress &, juce::Component *) override;
  bool keyStateChanged(bool, juce::Component *) override;
  std::set<int> pressedKeys;
  bool transportPending = false;
  void command(const var &, Done = {}, bool preserveReview = false);
  void control(const String &, double = 0, Done = {});
  void goBoundary(bool end);
  void prepare(Done = {});
  void syncNotes(bool initialise = false, Done = {});
  void audition(const Note &);
  void selectPart(int);
  void context(juce::Point<int>, var);
  void editNote(const Note &);
  void addNote(double, int);
  void removeNotes(std::set<std::int64_t>);
  void mergeNotes(std::set<std::int64_t>);
  bool canMerge(const std::set<std::int64_t> &) const;
  void fileMenu();
  String projectDisplayName() const;
  String midiExportName() const;
  void renameProject();
  Form* projectNameForm();
  juce::PopupMenu projectMenu();
  void choose(const String &, bool, const String &, std::function<void(String)>,
              String = {}, std::function<void()> cancelled = {});
  void permitReplace(std::function<void()>,std::function<void()> cancelled = {});
  void confirm(const String &, const String &, std::function<void(bool)>);
  void openAudio();
  void importMidi(const String& path={});
  void appendMidi(const String& path={});
  bool fileDropHover=false;
  void prepareMidiDrag(MidiDragButton::Ready);
  void showMidiAddition(var preview,var baseArgs);
  Form* midiAdditionForm(var preview,var baseArgs);
  void associateAudio(const String& path = {},int sourcePart=-1);
  void openProject(String path = {});
  void saveProject(bool as = false, std::function<void(bool)> done = {});
  void saveProjectTo(const String&,std::function<void(bool)>,bool versioned=false);
  void chooseProjectDestination(const String&,std::function<void(bool)>,bool versioned=false);
  void projectSaveFailed(const String&,std::function<void(bool)>);
  var ui() const;
  void loadUi(const var &);
  void exportMidi();
  Form* midiExportForm();
  void exportMidiTo(const String&,var);
  void exportMidiVoicesTo(const String&);
  void exportWav();
  void analyze();
  void refreshTranscription();
  void updateTranscription(var);

  void restoreTranscription(const var&,const var&);

  void settings();
  void addThirdPartySettings(juce::Component::SafePointer<Form>);
  void addCustomSoundfonts(juce::Component::SafePointer<Form>);
  void customInstrument(int);
  void tempoOptions();
  int tempoRefreshVersion=0;
  void refreshTempo();
  void loadGridUi(const var&);
  Form* tempoForm(var);
  void applyLanguage(localisation::Language);
  void addSoundbankSettings(juce::Component::SafePointer<Form>);
  void partDialog(bool,int target=-1);
  Form* voiceForm(bool create);void showVoiceForm(bool create);
  void manualDialog();
  void markerDialog(var,double);
  var noteClipboard;
  void copyNotes();
  void pasteNotes();
  void information();void openManual();juce::File manualFile() const;
  void audioCalculation(const String &, std::function<void()>);
  void estimateDynamics(bool force=false);
  void restoreDynamics();
  void suggestRange();
  void applyDynamics();
  int velocity(double) const;
  void compareDynamics();
  void verificationOptions();
  void verify();
  void updateDiagnostics();
  double linkOverlapMs = 20;
  void linkNotes(std::set<std::int64_t>, bool unlink = false);
  void linkOptions();
  String diagnosticActive;
  void navigateDiagnostic(const String &, const var &);
  void applyDiagnostic(const String &, const var &);
  var diagnosticDestinations() const;
  var diagnosticBatchPlan(const String &);
  void applyDiagnosticBatch(const String &);
  void listen(double, double, int);
  void locate(const var &);
  void cleanOptions();
  void searchClean();
  void applyClean(const std::vector<var> &);
  void toneOptions();
  void analyzeTone(bool, std::function<void()> done = {});
  struct ToneVoice {int key;String name;std::vector<Note> notes;};
  std::vector<ToneVoice> toneVoices() const;
  void selectToneNotes(int voice=-1);
  void focusToneNote(std::int64_t);
  Form* toneDetailsForm(int page=0);
  int toneVersion=0;
  void findShort(bool reuseScope = false);
  juce::TextButton &button(const String &, const String &,
                           std::function<void()>);
  String reportText;
  String operationLabel;
  String completedCalculation;
  double calculationStarted = 0;bool initialisingProject=false;
  bool closing = false, sidePending = false, referenceAudio = false,
       seekInFlight = false;
  double pendingSeek = -1;
  String presenceFilter = "probably_absent";
  void buildSide();
  juce::Component::SafePointer<App> safe() { return this; }
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(App)
};
