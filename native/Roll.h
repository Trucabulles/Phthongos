#pragma once
#include "VoicePalette.h"
#include "Bridge.h"
#include "GuidedMask.h"
#include "../core/midi/RhythmicGrid.h"
class Roll : public juce::Component, public juce::SettableTooltipClient {
  struct KeyboardHandle : juce::Component {
    std::function<bool(const juce::KeyPress &)> action;
    bool keyPressed(const juce::KeyPress &k) override {
      return action && action(k);
    }
    void paint(juce::Graphics &g) override {
      if (hasKeyboardFocus(false)) {
        g.setColour(juce::Colour(0xff315d78));
        g.drawRect(getLocalBounds(), 1);
      }
    }
    std::unique_ptr<juce::AccessibilityHandler>
    createAccessibilityHandler() override {
      return std::make_unique<juce::AccessibilityHandler>(
          *this, juce::AccessibilityRole::slider);
    }
  } headKey, audioKey;
  void updateHandles() {
    auto l = layout();
    headKey.setVisible(!laneMode && (bool)playback["ready"] &&
                       number(playback, "position") >= start() &&
                       number(playback, "position") <= start() + span());
    headKey.setBounds((int)x(number(playback, "position")) - 10, 0, 20, 16);
    audioKey.setVisible(!laneMode&&(displayMode==1||(stackHost&&displayMode!=0)));
    audioKey.setBounds(0, (int)l.dividerAudio - 4, getWidth(), 8);
  }

public:
  bool laneMode=false,stackHost=false;double laneTop=84,laneRow=12,laneDuration=1;
  phthongos::rhythm::Grid rhythmicGrid;
  std::vector<phthongos::rhythm::Line> gridLines() const {return rhythmicGrid.lines(start(),span(),layout().width);}
  int maskMode=0;bool maskVisible=true,maskPainting=false;double brushSemitones=1;
  phthongos::SpectralMask spectralMask;phthongos::MaskStroke maskDraft;GuidedMaskRaster maskRaster;
  std::uint64_t maskRevision=0,maskDraftRevision=0;
  std::vector<var> markers;
  std::function<void(const var&)> markerChanged;
  var movingMarker;bool markerDragging=false;
  double markerTime=0;int moveAxis=0;bool axisConstrained=false;
  var markerAt(juce::Point<float> p)const{
    const auto right=58+layout().width;if(p.y<0||p.y>=30||p.x<58||p.x>right)return {};
    var closest;double distance=16.;
    for(auto it=markers.rbegin();it!=markers.rend();++it){auto mx=x(number(*it,"time"));if(mx<58||mx>right)continue;auto d=std::abs(p.x-mx);if(d<=16.&&(closest.isVoid()||d<distance)){closest=*it;distance=d;}}
    return closest;
  }
  var guidedCurve;std::vector<var> guidedGhosts;
  std::function<void(const phthongos::MaskStroke&)> maskCommitted;
  std::function<void(const String&)> guidedFocused;
  double pitchY(double p)const{return layout().rollTop+8+(top()-p+.5)*row();}
  phthongos::MaskPoint maskPoint(juce::Point<float> p)const{return {timeAt(p.x),View::clamp(top()-(p.y-layout().rollTop-8)/row()+.5,0,127)};}
  double maskBrushRadius() const {
    // A circle in screen pixels, converted to the two persisted musical axes.
    // Respect PHT8 bounds without ever displaying a different painted footprint.
    double pixelsPerSecond=layout().width/span(),pixelsPerPitch=row();
    double minimum=std::max(.002*pixelsPerSecond,.1*pixelsPerPitch);
    double maximum=std::min(30*pixelsPerSecond,12*pixelsPerPitch);
    if(minimum>maximum)return 0;
    return std::clamp(brushSemitones*pixelsPerPitch,minimum,maximum);
  }
  void extendMask(juce::Point<float> p){auto point=maskPoint(p);if(maskDraft.points.size()>1){auto last=maskDraft.points.back();if(std::hypot((point.time-last.time)/maskDraft.timeRadius,(point.pitch-last.pitch)/maskDraft.pitchRadius)<.1)return;}if(maskDraft.points.size()>=2048){std::vector<phthongos::MaskPoint> reduced;for(std::size_t i=0;i<maskDraft.points.size();i+=2)reduced.push_back(maskDraft.points[i]);maskDraft.points=std::move(reduced);}maskDraft.points.push_back(point);++maskDraftRevision;repaint();}
  View view;
  std::vector<Note> notes,midiPreview;
  struct Overlap {
    int pitch=0;
    double start=0,end=0;
    std::set<std::int64_t> ids;
    std::vector<std::pair<double,double>> spans;
  };
  std::vector<Overlap> overlaps;
  void refreshOverlaps() {
    overlaps.clear();
    std::array<std::vector<const Note*>,128> byPitch;
    for(const auto& n:notes)if(n.pitch>=0&&n.pitch<128&&n.end>n.start)byPitch[n.pitch].push_back(&n);
    for(int pitch=0;pitch<128;++pitch){
      auto& ns=byPitch[pitch];
      std::sort(ns.begin(),ns.end(),[](auto* a,auto* b){return a->start!=b->start?a->start<b->start:a->id<b->id;});
      for(size_t first=0;first<ns.size();){
        size_t last=first+1;double end=ns[first]->end;
        while(last<ns.size()&&ns[last]->start<end){end=std::max(end,ns[last]->end);++last;}
        if(last-first>1){
          Overlap group;group.pitch=pitch;group.start=ns[first]->start;group.end=end;
          std::vector<std::pair<double,int>> events;
          for(size_t i=first;i<last;++i){group.ids.insert(ns[i]->id);events.emplace_back(ns[i]->start,1);events.emplace_back(ns[i]->end,-1);}
          std::sort(events.begin(),events.end());int count=0;double previous=events.front().first;
          for(size_t i=0;i<events.size();){double t=events[i].first;
            if(count>=2&&t>previous){if(!group.spans.empty()&&group.spans.back().second==previous)group.spans.back().second=t;else group.spans.emplace_back(previous,t);}
            while(i<events.size()&&events[i].first==t){count+=events[i].second;++i;}previous=t;
          }
          overlaps.push_back(std::move(group));
        }
        first=last;
      }
    }
    std::sort(overlaps.begin(),overlaps.end(),[](const auto& a,const auto& b){return a.start!=b.start?a.start<b.start:a.pitch<b.pitch;});
  }
  std::set<std::int64_t> stackedAt(juce::Point<float> position) const {
    std::set<std::int64_t> ids;auto l=layout();
    if(position.x<58||position.x>58+l.width||position.y<l.rollTop+8||position.y>=l.dividerKeys)return ids;
    double time=timeAt(position.x);int pitch=pitchAt(position.y);
    for(const auto& n:notes)if(n.pitch==pitch&&n.start<=time&&time<n.end)ids.insert(n.id);
    return ids;
  }
  static constexpr juce::uint32 tonalColour=0xffff00b8;
  std::set<std::int64_t> lockedParts;
  std::set<std::int64_t> selected, extra, tonal, shortIds, suspect, focused;
  std::set<std::int64_t> manuallyMuted,mutedParts;
  std::vector<var> ghosts, holds, timing;
  String diagnosticGhost;
  var audio, playback, selection;
  double scoreDuration = 1;
  bool editable = false, busy = false, follow = true;
  int tool = 0, displayMode = 1;
  bool spectral = false,bandEnabled=false,cutHeld=false;
  bool cutting() const {return cutHeld || tool==3;}
  juce::Point<float> brushPosition;bool brushInside=false;
  std::map<int,unsigned> partColours;
  unsigned voiceColourIndex(int key,int program=0) const {
    auto c=partColours.find(key);return c!=partColours.end()?c->second%12:unsigned(key>=256?key:program/8)%12;
  }
  juce::Colour voiceColour(int key,int program=0) const {return juce::Colour(voicePalette::colours[voiceColourIndex(key,program)]);}
  double eraserDiameter = 48;
  bool erasing = false;
  std::set<std::int64_t> erasedIds;
  juce::Point<float> eraserPrevious;
  // Distance between a swept circular brush and a note rectangle. Sweeping
  // avoids skipping notes when mouse events are far apart.
  static bool eraserTouches(juce::Rectangle<float> r, juce::Point<float> a,
                            juce::Point<float> b, float radius) {
    auto distance = [r](juce::Point<float> p) {
      auto dx=std::max({r.getX()-p.x,0.f,p.x-r.getRight()});
      auto dy=std::max({r.getY()-p.y,0.f,p.y-r.getBottom()});
      return dx*dx+dy*dy;
    };
    // Squared distance to a convex rectangle is convex along the segment.
    float lo=0,hi=1;
    for(int i=0;i<40;++i){float u=(2*lo+hi)/3,v=(lo+2*hi)/3;
      if(distance(a+(b-a)*u)<distance(a+(b-a)*v))hi=v;else lo=u;}
    return std::min({distance(a),distance(b),distance(a+(b-a)*((lo+hi)*.5f))})<=radius*radius;
  }
  void eraseTo(juce::Point<float> p) {
    auto l=layout();
    p.x=juce::jlimit(58.f,float(58+l.width),p.x);
    p.y=juce::jlimit(float(l.rollTop+8),float(l.dividerKeys),p.y);
    for(const auto& n:notes)if(n.editable&&eraserTouches(rect(n),eraserPrevious,p,float(eraserDiameter*.5)))erasedIds.insert(n.id);
    eraserPrevious=p;brushPosition=p;brushInside=true;repaint();
  }
  String noteInformation(const Note& n) const {
    return pitchName(n.pitch)+tr(juce::String::fromUTF8(" · vél. "))+String(n.velocity)+
      juce::String::fromUTF8(" · ")+String(n.start,3)+juce::String::fromUTF8(" → ")+String(n.end,3)+
      juce::String::fromUTF8(" s · ")+tr(String::fromUTF8("Durée "))+String(juce::roundToInt((n.end-n.start)*1000))+" ms · "+n.label+
      (mutedParts.count(n.part)?tr(String::fromUTF8(" · partie muette")):String())+
      (manuallyMuted.count(n.id)?tr(String::fromUTF8(" · note muette")):String());
  }
  void inspectNote(const Note& n){setTooltip(noteInformation(n));if(inspect)inspect(getTooltip());}
  int bandLow=36,bandHigh=96;
  std::function<void()> bandChanged;
  static String noteLabel(int pitch) {
    if(localisation::language()==localisation::Language::english)return pitchName(pitch);
    const char* names[]={"Do","Do♯","Ré","Ré♯","Mi","Fa","Fa♯","Sol","Sol♯","La","La♯","Si"};
    return String::fromUTF8(names[juce::jlimit(0,127,pitch)%12])+String(pitch/12-1)+" / "+pitchName(pitch);
  }
  std::function<void(juce::Point<int>,int)> bandContext;
  int hoveredBand=-1;
  void setBandBoundary(bool high,int pitch) {
    pitch=juce::jlimit(0,127,pitch);
    if(high){bandHigh=pitch;bandLow=std::min(bandLow,pitch);}
    else {bandLow=pitch;bandHigh=std::max(bandHigh,pitch);}
    bandEnabled=true;repaint();if(bandChanged)bandChanged();
  }
  String bandLabel(int pitch) const {
    return noteLabel(pitch)+juce::String::fromUTF8(" · ")+String(440.*std::pow(2.,(pitch-69)/12.),1).replaceCharacter('.',',')+" Hz";
  }
  double bandY(bool high) const {return high?y(bandHigh):y(bandLow)+row();}
  float overlayOpacity = .2f;
  double visualGain = 4.9;
  double waveReferenceZoom = 1;
  bool waveCalibrated = false;
  juce::Image spectrum;
  double spectrumStart=0,spectrumEnd=0;
  std::function<void(double)> seek;
  double keyboardSeekTarget=-1,keyboardSeekTime=0;
  bool seekByKey(const juce::KeyPress& k) {
    int code=k.getKeyCode();auto mods=k.getModifiers();
    if((code!=juce::KeyPress::leftKey&&code!=juce::KeyPress::rightKey)||mods.isCtrlDown()||mods.isAltDown()||mods.isCommandDown())return false;
    if(busy||drag||erasing||maskPainting||!seek||!(bool)playback["ready"])return true;
    double now=juce::Time::getMillisecondCounterHiRes();
    double base=keyboardSeekTarget>=0&&now-keyboardSeekTime<500?keyboardSeekTarget:number(playback,"position");
    double step=mods.isShiftDown()?.05:.5;
    keyboardSeekTarget=View::clamp(base+(code==juce::KeyPress::leftKey?-step:step),0,duration());keyboardSeekTime=now;
    if(view.zoom>1&&(keyboardSeekTarget<start()||keyboardSeekTarget>start()+span()))
      view.pan=View::clamp((keyboardSeekTarget-span()*.5)/(duration()-span()),0,1);
    seek(keyboardSeekTarget);
    playback=playback.clone();playback.getDynamicObject()->setProperty("position",keyboardSeekTarget);updateHandles();repaint();
    return true;
  }
  std::function<void(const Note &)> modify;
  std::function<void(std::int64_t,double)> split;
  std::function<void(const std::set<std::int64_t>&, double, int)> moveGroup, duplicateGroup;
  bool duplicateDrag=false;
  std::function<void(double, int)> add;
  std::function<void(const std::set<std::int64_t> &)> remove, merge, mute;
  std::function<void(const Note &)> audition;
  std::function<void(int)> keyAudition;bool keyboardDragging=false;int keyboardPitch=-1;
  void stopKeyAudition(){keyboardDragging=false;if(keyboardPitch>=0&&keyAudition)keyAudition(-1);keyboardPitch=-1;repaint();}
  void playKey(int pitch){if(pitch==keyboardPitch)return;if(keyAudition)keyAudition(pitch);keyboardPitch=pitch;repaint();}
  void focusLost(FocusChangeType) override {stopKeyAudition();}
  int keyAt(juce::Point<float> point)const{auto l=layout();if(point.x<58||point.x>=58+l.width||point.y<l.keyTop||point.y>=l.keyTop+l.keys)return -1;int whites=0;for(int p=0;p<128;++p)if(!black(p))++whites;auto kw=l.width/whites;int wi=0;
    if(point.y<l.keyTop+l.keys*.61)for(int p=0;p<128;++p){if(!black(p))++wi;else if(point.x>=58+wi*kw-kw*.3&&point.x<58+wi*kw+kw*.3)return p;}
    wi=0;for(int p=0;p<128;++p)if(!black(p)){if(point.x>=58+wi*kw&&point.x<58+(wi+1)*kw)return p;++wi;}return -1;
  }
  std::function<void()> beginEdit, changed;
  std::function<void(String)> inspect;
  std::function<void(juce::Point<int>, const var &)> context;
  static juce::MouseCursor scissorsCursor() {
    static auto cursor=[] {
      juce::Image image(juce::Image::ARGB,24,24,true);juce::Graphics g(image);
      g.setColour(juce::Colour(0xff254962));g.drawEllipse(2.f,14.f,6.f,6.f,1.5f);g.drawEllipse(14.f,14.f,6.f,6.f,1.5f);
      g.drawLine(5,16,18,2,1.6f);g.drawLine(17,16,4,2,1.6f);g.fillEllipse(10.f,10.f,3.f,3.f);
      return juce::MouseCursor(image,12,11);
    }();return cursor;
  }
  static juce::MouseCursor brushCursor(bool erase=false) {
    static auto paint=[] {
      juce::Image image(juce::Image::ARGB,24,24,true);juce::Graphics g(image);
      juce::Path handle;handle.startNewSubPath(8,14);handle.lineTo(19,2);handle.quadraticTo(22,0,22,4);handle.lineTo(12,17);handle.closeSubPath();
      g.setColour(juce::Colour(0xff254962));g.fillPath(handle);
      juce::Path tip;tip.startNewSubPath(11,15);tip.cubicTo(15,20,8,23,2,22);tip.cubicTo(7,20,3,16,8,15);tip.closeSubPath();g.setColour(juce::Colour(0xff2e8589));g.fillPath(tip);
      return juce::MouseCursor(image,3,21);
    }();
    static auto eraser=[] {
      juce::Image image(juce::Image::ARGB,24,24,true);juce::Graphics g(image);juce::Path p;p.startNewSubPath(3,16);p.lineTo(14,3);p.lineTo(22,10);p.lineTo(11,23);p.lineTo(3,16);g.setColour(juce::Colour(0xffb78055));g.fillPath(p);g.setColour(juce::Colour(0xff254962));g.strokePath(p,juce::PathStrokeType(1.5f));return juce::MouseCursor(image,4,17);
    }();return erase?eraser:paint;
  }
  void updateToolCursor(){setMouseCursor(maskMode?brushCursor(maskMode==2):tool==4?brushCursor(true):cutting()?scissorsCursor():juce::MouseCursor::NormalCursor);repaint();}
  void paintOverChildren(juce::Graphics& g) override {
    if((!maskMode&&tool!=4)||!brushInside)return;auto l=layout();g.saveState();g.reduceClipRegion(juce::Rectangle<int>(58,int(l.rollTop+8),int(l.width),int(l.dividerKeys-l.rollTop-8)));
    if(!maskMode&&tool==4){g.setColour(juce::Colour(0xffb78055).withAlpha(.16f));g.fillEllipse(brushPosition.x-float(eraserDiameter*.5),brushPosition.y-float(eraserDiameter*.5),float(eraserDiameter),float(eraserDiameter));g.restoreState();return;}
    g.setColour(juce::Colour(maskMode==2?0xffb78055:0xff2e8589).withAlpha(.16f));
    float rx=float(maskPainting?maskDraft.timeRadius*l.width/span():maskBrushRadius()),ry=float(maskPainting?maskDraft.pitchRadius*row():rx);g.fillEllipse(brushPosition.x-rx,brushPosition.y-ry,2*rx,2*ry);g.restoreState();
  }
  void mouseExit(const juce::MouseEvent&) override {brushInside=false;repaint();}
  Roll() {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    for (auto *c : {&headKey, &audioKey}) {
      addAndMakeVisible(c);
      c->setWantsKeyboardFocus(true);
      c->setInterceptsMouseClicks(false, false);
    }
    headKey.setName(tr(juce::String::fromUTF8("Tête de lecture")));
    audioKey.setName(tr(juce::String::fromUTF8("Hauteur de l’audio")));
    headKey.action = [this](const juce::KeyPress &k) {
      return seekByKey(k);
    };
    auto pane = [this](const juce::KeyPress &k) {
      double &v = view.audioHeight;
      auto l = layout();
      double available = std::max(0., getHeight() - 92. - voiceSourceHeader),
             scale = std::min(1., available / 196),
             minimum = 60 * scale,
             maximum = available - l.keys - 96 * scale;
      if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
        v = View::clamp(v + (k == juce::KeyPress::downKey ? 1 : -1) *
                                (k.getModifiers().isShiftDown() ? 24 : 8),
                        minimum, maximum);
      else if (k == juce::KeyPress::homeKey)
        v = minimum;
      else if (k == juce::KeyPress::endKey)
        v = maximum;
      else if (k == juce::KeyPress::returnKey)
        v = 116;
      else
        return false;
      notify();
      return true;
    };
    audioKey.action = pane;
  }
  void resized() override { updateHandles(); }
  double duration() const {
    if(dragExtent>0)return std::max(dragExtent,baseDuration());
    return baseDuration();
  }
  double baseDuration() const {
    if(laneMode)return laneDuration;
    return (editable||busy) ? std::min(1200., std::max(scoreDuration,
                                               number(audio, "duration", 1)) +
                                          5)
                    : std::max(scoreDuration, number(audio, "duration", 1));
  }
  // Keep seconds per pixel fixed when the editable timeline grows.
  void frameTime(double visibleStart,double visibleSpan){
    visibleSpan=std::clamp(visibleSpan,.0001,duration());view.zoom=duration()/visibleSpan;
    view.pan=duration()>visibleSpan?View::clamp(visibleStart/(duration()-visibleSpan),0,1):0;
    updateHandles();
  }
  void cancelTimelinePreview(){if(dragExtent>0){dragExtent=0;view=initialView;updateHandles();repaint();}}
  void clearTimelinePreview(){dragExtent=0;}
  void setScoreDuration(double value){auto a=start(),s=span();scoreDuration=value;dragExtent=0;frameTime(a,s);}
  double span() const { return duration() / view.zoom; }
  double start() const { return view.pan * (duration() - span()); }
  struct Layout {
    double audio, keys, roll, rollTop, keyTop, dividerAudio, dividerKeys, width;
  };
  int voiceSourceHeader=0;
  Layout layout() const {
    if(laneMode)return {0,0,double(getHeight()),0,double(getHeight()+100),-100,double(getHeight()),std::max(1.,getWidth()-76.)};
    double available = std::max(0., getHeight() - 92. - voiceSourceHeader),
           scale = std::min(1., available / 196), ma = 60 * scale,
           mr = 96 * scale, mk = 40 * scale;
    double keys = std::max(mk, std::min(available - mr - ma, 56.));
    double a =
        View::clamp(view.audioHeight, ma, std::max(ma, available - mr - keys));
    keys = View::clamp(56., mk, std::max(mk, available - a - mr));
    if ((displayMode!=1&&!stackHost)||(stackHost&&(displayMode==0||(bool)audio["midi_only"]))) a=0;
    auto roll = std::max(0., available - a - keys);
    return {a,
            keys,
            roll,
            33 + a + 8 + 31 + voiceSourceHeader,
            33 + a + 8 + 31 + voiceSourceHeader + roll + 8,
            33 + a,
            33 + a + 8 + 31 + voiceSourceHeader + roll,
            std::max(1., getWidth() - 76.)};
  }
  double row() const {
    if(laneMode)return laneRow;
    return std::max(1., layout().roll - 16) / 128 * view.pitchZoom;
  }
  double waveformScale() const { return row()*128/waveReferenceZoom*.43*visualGain; }
  double top() const {
    if(laneMode)return laneTop;
    return 127 - view.pitchPan * (128 - 128 / view.pitchZoom);
  }
  double x(double t) const {
    return 58 + (t - start()) / span() * layout().width;
  }
  double y(int p) const { return layout().rollTop + 8 + (top() - p) * row(); }
  double timeAt(float px) const {
    return View::clamp(start() + (px - 58) / layout().width * span(), 0,
                       duration());
  }
  int pitchAt(float py) const {
    return juce::jlimit(
        0, 127, (int)std::ceil(top() - (py - layout().rollTop - 8) / row()));
  }
  juce::Rectangle<float> rect(const Note &n) const {
    return {(float)x(n.start), (float)y(n.pitch) + .7f,
            (float)std::max(1., x(n.end) - x(n.start)),
            (float)std::max(2., row() - 1.3)};
  }
  void selectZone(double a, double b, int lo, int hi, bool temporal,
                  bool additive) {
    if (!additive)
      selected.clear();
    for (auto &n : notes)
      if (n.editable && n.pitch >= lo && n.pitch <= hi &&
          (a == b ? n.start <= a && n.end > a : n.start < b && n.end > a))
        selected.insert(n.id);
    selection = object({{"ids", idsVar(selected)},
                        {"start", a},
                        {"end", b},
                        {"low", lo},
                        {"high", hi},
                        {"temporal", temporal}});
    notify();
  }
  void clearSelection() {
    selected.clear();
    selection = var();
    notify();
  }
  void selectIds(std::set<std::int64_t> ids) {
    for(const auto& n:notes)if(lockedParts.count(n.part))ids.erase(n.id);
    selected = std::move(ids);
    double a = duration(), b = 0;
    int lo = 127, hi = 0;
    for (auto &n : notes)
      if (selected.count(n.id)) {
        a = std::min(a, n.start);
        b = std::max(b, n.end);
        lo = std::min(lo, n.pitch);
        hi = std::max(hi, n.pitch);
      }
    selection = selected.empty() ? var()
                                 : object({{"ids", idsVar(selected)},
                                           {"start", a},
                                           {"end", b},
                                           {"low", lo},
                                           {"high", hi}});
    notify();
  }
  void reveal(double a, double b, int pitch) {
    auto s = span(), length = b - a;
    if (length > s) {
      auto ns = std::min(duration(), length + 2 * std::min(.5, length * .08));
      view.zoom = duration() / ns;
      view.pan = view.zoom == 1
                     ? 0
                     : View::clamp((a + b - ns) / 2 / (duration() - ns), 0, 1);
    } else {
      auto margin = std::min({.5, s * .08, std::max(0., (s - length) / 2)});
      if (a < start())
        view.pan = view.zoom == 1
                       ? 0
                       : View::clamp((a - margin) / (duration() - s), 0, 1);
      else if (b > start() + s)
        view.pan = view.zoom == 1
                       ? 0
                       : View::clamp((b + margin - s) / (duration() - s), 0, 1);
    }
    if (view.pitchZoom > 1) {
      auto count = 128 / view.pitchZoom, margin = std::min(1., (count - 1) / 2);
      if (pitch > top())
        view.pitchPan =
            View::clamp((127 - pitch - margin) / (128 - count), 0, 1);
      else if (pitch < top() - count + 1)
        view.pitchPan = View::clamp(
            (127 - (pitch + count - 1 - margin)) / (128 - count), 0, 1);
    }
    notify();
  }
  std::uint64_t playbackRepaints=0;
  void updatePlayback(const var &p,bool render=true) {
    auto before=playback;double previousPan=view.pan;
    playback = p;
    if(keyboardSeekTarget>=0){
      if(!(bool)p["ready"]||juce::Time::getMillisecondCounterHiRes()-keyboardSeekTime>=500||std::abs(number(p,"position")-keyboardSeekTarget)<((bool)p["playing"]?.15:.001))keyboardSeekTarget=-1;
      else{playback=p.clone();playback.getDynamicObject()->setProperty("position",keyboardSeekTarget);}
    }
    if (follow && (bool)p["playing"] && !drag && view.zoom > 1) {
      double position = number(p, "position");
      if (position < start() || position > start() + span() * .95)
        view.pan =
            View::clamp((position - span() * .2) / (duration() - span()), 0, 1);
    }
    if(!render)return;
    bool positionChanged=number(before,"position")!=number(playback,"position")||before["ready"]!=playback["ready"];
    auto same=[](const var& a,const var& b){if(a.size()!=b.size())return false;for(int i=0;i<a.size();++i)if(a[i]!=b[i])return false;return true;};
    bool notesChanged=!same(before["active_note_indices"],playback["active_note_indices"]);
    bool keysChanged=!same(before["active_keys"],playback["active_keys"]);
    if(previousPan!=view.pan){++playbackRepaints;updateHandles();repaint();return;}
    if(positionChanged){
      ++playbackRepaints;updateHandles();
      for(const auto& state:{before,playback})if(bool(state["ready"])&&number(state,"position")>=start()&&number(state,"position")<=start()+span())
        repaint(juce::Rectangle<int>(int(x(number(state,"position")))-7,0,15,int(layout().dividerKeys)+1).getIntersection(getLocalBounds()));
    }
    if(notesChanged){
      ++playbackRepaints;std::set<int> changed;
      for(const auto& state:{before,playback})for(auto index:items(state["active_note_indices"]))changed.insert(int(index));
      for(const auto& n:notes)if(changed.count(n.index))repaint(rect(n).expanded(3).getSmallestIntegerContainer().getIntersection(getLocalBounds()));
    }
    if(keysChanged){++playbackRepaints;auto l=layout();repaint(0,int(l.dividerKeys),getWidth(),getHeight()-int(l.dividerKeys));}
  }
  void drawRhythmicGrid(juce::Graphics& g,const std::vector<phthongos::rhythm::Line>& lines,float upper,float lower) const {
    juce::Graphics::ScopedSaveState save(g);auto l=layout();
    g.reduceClipRegion(58,int(upper),int(l.width),std::max(0,int(lower-upper)));
    const juce::uint32 colours[]={0x7831516a,0x4031516a,0x2531516a,0x1531516a};
    for(auto& line:lines){g.setColour(juce::Colour(colours[line.level]));g.fillRect(float(x(line.seconds)),upper,line.level==0?1.4f:1.f,lower-upper);}
  }
  void paint(juce::Graphics &g) override {
    auto l = layout();
    const auto grid=gridLines();
    g.fillAll(juce::Colour(0xfff6f3eb));
    g.setFont(juce::FontOptions(14));
    {
      juce::Graphics::ScopedSaveState save(g);
      g.reduceClipRegion(58, 0, (int)l.width, getHeight());
      g.setColour(juce::Colour(0xfff1ebdf));
      g.fillRect(58.f, 33.f, (float)l.width, (float)l.audio);
      auto ruler=rhythmicGrid.mode==2?phthongos::rhythm::Grid::secondsLines(start(),span(),l.width):grid;
      double lastLabel=-1000;
      if(!laneMode)for(auto& line:ruler){
        bool musical=rhythmicGrid.mode==0;
        if(musical?(line.level>1||line.pulse==0):line.level!=0)continue;
        double px=x(line.seconds);if(px<lastLabel+66)continue;
        String label=musical?String((juce::int64)line.bar)+"."+String(line.pulse):
            (span()<10?String(line.seconds,3)+" s":timeName(line.seconds));
        g.setColour(juce::Colour(0xff617889));
        g.drawText(label,int(px)+5,6,95,20,juce::Justification::left);lastLabel=px;
      }
      auto peaks = items(audio["peaks"]);
      double amplitude = .01;
      for (auto &p : peaks)
        for (auto &v : items(p))
          amplitude = std::max(amplitude, std::abs((double)v));
      bool precise = detail.isArray() &&
                     std::abs(detailStart - start()) < 1e-6 &&
                     std::abs(detailEnd - std::min(start()+span(),number(audio,"duration"))) < 1e-6;
      auto &used = precise ? detail : audio["peaks"];
      auto ps = items(used);
      double psStart = precise ? detailStart : 0,
             psSpan = precise ? detailEnd - detailStart
                              : number(audio, "duration", 1);
      if (l.audio<=0 || (displayMode!=1&&!stackHost)) {}
      else if ((bool)audio["midi_only"]) {
        g.setColour(juce::Colour(0xff526b7d));
        g.drawText(tr(juce::String::fromUTF8("Aucun audio associé · Menu Fichier "
                                          "→ Associer un audio source")),
                   76, 50, (int)l.width - 20, (int)l.audio - 20,
                   juce::Justification::centredLeft);
      } else if (spectral && spectrum.isValid()) {
        drawSpectrum(g,33,l.audio,1.f);
      } else if (!ps.empty() && psSpan > 0) {
        juce::Graphics::ScopedSaveState audioClip(g);
        g.reduceClipRegion(58,33,(int)l.width,(int)l.audio);
        g.setColour(juce::Colour(0xff254962));
        for (int px = 0; px < (int)l.width; ++px) {
          int first = (int)std::floor(
              (start() + px / l.width * span() - psStart) / psSpan * ps.size());
          int last = std::min(
              (int)ps.size(),
              std::max(first + 1,
                       (int)std::ceil(
                           (start() + (px + 1) / l.width * span() - psStart) /
                           psSpan * ps.size())));
          double lo = 0, hi = 0;
          for (int i = std::max(0, first); i < last; ++i) {
            auto a = items(ps[(size_t)i]);
            if (a.size() > 1) {
              lo = std::min(lo, (double)a[0]);
              hi = std::max(hi, (double)a[1]);
            }
          }
          double centre = 33 + l.audio / 2, scale = l.audio * .405;
          g.fillRect((float)(58 + px), (float)(centre - hi / amplitude * scale),
                     1.f, (float)std::max(1., (hi - lo) / amplitude * scale));
        }
      }
    }
    if(displayMode==1||stackHost)drawRhythmicGrid(g,grid,33,float(l.dividerAudio));
    g.setColour(juce::Colour(0xffe9e0d1));
    g.fillRect(0.f, (float)(l.rollTop - 31 - voiceSourceHeader), (float)getWidth(), 31.f+voiceSourceHeader);
    g.setColour(juce::Colour(0xff526b7d));
    g.drawText("MIDI", 14, (int)l.rollTop - 29 - voiceSourceHeader, 45, 25,
               juce::Justification::left);
    g.drawText(localisation::count("notes",int(notes.size())), getWidth() - 130,
               (int)l.rollTop - 29 - voiceSourceHeader, 110, 25, juce::Justification::right);
    {
      juce::Graphics::ScopedSaveState save(g);
      g.reduceClipRegion(0, (int)l.rollTop, getWidth(), (int)l.roll);
      for (int p = 127; p >= 0; --p) {
        bool black = p % 12 == 1 || p % 12 == 3 || p % 12 == 6 || p % 12 == 8 ||
                     p % 12 == 10;
        g.setColour(juce::Colour(black ? 0xfff6f3eb : 0xfff0eadf));
        g.fillRect(58.f, (float)y(p), (float)l.width, (float)row());
        if (p % 12 == 0) {
          g.setColour(juce::Colour(0x38254962));
          g.drawHorizontalLine((int)(y(p) + row()), 58, (float)(58 + l.width));
        }
      }
      for (int p = 127; p >= 0; --p)
        if ((row() >= 13 || p % 12 == 0) && y(p) + row() / 2 >= l.rollTop + 7 &&
            y(p) + row() / 2 <= l.rollTop + l.roll - 7) {
          g.setColour(juce::Colour(p % 12 == 0 ? 0xff906027 : 0xff576f81));
          g.drawText(pitchName(p), 0, (int)(y(p) + row() / 2 - 7), 48, 14,
                     juce::Justification::right);
        }
      g.reduceClipRegion(58, (int)l.rollTop, (int)l.width, (int)l.roll);
      if (displayMode==2 && !(bool)audio["midi_only"]) {
        if (spectral) drawSpectrum(g,l.rollTop,l.roll,overlayOpacity);
        else if (!items(audio["peaks"]).empty()) {
          auto ps=items(audio["peaks"]); double d=number(audio,"duration",1);
          g.setColour(juce::Colour(0xff254962).withAlpha(overlayOpacity));
          for(int px=0;px<(int)l.width;++px) {
            int lo=(int)std::floor((start()+px/l.width*span())/d*ps.size());
            int hi=(int)std::ceil((start()+(px+1)/l.width*span())/d*ps.size());
            double low=0,high=0;
            for(int i=std::max(0,lo);i<std::min((int)ps.size(),std::max(lo+1,hi));++i) {
              auto values=items(ps[i]); if(values.size()>1){low=std::min(low,(double)values[0]); high=std::max(high,(double)values[1]);}
            }
            double centre=l.rollTop+l.roll/2, scale=waveformScale();
            g.fillRect((float)(58+px),(float)(centre-high*scale),1.f,
              (float)std::max(1.,(high-low)*scale));
          }
        }
      }
      drawRhythmicGrid(g,grid,float(l.rollTop+8),float(l.dividerKeys));
      if(maskVisible&&(!spectralMask.empty()||maskPainting))maskRaster.draw(g,spectralMask,maskPainting?&maskDraft:nullptr,
        juce::Rectangle<float>(58.f,float(l.rollTop+8),float(l.width),float(l.dividerKeys-l.rollTop-8)),start(),span(),top(),row(),maskRevision,maskDraftRevision);
      {juce::Path curve;bool active=false;double lastTime=0,lastPitch=0;for(auto& point:items(guidedCurve)){if(point[1].isVoid()){active=false;continue;}double time=(double)point[0],pitch=(double)point[1];float px=float(x(time)),py=float(pitchY(pitch));if(active&&time-lastTime<=.05&&std::abs(pitch-lastPitch)<=2)curve.lineTo(px,py);else curve.startNewSubPath(px,py);active=true;lastTime=time;lastPitch=pitch;}g.setColour(juce::Colour(0xff137a9e));g.strokePath(curve,juce::PathStrokeType(1.6f));}
      std::set<std::int64_t> paintSelected = selected;
      if (drag && dragKind == Select) {
        paintSelected = additive ? initialSelected : std::set<std::int64_t>{};
        for (auto &n : notes)
          if (n.editable && n.start < zoneB && n.end > zoneA &&
              n.pitch >= zoneLow && n.pitch <= zoneHigh)
            paintSelected.insert(n.id);
      }

      std::set<int> active;
      for (auto &v : items(playback["active_note_indices"]))
        active.insert((int)v);
      std::vector<Note> copies;
      if(duplicateDrag&&hasDraft){if(groupIds.size()>1){for(auto n:notes)if(groupIds.count(n.id)){n.start+=groupShift;n.end+=groupShift;n.pitch+=groupPitch;copies.push_back(n);}}else copies.push_back(draft);}
      std::vector<const Note*> paintOrder;paintOrder.reserve(notes.size()+copies.size());
      for(const auto& n:notes)paintOrder.push_back(&n);for(const auto& n:copies)paintOrder.push_back(&n);
      // Locked context goes behind the notes the user is currently editing.
      if(!lockedParts.empty())std::stable_partition(paintOrder.begin(),paintOrder.end(),[&](auto* n){return lockedParts.count(n->part)>0;});
      for(const auto* item:paintOrder){const auto& source=*item;
        if(erasing&&erasedIds.count(source.id))continue;
        auto n = !duplicateDrag && hasDraft && draft.id == source.id ? draft : source;
        if (!duplicateDrag && hasDraft && dragKind == Move && groupIds.size()>1 && groupIds.count(source.id)) {
          n = source; n.start += groupShift; n.end += groupShift; n.pitch += groupPitch;
        }
        auto r = rect(n);
        if(!g.clipRegionIntersects(r.expanded(3).getSmallestIntegerContainer()))continue;
        if (!r.intersects(juce::Rectangle<float>(
                58, (float)l.rollTop, (float)l.width, (float)l.roll)))
          continue;
        auto c = voiceColour(n.part,n.program);
        if (suspect.count(n.id))
          c = juce::Colour(0xffd38a3f);
        else if (tonal.count(n.id))
          c = juce::Colour(tonalColour);
        else if (shortIds.count(n.id))
          c = juce::Colour(0xff367e80);
        if (!n.muted && active.count(n.index))
          c = juce::Colour(0xffe9b843);
        if (paintSelected.count(n.id))
          c = c.interpolatedWith(juce::Colours::white, .48f);
        bool voiceLocked=lockedParts.count(n.part)>0;
        if(voiceLocked)c=juce::Colour(0xff939393);
        float opacity = voiceLocked ? .48f : n.muted ? .32f : 1.f;
        g.setColour(c.withMultipliedAlpha(.25f * opacity));
        g.fillRect(r.withTrimmedLeft(r.getWidth() * n.velocity / 127.f));
        g.setColour(c.withMultipliedAlpha(opacity));
        g.fillRect(r.withWidth(r.getWidth() * n.velocity / 127.f));
        std::vector<juce::Colour> annotations;
        if (suspect.count(n.id))
          annotations.emplace_back(0xffd38a3f);
        if (tonal.count(n.id) && !(hasDraft && n.id == draft.id))
          annotations.emplace_back(tonalColour);
        if (shortIds.count(n.id) && !(hasDraft && n.id == draft.id))
          annotations.emplace_back(0xff367e80);
        bool sounding = !n.muted && active.count(n.index);
        if (!voiceLocked && !annotations.empty() && (sounding || annotations.size() > 1)) {
          float height = sounding ? r.getHeight() * .3f : r.getHeight();
          for (size_t i = 0; i < annotations.size(); ++i) {
            auto colour = annotations[i];
            if (paintSelected.count(n.id))
              colour = colour.interpolatedWith(juce::Colours::white, .48f);
            auto strip = juce::Rectangle<float>(
                r.getX(),
                r.getBottom() - height + height * i / annotations.size(),
                r.getWidth(), height / annotations.size());
            g.setColour(colour.withMultipliedAlpha(opacity));
            g.fillRect(strip.withWidth(r.getWidth() * n.velocity / 127.f));
            g.setColour(colour.withMultipliedAlpha(.25f * opacity));
            g.fillRect(
                strip.withTrimmedLeft(r.getWidth() * n.velocity / 127.f));
          }
        }
        if (n.protectedNote) {
          g.setColour(juce::Colour(0xff315d78));
          g.drawRect(r, 1);
        }
        if (extra.count(n.id)) {
          g.setColour(juce::Colour(0xffb24e3d));
          dashed(g, r);
        }
        if (focused.count(n.id)) {
          g.setColour(juce::Colour(0xffb44735));
          g.drawRect(r.expanded(2.f), 2.f);
        }
        if(tonal.count(n.id)) {
          g.setColour(juce::Colour(0xff35152d));g.drawRect(r,3.f);
          g.setColour(juce::Colour(tonalColour));g.drawRect(r,1.5f);
        }
        if (!n.muted && row() >= 13 && r.getWidth() >= 28) {
          g.setColour(juce::Colour(voicePalette::textColour(c.interpolatedWith(juce::Colour(0xfff8f7f1),1.f-opacity).getARGB())));
          g.setFont(juce::FontOptions(float(View::clamp(row() * .35, 13., 22.))));
          g.drawText(pitchName(n.pitch), r.reduced(4, 0),
                     juce::Justification::centredLeft);
        }
        if (n.muted) {
          g.setColour(juce::Colour(0xff254962));
          float size=std::max(1.f,std::min({float(View::clamp(row()*.18,7.,14.)),r.getWidth()-2.f,r.getHeight()-2.f}));
          float left=r.getX()+std::min(3.f,std::max(0.f,(r.getWidth()-size)*.5f)),half=size*.5f,stroke=View::clamp(size/7.f,1.,2.);
          g.drawLine(left,r.getCentreY()-half,left+size,r.getCentreY()+half,stroke);
          g.drawLine(left+size,r.getCentreY()-half,left,r.getCentreY()+half,stroke);
        }
      }
      // Incoming MIDI is drawn separately until its single editor transaction completes.
      for(const auto& n:midiPreview){auto r=rect(n);auto c=voiceColour(n.part,n.program);
        g.setColour(c.withAlpha(.55f));g.fillRect(r);g.setColour(c);dashed(g,r);
        if(row()>=13&&r.getWidth()>=28){g.setColour(juce::Colour(0xff173746));g.setFont(juce::FontOptions(float(View::clamp(row()*.35,13.,22.))));g.drawText(pitchName(n.pitch),r.reduced(4,0),juce::Justification::centredLeft);}
      }
      for(const auto& group:overlaps)for(auto [a,b]:group.spans){
        auto r=juce::Rectangle<float>(float(x(a)),float(y(group.pitch)),float(x(b)-x(a)),float(std::max(1.,row()-1)));
        if(!r.intersects(juce::Rectangle<float>(58,float(l.rollTop),float(l.width),float(l.roll))))continue;
        g.saveState();g.reduceClipRegion(r.toNearestInt());
        float left=std::max(58.f,r.getX()),right=std::min(float(58+l.width),r.getRight());
        for(float at=left-r.getHeight();at<right;at+=7){g.setColour(juce::Colour(0xfffff8e8).withAlpha(.9f));g.drawLine(at,r.getBottom(),at+r.getHeight(),r.getY(),2.5f);g.setColour(juce::Colour(0xff254962));g.drawLine(at,r.getBottom(),at+r.getHeight(),r.getY(),1.f);}
        g.restoreState();
      }
      for(auto& n:guidedGhosts){juce::Rectangle<float> r(float(x(number(n,"start"))),float(y((int)n["pitch"])),float(std::max(3.,x(number(n,"end"))-x(number(n,"start")))),float(std::max(3.,row()-1)));g.setColour(juce::Colour(0x44339eb5));g.fillRect(r);g.setColour(juce::Colour(0xff08778f));dashed(g,r);if(n["id"].toString()==diagnosticGhost)g.drawRect(r.expanded(2),2);}
      for (auto &n : ghosts) {
        juce::Rectangle<float> r(
            (float)x(number(n, "start")), (float)y((int)n["pitch"]),
            (float)std::max(3., x(number(n, "end")) - x(number(n, "start"))),
            (float)std::max(3., row() - 1));
        g.setColour(juce::Colour(0x28367e80));
        g.fillRect(r);
        g.setColour(juce::Colour(0xff367e80));
        dashed(g, r);
        if (n["id"].toString() == diagnosticGhost) {
          g.setColour(juce::Colour(0xff254962));
          g.drawRect(r.expanded(2), 2);
        }
      }
      for (auto& n : timing) {
        bool active=n["id"].toString()==diagnosticGhost;
        g.setColour(juce::Colour(active?0xff254962:0xff367e80));
        float top=(float)y((int)n["pitch"]), height=(float)std::max(5.,row()-1);
        if(n["kind"].toString()=="split") {
          float at=(float)x(number(n,"at"));
          g.drawLine(at,top-3,at,top+height+3,active?3.f:2.f);
        } else {
          juce::Rectangle<float> tail((float)x(number(n,"end")),top,
              (float)std::max(3.,x(number(n,"proposed_end"))-x(number(n,"end"))),height);
          dashed(g,tail);
          if(active)g.drawRect(tail.expanded(1),2);
        }
      }
      for (auto &n : holds) {
        g.setColour(juce::Colour(0xff9f7328));
        dashed(g, {(float)x(number(n, "start")), (float)y((int)n["pitch"]),
                   (float)std::max(3.,
                                   x(number(n, "end")) - x(number(n, "start"))),
                   (float)std::max(3., row() - 1)});
      }
      auto zone = drag && dragKind == Select ? object({{"start", zoneA},
                                                       {"end", zoneB},
                                                       {"low", zoneLow},
                                                       {"high", zoneHigh}})
                                             : selection;
      if (!zone.isVoid() && number(zone, "end") > number(zone, "start")) {
        juce::Rectangle<float> r(
            (float)x(number(zone, "start")), (float)y((int)zone["high"]),
            (float)(x(number(zone, "end")) - x(number(zone, "start"))),
            (float)(y((int)zone["low"]) + row() - y((int)zone["high"])));
        g.setColour(juce::Colour(0x1b315d78));
        g.fillRect(r);
        g.setColour(juce::Colour(0xff315d78));
        dashed(g, r);
      }
    }
    if(bandEnabled) {
      juce::Graphics::ScopedSaveState save(g);
      g.reduceClipRegion(0,(int)l.rollTop,getWidth(),(int)l.roll);
      if(spectral && displayMode!=0) {
        double upper=View::clamp(bandY(true),l.rollTop,l.dividerKeys),lower=View::clamp(bandY(false),l.rollTop,l.dividerKeys);
        g.setColour(juce::Colour(0xfff9faf4).withAlpha(.72f));
        g.fillRect(58.f,(float)l.rollTop,(float)l.width,(float)(upper-l.rollTop));
        g.fillRect(58.f,(float)lower,(float)l.width,(float)(l.dividerKeys-lower));
      }
      for(bool high:{true,false}) {
        double edge=bandY(high);if(edge<l.rollTop || edge>l.dividerKeys)continue;
        bool active=hoveredBand==(high?1:0) || (drag && dragKind==Band && bandDraggingHigh==high);
        auto colour=juce::Colour(active?0xff254962:0xff367e80);
        g.setColour(colour);g.fillRect(58.f,(float)edge-.5f,(float)l.width,active?2.f:1.f);
        juce::Path triangle;triangle.addTriangle(49.f,(float)edge-5,58.f,(float)edge,49.f,(float)edge+5);g.fillPath(triangle);
        g.setFont(juce::FontOptions(12,juce::Font::bold));
        g.setColour(juce::Colour(0xfff9faf4));g.fillRect(0.f,(float)edge-9,48.f,18.f);
        g.setColour(colour);g.drawText((high?tr(String::fromUTF8("Aiguë")):tr(String::fromUTF8("Grave"))),1,(int)edge-9,46,18,juce::Justification::centred);
        auto handle=juce::Rectangle<float>((float)(58+l.width)-40,(float)edge-8,34,16);
        g.setColour(active?colour:juce::Colour(0xffe3efeb));g.fillRoundedRectangle(handle,3);
        g.setColour(active?juce::Colours::white:colour);
        float cx=handle.getCentreX(),cy=handle.getCentreY();
        g.drawLine(cx,cy-5,cx,cy+5,1.2f);
        for(float direction:{-1.f,1.f}) {g.drawLine(cx,cy+direction*5,cx-3,cy+direction*2,1.2f);g.drawLine(cx,cy+direction*5,cx+3,cy+direction*2,1.2f);}
        const auto text=(high?tr(String::fromUTF8("Aiguë : ")):tr(String::fromUTF8("Grave : ")))+bandLabel(high?bandHigh:bandLow);
        const juce::Font labelFont(juce::FontOptions(15));
        const int labelWidth=std::min(juce::GlyphArrangement::getStringWidthInt(labelFont,text)+12,std::max(0,(int)l.width-20));
        auto label=juce::Rectangle<int>((int)(58+l.width)-10-labelWidth,(int)edge+(high?10:-32),labelWidth,22);
        g.setColour(juce::Colour(0xfff9faf4).withAlpha(.95f));g.fillRect(label);
        g.setColour(juce::Colour(0xff254962));g.setFont(labelFont);
        g.drawText(text,label.reduced(6,0),juce::Justification::centredRight);
      }
    }
    for(const auto& marker:markers){double t=markerDragging&&marker["id"]==movingMarker["id"]?markerTime:number(marker,"time");if(t<start()||t>start()+span())continue;
      float mx=float(x(t));g.setColour(juce::Colour(0xff805b99));g.drawLine(mx,1,mx,30,1.5f);juce::Path flag;flag.addTriangle(mx,1,mx+9,5,mx,10);g.fillPath(flag);
    }
    double pos = number(playback, "position");
    if ((bool)playback["ready"] && pos >= start() && pos <= start() + span()) {
      g.setColour(juce::Colour(0xffa44737));
      g.drawVerticalLine((int)x(pos), 0, (float)l.dividerAudio);
      g.drawVerticalLine((int)x(pos), (float)l.rollTop, (float)l.dividerKeys);
      juce::Path triangle;
      triangle.addTriangle((float)x(pos) - 5, 0, (float)x(pos) + 5, 0,
                           (float)x(pos), 7);
      g.fillPath(triangle);
    }
    g.setColour(juce::Colour(0xffe9e0d1));
    g.fillRect(0.f, (float)l.dividerKeys, (float)getWidth(),
               (float)(getHeight() - l.dividerKeys));
    std::set<int> keys;if(keyboardPitch>=0)keys.insert(keyboardPitch);
    for (auto &v : items(playback["active_keys"]))
      keys.insert((int)v);
    int whites = 0;
    for (int p = 0; p < 128; ++p)
      if (!black(p))
        ++whites;
    double kw = l.width / whites;
    int wi = 0;
    for (int p = 0; p < 128; ++p)
      if (!black(p)) {
        auto kx = 58 + wi++ * kw;
        g.setColour(juce::Colour(keys.count(p) ? 0xffd2a35e : (p<21||p>108 ? 0xffd5d5d0 : 0xfffffdf6)));
        g.fillRect((float)kx, (float)l.keyTop, (float)kw - 1, (float)l.keys);
        if (p % 12 == 0) {
          g.setColour(juce::Colour(0xff253e51));
          g.setFont(juce::FontOptions(12));
          g.drawText(pitchName(p), (int)kx, (int)(l.keyTop + l.keys - 17),
                     34, 15, juce::Justification::left);
        }
      }
    wi = 0;
    for (int p = 0; p < 128; ++p)
      if (!black(p))
        ++wi;
      else {
        g.setColour(juce::Colour(keys.count(p) ? 0xffb8813c : (p<21||p>108 ? 0xff747b7d : 0xff1d3c53)));
        g.fillRect((float)(58 + wi * kw - kw * .3), (float)l.keyTop,
                   (float)(kw * .6), (float)(l.keys * .61));
      }
    g.setColour(juce::Colour(0xffccbea6));
    g.drawHorizontalLine((int)l.dividerAudio, 0, (float)getWidth());
    g.drawHorizontalLine((int)l.dividerKeys, 0, (float)getWidth());
    g.setColour(juce::Colour(0xff627c8e));
    g.setFont(juce::FontOptions(12));
    if(displayMode==1) g.drawText("AUDIO", 8, 45, 45, 20, juce::Justification::left);
  }
  void mouseDown(const juce::MouseEvent &e) override {
    keyboardSeekTarget=-1;
    if(!laneMode&&!busy&&e.position.x>=58&&e.position.y<30){auto marker=markerAt(e.position);
      if(e.mods.isPopupMenu()){if(context)context(e.getScreenPosition(),object({{"kind","timeline"},{"time",std::min(timeAt(e.position.x),std::max(scoreDuration,number(audio,"duration")))},{"marker",marker}}));return;}
      if(e.mods.isLeftButtonDown()&&!marker.isVoid()){movingMarker=marker;markerTime=number(marker,"time");markerDragging=true;down=e.position;moved=false;if(seek)seek(markerTime);return;}}
    moveAxis=0;axisConstrained=e.mods.isShiftDown();duplicateDrag=false;
    grabKeyboardFocus();
    if (busy)
      return;
    auto l = layout();
    if(e.mods.isLeftButtonDown()&&e.position.y>=l.keyTop){if(keyAudition){keyboardDragging=true;playKey(keyAt(e.position));}return;}
    if(!maskMode&&tool==4&&editable&&e.mods.isLeftButtonDown()&&e.position.x>=58&&e.position.y>=l.rollTop+8&&e.position.y<l.dividerKeys){erasing=true;erasedIds.clear();eraserPrevious=e.position;setTooltip({});if(beginEdit)beginEdit();eraseTo(e.position);return;}
    if(maskMode&&e.mods.isLeftButtonDown()&&e.position.x>=58&&e.position.y>=l.rollTop+8&&e.position.y<l.dividerKeys){double radius=maskBrushRadius();if(radius<=0){if(inspect)inspect(tr(String::fromUTF8("Zoomez horizontalement pour peindre un masque circulaire.")));return;}maskPainting=true;maskDraft={};maskDraft.erase=maskMode==2;maskDraft.timeRadius=radius/l.width*span();maskDraft.pitchRadius=radius/row();brushPosition=e.position;brushInside=true;extendMask(e.position);return;}
    if(!maskMode&&e.mods.isLeftButtonDown())for(auto& n:guidedGhosts)if((int)n["pitch"]==pitchAt(e.position.y)&&timeAt(e.position.x)>=number(n,"start")&&timeAt(e.position.x)<=number(n,"end")){if(guidedFocused)guidedFocused(n["id"].toString());return;}
    down = e.position;
    initialView = view;dragExtent=0;dragClock=juce::Time::getMillisecondCounterHiRes();
    initialSelected = selected;
    groupIds.clear();
    groupShift = 0; groupPitch = 0;
    initialTime = timeAt(e.position.x);
    initialPitch = pitchAt(e.position.y);
    temporal = e.mods.isShiftDown();
    additive = e.mods.isCtrlDown() || e.mods.isCommandDown();
    moved = false;
    hasDraft = false;
    if (e.mods.isPopupMenu()) {
      if(e.position.x<58 && e.position.y>=l.rollTop && e.position.y<l.dividerKeys) {
        if(bandContext)bandContext(e.getScreenPosition(),pitchAt(e.position.y));return;
      }
      if (e.position.y < l.rollTop - 31 || e.position.y >= l.keyTop)
        return;
      auto stacked=stackedAt(e.position);
      if(stacked.size()>1){if(context)context(e.getScreenPosition(),object({{"kind","overlap"},{"ids",idsVar(stacked)}}));return;}
      var target;
      double t = timeAt(e.position.x);
      int p = pitchAt(e.position.y);
      for(auto& n:guidedGhosts)if((int)n["pitch"]==p&&number(n,"start")<=t&&number(n,"end")>=t)target=object({{"kind","guided"},{"item",n}});
      for (auto &n : ghosts)
        if ((int)n["pitch"] == p && number(n, "start") <= t &&
            number(n, "end") >= t)
          target = object({{"kind", "missing"}, {"item", n}});
      if(target.isVoid()) {
        for(auto& n:timing) {
          double begin=n["kind"].toString()=="split"?number(n,"start"):number(n,"end");
          double finish=n["kind"].toString()=="split"?number(n,"end"):number(n,"proposed_end");
          if((int)n["pitch"]==p && t>=begin && t<=finish) {
            target=object({{"kind",n["kind"]},{"item",n}});
            selectIds({(juce::int64)n["note_id"]});
            break;
          }
        }
      }
      if (target.isVoid())
        for (auto &n : holds)
          if ((int)n["pitch"] == p && number(n, "start") <= t &&
              number(n, "end") >= t)
            target = object({{"kind", "hold"}, {"item", n}});
      if (target["kind"].toString() == "hold") {
        selectIds(idSet(target["item"]["ids"]));
      } else if (target.isVoid()) {
        if (auto* n = hit(e.position)) {
          if (!selected.count(n->id)) selectIds({n->id});
          if (extra.count(n->id))
            target = object({{"kind", "extra"}, {"id", (juce::int64)n->id}});
        }
      }
      if (context)
        context(e.getScreenPosition(), target);
      return;
    }
    if(bandEnabled && !cutting() && !e.mods.isAnyModifierKeyDown() && e.mods.isLeftButtonDown() && (e.position.x<58 || (spectral && displayMode!=0)) && e.position.y>=l.rollTop && e.position.y<=l.dividerKeys &&
       (std::abs(e.position.y-bandY(true))<=6 || std::abs(e.position.y-bandY(false))<=6)) {
      bandDraggingHigh=std::abs(e.position.y-bandY(true))<=std::abs(e.position.y-bandY(false));
      dragKind=Band;drag=true;return;
    }
    if (e.mods.isAltDown() && e.mods.isLeftButtonDown() && editable) {
      if (auto *n = hit(e.position))
        if (remove)
          remove({n->id});
      return;
    }
    if(cutting() && e.mods.isLeftButtonDown() && !e.mods.isAnyModifierKeyDown() && editable) {
      if(auto* n=hit(e.position)) {
        double t=timeAt(e.position.x);
        if(t-n->start>=.01 && n->end-t>=.01 && split) {if(beginEdit)beginEdit();split(n->id,t);}
      }
      return;
    }
    if (e.mods.isMiddleButtonDown() || tool == 2) {
      dragKind = Grab;
      drag = true;
      vertical = e.position.y >= l.rollTop && e.position.y < l.dividerKeys;
      return;
    }
    if (displayMode==1 && std::abs(e.position.y - l.dividerAudio) <= 4) {
      dragKind = AudioDivider;
      drag = true;
      return;
    }
    if (e.position.y < 14 &&
        std::abs(e.position.x - x(number(playback, "position"))) < 10) {
      dragKind = Head;
      drag = true;
      return;
    }
    if (e.position.x < 58)
      return;
    if (e.position.y < l.rollTop - 31) {
      if (seek)
        seek(initialTime);
      return;
    }
    if (e.position.y < l.rollTop || e.position.y >= l.dividerKeys)
      return;
    auto *n = hit(e.position);
    if (tool == 1 && !n && editable) {
      dragKind = Add;
      drag = true;
      return;
    }
    if(n&&editable&&(e.mods.isCtrlDown()||e.mods.isCommandDown())&&!e.mods.isAltDown())duplicateDrag=true;
    if (n && editable && (!additive || duplicateDrag)) {
      temporal=false;original = *n;
      lastAudition = n->pitch;
      auto r = rect(*n);
      dragKind = r.getWidth() >= 16 && e.position.x - r.getX() <= 6 ? Start
                 : r.getWidth() >= 16 && r.getRight() - e.position.x <= 6 ? End
                 : r.getHeight() >= 14 && r.getWidth() >= 16 &&
                         (e.position.y - r.getY() <= 3 ||
                          r.getBottom() - e.position.y <= 3)
                     ? Velocity
                     : Move;
      if(e.mods.isShiftDown()||duplicateDrag)dragKind=Move;
      if (dragKind == Move && selected.count(n->id)) {
        for (const auto& item : notes)
          if (item.editable && selected.count(item.id)) groupIds.insert(item.id);
        if(groupIds.size()>1)setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
      }
    } else
      dragKind = Select;
    drag = true;
    if(dragKind==Move||dragKind==End)beginDragAutoRepeat(30);
    zoneA = zoneB = initialTime;
    zoneLow = zoneHigh = initialPitch;
  }
  void mouseDrag(const juce::MouseEvent &e) override {
    if(keyboardDragging){playKey(keyAt(e.position));return;}
    if(markerDragging){moved|=std::abs(e.position.x-down.x)>=4;if(moved){markerTime=View::clamp(number(movingMarker,"time")+(e.position.x-down.x)/layout().width*span(),0.,std::max(scoreDuration,number(audio,"duration")));if(inspect)inspect(timeName(markerTime));repaint();}return;}
    if(erasing){if(!busy)eraseTo(e.position);return;}
    if(maskPainting){brushPosition=e.position;extendMask(e.position);return;}
    if (!drag)
      return;
    auto dx = e.position.x - down.x, dy = e.position.y - down.y;
    auto l = layout();
    if (!moved && std::hypot(dx, dy) < 4)
      return;
    if(dragKind==Move){
      axisConstrained=axisConstrained||e.mods.isShiftDown();
      if(axisConstrained){
        if(moveAxis<=0){
          // Ignore initial jitter and ambiguous diagonals. Once chosen, the axis
          // belongs to this gesture, including after Shift is released.
          auto ax=std::abs(dx),ay=std::abs(dy);
          if(std::max(ax,ay)<6 || (ax<ay*1.5f && ay<ax*1.5f)){
            moveAxis=-1;hasDraft=false;groupShift=0;groupPitch=0;repaint();return;
          }
          moveAxis=ax>ay?1:2;
        }
        if(moveAxis==1)dy=0;else dx=0;
      }
    }
    if (!moved && dragKind >= Move && dragKind <= Velocity && beginEdit)
      beginEdit();
    moved = true;
    if(dragKind==Band) {
      int pitch=pitchAt(e.position.y);
      if(bandDraggingHigh) bandHigh=juce::jlimit(bandLow,127,pitch);
      else bandLow=juce::jlimit(0,bandHigh,pitch);
      repaint();return;
    }
    if (dragKind == Grab) {
      if (view.zoom > 1)
        view.pan = View::clamp(
            initialView.pan - dx / (l.width * (view.zoom - 1)), 0, 1);
      if (vertical && view.pitchZoom > 1)
        view.pitchPan =
            View::clamp(initialView.pitchPan - dy / (std::max(1., l.roll - 16) *
                                                     (view.pitchZoom - 1)),
                        0, 1);
      notify();
      return;
    }
    if (dragKind == AudioDivider) {
      view.audioHeight = initialView.audioHeight + dy;
      view.audioHeight = std::min(2000., layout().audio);
      notify();
      return;
    }
    if (dragKind == Head) {
      if (seek)
        seek(timeAt(e.position.x));
      return;
    }
    if (dragKind >= Move && dragKind <= Velocity) {
      if(dragKind==Move||dragKind==End){
        const auto now=juce::Time::getMillisecondCounterHiRes();double dt=View::clamp((now-dragClock)/1000.,.001,.05);dragClock=now;
        const double visibleSpan=span();double visibleStart=start();
        double speed=e.position.x>58+l.width-26?View::clamp((e.position.x-(58+l.width-26))/26.,0,2):e.position.x<84?-View::clamp((84-e.position.x)/26.,0,2):0;
        if(dragKind==Move&&moveAxis==2)speed=0;
        if(speed!=0){visibleStart=View::clamp(visibleStart+speed*visibleSpan*.65*dt,0,1200-visibleSpan);
          dragExtent=std::min(1200.,std::max(duration(),visibleStart+visibleSpan));frameTime(visibleStart,visibleSpan);}
      }
      draft = original;
      double ds = (dragKind==Move||dragKind==End)?start()+(e.position.x-58)/l.width*span()-initialTime:dx/l.width*span(),
             minimum = std::min(.002, original.end - original.start);
      if(dragKind==Move&&moveAxis==2)ds=0;
      if (dragKind == Start)
        draft.start =
            View::clamp(original.start + ds, 0, original.end - minimum);
      else if (dragKind == End)
        draft.end = View::clamp(original.end + ds, original.start + minimum,1200.);
      else if (dragKind == Velocity)
        draft.velocity = juce::jlimit(
            1, 127, original.velocity + (int)std::floor(-dy / 2 + .5));
      else {
        if (groupIds.size() > 1) {
          double earliest = duration(), latest = 0; int low = 127, high = 0;
          for (const auto& item : notes) if (groupIds.count(item.id)) {
            earliest = std::min(earliest, item.start); latest = std::max(latest,item.end);
            low = std::min(low,item.pitch); high = std::max(high,item.pitch);
          }
          groupShift = View::clamp(ds,-earliest,1200.-latest);
          groupPitch = juce::jlimit(-low,127-high,(int)std::floor(-dy/row()+.5));
          draft.start = original.start + groupShift; draft.end = original.end + groupShift;
          draft.pitch = original.pitch + groupPitch;
          extendDragTimeline(latest+groupShift);hasDraft = true; inspectNote(draft); repaint(); return;
        }
        draft.start = View::clamp(original.start + ds, 0,
                                  1200. - (original.end - original.start));
        draft.end = draft.start + original.end - original.start;
        draft.pitch = juce::jlimit(
            0, 127, original.pitch + (int)std::floor(-dy / row() + .5));
        if (draft.pitch != lastAudition && audition && !draft.muted) {
          lastAudition = draft.pitch;
          audition(draft);
        }
      }
      if(dragKind==Move&&moveAxis==2){draft.start=original.start;draft.end=original.end;}
      if(dragKind==Move||dragKind==End)extendDragTimeline(draft.end);
      hasDraft = true;
      inspectNote(draft);
      repaint();
      return;
    }
    zoneA = std::min(initialTime, timeAt(e.position.x));
    zoneB = std::max(initialTime, timeAt(e.position.x));
    zoneLow = temporal ? 0 : std::min(initialPitch, pitchAt(e.position.y));
    zoneHigh = temporal ? 127 : std::max(initialPitch, pitchAt(e.position.y));
    repaint();
  }
  void mouseUp(const juce::MouseEvent &e) override {
    beginDragAutoRepeat(0);
    if(keyboardDragging){stopKeyAudition();return;}
    if(markerDragging){markerDragging=false;if(moved&&markerChanged)markerChanged(object({{"kind","modify_marker"},{"id",movingMarker["id"]},{"at",markerTime},{"name",movingMarker["name"]}}));movingMarker=var();repaint();return;}
    if(erasing){if(!busy)eraseTo(e.position);erasing=false;auto ids=std::move(erasedIds);erasedIds.clear();if(!busy&&!ids.empty()&&remove)remove(ids);repaint();return;}
    if(maskPainting){extendMask(e.position);maskPainting=false;++maskDraftRevision;if(maskCommitted&&!maskDraft.points.empty())maskCommitted(maskDraft);repaint();return;}
    if (!drag)
      return;
    auto kind = dragKind;
    drag = false;
    mouseMove(e);
    if(kind==Band){repaint();if(bandChanged)bandChanged();return;}
    if (kind == Head) {
      if (seek)
        seek(timeAt(e.position.x));
      return;
    }
    if (kind == Grab || kind == AudioDivider) {
      notify();
      return;
    }
    if (kind == Add) {
      if (!moved && add)
        add(initialTime, initialPitch);
      return;
    }
    if(kind==Move&&axisConstrained&&moveAxis<0){cancelTimelinePreview();repaint();return;}
    if (hasDraft) {
      auto n = draft;
      hasDraft = false;
      if (n.start != original.start || n.end != original.end ||
          n.pitch != original.pitch || n.velocity != original.velocity) {
        if(kind==Move&&duplicateDrag&&duplicateGroup){auto ids=groupIds.size()>1?groupIds:std::set<std::int64_t>{original.id};duplicateGroup(ids,groupIds.size()>1?groupShift:n.start-original.start,groupIds.size()>1?groupPitch:n.pitch-original.pitch);return;}
        if (kind == Move && groupIds.size()>1 && moveGroup) {
          selectIds(groupIds); moveGroup(groupIds,groupShift,groupPitch); return;
        }
        selectIds({n.id});
        if (modify)
          modify(n);
      }else cancelTimelinePreview();
      return;
    }
    if (moved) {
      selectZone(zoneA, zoneB, zoneLow, zoneHigh, temporal, additive);
      return;
    }
    if (auto *n = hit(e.position)) {
      auto ids = additive ? selected : std::set<std::int64_t>{};
      if (ids.count(n->id))
        ids.erase(n->id);
      else
        ids.insert(n->id);
      selectIds(ids);
      if (!additive && !temporal && !n->muted && !(bool)playback["playing"] &&
          audition)
        audition(*n);
    } else {
      clearSelection();
      if (!additive && !temporal && seek)
        seek(initialTime);
    }
  }
  void mouseDoubleClick(const juce::MouseEvent &e) override {
    if(maskMode)return;
    if (busy)
      return;
    if(e.position.x<58) {
      view.pan=0;if(seek)seek(0);notify();return;
    }
    auto l = layout();
    if (displayMode==1 && std::abs(e.position.y - l.dividerAudio) <= 4) {
      view.audioHeight = 116;
      notify();
      return;
    }
    if (editable && tool != 1 && tool != 4 && e.position.y >= l.rollTop &&
        e.position.y < l.dividerKeys && !hit(e.position) &&
        !e.mods.isAnyModifierKeyDown())
      if (add)
        add(timeAt(e.position.x), pitchAt(e.position.y));
  }
  void mouseWheelMove(const juce::MouseEvent &e,
                      const juce::MouseWheelDetails &w) override {
    if(maskPainting)return;
    if(!maskMode&&tool==4&&!e.mods.isAnyModifierKeyDown()){if(!erasing){eraserDiameter=View::clamp(eraserDiameter*std::exp(w.deltaY*2),8,512);brushPosition=e.position;brushInside=true;repaint();}return;}
    if (drag)
      return;
    auto l = layout();
    auto delta = -w.deltaY * 360, dx = -w.deltaX * 360;
    auto px = e.position.x, py = e.position.y;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
      view.proportional(duration(), std::exp(-delta * .003),
                        (px - 58) / l.width,
                        py >= l.rollTop && py < l.dividerKeys
                            ? (py - l.rollTop - 8) / std::max(1., l.roll - 16)
                            : .5);
    else if (e.mods.isAltDown() && py >= l.rollTop && py < l.dividerKeys) {
      auto anchor = View::clamp(
               (py - l.rollTop - 8) / std::max(1., l.roll - 16), 0, 1),
           pitch = top() - anchor * 128 / view.pitchZoom;
      view.pitchZoom =
          View::clamp(view.pitchZoom * std::exp(-delta * .003), 1, 64);
      view.pitchPan =
          view.pitchZoom == 1
              ? 0
              : View::clamp((127 - (pitch + anchor * 128 / view.pitchZoom)) /
                                (128 - 128 / view.pitchZoom),
                            0, 1);
    } else if (e.mods.isShiftDown() || std::abs(dx) > std::abs(delta) ||
               py < l.rollTop)
      view.pan = View::clamp(view.pan + (dx ? dx : delta) / l.width /
                                            std::max(view.zoom - 1, 1.),
                             0, 1);
    else if (view.pitchZoom > 1 && py < l.dividerKeys)
      view.pitchPan =
          View::clamp(view.pitchPan + delta / (std::max(1., l.roll - 16) *
                                               (view.pitchZoom - 1)),
                      0, 1);
    notify();
  }
  void mouseMove(const juce::MouseEvent &e) override {
    auto key=keyAt(e.position);if(key>=0){brushInside=false;setMouseCursor(juce::MouseCursor::PointingHandCursor);setTooltip(tr(String::fromUTF8("Préécouter"))+" · "+pitchName(key)+(key<21||key>108?" · "+tr(String::fromUTF8("Hors registre piano 88 touches")):String()));if(inspect)inspect(getTooltip());return;}

    auto marker=markerAt(e.position);if(!marker.isVoid()){setTooltip(tr(String::fromUTF8("Repère"))+" · "+timeName(number(marker,"time"))+(marker["name"].toString().isEmpty()?String():" · "+marker["name"].toString()));return;}
    if(maskMode||tool==4){brushPosition=e.position;brushInside=true;setTooltip({});if(inspect)inspect(tool==4&&!maskMode?tr(String::fromUTF8("Gomme : molette pour régler le diamètre ; Ctrl+Z pour annuler un passage."))+" ("+String(int(eraserDiameter))+" px)":String());updateToolCursor();return;}
    if (drag)
      return;
    auto l = layout();
    int hover=bandEnabled && e.position.y>=l.rollTop && e.position.y<=l.dividerKeys &&
      (e.position.x<58 || (spectral && displayMode!=0)) ?
      (std::abs(e.position.y-bandY(true))<=8 ? 1 : std::abs(e.position.y-bandY(false))<=8 ? 0 : -1) : -1;
    if(hover!=hoveredBand){hoveredBand=hover;repaint();}
    auto cursor = tool == 2   ? juce::MouseCursor::DraggingHandCursor
                  : tool == 1 ? juce::MouseCursor::CrosshairCursor
                              : juce::MouseCursor::NormalCursor;
    if(bandEnabled && (e.position.x<58 || (spectral && displayMode!=0)) && (std::abs(e.position.y-bandY(true))<=8 || std::abs(e.position.y-bandY(false))<=8))
      cursor=juce::MouseCursor::UpDownResizeCursor;
    else if (displayMode==1 && std::abs(e.position.y - l.dividerAudio) < 4)
      cursor = juce::MouseCursor::UpDownResizeCursor;
    else if (auto *n = hit(e.position)) {
      auto r = rect(*n);
      if (r.getWidth() >= 16 &&
          (e.position.x - r.getX() <= 6 || r.getRight() - e.position.x <= 6))
        cursor = juce::MouseCursor::LeftRightResizeCursor;
      else if (r.getHeight() >= 14 && r.getWidth() >= 16 &&
               (e.position.y - r.getY() <= 3 ||
                r.getBottom() - e.position.y <= 3))
        cursor = juce::MouseCursor::UpDownResizeCursor;
      else if(tool==0 && editable && n->editable && selected.count(n->id) && selected.size()>1)
        cursor = juce::MouseCursor::UpDownLeftRightResizeCursor;
      setTooltip(noteInformation(*n));
    } else
      setTooltip({});
    if (inspect)
      inspect(getTooltip());
    setMouseCursor(cutting() && editable ? scissorsCursor():cursor);
  }
  bool keyPressed(const juce::KeyPress &k) override {
    if(k==juce::KeyPress::escapeKey&&keyboardDragging){stopKeyAudition();return true;}
    if(k==juce::KeyPress::escapeKey&&markerDragging){markerDragging=false;movingMarker=var();repaint();return true;}
    if(seekByKey(k))return true;
    if (k == juce::KeyPress::escapeKey) {
      beginDragAutoRepeat(0);cancelTimelinePreview();
      erasing=false;erasedIds.clear();
      drag = false;
      hasDraft = false;
      tool = 0;
      clearSelection();
      return true;
    }
    if (busy || !editable)
      return false;
    if(k.getKeyCode()=='C' && !k.getModifiers().isAnyModifierKeyDown()) {cutHeld=true;setMouseCursor(scissorsCursor());return true;}
    if (k.getModifiers().isCtrlDown() && k.getKeyCode() == 'A') {
      std::set<std::int64_t> ids;
      for (auto &n : notes)
        if (n.editable)
          ids.insert(n.id);
      selectIds(ids);
      return true;
    }
    if (k == juce::KeyPress::deleteKey) {
      if (remove)
        remove(selected);
      return true;
    }
    if (!k.getModifiers().isAnyModifierKeyDown()) {
      auto key = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
      if (key == 'f') {
        if (merge)
          merge(selected);
        return true;
      }
      if (key == 'm') {
        if (mute)
          mute(selected);
        return true;
      }
    }
    return false;
  }
  void setDetail(var p, double a, double b) {
    detail = std::move(p);
    detailStart = a;
    detailEnd = b;
    repaint();
  }
  void clearDetail() { detail = var(); }

  void mirrorLaneTime(double total,double a,double s){dragExtent=total>baseDuration()+1e-8?total:0;frameTime(a,s);}
  void cancelPointerEdit(){beginDragAutoRepeat(0);cancelTimelinePreview();drag=false;hasDraft=false;erasing=false;erasedIds.clear();}
  bool pointerEditing()const{return drag||erasing||maskPainting;}
private:
  void drawSpectrum(juce::Graphics& g,double destTop,double destHeight,float alpha) {
    if(!spectrum.isValid() || spectrumEnd<=spectrumStart) return;
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(58,(int)destTop,(int)layout().width,(int)destHeight);
    g.setOpacity(alpha);
    // Image rows are semitones in descending order, with three frequency bands per note.
    double fullHeight = displayMode==2&&!stackHost ? row()*128 : destHeight;
    double fullTop = displayMode==2&&!stackHost ? y(127) : destTop;
    auto target=juce::Rectangle<float>((float)x(spectrumStart),(float)fullTop,
      (float)(x(spectrumEnd)-x(spectrumStart)),(float)fullHeight);
    g.drawImage(spectrum,target,juce::RectanglePlacement::stretchToFit);
    if(bandEnabled && displayMode==1) {
      g.setOpacity(1);
      double upper=destTop+(127-bandHigh)/128.*destHeight,lower=destTop+(128-bandLow)/128.*destHeight;
      g.setColour(juce::Colour(0xfff9faf4).withAlpha(.72f));
      g.fillRect(58.f,(float)destTop,(float)layout().width,(float)(upper-destTop));
      g.fillRect(58.f,(float)lower,(float)layout().width,(float)(destTop+destHeight-lower));
    }
  }
  enum Kind {
    Select,
    Add,
    Grab,
    Head,
    AudioDivider,
    Move,
    Start,
    End,
    Velocity,
    Band
  };
  bool bandDraggingHigh=false;
  Kind dragKind = Select;
  bool drag = false, moved = false, temporal = false, additive = false,
       vertical = false, hasDraft = false;
  juce::Point<float> down;
  double dragExtent=0,dragClock=0;
  void extendDragTimeline(double end){
    if(end<duration()-.5)return;
    auto a=start(),s=span();dragExtent=std::min(1200.,std::max(duration(),end+std::max(1.,s*.1)));frameTime(a,s);
  }
  View initialView;
  Note original, draft;
  int initialPitch = 0, zoneLow = 0, zoneHigh = 127, lastAudition = -1;
  double initialTime = 0, zoneA = 0, zoneB = 0, detailStart = 0, detailEnd = 0;
  std::set<std::int64_t> initialSelected, groupIds;
  double groupShift = 0; int groupPitch = 0;
  var detail;
  bool black(int p) const {
    return p % 12 == 1 || p % 12 == 3 || p % 12 == 6 || p % 12 == 8 ||
           p % 12 == 10;
  }
  Note *hit(juce::Point<float> p) {
    auto l = layout();
    if (p.x < 58 || p.x > 58 + l.width || p.y < l.rollTop ||
        p.y >= l.dividerKeys)
      return nullptr;
    auto t = timeAt(p.x);
    auto pitch = pitchAt(p.y);
    for (auto &n : notes)
      if (n.editable && n.pitch == pitch && n.start <= t && n.end >= t)
        return &n;
    return nullptr;
  }
  void notify() {
    updateHandles();
    repaint();
    if (changed)
      changed();
  }
  void dashed(juce::Graphics &g, juce::Rectangle<float> r) {
    juce::Path p;
    p.addRectangle(r);
    const float pattern[] = {4, 3};
    juce::PathStrokeType(1).createDashedStroke(p, p, pattern, 2);
    g.fillPath(p);
  }
  // JUCE's TooltipClient is supplied through a small internal property below.
};
