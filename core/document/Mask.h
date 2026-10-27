#pragma once
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>
namespace phthongos {
struct MaskPoint {double time=0,pitch=60;};
struct MaskStroke {bool erase=false;double timeRadius=.05,pitchRadius=1;std::vector<MaskPoint> points;};
using SpectralMask=std::vector<std::shared_ptr<const MaskStroke>>;
constexpr unsigned allMaskParts=65535;
void validateMaskStroke(const MaskStroke&);
void validateMask(const SpectralMask&);
bool maskContains(const SpectralMask&,double time,double pitch);
}
