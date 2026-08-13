/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2026 Baldur Karlsson
 * Copyright (c) 2014 Crytek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "core/core.h"
#include "hooks/hooks.h"
#include "dxgi_wrapped.h"

// 3DMigoto-style DXGI hooking.
//
// When USE_VTABLE_HOOK is defined the factory objects returned to the game are
// the real DXGI factories - we no longer replace them with WrappedIDXGIFactory.
// Instead the factory's vtable slots for the CreateSwapChain* methods are
// hooked directly (see os/win32/win32_vtablehook.h), which is the same
// technique 3DMigoto uses in DirectX11/HookedDXGI.cpp. Preserving object
// identity means other components that vtable-hook the same methods (anti-cheat
// drivers, overlays, mod frameworks) keep seeing the object they expect, and
// our hook chains with whatever hook was already installed in the slot.
//
// The swap chain itself is still wrapped in WrappedIDXGISwapChain4 because
// RenderDoc's D3D11/D3D12 drivers need the wrapper to track backbuffers.
#ifdef USE_VTABLE_HOOK
#define USE_VTABLE_HOOK_IMPL OPTION_ON
#else
#define USE_VTABLE_HOOK_IMPL OPTION_OFF
#endif

#if ENABLED(USE_VTABLE_HOOK_IMPL) && ENABLED(RDOC_WIN32)
#include "os/win32/win32_vtablehook.h"

ID3DDevice *GetD3DDevice(IUnknown *pDevice);

typedef HRESULT(STDMETHODCALLTYPE *PFN_FactoryCreateSwapChain)(IDXGIFactory *, IUnknown *,
                                                               DXGI_SWAP_CHAIN_DESC *,
                                                               IDXGISwapChain **);
typedef HRESULT(STDMETHODCALLTYPE *PFN_FactoryCreateSwapChainForHwnd)(
    IDXGIFactory2 *, IUnknown *, HWND, const DXGI_SWAP_CHAIN_DESC1 *,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **);
typedef HRESULT(STDMETHODCALLTYPE *PFN_FactoryCreateSwapChainForCoreWindow)(
    IDXGIFactory2 *, IUnknown *, IUnknown *, const DXGI_SWAP_CHAIN_DESC1 *, IDXGIOutput *,
    IDXGISwapChain1 **);
typedef HRESULT(STDMETHODCALLTYPE *PFN_FactoryCreateSwapChainForComposition)(
    IDXGIFactory2 *, IUnknown *, const DXGI_SWAP_CHAIN_DESC1 *, IDXGIOutput *, IDXGISwapChain1 **);

// The factory vtable is shared between all instances of a given class, so a
// single set of "original" pointers covers every factory.
static PFN_FactoryCreateSwapChain fnOrigCreateSwapChain = NULL;
static PFN_FactoryCreateSwapChainForHwnd fnOrigCreateSwapChainForHwnd = NULL;
static PFN_FactoryCreateSwapChainForCoreWindow fnOrigCreateSwapChainForCoreWindow = NULL;
static PFN_FactoryCreateSwapChainForComposition fnOrigCreateSwapChainForComposition = NULL;
static bool s_FactoryHooksInstalled = false;

static HRESULT STDMETHODCALLTYPE Hooked_CreateSwapChain(IDXGIFactory *factory, IUnknown *pDevice,
                                                        DXGI_SWAP_CHAIN_DESC *pDesc,
                                                        IDXGISwapChain **ppSwapChain)
{
  ID3DDevice *wrapDevice = GetD3DDevice(pDevice);

  if(wrapDevice)
  {
    DXGI_SWAP_CHAIN_DESC local = {};
    DXGI_SWAP_CHAIN_DESC *desc = NULL;

    if(pDesc)
    {
      local = *pDesc;
      desc = &local;
    }

    local.BufferUsage |= DXGI_USAGE_RENDER_TARGET_OUTPUT;

    if(!RenderDoc::Inst().GetCaptureOptions().allowFullscreen)
      local.Windowed = TRUE;

    HRESULT ret = fnOrigCreateSwapChain(factory, wrapDevice->GetRealIUnknown(), desc, ppSwapChain);

    if(SUCCEEDED(ret))
    {
      *ppSwapChain =
          new WrappedIDXGISwapChain4(*ppSwapChain, desc ? desc->OutputWindow : NULL, wrapDevice);
    }

    return ret;
  }

  RDCERR("Creating swap chain with non-hooked device!");

  return fnOrigCreateSwapChain(factory, pDevice, pDesc, ppSwapChain);
}

static HRESULT STDMETHODCALLTYPE Hooked_CreateSwapChainForHwnd(
    IDXGIFactory2 *factory, IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc, IDXGIOutput *pRestrictToOutput,
    IDXGISwapChain1 **ppSwapChain)
{
  ID3DDevice *wrapDevice = GetD3DDevice(pDevice);

  IDXGIOutput *unwrappedOutput = pRestrictToOutput;
  if(unwrappedOutput && WrappedIDXGIOutput6::IsAlloc(unwrappedOutput))
    unwrappedOutput = ((WrappedIDXGIOutput6 *)unwrappedOutput)->GetReal();

  if(wrapDevice)
  {
    DXGI_SWAP_CHAIN_DESC1 local = {};
    DXGI_SWAP_CHAIN_DESC1 *desc = NULL;

    if(pDesc)
    {
      local = *pDesc;
      desc = &local;
    }

    local.BufferUsage |= DXGI_USAGE_RENDER_TARGET_OUTPUT;

    if(!RenderDoc::Inst().GetCaptureOptions().allowFullscreen && pFullscreenDesc)
      pFullscreenDesc = NULL;

    HRESULT ret = fnOrigCreateSwapChainForHwnd(factory, wrapDevice->GetRealIUnknown(), hWnd, desc,
                                               pFullscreenDesc, unwrappedOutput, ppSwapChain);

    if(SUCCEEDED(ret))
      *ppSwapChain = new WrappedIDXGISwapChain4(*ppSwapChain, hWnd, wrapDevice);

    return ret;
  }

  RDCERR("Creating swap chain with non-hooked device!");

  return fnOrigCreateSwapChainForHwnd(factory, pDevice, hWnd, pDesc, pFullscreenDesc,
                                      unwrappedOutput, ppSwapChain);
}

static HRESULT STDMETHODCALLTYPE Hooked_CreateSwapChainForCoreWindow(
    IDXGIFactory2 *factory, IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain)
{
  ID3DDevice *wrapDevice = GetD3DDevice(pDevice);

  IDXGIOutput *unwrappedOutput = pRestrictToOutput;
  if(unwrappedOutput && WrappedIDXGIOutput6::IsAlloc(unwrappedOutput))
    unwrappedOutput = ((WrappedIDXGIOutput6 *)unwrappedOutput)->GetReal();

  if(!RenderDoc::Inst().GetCaptureOptions().allowFullscreen)
    RDCWARN("Impossible to disallow fullscreen on call to CreateSwapChainForCoreWindow");

  if(wrapDevice)
  {
    DXGI_SWAP_CHAIN_DESC1 local = {};
    DXGI_SWAP_CHAIN_DESC1 *desc = NULL;

    if(pDesc)
    {
      local = *pDesc;
      desc = &local;
    }

    local.BufferUsage |= DXGI_USAGE_RENDER_TARGET_OUTPUT;

    HRESULT ret = fnOrigCreateSwapChainForCoreWindow(factory, wrapDevice->GetRealIUnknown(),
                                                     pWindow, desc, unwrappedOutput, ppSwapChain);

    if(SUCCEEDED(ret))
    {
      HWND wnd = NULL;
      (*ppSwapChain)->GetHwnd(&wnd);
      if(wnd == NULL)
        wnd = (HWND)pWindow;
      *ppSwapChain = new WrappedIDXGISwapChain4(*ppSwapChain, wnd, wrapDevice);
    }

    return ret;
  }

  RDCERR("Creating swap chain with non-hooked device!");

  return fnOrigCreateSwapChainForCoreWindow(factory, pDevice, pWindow, pDesc, unwrappedOutput,
                                            ppSwapChain);
}

static HRESULT STDMETHODCALLTYPE Hooked_CreateSwapChainForComposition(
    IDXGIFactory2 *factory, IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
    IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain)
{
  ID3DDevice *wrapDevice = GetD3DDevice(pDevice);

  IDXGIOutput *unwrappedOutput = pRestrictToOutput;
  if(unwrappedOutput && WrappedIDXGIOutput6::IsAlloc(unwrappedOutput))
    unwrappedOutput = ((WrappedIDXGIOutput6 *)unwrappedOutput)->GetReal();

  if(!RenderDoc::Inst().GetCaptureOptions().allowFullscreen)
    RDCWARN("Impossible to disallow fullscreen on call to CreateSwapChainForComposition");

  if(wrapDevice)
  {
    DXGI_SWAP_CHAIN_DESC1 local = {};
    DXGI_SWAP_CHAIN_DESC1 *desc = NULL;

    if(pDesc)
    {
      local = *pDesc;
      desc = &local;
    }

    local.BufferUsage |= DXGI_USAGE_RENDER_TARGET_OUTPUT;

    HRESULT ret = fnOrigCreateSwapChainForComposition(factory, wrapDevice->GetRealIUnknown(), desc,
                                                      unwrappedOutput, ppSwapChain);

    if(SUCCEEDED(ret))
    {
      HWND wnd = NULL;
      (*ppSwapChain)->GetHwnd(&wnd);
      if(wnd == NULL)
        wnd = (HWND)0x1;
      *ppSwapChain = new WrappedIDXGISwapChain4(*ppSwapChain, wnd, wrapDevice);
    }

    return ret;
  }

  RDCERR("Creating swap chain with non-hooked device!");

  return fnOrigCreateSwapChainForComposition(factory, pDevice, pDesc, unwrappedOutput, ppSwapChain);
}

// Installs the CreateSwapChain* vtable hooks on a factory. Because the vtable
// is shared per class, this only needs to happen once per process; subsequent
// calls are no-ops.
static void InstallFactoryHooks(void *factory)
{
  if(factory == NULL || s_FactoryHooksInstalled)
    return;

  if(!VTableHook::Install(factory, 10, (void *)&Hooked_CreateSwapChain,
                          (void **)&fnOrigCreateSwapChain))
  {
    RDCWARN("Failed to install vtable hook for IDXGIFactory::CreateSwapChain");
    return;
  }

  // IDXGIFactory2+ methods only exist on Win8.1+ factories; querying for the
  // interface guarantees the vtable slots are valid.
  IUnknown *factoryUnknown = (IUnknown *)factory;
  IDXGIFactory2 *factory2 = NULL;
  if(SUCCEEDED(factoryUnknown->QueryInterface(__uuidof(IDXGIFactory2), (void **)&factory2)))
  {
    VTableHook::Install(factory2, 15, (void *)&Hooked_CreateSwapChainForHwnd,
                        (void **)&fnOrigCreateSwapChainForHwnd);
    VTableHook::Install(factory2, 16, (void *)&Hooked_CreateSwapChainForCoreWindow,
                        (void **)&fnOrigCreateSwapChainForCoreWindow);
    VTableHook::Install(factory2, 24, (void *)&Hooked_CreateSwapChainForComposition,
                        (void **)&fnOrigCreateSwapChainForComposition);
    factory2->Release();
  }

  s_FactoryHooksInstalled = true;
  RDCLOG("Installed 3DMigoto-style vtable hooks on IDXGIFactory");
}
#endif    // USE_VTABLE_HOOK_IMPL && RDOC_WIN32

typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_CREATE_DXGI_FACTORY2)(UINT, REFIID, void **);
typedef HRESULT(WINAPI *PFN_GET_DEBUG_INTERFACE)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_GET_DEBUG_INTERFACE1)(UINT, REFIID, void **);

MIDL_INTERFACE("9F251514-9D4D-4902-9D60-18988AB7D4B5")
IDXGraphicsAnalysis : public IUnknown
{
  virtual void STDMETHODCALLTYPE BeginCapture() = 0;
  virtual void STDMETHODCALLTYPE EndCapture() = 0;
};

struct RenderDocAnalysis : IDXGraphicsAnalysis
{
  // IUnknown boilerplate
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) { return E_NOINTERFACE; }
  ULONG STDMETHODCALLTYPE AddRef()
  {
    InterlockedIncrement(&m_iRefcount);
    return m_iRefcount;
  }
  ULONG STDMETHODCALLTYPE Release() { return InterlockedDecrement(&m_iRefcount); }
  unsigned int m_iRefcount = 0;

  // IDXGraphicsAnalysis
  void STDMETHODCALLTYPE BeginCapture()
  {
    DeviceOwnedWindow devWnd;
    RenderDoc::Inst().GetActiveWindow(devWnd);

    RenderDoc::Inst().StartFrameCapture(devWnd);
  }

  void STDMETHODCALLTYPE EndCapture()
  {
    DeviceOwnedWindow devWnd;
    RenderDoc::Inst().GetActiveWindow(devWnd);

    RenderDoc::Inst().EndFrameCapture(devWnd);
  }
};

struct DummyDXGIInfoQueue : public IDXGIInfoQueue
{
public:
  // IUnknown boilerplate
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) { return E_NOINTERFACE; }
  ULONG STDMETHODCALLTYPE AddRef()
  {
    InterlockedIncrement(&m_iRefcount);
    return m_iRefcount;
  }
  ULONG STDMETHODCALLTYPE Release() { return InterlockedDecrement(&m_iRefcount); }
  unsigned int m_iRefcount = 0;
  // IDXGIInfoQueue
  virtual HRESULT STDMETHODCALLTYPE SetMessageCountLimit(DXGI_DEBUG_ID Producer,
                                                         UINT64 MessageCountLimit)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE ClearStoredMessages(DXGI_DEBUG_ID Producer) { return; }
  virtual HRESULT STDMETHODCALLTYPE GetMessage(DXGI_DEBUG_ID Producer, UINT64 MessageIndex,
                                               _Out_writes_bytes_opt_(*pMessageByteLength)
                                                   DXGI_INFO_QUEUE_MESSAGE *pMessage,
                                               _Inout_ SIZE_T *pMessageByteLength)
  {
    return S_OK;
  }

  virtual UINT64 STDMETHODCALLTYPE GetNumStoredMessagesAllowedByRetrievalFilters(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual UINT64 STDMETHODCALLTYPE GetNumStoredMessages(DXGI_DEBUG_ID Producer) { return 0; }
  virtual UINT64 STDMETHODCALLTYPE GetNumMessagesDiscardedByMessageCountLimit(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual UINT64 STDMETHODCALLTYPE GetMessageCountLimit(DXGI_DEBUG_ID Producer) { return 0; }
  virtual UINT64 STDMETHODCALLTYPE GetNumMessagesAllowedByStorageFilter(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual UINT64 STDMETHODCALLTYPE GetNumMessagesDeniedByStorageFilter(DXGI_DEBUG_ID Producer)
  {
    return 0;
  }

  virtual HRESULT STDMETHODCALLTYPE AddStorageFilterEntries(DXGI_DEBUG_ID Producer,
                                                            DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE GetStorageFilter(DXGI_DEBUG_ID Producer,
                                                     _Out_writes_bytes_opt_(*pFilterByteLength)
                                                         DXGI_INFO_QUEUE_FILTER *pFilter,
                                                     _Inout_ SIZE_T *pFilterByteLength)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE ClearStorageFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual HRESULT STDMETHODCALLTYPE PushEmptyStorageFilter(DXGI_DEBUG_ID Producer) { return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE PushDenyAllStorageFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushCopyOfStorageFilter(DXGI_DEBUG_ID Producer) { return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE PushStorageFilter(DXGI_DEBUG_ID Producer,
                                                      DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE PopStorageFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual UINT STDMETHODCALLTYPE GetStorageFilterStackSize(DXGI_DEBUG_ID Producer) { return 0; }
  virtual HRESULT STDMETHODCALLTYPE AddRetrievalFilterEntries(DXGI_DEBUG_ID Producer,
                                                              DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE GetRetrievalFilter(DXGI_DEBUG_ID Producer,
                                                       _Out_writes_bytes_opt_(*pFilterByteLength)
                                                           DXGI_INFO_QUEUE_FILTER *pFilter,
                                                       _Inout_ SIZE_T *pFilterByteLength)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE ClearRetrievalFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual HRESULT STDMETHODCALLTYPE PushEmptyRetrievalFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushDenyAllRetrievalFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushCopyOfRetrievalFilter(DXGI_DEBUG_ID Producer)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE PushRetrievalFilter(DXGI_DEBUG_ID Producer,
                                                        DXGI_INFO_QUEUE_FILTER *pFilter)
  {
    return S_OK;
  }

  virtual void STDMETHODCALLTYPE PopRetrievalFilter(DXGI_DEBUG_ID Producer) { return; }
  virtual UINT STDMETHODCALLTYPE GetRetrievalFilterStackSize(DXGI_DEBUG_ID Producer) { return 0; }
  virtual HRESULT STDMETHODCALLTYPE AddMessage(DXGI_DEBUG_ID Producer,
                                               DXGI_INFO_QUEUE_MESSAGE_CATEGORY Category,
                                               DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity,
                                               DXGI_INFO_QUEUE_MESSAGE_ID ID, LPCSTR pDescription)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE AddApplicationMessage(DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity,
                                                          LPCSTR pDescription)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE SetBreakOnCategory(DXGI_DEBUG_ID Producer,
                                                       DXGI_INFO_QUEUE_MESSAGE_CATEGORY Category,
                                                       BOOL bEnable)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE SetBreakOnSeverity(DXGI_DEBUG_ID Producer,
                                                       DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity,
                                                       BOOL bEnable)
  {
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE SetBreakOnID(DXGI_DEBUG_ID Producer,
                                                 DXGI_INFO_QUEUE_MESSAGE_ID ID, BOOL bEnable)
  {
    return S_OK;
  }

  virtual BOOL STDMETHODCALLTYPE GetBreakOnCategory(DXGI_DEBUG_ID Producer,
                                                    DXGI_INFO_QUEUE_MESSAGE_CATEGORY Category)
  {
    return FALSE;
  }

  virtual BOOL STDMETHODCALLTYPE GetBreakOnSeverity(DXGI_DEBUG_ID Producer,
                                                    DXGI_INFO_QUEUE_MESSAGE_SEVERITY Severity)
  {
    return FALSE;
  }

  virtual BOOL STDMETHODCALLTYPE GetBreakOnID(DXGI_DEBUG_ID Producer, DXGI_INFO_QUEUE_MESSAGE_ID ID)
  {
    return FALSE;
  }

  virtual void STDMETHODCALLTYPE SetMuteDebugOutput(DXGI_DEBUG_ID Producer, BOOL bMute) { return; }
  virtual BOOL STDMETHODCALLTYPE GetMuteDebugOutput(DXGI_DEBUG_ID Producer) { return FALSE; }
};

class DXGIHook : LibraryHook
{
public:
  void RegisterHooks()
  {
    RDCLOG("Registering DXGI hooks");

    LibraryHooks::RegisterLibraryHook("dxgi.dll", NULL);

    CreateDXGIFactory.Register("dxgi.dll", "CreateDXGIFactory", CreateDXGIFactory_hook);
    CreateDXGIFactory1.Register("dxgi.dll", "CreateDXGIFactory1", CreateDXGIFactory1_hook);
    CreateDXGIFactory2.Register("dxgi.dll", "CreateDXGIFactory2", CreateDXGIFactory2_hook);
    GetDebugInterface.Register("dxgi.dll", "DXGIGetDebugInterface", DXGIGetDebugInterface_hook);
    GetDebugInterface1.Register("dxgi.dll", "DXGIGetDebugInterface1", DXGIGetDebugInterface1_hook);
  }

private:
  static DXGIHook dxgihooks;

  RenderDocAnalysis m_RenderDocAnalysis;
  DummyDXGIInfoQueue m_DummyInfoQueue;

  HookedFunction<PFN_CREATE_DXGI_FACTORY> CreateDXGIFactory;
  HookedFunction<PFN_CREATE_DXGI_FACTORY> CreateDXGIFactory1;
  HookedFunction<PFN_CREATE_DXGI_FACTORY2> CreateDXGIFactory2;
  HookedFunction<PFN_GET_DEBUG_INTERFACE> GetDebugInterface;
  HookedFunction<PFN_GET_DEBUG_INTERFACE1> GetDebugInterface1;

  static HRESULT WINAPI CreateDXGIFactory_hook(__in REFIID riid, __out void **ppFactory)
  {
    if(ppFactory)
      *ppFactory = NULL;
    HRESULT ret = dxgihooks.CreateDXGIFactory()(riid, ppFactory);

    if(SUCCEEDED(ret))
    {
#if ENABLED(USE_VTABLE_HOOK_IMPL) && ENABLED(RDOC_WIN32)
      InstallFactoryHooks(*ppFactory);
#else
      RefCountDXGIObject::HandleWrap("CreateDXGIFactory", riid, ppFactory);
#endif
    }

    return ret;
  }

  static HRESULT WINAPI CreateDXGIFactory1_hook(__in REFIID riid, __out void **ppFactory)
  {
    if(ppFactory)
      *ppFactory = NULL;
    HRESULT ret = dxgihooks.CreateDXGIFactory1()(riid, ppFactory);

    if(SUCCEEDED(ret))
    {
#if ENABLED(USE_VTABLE_HOOK_IMPL) && ENABLED(RDOC_WIN32)
      InstallFactoryHooks(*ppFactory);
#else
      RefCountDXGIObject::HandleWrap("CreateDXGIFactory1", riid, ppFactory);
#endif
    }

    return ret;
  }

  static HRESULT WINAPI CreateDXGIFactory2_hook(UINT Flags, REFIID riid, void **ppFactory)
  {
    if(ppFactory)
      *ppFactory = NULL;
    HRESULT ret = dxgihooks.CreateDXGIFactory2()(Flags, riid, ppFactory);

    if(SUCCEEDED(ret))
    {
#if ENABLED(USE_VTABLE_HOOK_IMPL) && ENABLED(RDOC_WIN32)
      InstallFactoryHooks(*ppFactory);
#else
      RefCountDXGIObject::HandleWrap("CreateDXGIFactory2", riid, ppFactory);
#endif
    }

    return ret;
  }

  static HRESULT WINAPI DXGIGetDebugInterface_hook(REFIID riid, void **ppDebug)
  {
    if(ppDebug)
      *ppDebug = NULL;

    if(riid == __uuidof(IDXGraphicsAnalysis))
    {
      dxgihooks.m_RenderDocAnalysis.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_RenderDocAnalysis;
      return S_OK;
    }
    if(riid == __uuidof(IDXGIInfoQueue))
    {
      RDCWARN(
          "Returning a dummy IDXGIInfoQueue that does nothing. RenderDoc takes control of the "
          "debug layer.");
      dxgihooks.m_DummyInfoQueue.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_DummyInfoQueue;
      return S_OK;
    }

    // IDXGIDebug and IDXGIDebug1 can come through here, but we don't need to wrap them.

    if(dxgihooks.GetDebugInterface())
      return dxgihooks.GetDebugInterface()(riid, ppDebug);
    else
      return E_NOINTERFACE;
  }

  static HRESULT WINAPI DXGIGetDebugInterface1_hook(UINT Flags, REFIID riid, void **ppDebug)
  {
    if(ppDebug)
      *ppDebug = NULL;

    if(riid == __uuidof(IDXGraphicsAnalysis))
    {
      dxgihooks.m_RenderDocAnalysis.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_RenderDocAnalysis;
      return S_OK;
    }
    if(riid == __uuidof(IDXGIInfoQueue))
    {
      RDCWARN(
          "Returning a dummy IDXGIInfoQueue that does nothing. RenderDoc takes control of the "
          "debug layer.");
      dxgihooks.m_DummyInfoQueue.AddRef();
      if(ppDebug)
        *ppDebug = &dxgihooks.m_DummyInfoQueue;
      return S_OK;
    }

    // IDXGIDebug and IDXGIDebug1 can come through here, but we don't need to wrap them.

    if(dxgihooks.GetDebugInterface1())
      return dxgihooks.GetDebugInterface1()(Flags, riid, ppDebug);
    else
      return E_NOINTERFACE;
  }
};

DXGIHook DXGIHook::dxgihooks;
