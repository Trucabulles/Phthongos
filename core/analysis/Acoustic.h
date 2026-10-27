#pragma once
#include "Dynamics.h"
namespace phthongos::analysis {
enum class AcousticVerdict { continuity, reattack, uncertain };
struct AcousticEvidence {
    AcousticVerdict verdict = AcousticVerdict::uncertain;
    double continuity = 0, rise = 0;
    std::optional<double> attackTime;
    std::string reason;
};
struct BoundaryQuery { int pitch; double time, gap; };
// Existing MIDI boundary evidence only. No note or audio modification.
AcousticEvidence acousticBoundary(const StereoPcm&, unsigned rate, int pitch,
                                  double time, double gap, const std::vector<Note>&);
std::vector<AcousticEvidence> acousticBoundaries(const StereoPcm&,unsigned rate,
                                                const std::vector<BoundaryQuery>&,const std::vector<Note>&);
}
