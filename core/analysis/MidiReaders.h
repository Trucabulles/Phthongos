#pragma once
#include "../document/Document.h"
#include <array>
namespace phthongos::analysis {
enum class Mode { major, minor, harmonicMinor, melodicMinor, dorian, phrygian,
                  lydian, mixolydian, locrian, majorPentatonic, minorPentatonic, chromatic };
struct Scale { int tonic=0;Mode mode=Mode::major;bool contains(int pitch) const; };
struct Suggestion { Scale scale;double score; };
struct Tonality { Scale scale;std::vector<NoteId> examined, outside;std::vector<Suggestion> suggestions;bool ambiguous=false; };
struct ShortHit { NoteId id;double durationMs;bool under50,protectedNote; };
struct ShortNotes { double thresholdMs;std::vector<NoteId> examined;std::vector<ShortHit> hits; };
std::vector<NoteId> scope(const std::vector<Note>&,const std::optional<std::vector<NoteId>>&,std::optional<PartId>);
Tonality tonal(const std::vector<Note>&,const std::optional<std::vector<NoteId>>&,std::optional<Scale>);
ShortNotes shortNotes(const std::vector<Note>&,const std::optional<std::vector<NoteId>>&,double thresholdMs);
} // namespace phthongos::analysis
