#include "Mask.h"
#include <algorithm>
namespace phthongos {
void validateMaskStroke(const MaskStroke& s){if(!std::isfinite(s.timeRadius)||!std::isfinite(s.pitchRadius)||s.timeRadius<.002||s.timeRadius>30||s.pitchRadius<.1||s.pitchRadius>12||s.points.empty()||s.points.size()>2048)throw std::invalid_argument("Trait de masque invalide.");for(auto& p:s.points)if(!std::isfinite(p.time)||!std::isfinite(p.pitch)||p.time<0||p.time>1200||p.pitch<-.5||p.pitch>127.5)throw std::invalid_argument("Point de masque invalide.");}
void validateMask(const SpectralMask& m){if(m.size()>1000)throw std::invalid_argument("Masque trop volumineux (1000 traits maximum).");std::size_t n=0;for(auto& s:m){if(!s)throw std::invalid_argument("Trait de masque absent.");validateMaskStroke(*s);n+=s->points.size();}if(n>100000)throw std::invalid_argument("Masque trop volumineux (100 000 points maximum).");}
bool maskContains(const SpectralMask& m,double t,double p){
 for(auto it=m.rbegin();it!=m.rend();++it){auto& s=**it;double x=t/s.timeRadius,y=p/s.pitchRadius;auto previous=s.points.front();bool inside=false;
  for(auto& point:s.points){double ax=previous.time/s.timeRadius,ay=previous.pitch/s.pitchRadius,bx=point.time/s.timeRadius,by=point.pitch/s.pitchRadius,dx=bx-ax,dy=by-ay;double at=dx*dx+dy*dy>0?std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.,1.):0.;if(std::hypot(x-ax-at*dx,y-ay-at*dy)<=1){inside=true;break;}previous=point;}
  if(inside)return !s.erase;
 }return false;
}
}
