#include "PointerSafety.h"
#include "../core/services/Diagnostics.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
pointerSafety::State pointerState(){
 pointerSafety::State state;
 state.buttonsDown=juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown();
#if JUCE_WINDOWS
 CURSORINFO cursor{};cursor.cbSize=sizeof(cursor);
 if(!GetCursorInfo(&cursor))return state;
 auto owned=[](HWND window){DWORD pid=0;if(window)GetWindowThreadProcessId(window,&pid);return pid==GetCurrentProcessId();};
 state.foregroundOwned=owned(GetForegroundWindow());
 state.overOwned=owned(WindowFromPoint(cursor.ptScreenPos));
 state.visible=(cursor.flags&CURSOR_SHOWING)!=0;
 state.suppressed=(cursor.flags&0x2)!=0; // CURSOR_SUPPRESSED: touch/pen suppression is intentional.
#else
 state.foregroundOwned=juce::Process::isForegroundProcess();
 auto* under=juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
 state.overOwned=under&&under->isShowing();
#endif
 return state;
}
}
void restorePointerAfterInterruption(bool force){
 auto state=pointerState();
 for(auto source:juce::Desktop::getInstance().getMouseSources()){
  if(!source.isMouse())continue;
  state.unbounded=source.isUnboundedMouseMovementEnabled();
  auto action=pointerSafety::recoveryFor(state,force);
  if(action==pointerSafety::Recovery::none)continue;
  if(action==pointerSafety::Recovery::endUnbounded)source.enableUnboundedMouseMovement(false);
  // Bypass JUCE's cursor-handle cache even when no unbounded drag is active.
  // Retain the current tool's cursor rather than replacing it with an arrow.
  source.forceMouseCursorUpdate();source.triggerFakeMove();
  if(!state.visible||state.unbounded){
   static double lastReport=-10000;double now=juce::Time::getMillisecondCounterHiRes();
   if(now-lastReport>=2000){lastReport=now;auto after=pointerState();phthongos::diagnostics::event("cursor_recovery",juce::String("hidden=")+juce::String(int(!state.visible))+" unbounded="+juce::String(int(state.unbounded))+" forced="+juce::String(int(force))+" visible_after="+juce::String(int(after.visible)));}
  }
 }
}
