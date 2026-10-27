#pragma once
#include "../project/Prepared.h"
#include <mutex>
namespace phthongos::services {
struct ProjectRead {
    std::uint64_t generation=0;
    project::SourceKind kind=project::SourceKind::none;
    View view;
    double duration=0;
};
// Worker-thread owner of a C++ project. Readers receive value snapshots, never a
// mutable Document, JSON tree or audio callback. The app switches to this owner
// only after all remaining command adapters are ready.
class ProjectStore {
public:
    ProjectRead snapshot() const;
    ProjectRead open(const juce::File&,const std::vector<std::string>& gmNames,
                     const std::string& runtimeHash,std::uint64_t expectedGeneration);
    ProjectRead beginEditing(std::uint64_t generation,std::uint64_t revision);
    ProjectRead select(std::uint64_t generation,std::uint64_t revision,double sensitivity);
    ProjectRead act(std::uint64_t generation,std::uint64_t revision,const Action&);
    void cacheDynamics(std::uint64_t generation,std::uint64_t revision,const analysis::DynamicsCache&);
    ProjectRead applyDynamics(std::uint64_t generation,std::uint64_t revision,
                              const std::vector<NoteId>&,const Mapping&);
    void previewClean(std::uint64_t generation,std::uint64_t revision,const juce::var&);
    ProjectRead applyClean(std::uint64_t generation,std::uint64_t revision,
                           const std::vector<NoteId>&,double sensitivity);
    void save(const juce::File&,std::uint64_t generation,std::uint64_t revision,const juce::var& ui) const;
    std::vector<std::uint8_t> midi(std::uint64_t generation,std::uint64_t revision) const;
private:
    mutable std::mutex mutex_;
    std::optional<project::Prepared> project_;
    std::uint64_t generation_=0;
    std::optional<CleanPreview> clean_;
    ProjectRead read() const;
    void require(std::uint64_t,std::uint64_t) const;
};
}
