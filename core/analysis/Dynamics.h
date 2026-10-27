#pragma once
#include "../document/Document.h"
#include <array>

namespace phthongos::analysis {
using StereoPcm = std::vector<std::array<float, 2>>;
struct DynamicsEstimate {
    Fingerprint note;
    double harmonic = 0, attack = 0, relative = 0, score = .5;
    bool usable = false;
};
struct DynamicsCache {
    unsigned version = 1;
    std::vector<Fingerprint> fingerprints;
    std::vector<DynamicsEstimate> estimates;
    bool matches(const std::vector<Note>&) const;
    void validate() const;
};
Fingerprint fingerprint(const Note&);
bool sameFingerprint(const Fingerprint&, const Fingerprint&);
void validateMapping(const Mapping&);
int mappedVelocity(const Mapping&, double score);
double frequency(int pitch);
double band(const StereoPcm&, unsigned rate, double start, double length, double hz);
DynamicsCache dynamics(const StereoPcm&, unsigned rate, const std::vector<Note>&);
}
