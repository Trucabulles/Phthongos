#pragma once
#include "../core/document/Mask.h"
#include <juce_gui_basics/juce_gui_basics.h>
// Persist musical coordinates; cache committed strokes separately from the live gesture.
class GuidedMaskRaster {
  juce::Image base, image;juce::String key;
  std::size_t draftPoints=0;std::uint64_t lastDraftRevision=0;
public:
  void draw(juce::Graphics& g,const phthongos::SpectralMask& mask,const phthongos::MaskStroke* draft,
            juce::Rectangle<float> area,double start,double span,double top,double row,std::uint64_t revision,std::uint64_t draftRevision){
    if(area.isEmpty()||span<=0||row<=0)return;
    juce::Graphics::ScopedSaveState saved(g);g.setOpacity(1);
    double scale=std::min(1.,std::sqrt(4000000./std::max(1.,double(area.getWidth())*area.getHeight())));
    int width=std::max(1,int(std::ceil(area.getWidth()*scale))),height=std::max(1,int(std::ceil(area.getHeight()*scale)));
    auto next=juce::String((juce::int64)revision)+":"+juce::String(start,17)+":"+juce::String(span,17)+":"+juce::String(top,17)+":"+juce::String(row,17)+":"+area.toString();
    auto paint=[&](juce::Image& target,const phthongos::MaskStroke& s,std::size_t first){
      if(s.points.empty())return;juce::Image::BitmapData pixels(target,juce::Image::BitmapData::readWrite);
      auto previous=s.points[first?first-1:0];double rx=s.timeRadius/span*area.getWidth()*scale,ry=s.pitchRadius*row*scale;
      auto point=[&](auto p){return juce::Point<double>((p.time-start)/span*area.getWidth()*scale,(top-p.pitch+.5)*row*scale);};
      for(std::size_t i=first;i<s.points.size();++i){auto p=s.points[i];auto a=point(previous),b=point(p);
        int x0=std::max(0,int(std::floor(std::min(a.x,b.x)-rx))),x1=std::min(width-1,int(std::ceil(std::max(a.x,b.x)+rx))),y0=std::max(0,int(std::floor(std::min(a.y,b.y)-ry))),y1=std::min(height-1,int(std::ceil(std::max(a.y,b.y)+ry)));
        double dx=(b.x-a.x)/rx,dy=(b.y-a.y)/ry,d=dx*dx+dy*dy;
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){double px=(x+.5-a.x)/rx,py=(y+.5-a.y)/ry,at=d>0?std::clamp((px*dx+py*dy)/d,0.,1.):0.;if(std::hypot(px-at*dx,py-at*dy)<=1)pixels.setPixelColour(x,y,s.erase?juce::Colours::transparentBlack:juce::Colour(0x5838a0bd));}
        previous=p;
      }
    };
    if(next!=key){key=next;base=juce::Image(juce::Image::ARGB,width,height,true);for(auto& stroke:mask)paint(base,*stroke,0);draftPoints=0;image={};}
    if(draft&&!draft->points.empty()){
      if(draftRevision!=lastDraftRevision||!draftPoints){
        if(!draftPoints||draft->points.size()<=draftPoints){image=base.createCopy();draftPoints=0;}
        paint(image,*draft,draftPoints);draftPoints=draft->points.size();lastDraftRevision=draftRevision;
      }
      g.drawImage(image,area);
    }else{draftPoints=0;image={};g.drawImage(base,area);}
  }
};
