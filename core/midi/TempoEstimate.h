#pragma once
#include <vector>
namespace phthongos::midi {
struct TempoOnset {double start=0;int pitch=60,velocity=90;};
struct TempoCandidate {double bpm=0,score=0;};
struct TempoEstimate {
  double bpm=0,support=0,seconds=0;unsigned attacks=0;const char* reason="uncertain";
  std::vector<TempoCandidate> candidates;
  std::vector<double> histogram; // relative weights in beat-duration order
  double shortestSeconds=0;unsigned sampleRate=450;
};
TempoEstimate estimateTempo(const std::vector<TempoOnset>&);
// Convenience for timing-only callers; live capture passes original pitch/velocity.
TempoEstimate estimateTempo(const std::vector<double>&);
}
