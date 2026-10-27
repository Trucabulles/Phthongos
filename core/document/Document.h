#pragma once
#include "Mask.h"
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace phthongos {
namespace analysis { struct DynamicsCache; }
using NoteId = std::uint64_t;
using PartId = std::uint16_t;
constexpr NoteId manualBase = NoteId{1} << 40;

struct Part {
    std::optional<PartId> identity;
    PartId program = 0;
    bool drum = false;
    std::string label;
    std::string soundfontPath,soundfontHash;unsigned soundfontBank=0,soundfontPreset=0;
    PartId key() const { return identity.value_or(program); }
};
enum class VelocitySource { detected, audioEstimated, manual };
struct Note {
    NoteId id = 0;
    std::optional<Part> part;
    int pitch = 60, velocity = 80;
    float start = 0, end = 1, velocityNormalised = 0, onsetProbability = 0;
    std::size_t startFrame = 0, endFrame = 0, frameDuration = 0;
    std::vector<std::int8_t> pitchBend;
    std::vector<NoteId> sourceIds;
    bool protectedNote = false, muted = false;
    VelocitySource velocitySource = VelocitySource::detected;
    PartId key() const { return part ? part->key() : 0; }
};
// A caller-owned immutable projection of the source at one threshold. The all-
// source projection is needed for stable IDs when the threshold hides a note.
struct SourceSnapshot {
    std::vector<Note> notes, allNotes;
    std::vector<std::string> gmNames;
    std::size_t sourceCount = 0; // Explicit candidate count when source IDs are sparse.
};
struct Mapping {
    double level = 80, contrast = 100;
    int min = 35, max = 120;
};
struct Fingerprint {
    NoteId id = 0;
    int pitch = 0;
    float start = 0, end = 0;
    PartId program = 0;
    std::optional<PartId> part;
};
struct AppliedDynamics {
    Fingerprint note;
    double score = 0;
    int velocity = 0;
    Mapping mapping;
    bool overrideManual = false;
};
struct Marker {std::uint64_t id=0;double time=0;std::string name;};
struct Layer {
    std::vector<Marker> markers;
    std::map<PartId,SpectralMask> masks;
    std::map<PartId, Part> parts;
    std::map<PartId, std::string> audioSources;
    std::map<PartId, unsigned> partColours;
    std::set<NoteId> muted;
    std::set<PartId> mutedParts;
    std::set<PartId> lockedParts;
    std::set<PartId> deletedParts;
    std::map<NoteId, AppliedDynamics> dynamics;
    std::optional<Mapping> dynamicsMapping;
    std::vector<std::vector<std::pair<NoteId, NoteId>>> automaticMerges;
    // Persistence-only description of each batch (scope/settings). The document
    // does not interpret JSON; the codec owns validation and representation.
    std::vector<std::string> automaticMetadata;
    std::map<NoteId, std::optional<Note>> manual;
};
struct View {
    std::vector<Marker> markers;
    std::map<PartId,SpectralMask> masks;
    std::vector<Note> notes;
    std::vector<Part> parts;
    std::map<PartId, std::string> audioSources;
    std::map<PartId, unsigned> partColours;
    std::set<NoteId> mutedNotes;
    std::set<PartId> mutedParts;
    std::set<PartId> lockedParts;
    std::optional<Mapping> dynamicsMapping;
    std::uint64_t revision = 0;
    std::size_t protectedRetained = 0;
    bool canUndo = false, canRedo = false, hasChanges = false;
};
struct MidiMapping {
    PartId source = 0;
    std::optional<PartId> target;
    std::optional<PartId> program;
    std::string name, audioSource;
};
struct Action {
    enum class Kind { add, modify, remove, restore, moveGroup, merge, split,
        extend, link, unlink, toggleMute, togglePartMute, transfer, newPart,
        createEmptyPart, setPartColour, setInstrument, togglePartLock, deletePart, addMarker, modifyMarker, deleteMarker, maskStroke, clearMask, pasteNotes, duplicateNotes, appendMidi, replaceVoiceNotes, setAudioSource, undo, redo, resetClean, resetDynamics };
    Kind kind = Kind::add;
    MaskStroke stroke;
    std::vector<NoteId> ids;
    NoteId id = 0;
    int pitch = 60, velocity = 80, semitones = 0;
    unsigned colour=0;
    bool keepName=false;
    std::string soundfontPath,soundfontHash;unsigned soundfontBank=0,soundfontPreset=0;
    PartId program = 0, partKey = 0;
    double start = 0, end = 1, at = 0, seconds = 0, overlapMs = 20;
    std::optional<Part> part;
    std::string name;
    std::optional<PartId> pastePart;
    std::vector<Note> imported;
    std::vector<MidiMapping> mappings;
};

// Search audit retained by the worker service, separate from the editable layer.
// Metadata contains the scope/settings encoded and validated by the project codec.
struct CleanRun { std::vector<NoteId> ids; bool proposed = false; };
struct CleanPreview {
    std::uint64_t revision = 0;
    float threshold = 0;
    std::size_t before = 0, after = 0;
    std::vector<CleanRun> runs;
    std::vector<std::pair<NoteId, NoteId>> proposals;
    std::string metadata;
};

// No JUCE, JSON, filesystem, audio thread, or global mutable state. This module
// owns the active editable state. History holds immutable layer snapshots.
class Document {
public:
    static constexpr std::size_t historyLimit = 25;
    struct State {
        Layer layer;
        std::vector<std::shared_ptr<const Layer>> undo, redo;
        std::uint64_t revision = 0;
        NoteId nextManual = 0;
    };
    Document();
    static Document restore(State, const SourceSnapshot&);
    State state() const { return {*layer_, undo_, redo_, revision_, nextManual_}; }
    View view(const SourceSnapshot&) const;
    void act(const SourceSnapshot&, std::uint64_t expectedRevision, const Action&);
    void actBatch(const SourceSnapshot&, std::uint64_t expectedRevision, const std::vector<Action>&, const std::optional<std::string>& newVoice = std::nullopt);
    void applyDynamics(const SourceSnapshot&, std::uint64_t expectedRevision,
                       const std::vector<NoteId>&, const Mapping&, const analysis::DynamicsCache&);
    void applyCleanRuns(const SourceSnapshot&, float threshold, std::uint64_t expectedRevision,
                        const std::vector<NoteId>&, std::optional<CleanPreview>&);
    double timelineDuration(double base) const;
    std::uint64_t revision() const { return revision_; }
    NoteId nextManual() const { return nextManual_; }
    const Layer& layer() const { return *layer_; }
    const std::vector<std::shared_ptr<const Layer>>& undoLayers() const { return undo_; }
    const std::vector<std::shared_ptr<const Layer>>& redoLayers() const { return redo_; }
private:
    std::shared_ptr<const Layer> layer_;
    std::vector<std::shared_ptr<const Layer>> undo_, redo_;
    std::uint64_t revision_ = 0;
    NoteId nextManual_ = 0;
    std::vector<Note> resolve(const SourceSnapshot&) const;
    std::vector<Part> parts(const SourceSnapshot&) const;
    std::set<PartId> reservedPartIds(const SourceSnapshot&) const;
    PartId freshPartId(const SourceSnapshot&) const;
    void execute(const SourceSnapshot&, const Action&);
    void checkpoint(std::shared_ptr<Layer>);
    void checkLockedNotes(const SourceSnapshot&, const Document&) const;
};
} // namespace phthongos
