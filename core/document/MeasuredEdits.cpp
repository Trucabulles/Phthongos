#include "Document.h"
#include "../analysis/Dynamics.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace phthongos {
void Document::applyDynamics(const SourceSnapshot& source, std::uint64_t expected,
                             const std::vector<NoteId>& ids, const Mapping& mapping,
                             const analysis::DynamicsCache& cache) {
    analysis::validateMapping(mapping);
    if (expected != revision_) throw std::invalid_argument("Notes modifiées. Recalculez la dynamique.");
    const auto notes = resolve(source);
    if (!cache.matches(notes)) throw std::invalid_argument("Estimez la dynamique depuis l’audio avant de l’appliquer.");
    const std::set<NoteId> chosen(ids.begin(), ids.end());
    if (chosen.empty() || chosen.size() != ids.size())
        throw std::invalid_argument("Aucune note admissible ou sélection dupliquée.");
    std::vector<AppliedDynamics> applied;
    for (auto id : chosen) {
        auto n = std::find_if(notes.begin(), notes.end(), [id](const auto& note) { return note.id == id; });
        if (n == notes.end()) throw std::invalid_argument("Note obsolète.");
        if(layer_->lockedParts.count(n->key()))continue;
        if (n->part && n->part->program == 128)
            throw std::invalid_argument("Note protégée ou percussion : dynamique conservée.");
        const auto fp = analysis::fingerprint(*n);
        auto e = std::find_if(cache.estimates.begin(), cache.estimates.end(), [&](const auto& value) {
            return value.usable && analysis::sameFingerprint(value.note, fp);
        });
        if (e == cache.estimates.end()) throw std::invalid_argument("Mesure acoustique indisponible pour cette note.");
        applied.push_back({e->note, e->score, analysis::mappedVelocity(mapping, e->score), mapping, n->protectedNote});
    }
    if(applied.empty())throw std::invalid_argument("Aucune note admissible ou sélection dupliquée.");
    auto next = std::make_shared<Layer>(*layer_);
    next->dynamicsMapping = mapping;
    for (const auto& d : applied) next->dynamics[d.note.id] = d;
    auto candidate=*this;candidate.layer_=next;checkLockedNotes(source,candidate);
    checkpoint(std::move(next));
}
void Document::applyCleanRuns(const SourceSnapshot& source, float threshold, std::uint64_t expected,
                              const std::vector<NoteId>& ids, std::optional<CleanPreview>& preview) {
    if (!preview) throw std::invalid_argument("Recherchez les séries avant de les fusionner.");
    if (expected != revision_ || preview->revision != revision_ ||
        std::memcmp(&preview->threshold, &threshold, sizeof(float)) != 0)
        throw std::invalid_argument("Recherche obsolète. Relancez la recherche de séries.");
    const std::set<NoteId> chosen(ids.begin(), ids.end());
    if (chosen.empty() || chosen.size() != ids.size() || std::any_of(chosen.begin(), chosen.end(), [&](auto id) {
        return std::none_of(preview->runs.begin(), preview->runs.end(), [&](const auto& run) {
            return run.proposed && !run.ids.empty() && run.ids.front() == id;
        });
    })) throw std::invalid_argument("Choisissez uniquement des séries proposées par cette recherche.");
    auto remaining = *preview;
    std::vector<std::pair<NoteId, NoteId>> merges;
    for (const auto& edge : remaining.proposals) if (chosen.count(edge.first)) merges.push_back(edge);
    if (merges.size() > remaining.before || remaining.proposals.size() > remaining.before)
        throw std::invalid_argument("Recherche de séries invalide.");
    auto next = std::make_shared<Layer>(*layer_);
    next->automaticMerges.push_back(merges);
    next->automaticMetadata.push_back(remaining.metadata);
    remaining.revision = revision_ + 1;
    remaining.before -= merges.size();
    remaining.proposals.erase(std::remove_if(remaining.proposals.begin(), remaining.proposals.end(),
        [&](auto edge) { return chosen.count(edge.first) > 0; }), remaining.proposals.end());
    remaining.runs.erase(std::remove_if(remaining.runs.begin(), remaining.runs.end(),
        [&](const auto& run) { return !run.ids.empty() && chosen.count(run.ids.front()); }), remaining.runs.end());
    remaining.after = remaining.before - remaining.proposals.size();
    // Prepare both new values before changing either live document or preview.
    std::optional<CleanPreview> updated;
    if (!remaining.runs.empty()) updated = std::move(remaining);
    auto candidate=*this;candidate.layer_=next;checkLockedNotes(source,candidate);
    checkpoint(std::move(next));
    preview = std::move(updated);
}
}
