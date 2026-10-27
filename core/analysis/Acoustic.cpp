#include "Acoustic.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace phthongos::analysis {
namespace {
bool melodic(const Note& n) { return !n.part || n.part->program != 128; }
AcousticEvidence uncertain(const char* reason) { AcousticEvidence e;e.reason=reason;return e; }
double median(const std::vector<double>& values, std::size_t first, std::size_t last) {
    std::vector<double> selected(values.begin()+first,values.begin()+last);
    std::sort(selected.begin(),selected.end());return selected[selected.size()/2];
}
}
static AcousticEvidence measure(const StereoPcm& pcm,unsigned rate,int pitch,double time,double gap,const std::vector<Note>& notes){
    if(!std::isfinite(time)||!std::isfinite(gap)||pitch<0||pitch>127)
        throw std::invalid_argument("Audio invalide pour la vérification acoustique.");
    if(time<.12||time+.12>static_cast<double>(pcm.size())/rate)
        return uncertain("Bord du fichier : contexte audio insuffisant.");
    const auto f=frequency(pitch);
    if(f<65.)return uncertain("Registre très grave : résolution insuffisante pour cet essai.");
    std::vector<int> partials;
    for(int h=1;h<=6;++h){
        if(f*h>=rate*.48)continue;
        bool shared=false;
        for(const auto& n:notes)if(melodic(n)&&n.pitch!=pitch&&n.start<time+.1&&n.end>time-.1)
            for(int k=1;k<=6;++k)if(std::abs(std::log(frequency(n.pitch)*k/(f*h)))<.018)shared=true;
        if(!shared)partials.push_back(h);
    }
    if(partials.size()<2)return uncertain("Trop d’harmoniques partagées : cette hauteur ne peut pas être isolée dans cet accord.");
    std::vector<double> curve;
    for(int i=-9;i<=9;++i){double energy=0;for(auto h:partials)energy+=band(pcm,rate,time+i*.01-.02,.04,f*h)/h;curve.push_back(energy);}
    const auto peak=*std::max_element(curve.begin(),curve.end());
    if(peak<1e-9)return uncertain("Signal de cette hauteur trop faible pour conclure.");
    const auto before=median(curve,0,4),after=median(curve,15,19);
    const auto valley=*std::min_element(curve.begin()+6,curve.begin()+13);
    AcousticEvidence out;out.continuity=std::clamp(valley/std::max(std::min(before,after),1e-12),0.,1.);
    double at=time;
    for(std::size_t i=4;i<=14;++i){
        const auto pre=median(curve,i-3,i),post=median(curve,i,i+3);
        const auto rise=std::max(post-pre,0.)/std::max(std::max(pre,peak*.15),1e-12);
        if(rise>out.rise){out.rise=rise;at=time+(static_cast<double>(i)-9.)*.01;}
    }
    std::set<PartId> parts;bool overlap=false,competing=false;
    for(const auto& n:notes){
        if(melodic(n)&&n.pitch==pitch){
            if(n.start<time+.06&&n.end>time-.06)parts.insert(n.key());
            if(n.start<time-.06&&n.end>time+.06)overlap=true;
        }
        if((!melodic(n)||n.pitch!=pitch)&&std::abs(static_cast<double>(n.start)-at)<.06)competing=true;
    }
    if(parts.size()>1||overlap)
        out.reason="Harmoniques partagées avec une autre note : continuité non attribuable avec assez de sûreté.";
    else if(out.rise>=.9&&competing)
        out.reason="Remontée d’énergie proche d’une autre attaque : attribution incertaine.";
    else if(out.rise>=.9){out.verdict=AcousticVerdict::reattack;out.reason="Nouvelle montée d’énergie nette autour de cette hauteur : conserver la séparation.";}
    else if(std::min(before,after)<peak*.08)
        out.reason="Extinction ou signal trop faible d’un côté de la frontière.";
    else if(out.continuity>=.55&&out.rise<=.35&&gap<=.03){
        out.verdict=AcousticVerdict::continuity;out.reason="Son continu et aucune remontée nette détectée : fusion compatible avec l’audio.";
    }else out.reason="Indices de continuité et d’attaque insuffisants ou contradictoires.";
    if(out.rise>=.35)out.attackTime=at;
    return out;
}
std::vector<AcousticEvidence> acousticBoundaries(const StereoPcm& pcm,unsigned rate,const std::vector<BoundaryQuery>& queries,const std::vector<Note>& notes){
    if(rate<8000||std::any_of(pcm.begin(),pcm.end(),[](auto& f){return !std::isfinite(f[0])||!std::isfinite(f[1]);}))
        throw std::invalid_argument("Audio invalide pour la vérification acoustique.");
    std::vector<AcousticEvidence> out;out.reserve(queries.size());
    for(const auto& q:queries)out.push_back(measure(pcm,rate,q.pitch,q.time,q.gap,notes));return out;
}
AcousticEvidence acousticBoundary(const StereoPcm& pcm,unsigned rate,int pitch,double time,double gap,const std::vector<Note>& notes){
    return acousticBoundaries(pcm,rate,{{pitch,time,gap}},notes).front();
}
}
