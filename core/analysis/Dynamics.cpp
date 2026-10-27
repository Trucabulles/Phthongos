#include "Dynamics.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace phthongos::analysis {
namespace {
constexpr double pi = 3.141592653589793238462643383279502884;
PartId program(const Note& n) { return n.part ? n.part->program : 0; }
double harmonic(const StereoPcm& pcm, unsigned rate, double start, double length,
                int pitch, const std::set<int>& competitors) {
    double result = 0;
    for (int h = 1; h <= 6; ++h) {
        const auto target = frequency(pitch) * h;
        double sharing = 0;
        for (auto p : competitors) if (p != pitch) {
            double partials = 0;
            for (int k = 1; k <= 6; ++k)
                if (std::abs(std::log(frequency(p) * k / target)) < .018) partials += 1. / k;
            sharing += partials;
        }
        result += band(pcm, rate, start, length, target) / (h * (1. + sharing * 2.));
    }
    return result;
}
double quantile(const std::vector<double>& values, double q) {
    return values.empty() ? 0. : values[static_cast<std::size_t>(std::round((values.size() - 1) * q))];
}
}
Fingerprint fingerprint(const Note& n) {
    return {n.id, n.pitch, n.start, n.end, program(n), n.part ? n.part->identity : std::optional<PartId>{}};
}
bool sameFingerprint(const Fingerprint& a, const Fingerprint& b) {
    return a.id == b.id && a.pitch == b.pitch && a.start == b.start && a.end == b.end &&
           a.program == b.program && a.part == b.part;
}
bool DynamicsCache::matches(const std::vector<Note>& notes) const {
    if (version != 1 || fingerprints.size() != notes.size()) return false;
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (!sameFingerprint(fingerprints[i], fingerprint(notes[i]))) return false;
    return true;
}
void DynamicsCache::validate() const {
    if (version > 1 || fingerprints.size() > 100000 || estimates.size() > 100000)
        throw std::invalid_argument("Mesures de dynamique trop volumineuses.");
    auto check = [](const Fingerprint& n) {
        if ((n.part && (*n.part < 256 || *n.part >= 32768)) || !n.id || n.pitch < 0 || n.pitch > 127 ||
            n.program > 128 || !std::isfinite(n.start) || !std::isfinite(n.end) || n.start < 0 || n.end <= n.start)
            throw std::invalid_argument("Mesure de dynamique invalide.");
    };
    for (const auto& f : fingerprints) check(f);
    for (const auto& e : estimates) {
        check(e.note);
        for (double x : {e.harmonic, e.attack, e.relative, e.score})
            if (!std::isfinite(x) || x < 0) throw std::invalid_argument("Score de dynamique invalide.");
        if (e.score > 1) throw std::invalid_argument("Score de dynamique invalide.");
    }
}
void validateMapping(const Mapping& m) {
    if (m.min < 1 || m.min > m.max || m.max > 127 || !std::isfinite(m.level) || m.level < m.min ||
        m.level > m.max || !std::isfinite(m.contrast) || m.contrast < 0 || m.contrast > 250)
        throw std::invalid_argument("Dynamique : niveau dans la plage min–max (1–127), contraste 0–250 %.");
}
int mappedVelocity(const Mapping& m, double score) {
    return static_cast<int>(std::clamp(std::round(m.level + (score - .5) * 80. * m.contrast / 100.),
                                       static_cast<double>(m.min), static_cast<double>(m.max)));
}
double frequency(int p) { return 440. * std::pow(2., (p - 69.) / 12.); }
double band(const StereoPcm& pcm, unsigned rate, double start, double length, double f) {
    if (f >= rate * .48) return 0.;
    const auto bounded = [&](double t) {
        const auto x = std::round(std::max(t, 0.) * rate);
        return x >= static_cast<double>(pcm.size()) ? pcm.size() : static_cast<std::size_t>(x);
    };
    const auto a = bounded(start), b = std::max(a, bounded(start + length));
    if (b - a < 16) return 0.;
    double best = 0;
    for (double tune : {.995, 1., 1.005}) {
        const auto omega = 2. * pi * f * tune / rate, coeff = 2. * std::cos(omega);
        std::array<double, 2> prev{}, prev2{};
        double weight = 0;
        for (std::size_t i = 0; i < b - a; ++i) {
            const auto w = .5 - .5 * std::cos(2. * pi * i / (b - a - 1));
            weight += w;
            for (int c = 0; c < 2; ++c) {
                const auto v = static_cast<double>(pcm[a + i][c]) * w + coeff * prev[c] - prev2[c];
                prev2[c] = prev[c]; prev[c] = v;
            }
        }
        double power = 0;
        for (int c = 0; c < 2; ++c)
            power += std::max(prev[c] * prev[c] + prev2[c] * prev2[c] - coeff * prev[c] * prev2[c], 0.);
        best = std::max(best, power / std::max(weight * weight, 1.));
    }
    return best;
}
DynamicsCache dynamics(const StereoPcm& pcm, unsigned rate, const std::vector<Note>& notes) {
    if (rate < 8000 || std::any_of(pcm.begin(), pcm.end(), [](const auto& frame) {
        return !std::isfinite(frame[0]) || !std::isfinite(frame[1]);
    })) throw std::invalid_argument("PCM invalide pour la dynamique.");
    DynamicsCache out;
    for (const auto& n : notes) {
        out.fingerprints.push_back(fingerprint(n));
        if (program(n) == 128) continue;
        const double onset = n.start;
        const auto length = std::min(std::clamp(8. / frequency(n.pitch), .045, .16),
                                     std::max(static_cast<double>(n.end) - onset, .012));
        std::set<int> competitors;
        for (const auto& m : notes)
            if (program(m) != 128 && m.start < onset + length && m.end > onset) competitors.insert(m.pitch);
        const auto post = harmonic(pcm, rate, onset + .005, length, n.pitch, competitors);
        const auto pre = onset > length + .005 ? harmonic(pcm, rate, onset - length - .005, length, n.pitch, competitors) : 0.;
        out.estimates.push_back({fingerprint(n), post, std::max(post - pre, 0.), 0., .5,
                                 post > 1e-10 && onset < static_cast<double>(pcm.size()) / rate});
    }
    std::vector<double> loudness;
    for (const auto& e : out.estimates) loudness.push_back(std::log(std::max(.7 * e.harmonic + .3 * e.attack, 1e-12)));
    for (std::size_t i = 0; i < out.estimates.size(); ++i) {
        auto& e = out.estimates[i];
        std::vector<double> distribution, peers;
        for (std::size_t j = 0; j < out.estimates.size(); ++j) {
            const auto& other = out.estimates[j];
            if (!other.usable || other.note.program != e.note.program) continue;
            distribution.push_back(loudness[j]);
            if (std::abs(other.note.start - e.note.start) < .07f) peers.push_back(loudness[j]);
        }
        std::sort(distribution.begin(), distribution.end());
        const auto median = quantile(distribution, .5), spread = std::max(quantile(distribution, .9) - quantile(distribution, .1), 1.);
        double sum = 0; for (auto p : peers) sum += p;
        const auto local = peers.size() > 1 ? loudness[i] - sum / peers.size() : 0.;
        e.relative = std::clamp(std::exp(local), 0., 1000.);
        e.score = std::clamp(.5 + .65 * (loudness[i] - median) / spread + .12 * std::clamp(local, -2., 2.), 0., 1.);
    }
    return out;
}
}
