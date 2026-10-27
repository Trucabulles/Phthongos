#include "Widgets.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
void attachDialogOwner(juce::DialogWindow& dialog,juce::Component* owner){
  if(!owner||!owner->getPeer()||!dialog.getPeer())return;
#if JUCE_WINDOWS
  auto hwnd=static_cast<HWND>(dialog.getPeer()->getNativeHandle());
  auto parent=static_cast<HWND>(owner->getPeer()->getNativeHandle());
  SetWindowLongPtrW(hwnd,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(parent));
  auto style=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
  SetWindowLongPtrW(hwnd,GWL_EXSTYLE,(style & ~WS_EX_APPWINDOW)|WS_EX_TOOLWINDOW);
  SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
#endif
}
bool dialogHasOwner(const juce::DialogWindow& dialog,const juce::Component& owner){
#if JUCE_WINDOWS
  if(!dialog.getPeer()||!owner.getPeer())return false;
  auto hwnd=static_cast<HWND>(dialog.getPeer()->getNativeHandle());
  auto parent=static_cast<HWND>(owner.getPeer()->getNativeHandle());
  auto style=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
  return GetWindow(hwnd,GW_OWNER)==parent&&(style&WS_EX_APPWINDOW)==0&&(style&WS_EX_TOOLWINDOW)!=0;
#else
  return true;
#endif
}
