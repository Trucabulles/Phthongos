#include "FileTransfer.h"
#include "../core/services/Diagnostics.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <wrl/client.h>
#endif

juce::Image fileTransfer::dragImage(const juce::StringArray& files) {
  juce::Image image(juce::Image::ARGB,260,82,true);juce::Graphics g(image);
  const bool batch=files.size()>1;
  g.setColour(juce::Colour(0xffd0d9de));
  if(batch){g.fillRoundedRectangle(18,9,232,65,5);g.fillRoundedRectangle(13,5,232,65,5);}
  g.setColour(juce::Colour(0xfffaf9f3));g.fillRoundedRectangle(7,1,232,65,5);
  g.setColour(juce::Colour(0xff9aa7ae));g.drawRoundedRectangle(7,1,232,65,5,1);
  juce::Path paper;paper.startNewSubPath(19,13);paper.lineTo(39,13);paper.lineTo(48,22);paper.lineTo(48,52);paper.lineTo(19,52);paper.closeSubPath();
  g.setColour(juce::Colour(0xffe9e0d1));g.fillPath(paper);g.setColour(juce::Colour(0xff244d65));g.strokePath(paper,juce::PathStrokeType(1));
  g.setFont(juce::FontOptions(10,juce::Font::bold));g.drawText("MIDI",19,30,29,15,juce::Justification::centred);
  g.setFont(juce::FontOptions(13));
  auto label=files.isEmpty()?juce::String():juce::File(files[0]).getFileName();
  g.drawFittedText(label,59,12,170,21,juce::Justification::centredLeft,1);
  g.setColour(juce::Colour(0xff667c89));g.setFont(juce::FontOptions(12));
  g.drawText(batch?juce::String(files.size())+" MIDI":".mid",59,36,170,18,juce::Justification::centredLeft);
  return image;
}

#if JUCE_WINDOWS
namespace {
using Microsoft::WRL::ComPtr;
struct OleScope {
  HRESULT result=OleInitialize(nullptr);
  ~OleScope(){if(SUCCEEDED(result))OleUninitialize();}
};
ComPtr<IDataObject> shellFiles(const juce::StringArray& files) {
  std::vector<PIDLIST_ABSOLUTE> ids;
  for(const auto& path:files){PIDLIST_ABSOLUTE id=nullptr;
    if(!juce::File(path).existsAsFile()||FAILED(SHParseDisplayName(path.toWideCharPointer(),nullptr,&id,0,nullptr))){for(auto p:ids)CoTaskMemFree(p);return {};}
    ids.push_back(id);
  }
  ComPtr<IShellItemArray> items;ComPtr<IDataObject> data;
  if(!ids.empty()&&SUCCEEDED(SHCreateShellItemArrayFromIDLists(UINT(ids.size()),const_cast<PCIDLIST_ABSOLUTE*>(ids.data()),&items)))
    items->BindToHandler(nullptr,BHID_DataObject,IID_PPV_ARGS(&data));
  for(auto id:ids)CoTaskMemFree(id);
  return data;
}
HRESULT attachImage(IDataObject* data,const juce::StringArray& files) {
  ComPtr<IDragSourceHelper> helper;
  auto result=CoCreateInstance(CLSID_DragDropHelper,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&helper));
  if(FAILED(result))return result;
  auto image=fileTransfer::dragImage(files);BITMAPINFO info{};
  info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=image.getWidth();info.bmiHeader.biHeight=-image.getHeight();
  info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
  void* pixels=nullptr;auto bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
  if(!bitmap)return E_OUTOFMEMORY;
  auto* out=static_cast<DWORD*>(pixels);
  // Shell applies premultiplication itself; provide straight-alpha BGRA pixels.
  for(int y=0;y<image.getHeight();++y)for(int x=0;x<image.getWidth();++x)*out++=image.getPixelAt(x,y).getARGB();
  SHDRAGIMAGE drag{};drag.sizeDragImage={image.getWidth(),image.getHeight()};drag.ptOffset={0,0};drag.hbmpDragImage=bitmap;drag.crColorKey=CLR_NONE;
  result=helper->InitializeFromBitmap(&drag,data);
  // The helper takes ownership on success.
  if(FAILED(result))DeleteObject(bitmap);
  return result;
}
class DropSource final : public IDropSource {
public:
  explicit DropSource(bool cancel=false):testCancellation(cancel){}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override {
    if(!out)return E_POINTER;*out=nullptr;
    if(id!=IID_IUnknown&&id!=IID_IDropSource)return E_NOINTERFACE;
    *out=static_cast<IDropSource*>(this);AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef()override{return ++references;}
  ULONG STDMETHODCALLTYPE Release()override{auto n=--references;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys)override {
    if(testCancellation||escape)return DRAGDROP_S_CANCEL;
    return (keys&MK_LBUTTON)?S_OK:DRAGDROP_S_DROP;
  }
  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD)override{return DRAGDROP_S_USEDEFAULTCURSORS;}
private:
  std::atomic<ULONG> references{1};bool testCancellation;
};
}
#endif

bool fileTransfer::performExternalDrag(const juce::StringArray& files,juce::Component* source,std::function<void()> done) {
#if JUCE_WINDOWS
  // Run OLE on the owning GUI STA. JUCE's worker-based external drag depends
  // on shared input queues, deliberately disabled in Phthongos for pointer safety.
  if(!juce::MessageManager::getInstance()->isThisTheMessageThread()||files.isEmpty())return false;
  OleScope ole;if(FAILED(ole.result))return false;
  auto data=shellFiles(files);if(!data)return false;
  auto imageResult=attachImage(data.Get(),files);
  phthongos::diagnostics::event("midi_drag_start","files="+juce::String(files.size())+" image="+juce::String::toHexString(int(imageResult)));
  ComPtr<IDropSource> drag;drag.Attach(new DropSource);
  DWORD effect=DROPEFFECT_NONE;
  // OLE owns capture for the duration of its modal message loop.
  ReleaseCapture();
  auto result=DoDragDrop(data.Get(),drag.Get(),DROPEFFECT_COPY,&effect);
  phthongos::diagnostics::event("midi_drag_finish","result="+juce::String::toHexString(int(result))+" effect="+juce::String(int(effect)));
  if(done)done();
  juce::Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
  return result==DRAGDROP_S_DROP||result==DRAGDROP_S_CANCEL;
#else
  return juce::DragAndDropContainer::performExternalDragDropOfFiles(files,false,source,std::move(done));
#endif
}

bool fileTransfer::nativeDragTests(const juce::File& directory) {
#if JUCE_WINDOWS
  OleScope ole;if(FAILED(ole.result))return false;
  juce::StringArray files;
  for(auto name:{juce::String::fromUTF8("Piano été.mid"),juce::String("Bass.mid")}){
    auto file=directory.getChildFile(name);if(!file.replaceWithText("MIDI regression"))return false;files.add(file.getFullPathName());
  }
  auto data=shellFiles(files);if(!data||FAILED(attachImage(data.Get(),files)))return false;
  FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM medium{};
  if(FAILED(data->GetData(&format,&medium)))return false;
  auto drop=static_cast<HDROP>(medium.hGlobal);bool matches=DragQueryFileW(drop,0xffffffff,nullptr,0)==UINT(files.size());
  for(int i=0;i<files.size();++i){wchar_t path[32768]{};DragQueryFileW(drop,UINT(i),path,32768);matches=matches&&juce::String(path)==files[i];}
  ReleaseStgMedium(&medium);
  // Use the real filesystem Shell drop target in an isolated test directory.
  // This checks that Explorer's copy path can consume the exact IDataObject
  // used by the button, without moving the pointer or touching the Desktop.
  auto destination=directory.getChildFile("shell-drop-"+juce::Uuid().toString());
  if(destination.createDirectory().failed())return false;
  ComPtr<IShellItem> folder;ComPtr<IDropTarget> target;
  auto log=[&](const char* stage,HRESULT hr,DWORD effect=0){directory.getChildFile("shell-test.txt").appendText(juce::String(stage)+" "+juce::String::toHexString(int(hr))+" effect="+juce::String(int(effect))+"\n");};
  auto hr=SHCreateItemFromParsingName(destination.getFullPathName().toWideCharPointer(),nullptr,IID_PPV_ARGS(&folder));log("folder",hr);if(FAILED(hr))return false;
  hr=folder->BindToHandler(nullptr,BHID_SFUIObject,IID_PPV_ARGS(&target));log("target",hr);if(FAILED(hr))return false;
  DWORD copy=DROPEFFECT_COPY;POINTL point{};
  hr=target->DragEnter(data.Get(),MK_LBUTTON,point,&copy);log("enter",hr,copy);if(FAILED(hr)||!(copy&DROPEFFECT_COPY))return false;
  copy=DROPEFFECT_COPY;
  hr=target->Drop(data.Get(),0,point,&copy);log("drop",hr,copy);if(FAILED(hr)||!(copy&DROPEFFECT_COPY))return false;
  for(const auto& path:files){auto original=juce::File(path);matches=matches&&destination.getChildFile(original.getFileName()).loadFileAsString()==original.loadFileAsString();}
  // Continuation semantics can be checked without injecting a physical mouse
  // press. Starting DoDragDrop without one is rejected by OLE with E_FAIL.
  ComPtr<IDropSource> source;source.Attach(new DropSource);
  return matches&&source->QueryContinueDrag(FALSE,MK_LBUTTON)==S_OK
    &&source->QueryContinueDrag(FALSE,0)==DRAGDROP_S_DROP
    &&source->QueryContinueDrag(TRUE,MK_LBUTTON)==DRAGDROP_S_CANCEL;
#else
  juce::ignoreUnused(directory);return true;
#endif
}
