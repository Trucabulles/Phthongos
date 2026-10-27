#include "Acoustic.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace phthongos::services {
juce::var acousticEvidence(const analysis::AcousticEvidence& e){
    auto* out=new juce::DynamicObject;
    out->setProperty("verdict",e.verdict==analysis::AcousticVerdict::continuity?"continuity":e.verdict==analysis::AcousticVerdict::reattack?"reattack":"uncertain");
    out->setProperty("continuity",e.continuity);out->setProperty("rise",e.rise);
    out->setProperty("attack_time",e.attackTime?juce::var(*e.attackTime):juce::var());
    out->setProperty("reason",juce::String::fromUTF8(e.reason.c_str()));return juce::var(out);
}
juce::var verifyContinuity(const juce::var& preview,const analysis::StereoPcm& pcm,unsigned rate,const std::vector<Note>& notes){
    if(rate<8000||std::any_of(pcm.begin(),pcm.end(),[](auto& frame){return !std::isfinite(frame[0])||!std::isfinite(frame[1]);}))
        throw std::invalid_argument("Audio invalide pour la vérification acoustique.");
    if(!(bool)preview["midi_only"])throw std::invalid_argument("Cet essai utilise les séries MIDI MuScriptor.");
    auto result=preview.clone();auto* runs=result["runs"].getArray();auto* diagnostics=result["diagnostics"].getDynamicObject();
    if(!runs||!diagnostics||!result.isObject())throw std::invalid_argument("Recherche de séries invalide.");
    std::vector<analysis::BoundaryQuery> queries;
    for(const auto& run:*runs){
        auto* edges=run["boundaries"].getArray();if(!edges)throw std::invalid_argument("Recherche de séries invalide.");
        for(const auto& edge:*edges)queries.push_back({(int)edge["pitch"],static_cast<float>((double)edge["start"]),(double)edge["gap_ms"]/1000.});
    }
    const auto evidence=analysis::acousticBoundaries(pcm,rate,queries,notes);std::size_t index=0;
    juce::Array<juce::var> proposals;int proposed=0,review=0;
    for(auto& run:*runs){
        auto* edges=run["boundaries"].getArray();auto* ids=run["ids"].getArray();
        if(!edges||!ids||ids->isEmpty()||!run.isObject())throw std::invalid_argument("Recherche de séries invalide.");
        bool compatible=ids->size()>=3;
        for(auto& edge:*edges){
            if(!edge.isObject())throw std::invalid_argument("Recherche de séries invalide.");
            const auto& boundary=evidence[index++];
            edge.getDynamicObject()->setProperty("acoustic",acousticEvidence(boundary));
            compatible=compatible&&boundary.verdict==analysis::AcousticVerdict::continuity;
        }
        run.getDynamicObject()->setProperty("proposed",compatible);
        if(compatible){for(auto& edge:*edges){auto proposal=edge.clone();proposal.getDynamicObject()->setProperty("left",(*ids)[0]);proposals.add(proposal);}proposed+=edges->size();}
        else review+=edges->size();
    }
    diagnostics->setProperty("proposed",proposed);diagnostics->setProperty("review",review);
    result.getDynamicObject()->setProperty("after",(juce::int64)result["before"]-proposals.size());
    result.getDynamicObject()->setProperty("proposals",juce::var(proposals));return result;
}
}
