/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2026 Baldur Karlsson
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

// 3DMigoto-style dxgi proxy DLL.
//
// Rename the built binary to dxgi.dll and place it next to the target
// executable. All exports are forwarded to the real system dxgi.dll, so no
// system code is modified by this proxy. DllMain loads RenderDoc before it
// returns; RenderDoc then hooks the forwarded CreateDXGIFactory* exports (or
// falls back to vtable hooking when the factory is created), exactly as it
// would for a normal injection.

#include <windows.h>
#include <stddef.h>

#if defined(_M_X64)
#include "dxgi_proxy64_exports.h"
#else
#include "dxgi_proxy32_exports.h"
#endif

// POD copy of CaptureOptions. RenderDoc serialises this struct by raw bytes,
// so the layout below must match renderdoc/api/replay/capture_options.h.
struct RenderDocCaptureOptions
{
  unsigned char allowVSync;
  unsigned char allowFullscreen;
  unsigned char apiValidation;
  unsigned char captureCallstacks;
  unsigned char captureCallstacksOnlyActions;
  unsigned char padding0[3];
  unsigned int delayForDebugger;
  unsigned char verifyBufferAccess;
  unsigned char hookIntoChildren;
  unsigned char refAllResources;
  unsigned char captureAllCmdLists;
  unsigned char debugOutputMute;
  unsigned char padding1[3];
  unsigned int softMemoryLimit;
};

static_assert(offsetof(RenderDocCaptureOptions, allowVSync) == 0, "CaptureOptions layout");
static_assert(offsetof(RenderDocCaptureOptions, allowFullscreen) == 1, "CaptureOptions layout");
static_assert(offsetof(RenderDocCaptureOptions, apiValidation) == 2, "CaptureOptions layout");
static_assert(offsetof(RenderDocCaptureOptions, captureCallstacks) == 3, "CaptureOptions layout");
static_assert(offsetof(RenderDocCaptureOptions, captureCallstacksOnlyActions) == 4,
              "CaptureOptions layout");
static_assert(offsetof(RenderDocCaptureOptions, delayForDebugger) == 8, "CaptureOptions layout");
static_assert(offsetof(RenderDocCaptureOptions, verifyBufferAccess) == 12, "CaptureOptions layout");
static_assert(offsetof(RenderDocCaptureOptions, hookIntoChildren) == 13, "CaptureOptions layout");
static_assert(offsetof(RenderDocCaptureOptions, refAllResources) == 14, "CaptureOptions layout");
static_assert(offsetof(RenderDocCaptureOptions, captureAllCmdLists) == 15, "CaptureOptions layout");
static_assert(offsetof(RenderDocCaptureOptions, debugOutputMute) == 16, "CaptureOptions layout");
static_assert(offsetof(RenderDocCaptureOptions, softMemoryLimit) == 20, "CaptureOptions layout");
static_assert(sizeof(RenderDocCaptureOptions) == 24, "CaptureOptions layout");

static HMODULE g_ProxyModule = NULL;
static HMODULE g_SystemDXGI = NULL;
static HMODULE g_RenderDoc = NULL;
static volatile LONG g_LoadState = 0;
static HANDLE g_LoadCompleteEvent = NULL;
static HANDLE g_DebugFile = INVALID_HANDLE_VALUE;

static void ClearBytes(void *dst, size_t size)
{
  unsigned char *p = (unsigned char *)dst;
  while(size-- > 0)
    *p++ = 0;
}

static void DebugLog(const char *text)
{
#if defined(DXGI_PROXY_VERBOSE)
  OutputDebugStringA(text);
#endif
  if(g_DebugFile != INVALID_HANDLE_VALUE)
  {
    DWORD written = 0;
    WriteFile(g_DebugFile, text, (DWORD)lstrlenA(text), &written, NULL);
  }
}

static void OpenDebugLog(HMODULE module)
{
  char enabled[8];
  ClearBytes(enabled, sizeof(enabled));
  if(GetEnvironmentVariableA("DXGI_PROXY_DEBUG", enabled, 8) == 0)
    return;

  wchar_t logPath[MAX_PATH * 2];
  ClearBytes(logPath, sizeof(logPath));
  DWORD len = GetModuleFileNameW(module, logPath, MAX_PATH);
  if(len == 0 || len >= MAX_PATH)
    return;

  int dirEnd = (int)len;
  while(dirEnd > 0 && logPath[dirEnd - 1] != L'\\')
    dirEnd--;

  lstrcpyW(logPath + dirEnd, L"dxgi_proxy.log");
  g_DebugFile = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static BOOL LoadSystemDXGI()
{
  if(g_SystemDXGI != NULL)
    return TRUE;

  wchar_t sysDXGI[MAX_PATH + 16];
  ClearBytes(sysDXGI, sizeof(sysDXGI));
  UINT len = GetSystemDirectoryW(sysDXGI, MAX_PATH);
  if(len == 0 || len >= MAX_PATH)
  {
    DebugLog("dxgi proxy: GetSystemDirectoryW failed\n");
    return FALSE;
  }

  if(sysDXGI[len - 1] != L'\\')
  {
    sysDXGI[len] = L'\\';
    len++;
  }

  lstrcpyW(sysDXGI + len, L"dxgi.dll");
  g_SystemDXGI = LoadLibraryW(sysDXGI);
  if(g_SystemDXGI == NULL)
  {
    DebugLog("dxgi proxy: failed to load real system dxgi.dll\n");
    return FALSE;
  }

  return TRUE;
}

static void SetDefaultCaptureOptions(HMODULE renderDoc)
{
  typedef void(__cdecl *PFN_InternalSetCaptureOptions)(void *opts);

  PFN_InternalSetCaptureOptions setOptions =
      (PFN_InternalSetCaptureOptions)GetProcAddress(renderDoc, "INTERNAL_SetCaptureOptions");
  if(setOptions == NULL)
  {
    DebugLog("dxgi proxy: INTERNAL_SetCaptureOptions not found\n");
    return;
  }

  unsigned char raw[sizeof(RenderDocCaptureOptions)];
  ClearBytes(raw, sizeof(raw));
  RenderDocCaptureOptions *opts = (RenderDocCaptureOptions *)raw;

  opts->allowVSync = 1;
  opts->allowFullscreen = 1;
  opts->apiValidation = 0;
  opts->captureCallstacks = 0;
  opts->captureCallstacksOnlyActions = 0;
  opts->delayForDebugger = 0;
  opts->verifyBufferAccess = 0;
  opts->hookIntoChildren = 0;
  opts->refAllResources = 0;
  opts->captureAllCmdLists = 0;
  opts->debugOutputMute = 1;
  opts->softMemoryLimit = 0;

  setOptions(opts);
}

static BOOL ReadRenderDocPathFromConfig(wchar_t *rdocPath, DWORD pathLen)
{
  wchar_t configPath[MAX_PATH * 2];
  ClearBytes(configPath, sizeof(configPath));
  DWORD len = GetModuleFileNameW(g_ProxyModule, configPath, MAX_PATH);
  if(len == 0 || len >= MAX_PATH)
    return FALSE;

  int dirEnd = (int)len;
  while(dirEnd > 0 && configPath[dirEnd - 1] != L'\\')
    dirEnd--;

  lstrcpyW(configPath + dirEnd, L"renderdoc_proxy_rdoc_path.txt");

  HANDLE file = CreateFileW(configPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
  if(file == INVALID_HANDLE_VALUE)
    return FALSE;

  char utf8[MAX_PATH * 4];
  ClearBytes(utf8, sizeof(utf8));
  DWORD read = 0;
  BOOL ok = ReadFile(file, utf8, sizeof(utf8) - 1, &read, NULL);
  CloseHandle(file);
  if(!ok || read == 0)
    return FALSE;

  char *text = utf8;
  int textLen = (int)read;
  if(textLen >= 3 && (unsigned char)text[0] == 0xEF &&
     (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
  {
    text += 3;
    textLen -= 3;
  }

  int end = textLen;
  while(end > 0 && (text[end - 1] == '\n' || text[end - 1] == '\r' ||
                    text[end - 1] == ' ' || text[end - 1] == '\t'))
    end--;

  text[end] = 0;

  return MultiByteToWideChar(CP_UTF8, 0, text, -1, rdocPath, (int)pathLen) > 0;
}

static void SetDebugLogFile(HMODULE renderDoc)
{
  if(g_DebugFile == INVALID_HANDLE_VALUE)
    return;

  typedef void(__cdecl *PFN_InternalSetDebugLogFile)(const char *logfile);
  PFN_InternalSetDebugLogFile setLog =
      (PFN_InternalSetDebugLogFile)GetProcAddress(renderDoc, "INTERNAL_SetDebugLogFile");
  if(setLog == NULL)
    return;

  wchar_t logPath[MAX_PATH * 2];
  ClearBytes(logPath, sizeof(logPath));
  DWORD len = GetModuleFileNameW(g_ProxyModule, logPath, MAX_PATH);
  if(len == 0 || len >= MAX_PATH)
    return;

  int dirEnd = (int)len;
  while(dirEnd > 0 && logPath[dirEnd - 1] != L'\\')
    dirEnd--;

  lstrcpyW(logPath + dirEnd, L"dxgi_proxy_renderdoc.log");

  char utf8LogPath[MAX_PATH * 4];
  ClearBytes(utf8LogPath, sizeof(utf8LogPath));
  WideCharToMultiByte(CP_UTF8, 0, logPath, -1, utf8LogPath, sizeof(utf8LogPath), NULL, NULL);
  setLog(utf8LogPath);
}

static BOOL LoadRenderDoc()
{
  wchar_t rdocPath[MAX_PATH * 2];
  ClearBytes(rdocPath, sizeof(rdocPath));

  DWORD envLen = GetEnvironmentVariableW(L"RENDERDOC_RDOC_PATH", rdocPath, MAX_PATH);
  if(envLen > 0 && envLen < MAX_PATH)
  {
    g_RenderDoc = LoadLibraryW(rdocPath);
    if(g_RenderDoc != NULL)
    {
      SetDefaultCaptureOptions(g_RenderDoc);
      SetDebugLogFile(g_RenderDoc);
      return TRUE;
    }
  }

  if(ReadRenderDocPathFromConfig(rdocPath, MAX_PATH))
  {
    g_RenderDoc = LoadLibraryW(rdocPath);
    if(g_RenderDoc != NULL)
    {
      SetDefaultCaptureOptions(g_RenderDoc);
      SetDebugLogFile(g_RenderDoc);
      return TRUE;
    }
  }

  DWORD modLen = GetModuleFileNameW(g_ProxyModule, rdocPath, MAX_PATH);
  if(modLen == 0 || modLen >= MAX_PATH)
  {
    DebugLog("dxgi proxy: GetModuleFileNameW failed\n");
    return FALSE;
  }

  int dirEnd = (int)modLen;
  while(dirEnd > 0 && rdocPath[dirEnd - 1] != L'\\')
    dirEnd--;

  lstrcpyW(rdocPath + dirEnd, L"rdhelper.dll");
  g_RenderDoc = LoadLibraryW(rdocPath);
  if(g_RenderDoc == NULL)
  {
    lstrcpyW(rdocPath + dirEnd, L"renderdoc.dll");
    g_RenderDoc = LoadLibraryW(rdocPath);
  }

  if(g_RenderDoc == NULL)
  {
    DebugLog("dxgi proxy: failed to load rdhelper.dll/renderdoc.dll from proxy directory\n");
    return FALSE;
  }

  SetDefaultCaptureOptions(g_RenderDoc);
  SetDebugLogFile(g_RenderDoc);
  return TRUE;
}

static void EnsureRenderDocLoaded()
{
  if(InterlockedCompareExchange(&g_LoadState, 1, 0) == 0)
  {
    DebugLog("dxgi proxy: EnsureRenderDocLoaded begin\n");
    LoadSystemDXGI();
    LoadRenderDoc();
    InterlockedExchange(&g_LoadState, 2);
    if(g_LoadCompleteEvent != NULL)
      SetEvent(g_LoadCompleteEvent);
    DebugLog("dxgi proxy: EnsureRenderDocLoaded end\n");
    return;
  }

  while(g_LoadState != 2)
  {
    if(g_LoadCompleteEvent != NULL)
      WaitForSingleObject(g_LoadCompleteEvent, INFINITE);
    else
      Sleep(1);
  }
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD reason, LPVOID reserved)
{
  (void)reserved;

  if(reason == DLL_PROCESS_ATTACH)
  {
    DisableThreadLibraryCalls(hModule);
    g_ProxyModule = hModule;
    g_LoadCompleteEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    OpenDebugLog(hModule);
    DebugLog("dxgi proxy: DLL_PROCESS_ATTACH\n");

    // Load RenderDoc before DllMain returns so the forwarded dxgi exports are
    // already hooked before the game can issue its first factory creation call.
    EnsureRenderDocLoaded();
  }

  return TRUE;
}
