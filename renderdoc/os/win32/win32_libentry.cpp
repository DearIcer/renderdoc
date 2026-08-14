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

// win32_libentry.cpp : Defines the entry point for the DLL
#include <tchar.h>
#include <windows.h>
#include "common/common.h"
#include "core/core.h"
#include "hooks/hooks.h"
#include "strings/string_utils.h"

#include "Hidedll.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// ---------------------------------------------------------------------------
// 3DMigoto Loader support
//
// The 3DMigoto Loader injects a DLL by loading it, fetching its CBTProc export
// and installing it as a WH_CBT system hook. That loads this DLL into every
// process that subsequently creates a window, so before doing any real work we
// must verify that the current process is the one named in d3dx.ini's
// [Loader] target setting.
//
// The parser below is intentionally minimal and avoids RenderDoc's full config
// machinery, because this decision happens inside DllMain where we should not
// touch anything beyond kernel32/user32 and the CRT.
// ---------------------------------------------------------------------------

static const char *MigotoSkipSpace(const char *buf)
{
  for(; *buf == ' ' || *buf == '\t'; buf++)
    ;
  return buf;
}

static const char *MigotoNextLine(const char *buf)
{
  for(; *buf != '\0' && *buf != '\n' && *buf != '\r'; buf++)
    ;
  for(; *buf == '\n' || *buf == '\r' || *buf == ' ' || *buf == '\t'; buf++)
    ;
  return buf;
}

static const char *MigotoFindINISection(const char *buf, const char *section_name)
{
  for(buf = MigotoSkipSpace(buf); *buf; buf = MigotoNextLine(buf))
  {
    if(*buf == '[')
    {
      const char *p = section_name;
      for(buf++; *p && tolower((unsigned char)*buf) == *p; buf++, p++)
        ;
      if(*buf == ']' && *p == '\0')
        return MigotoNextLine(buf);
    }
  }

  return NULL;
}

static bool MigotoFindINISetting(const char *buf, const char *setting, char *ret, size_t n)
{
  for(buf = MigotoSkipSpace(buf); *buf; buf = MigotoNextLine(buf))
  {
    if(*buf == '[')
      return false;

    const char *p = setting;
    for(; *p && tolower((unsigned char)*buf) == *p; buf++, p++)
      ;
    buf = MigotoSkipSpace(buf);
    if(*buf != '=' || *p != '\0')
      continue;

    buf = MigotoSkipSpace(buf + 1);

    size_t i = 0;
    char *r = ret;
    for(; i < n; i++, buf++, r++)
    {
      *r = *buf;
      if(*buf == '\n' || *buf == '\r' || *buf == '\0')
      {
        for(; r >= ret && (*r == '\0' || *r == '\n' || *r == '\r' || *r == ' ' || *r == '\t');
            r--)
          *r = '\0';
        return true;
      }
    }

    return false;
  }

  return false;
}

static bool Is3DMigotoInjection(HMODULE module)
{
  wchar_t module_path[MAX_PATH] = {0};
  DWORD len = GetModuleFileNameW(module, module_path, MAX_PATH);
  if(len == 0 || len >= MAX_PATH)
    return false;

  wchar_t *basename = wcsrchr(module_path, L'\\');
  if(basename == NULL)
    return false;

  const size_t used = (size_t)(basename + 1 - module_path);
  if(used + 9 > MAX_PATH)
    return false;

  memcpy(basename + 1, L"d3dx.ini", sizeof(L"d3dx.ini"));

  DWORD attrs = GetFileAttributesW(module_path);
  return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool Verify3DMigotoTarget(HMODULE module)
{
  wchar_t our_path[MAX_PATH] = {0};
  wchar_t exe_path[MAX_PATH] = {0};
  wchar_t *our_basename = NULL;
  wchar_t *exe_basename = NULL;
  DWORD filesize = 0;
  DWORD readsize = 0;
  bool rc = false;
  char *buf = NULL;
  const char *section = NULL;
  char target[MAX_PATH];
  wchar_t target_w[MAX_PATH] = {0};
  size_t target_len = 0;
  size_t exe_len = 0;
  HANDLE f = INVALID_HANDLE_VALUE;

  if(!GetModuleFileNameW(module, our_path, MAX_PATH))
    return false;
  if(!GetModuleFileNameW(NULL, exe_path, MAX_PATH))
    return false;

  our_basename = wcsrchr(our_path, L'\\');
  exe_basename = wcsrchr(exe_path, L'\\');
  if(our_basename == NULL || exe_basename == NULL)
    return false;

  *our_basename++ = L'\0';
  *exe_basename++ = L'\0';

  // A DLL sitting next to the executable is either a proxy dropped into the
  // game directory or the copy loaded by the 3DMigoto loader itself. In both
  // cases it is safe to remain loaded.
  if(!_wcsicmp(our_path, exe_path))
    return true;

  // Restore the full executable path for suffix matching below.
  *(exe_basename - 1) = L'\\';

  // RenderDoc does not have a rundll32 profile-helper entry point.
  if(!_wcsicmp(exe_basename, L"rundll32.exe"))
    return false;

  // d3dx.ini lives next to our DLL, the same file the 3DMigoto loader reads.
  size_t dir_len = wcslen(our_path);
  if(dir_len + 9 > MAX_PATH)
    return false;
  if(wcscat_s(our_path, MAX_PATH, L"d3dx.ini") != 0)
    return false;

  f = CreateFileW(our_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, NULL);
  if(f == INVALID_HANDLE_VALUE)
    return false;

  filesize = GetFileSize(f, NULL);
  if(filesize == INVALID_FILE_SIZE)
    goto out_close;

  buf = new char[filesize + 1];
  if(buf == NULL)
    goto out_close;

  if(!ReadFile(f, buf, filesize, &readsize, NULL) || filesize != readsize)
    goto out_free;

  buf[filesize] = '\0';

  section = MigotoFindINISection(buf, "loader");
  if(section == NULL)
    goto out_free;

  if(!MigotoFindINISetting(section, "target", target, MAX_PATH))
    goto out_free;

  if(!MultiByteToWideChar(CP_UTF8, 0, target, -1, target_w, MAX_PATH))
    goto out_free;

  target_len = wcslen(target_w);
  exe_len = wcslen(exe_path);
  if(exe_len < target_len)
    goto out_free;

  // Unless target is a full path, ensure the match starts after a directory
  // separator (e.g. target=game.exe must not match foo_game.exe).
  if(target_w[0] != L'\\' && exe_len > target_len &&
     exe_path[exe_len - target_len - 1] != L'\\')
    goto out_free;

  rc = !_wcsicmp(exe_path + exe_len - target_len, target_w);

  if(rc)
  {
    // Bump our refcount so we stay loaded even if the injector exits before
    // the target process starts using DirectX.
    HMODULE handle = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       (LPCWSTR)&Verify3DMigotoTarget, &handle);
  }

out_free:
  delete[] buf;
out_close:
  CloseHandle(f);
  return rc;
}

extern "C" LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam)
{
  return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static BOOL add_hooks()
{
  wchar_t curFile[512];
  GetModuleFileNameW(NULL, curFile, 512);

  rdcstr f = get_basename(strlower(StringFormat::Wide2UTF8(curFile)));

  // bail immediately if we're in a system process. We don't want to hook, log, anything -
  // this instance is being used for a shell extension.
  if(f == "dllhost.exe" || f == "explorer.exe")
  {
#if ENABLED(RDOC_RELEASE)
    OutputDebugStringA(
        "Detecting shell process! Disabling hooking in dllhost.exe or explorer.exe\n");
#endif
    return TRUE;
  }

  // search for an exported symbol with this name, typically renderdoc__replay__marker
  if(LibraryHooks::Detect(STRINGIZE(RDOC_BASE_NAME) "__replay__marker"))
  {
    RDCDEBUG("Not creating hooks - in replay app");

    RenderDoc::Inst().SetReplayApp(true);

    RenderDoc::Inst().Initialise();

    LibraryHooks::ReplayInitialise();

    return true;
  }

  RenderDoc::Inst().Initialise();

  RDCLOG("Loading into %ls", curFile);

  LibraryHooks::RegisterHooks();

  return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
  if(ul_reason_for_call == DLL_PROCESS_ATTACH)
  {
    DisableThreadLibraryCalls(hModule);

    // If the 3DMigoto Loader injected us via SetWindowsHookEx(WH_CBT, CBTProc)
    // we are loaded into every window-creating process. Filter by the [Loader]
    // target in d3dx.ini next to this DLL before doing any real work.
    if(Is3DMigotoInjection(hModule) && !Verify3DMigotoTarget(hModule))
      return FALSE;

    //todo: ??????????????????
    // HideDll DLL(hModule);
    // DLL.RemoveLDR();
    // DLL.RemoveMAP();
    // DLL.RemovePEH();
    BOOL ret = add_hooks();

    SetLastError(0);
    return ret;
  }

  return TRUE;
}
