#pragma once
#include <array>
#include <cmath>
#include <stdexcept>

namespace phthongos::midi {
inline void validateTempo(double bpm) {
    if (!std::isfinite(bpm) || bpm < 10 || bpm > 600)
        throw std::invalid_argument("Tempo invalide : 10 à 600 BPM.");
}
// Durations in quarter notes: whole, half, quarter, eighth, sixteenth, 32nd.
inline constexpr std::array<double, 6> rhythmBeats{4, 2, 1, .5, .25, .125};
inline double rhythmMilliseconds(double bpm, int value, int variant) {
    validateTempo(bpm);
    if (value < 0 || value >= int(rhythmBeats.size()) || variant < 0 || variant > 2)
        throw std::invalid_argument("Valeur rythmique invalide.");
    return 60000. / bpm * rhythmBeats[value] * (variant == 1 ? 1.5 : variant == 2 ? 2./3. : 1.);
}
struct RhythmMatch { int value=0, variant=0; double milliseconds=0, difference=0; };
inline RhythmMatch nearestRhythm(double bpm, double milliseconds) {
    validateTempo(bpm);
    if (!std::isfinite(milliseconds) || milliseconds <= 0)
        throw std::invalid_argument("Saisissez une durée positive en ms.");
    RhythmMatch best; double error=INFINITY;
    for (int value=0; value<int(rhythmBeats.size()); ++value) for (int variant=0; variant<3; ++variant) {
        auto duration=rhythmMilliseconds(bpm,value,variant), delta=milliseconds-duration;
        if(std::abs(delta)<error){error=std::abs(delta);best={value,variant,duration,delta};}
    }
    return best;
}
}
