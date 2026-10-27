#include "ProjectStore.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace phthongos::services {
ProjectRead ProjectStore::read() const {
    if(!project_)throw std::invalid_argument("Aucun projet chargé.");auto& p=*project_;ProjectRead r;r.generation=generation_;r.kind=p.kind;r.view=p.editor.document.view(p.source);r.duration=p.audio.duration;
    for(auto& n:r.view.notes)r.duration=std::max(r.duration,static_cast<double>(n.end));return r;
}
ProjectRead ProjectStore::snapshot() const {std::lock_guard<std::mutex> lock(mutex_);return read();}
void ProjectStore::require(std::uint64_t generation,std::uint64_t revision) const {
    if(!project_||generation!=generation_)throw std::invalid_argument("Le projet a changé. Rechargez la sélection.");
    if(revision!=project_->editor.document.revision())throw std::invalid_argument("La transcription a changé. Rechargez la sélection.");
}
ProjectRead ProjectStore::open(const juce::File& file,const std::vector<std::string>& names,const std::string& runtime,std::uint64_t expected){
    auto prepared=project::prepare(project::read(file),names,runtime);
    std::lock_guard<std::mutex> lock(mutex_);if(generation_!=expected)throw std::invalid_argument("Le projet a changé pendant le chargement.");
    // All potentially failing decoding/validation is complete before adoption.
    project_=std::move(prepared);clean_.reset();++generation_;return read();
}
ProjectRead ProjectStore::beginEditing(std::uint64_t generation,std::uint64_t revision){
    std::lock_guard<std::mutex> lock(mutex_);require(generation,revision);auto& p=*project_;
    if(p.kind!=project::SourceKind::none)throw std::invalid_argument("Le projet a changé ou contient déjà un MIDI.");
    auto name=juce::File::getCurrentWorkingDirectory().getChildFile(p.container.metadata["source_name"].toString()).getFileNameWithoutExtension()+".mid";
    auto source=phthongos::midi::Source::blank(name.toStdString());
    juce::Array<juce::var> bytes;for(auto b:source.bytes)bytes.add((int)b);
    auto* object=new juce::DynamicObject;object->setProperty("created_empty",true);object->setProperty("source_path","");object->setProperty("name",name);object->setProperty("bytes",juce::var(bytes));
    auto staged=p.container;staged.metadata=staged.metadata.clone();staged.metadata.getDynamicObject()->setProperty("imported_midi",juce::var(object));staged.metadata.getDynamicObject()->setProperty("editor",project::writeEditor(project::Editor{}));
    auto candidate=project::prepare(std::move(staged),p.source.gmNames,p.container.metadata["runtime_sha256"].toString().toStdString());
    project_=std::move(candidate);clean_.reset();++generation_;return read();
}
ProjectRead ProjectStore::select(std::uint64_t generation,std::uint64_t revision,double sensitivity){
    std::lock_guard<std::mutex> lock(mutex_);require(generation,revision);
    if(!std::isfinite(sensitivity)||sensitivity<0||sensitivity>100)throw std::invalid_argument("La sensibilité doit être comprise entre 0 et 100 %.");
    if(project_->kind==project::SourceKind::legacy){
        float threshold=static_cast<float>(1.-sensitivity/100.);std::vector<Note> notes;
        for(auto& n:project_->source.allNotes)if(n.onsetProbability>=threshold)notes.push_back(n);project_->source.notes=std::move(notes);
    }
    return read();
}
ProjectRead ProjectStore::act(std::uint64_t generation,std::uint64_t revision,const Action& action){
    std::lock_guard<std::mutex> lock(mutex_);require(generation,revision);if(project_->kind==project::SourceKind::none)throw std::invalid_argument("Lancez d’abord une analyse.");
    auto previous=project_->editor.document.revision();project_->editor.document.act(project_->source,revision,action);
    if(previous!=project_->editor.document.revision())clean_.reset();return read();
}
void ProjectStore::cacheDynamics(std::uint64_t generation,std::uint64_t revision,const analysis::DynamicsCache& cache){
    cache.validate();auto encoded=project::writeDynamicsCache(cache);
    std::lock_guard<std::mutex> lock(mutex_);require(generation,revision);
    if(!cache.matches(project_->editor.document.view(project_->source).notes))
        throw std::invalid_argument("La session a changé pendant l’estimation.");
    project_->editor.dynamicsCache=std::move(encoded);
}
ProjectRead ProjectStore::applyDynamics(std::uint64_t generation,std::uint64_t revision,const std::vector<NoteId>& ids,const Mapping& mapping){
    std::lock_guard<std::mutex> lock(mutex_);require(generation,revision);auto& p=*project_;
    p.editor.document.applyDynamics(p.source,revision,ids,mapping,project::readDynamicsCache(p.editor.dynamicsCache));
    clean_.reset();return read();
}
void ProjectStore::previewClean(std::uint64_t generation,std::uint64_t revision,const juce::var& preview){
    auto staged=project::readCleanPreview(preview);
    std::lock_guard<std::mutex> lock(mutex_);require(generation,revision);
    if(!staged||staged->revision!=revision)throw std::invalid_argument("Recherche obsolète. Relancez la recherche de séries.");
    clean_=std::move(staged);
}
ProjectRead ProjectStore::applyClean(std::uint64_t generation,std::uint64_t revision,const std::vector<NoteId>& ids,double sensitivity){
    if(!std::isfinite(sensitivity)||sensitivity<0||sensitivity>100)throw std::invalid_argument("La sensibilité doit être comprise entre 0 et 100 %.");
    std::lock_guard<std::mutex> lock(mutex_);require(generation,revision);
    project_->editor.document.applyCleanRuns(project_->source,static_cast<float>(1.-sensitivity/100.),revision,ids,clean_);return read();
}
void ProjectStore::save(const juce::File& file,std::uint64_t generation,std::uint64_t revision,const juce::var& ui) const {
    std::lock_guard<std::mutex> lock(mutex_);require(generation,revision);auto& p=*project_;auto original=p.container.metadata["source_path"].toString();
    if(original.isNotEmpty()&&juce::File(original)==file)throw std::invalid_argument("Le projet ne peut pas remplacer son audio.");
    project::Container staged=p.container;staged.metadata=staged.metadata.clone();staged.metadata.getDynamicObject()->setProperty("editor",project::writeEditor(p.editor));staged.metadata.getDynamicObject()->setProperty("ui",ui.clone());staged.metadata.getDynamicObject()->setProperty("sensitivity",ui["sensitivity"]);
    // Validate the whole candidate again; a bad save request leaves the target
    // and the current project untouched. No history or preview is mutated.
    auto checked=project::prepare(std::move(staged),p.source.gmNames,p.container.metadata["runtime_sha256"].toString().toStdString());project::saveAtomic(file,checked.container);
}
std::vector<std::uint8_t> ProjectStore::midi(std::uint64_t generation,std::uint64_t revision) const {
    std::lock_guard<std::mutex> lock(mutex_);require(generation,revision);auto& p=*project_;auto notes=p.editor.document.view(p.source).notes;
    auto tempo=p.container.metadata["tempo_bpm"];double bpm=tempo.isVoid()||tempo.isUndefined()?0:double(tempo);
    return p.midi?p.midi->exportNotes(notes,p.source.gmNames,bpm):phthongos::midi::exportParts(notes,p.source.gmNames,bpm);
}
}
