#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace pointerSafety {
enum class Recovery { none, endUnbounded, refresh };
struct State {
 bool foregroundOwned=false,overOwned=false,buttonsDown=false,suppressed=false;
 bool visible=true,unbounded=false;
};
inline Recovery recoveryFor(const State& state,bool force){
 // Never change another application's pointer, a held drag or touch suppression.
 if(!state.foregroundOwned||!state.overOwned||state.buttonsDown||state.suppressed)return Recovery::none;
 if(state.unbounded)return Recovery::endUnbounded;
 return force||!state.visible?Recovery::refresh:Recovery::none;
}
}
// Recovery is restricted to an active Phthongos window under the pointer.
void restorePointerAfterInterruption(bool force = false);

class VisibleCursorSlider : public juce::Slider {
public:
  VisibleCursorSlider() {
    setVelocityBasedMode(false);
    setVelocityModeParameters(1., 1, 0., false);
  }
  void mouseUp(const juce::MouseEvent& e) override {
    juce::Slider::mouseUp(e);
    restorePointerAfterInterruption();
  }
};
