#pragma once
#include "Roll.h"
#include "Widgets.h"
// Alternate editors of the existing document. No MIDI document or audio engine here.
class StackedVoices : public juce::Component {
public:
 struct Frame {int height=220;bool folded=false,framed=false,manual=false,heightManual=false;double top=84,row=12;};
 class Lane;
 struct Body : juce::Component {void paint(juce::Graphics& g)override{g.fillAll(juce::Colour(0xfff6f3eb));}};
 Roll& master;juce::Viewport viewport;Body body;
 std::map<int,std::unique_ptr<Lane>> lanes;std::map<int,Frame> frames;
 std::vector<int> order;bool common=false,commonFramed=false,commonManual=false,entryFit=true,transferring=false,syncing=false;double commonTop=96,commonRow=12;
 int active=-1,dropTarget=-1;bool busy=false;std::set<int> hidden,locked;
 std::map<int,String> audioSources;std::function<void(int)> reloadAudio;
 std::function<void(int)> activate,instrument,toggleMute,toggleVisible,toggleLock,colour;
 std::function<void(int,double)> level;
 std::function<void(const std::set<std::int64_t>&,int)> transfer;
 std::function<void()> changed;
 explicit StackedVoices(Roll&);~StackedVoices() override;
 void sync(const std::vector<std::pair<int,String>>&,int,const std::set<std::int64_t>&,const var&,bool);
 void resized() override;void layoutLanes();void refreshFrames();void frameVoice(int);void fit(int);void setCommon(bool);void focusCommon();void focusOnEntry(){entryFit=true;}
 var state()const;void load(const var&);void cancelTransfer();
 Roll& editorFor(int);juce::Component& laneFor(int);
 int targetAt(juce::Point<int>)const;void dragTo(juce::Point<int>);bool allowed(int,const std::set<std::int64_t>&)const;
 void publish(Lane&);void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
};
