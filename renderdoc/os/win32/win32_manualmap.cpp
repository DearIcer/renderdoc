#include <windows.h>
#include "win32_manualmap.h"
#include "common/formatting.h"
#include "core/core.h"
#include "os/os_specific.h"
#include "ntapi.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#if ENABLED(RDOC_X64)
static const uint8_t g_DllMainShellcode[] = {
    // mov rax, [rcx]           ; rax = imageBase
    0x48, 0x8B, 0x01,
    // mov r9, [rcx + 8]        ; r9 = entryPointRVA
    0x4D, 0x8B, 0x49, 0x08,
    // mov rcx, rax             ; hModule = imageBase
    0x48, 0x8B, 0xC8,
    // mov edx, 1               ; dwReason = DLL_PROCESS_ATTACH
    0xBA, 0x01, 0x00, 0x00, 0x00,
    // xor r8, r8                ; lpReserved = NULL
    0x4D, 0x33, 0xC0,
    // add rax, r9               ; rax += entryPointRVA
    0x4D, 0x01, 0xC8,
    // call rax                  ; call DllMain
    0xFF, 0xD0,
    // ret
    0xC3,
};

static const uint8_t g_PebUnlinkShellcode[] = {
    // mov rax, gs:[0x60]       ; rax = PEB
    0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00,
    // mov rax, [rax + 0x18]    ; rax = PEB->Ldr
    0x48, 0x8B, 0x40, 0x18,
    // mov r8, [rax + 0x10]     ; r8 = InLoadOrderModuleList.Flink
    0x4C, 0x8B, 0x40, 0x10,
    // mov r9, r8               ; r9 = head
    0x4D, 0x8B, 0xC8,
    // _loop:
    // cmp [r8 + 0x30], rcx     ; DllBase == imageBase?
    0x49, 0x39, 0x48, 0x30,
    // je _found
    0x74, 0x09,
    // mov r8, [r8]             ; r8 = r8->Flink
    0x4D, 0x8B, 0x00,
    // cmp r8, r9               ; back to head?
    0x4D, 0x3B, 0xC1,
    // jne _loop
    0x75, 0xF2,
    // ret                       ; not found
    0xC3,
    // _found:
    // Unlink InLoadOrderLinks
    // mov rdx, [r8]            ; rdx = Flink
    0x49, 0x8B, 0x10,
    // mov rcx, [r8 + 0x08]     ; rcx = Blink
    0x49, 0x8B, 0x48, 0x08,
    // mov [rcx], rdx            ; Blink->Flink = Flink
    0x48, 0x89, 0x11,
    // mov [rdx + 0x08], rcx    ; Flink->Blink = Blink
    0x48, 0x89, 0x4A, 0x08,
    // Unlink InMemoryOrderLinks
    // mov rdx, [r8 + 0x10]
    0x49, 0x8B, 0x50, 0x10,
    // mov rcx, [r8 + 0x18]
    0x49, 0x8B, 0x48, 0x18,
    // mov [rcx], rdx
    0x48, 0x89, 0x11,
    // mov [rdx + 0x08], rcx
    0x48, 0x89, 0x4A, 0x08,
    // Unlink InInitializationOrderLinks
    // mov rdx, [r8 + 0x20]
    0x49, 0x8B, 0x50, 0x20,
    // mov rcx, [r8 + 0x28]
    0x49, 0x8B, 0x48, 0x28,
    // mov [rcx], rdx
    0x48, 0x89, 0x11,
    // mov [rdx + 0x08], rcx
    0x48, 0x89, 0x4A, 0x08,
    // xor eax, eax
    0x33, 0xC0,
    // ret
    0xC3,
};
#else
static const uint8_t g_DllMainShellcode[] = {
    // mov eax, [esp + 4]       ; eax = context ptr
    0x8B, 0x44, 0x24, 0x04,
    // push ebx
    0x53,
    // mov ebx, [eax]           ; ebx = imageBase
    0x8B, 0x18,
    // push 0                   ; lpReserved = NULL
    0x6A, 0x00,
    // push 1                   ; dwReason = DLL_PROCESS_ATTACH
    0x6A, 0x01,
    // add ebx, [eax + 4]       ; ebx += entryPointRVA
    0x03, 0x58, 0x04,
    // call ebx                 ; call DllMain
    0xFF, 0xD3,
    // pop ebx
    0x5B,
    // ret 4                    ; stdcall return
    0xC2, 0x04, 0x00,
};

static const uint8_t g_PebUnlinkShellcode[] = {
    // mov ebx, [esp + 4]       ; ebx = imageBase
    0x8B, 0x5C, 0x24, 0x04,
    // mov eax, fs:[0x30]       ; eax = PEB
    0x64, 0xA1, 0x30, 0x00, 0x00, 0x00,
    // mov eax, [eax + 0x0C]    ; eax = PEB->Ldr
    0x8B, 0x40, 0x0C,
    // mov edx, [eax + 0x0C]    ; edx = InLoadOrderModuleList.Flink
    0x8B, 0x50, 0x0C,
    // push edx                  ; save head
    0x52,
    // _loop:
    // cmp [edx + 0x1C], ebx    ; DllBase == imageBase?
    0x39, 0x5A, 0x1C,
    // je _found
    0x74, 0x0D,
    // mov edx, [edx]            ; edx = edx->Flink
    0x8B, 0x12,
    // cmp edx, [esp]            ; back to head?
    0x3B, 0x14, 0x24,
    // jne _loop
    0x75, 0xF4,
    // pop eax                   ; clean saved head
    0x58,
    // xor eax, eax
    0x33, 0xC0,
    // ret 4                     ; not found
    0xC2, 0x04, 0x00,
    // _found:
    // Unlink InLoadOrderLinks
    // mov eax, [edx]            ; eax = Flink
    0x8B, 0x02,
    // mov ecx, [edx + 0x04]     ; ecx = Blink
    0x8B, 0x4A, 0x04,
    // mov [ecx], eax            ; Blink->Flink = Flink
    0x89, 0x01,
    // mov [eax + 0x04], ecx     ; Flink->Blink = Blink
    0x89, 0x48, 0x04,
    // Unlink InMemoryOrderLinks
    // mov eax, [edx + 0x08]
    0x8B, 0x42, 0x08,
    // mov ecx, [edx + 0x0C]
    0x8B, 0x4A, 0x0C,
    // mov [ecx], eax
    0x89, 0x01,
    // mov [eax + 0x04], ecx
    0x89, 0x48, 0x04,
    // Unlink InInitializationOrderLinks
    // mov eax, [edx + 0x10]
    0x8B, 0x42, 0x10,
    // mov ecx, [edx + 0x14]
    0x8B, 0x4A, 0x14,
    // mov [ecx], eax
    0x89, 0x01,
    // mov [eax + 0x04], ecx
    0x89, 0x48, 0x04,
    // pop eax                   ; clean saved head
    0x58,
    // xor eax, eax
    0x33, 0xC0,
    // ret 4
    0xC2, 0x04, 0x00,
};
#endif

struct DllMainContext
{
#if ENABLED(RDOC_X64)
  uint64_t imageBase;
  uint32_t entryPointRVA;
#else
  uint32_t imageBase;
  uint32_t entryPointRVA;
#endif
};

static bool WriteRemoteMem(HANDLE hProcess, uintptr_t base, const void *data, size_t size)
{
  NtApi &ntapi = NtApi::GetInstance();
  if(!ntapi.Initialize())
    return false;

  SIZE_T written = 0;
  NTSTATUS status =
      ntapi.WriteVirtualMemory(hProcess, (PVOID)base, (PVOID)data, size, &written);
  return NT_SUCCESS(status) && written == size;
}

static bool ReadRemoteMem(HANDLE hProcess, uintptr_t base, void *data, size_t size)
{
  NtApi &ntapi = NtApi::GetInstance();
  if(!ntapi.Initialize())
    return false;

  SIZE_T rd = 0;
  NTSTATUS status = ntapi.ReadVirtualMemory(hProcess, (PVOID)base, data, size, &rd);
  return NT_SUCCESS(status) && rd == size;
}

static uintptr_t RemoteLoadLibrary(HANDLE hProcess, const wchar_t *dllName)
{
  NtApi &ntapi = NtApi::GetInstance();
  if(!ntapi.Initialize())
    return 0;

  SIZE_T pathSize = (wcslen(dllName) + 1) * sizeof(wchar_t);
  PVOID remotePath = NULL;
  SIZE_T regionSize = pathSize;
  NTSTATUS status = ntapi.AllocateVirtualMemory(hProcess, &remotePath, 0, &regionSize,
                                                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if(!NT_SUCCESS(status) || !remotePath)
    return 0;

  if(!NT_SUCCESS(ntapi.WriteVirtualMemory(hProcess, remotePath, (PVOID)dllName, pathSize, NULL)))
  {
    PVOID tmp = remotePath;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    return 0;
  }

  HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  FARPROC loadLib = GetProcAddress(kernel32, "LoadLibraryW");
  if(!loadLib)
  {
    PVOID tmp = remotePath;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    return 0;
  }

  HANDLE hThread = NULL;
  status = ntapi.CreateThreadEx(&hThread, THREAD_ALL_ACCESS, NULL, hProcess,
                                (PVOID)loadLib, remotePath, 0, 0, 1024 * 1024, 0, NULL);
  if(NT_SUCCESS(status) && hThread)
  {
    WaitForSingleObject(hThread, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);

    PVOID tmp = remotePath;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    return (uintptr_t)exitCode;
  }

  PVOID tmp = remotePath;
  SIZE_T tmpSize = 0;
  ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
  return 0;
}

// Resolve an imported function in the injector process. The imported modules
// of renderdoc.dll are all system/CRT DLLs, which Windows maps at the same
// virtual address in every process (ASLR is per-boot for system images), so the
// address returned by the local GetProcAddress is also valid in the target. The
// DLL itself is still loaded into the target first (RemoteLoadLibrary above) so
// the import is present there.
static uintptr_t ResolveImportLocally(const char *dllName, const char *funcName)
{
  HMODULE localModule = LoadLibraryA(dllName);
  if(localModule == NULL)
    return 0;

  uintptr_t func = (uintptr_t)GetProcAddress(localModule, funcName);

  // LoadLibraryA above only incremented the loader refcount, releasing it again
  // does not unload a module that was already resident.
  FreeLibrary(localModule);
  return func;
}

static uintptr_t ResolveImportLocally(const char *dllName, WORD ordinal)
{
  HMODULE localModule = LoadLibraryA(dllName);
  if(localModule == NULL)
    return 0;

  uintptr_t func = (uintptr_t)GetProcAddress(localModule, (const char *)(uintptr_t)ordinal);
  FreeLibrary(localModule);
  return func;
}

static bool ResolveImports(HANDLE hProcess, uintptr_t remoteImageBase,
                            IMAGE_NT_HEADERS *pNTHeader)
{
  IMAGE_DATA_DIRECTORY &importDir =
      pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if(importDir.VirtualAddress == 0 || importDir.Size == 0)
    return true;

  auto pImportDesc = (IMAGE_IMPORT_DESCRIPTOR *)((uintptr_t)pNTHeader -
                                                  (uintptr_t)pNTHeader->OptionalHeader.ImageBase +
                                                  remoteImageBase + importDir.VirtualAddress);

  std::vector<uint8_t> descBuffer(importDir.Size);
  if(!ReadRemoteMem(hProcess, (uintptr_t)pImportDesc, descBuffer.data(), importDir.Size))
    return false;

  auto pLocalImportDesc = (IMAGE_IMPORT_DESCRIPTOR *)descBuffer.data();

  for(IMAGE_IMPORT_DESCRIPTOR *pDesc = pLocalImportDesc; pDesc->Name != 0; ++pDesc)
  {
    uintptr_t remoteNameAddr = remoteImageBase + pDesc->Name;
    char dllName[256] = {};
    if(!ReadRemoteMem(hProcess, remoteNameAddr, dllName, sizeof(dllName) - 1))
      return false;

    std::wstring wDllName;
    size_t len = strlen(dllName);
    wDllName.resize(len);
    for(size_t i = 0; i < len; i++)
      wDllName[i] = (wchar_t)(unsigned char)dllName[i];

    // Ensure the DLL is present in the target process.
    if(!RemoteLoadLibrary(hProcess, wDllName.c_str()))
      return false;

    uintptr_t remoteOrigThunkAddr = remoteImageBase + (uintptr_t)pDesc->OriginalFirstThunk;
    uintptr_t remoteIatAddr = remoteImageBase + pDesc->FirstThunk;

    if(pDesc->OriginalFirstThunk == 0)
      remoteOrigThunkAddr = remoteIatAddr;

    DWORD thunkCount = 0;
    while(true)
    {
#if ENABLED(RDOC_X64)
      IMAGE_THUNK_DATA64 thunkData = {};
      uintptr_t thunkAddr = remoteOrigThunkAddr + thunkCount * sizeof(IMAGE_THUNK_DATA64);
      if(!ReadRemoteMem(hProcess, thunkAddr, &thunkData, sizeof(thunkData)))
        return false;

      if(thunkData.u1.AddressOfData == 0)
        break;

      uintptr_t remoteFunc = 0;
      if(thunkData.u1.Ordinal & IMAGE_ORDINAL_FLAG64)
      {
        remoteFunc = ResolveImportLocally(dllName, (WORD)(thunkData.u1.Ordinal & 0xFFFF));
      }
      else
      {
        char funcNameBuf[256] = {};
        uintptr_t nameAddr = remoteImageBase + (uintptr_t)thunkData.u1.AddressOfData + 2;
        if(ReadRemoteMem(hProcess, nameAddr, funcNameBuf, sizeof(funcNameBuf) - 1))
          remoteFunc = ResolveImportLocally(dllName, funcNameBuf);
      }

      if(!remoteFunc)
        return false;

      uintptr_t iatEntryAddr = remoteIatAddr + thunkCount * sizeof(IMAGE_THUNK_DATA64);
      if(!WriteRemoteMem(hProcess, iatEntryAddr, &remoteFunc, sizeof(remoteFunc)))
        return false;
#else
      IMAGE_THUNK_DATA32 thunkData = {};
      uintptr_t thunkAddr = remoteOrigThunkAddr + thunkCount * sizeof(IMAGE_THUNK_DATA32);
      if(!ReadRemoteMem(hProcess, thunkAddr, &thunkData, sizeof(thunkData)))
        return false;

      if(thunkData.u1.AddressOfData == 0)
        break;

      uintptr_t remoteFunc = 0;
      if(thunkData.u1.Ordinal & IMAGE_ORDINAL_FLAG32)
      {
        remoteFunc = ResolveImportLocally(dllName, (WORD)(thunkData.u1.Ordinal & 0xFFFF));
      }
      else
      {
        char funcNameBuf[256] = {};
        uintptr_t nameAddr = remoteImageBase + (uintptr_t)thunkData.u1.AddressOfData + 2;
        if(ReadRemoteMem(hProcess, nameAddr, funcNameBuf, sizeof(funcNameBuf) - 1))
          remoteFunc = ResolveImportLocally(dllName, funcNameBuf);
      }

      if(!remoteFunc)
        return false;

      uintptr_t iatEntryAddr = remoteIatAddr + thunkCount * sizeof(IMAGE_THUNK_DATA32);
      if(!WriteRemoteMem(hProcess, iatEntryAddr, &remoteFunc, sizeof(remoteFunc)))
        return false;
#endif

      thunkCount++;
    }
  }

  return true;
}

static bool ApplyRelocations(HANDLE hProcess, uintptr_t remoteImageBase, uintptr_t localImageBase,
                              IMAGE_NT_HEADERS *pNTHeader)
{
  IMAGE_DATA_DIRECTORY &relocDir =
      pNTHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  if(relocDir.VirtualAddress == 0 || relocDir.Size == 0)
    return true;

  ptrdiff_t delta = (ptrdiff_t)(remoteImageBase - pNTHeader->OptionalHeader.ImageBase);
  if(delta == 0)
    return true;

  std::vector<uint8_t> relocData(relocDir.Size);
  if(!ReadRemoteMem(hProcess, remoteImageBase + relocDir.VirtualAddress, relocData.data(),
                    relocDir.Size))
    return false;

  uint8_t *pRelocData = relocData.data();
  uint8_t *pRelocEnd = pRelocData + relocDir.Size;

  while(pRelocData < pRelocEnd)
  {
    auto pBlock = (IMAGE_BASE_RELOCATION *)pRelocData;
    if(pBlock->VirtualAddress == 0 || pBlock->SizeOfBlock == 0)
      break;

    DWORD entryCount = (pBlock->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
    WORD *pEntry = (WORD *)(pRelocData + sizeof(IMAGE_BASE_RELOCATION));

    for(DWORD i = 0; i < entryCount; i++)
    {
      WORD entry = pEntry[i];
      WORD type = entry >> 12;
      WORD offset = entry & 0xFFF;

      if(type == IMAGE_REL_BASED_ABSOLUTE)
        continue;

      uintptr_t patchAddr = remoteImageBase + pBlock->VirtualAddress + offset;

#if ENABLED(RDOC_X64)
      if(type == IMAGE_REL_BASED_DIR64)
      {
        uint64_t originalValue = 0;
        if(ReadRemoteMem(hProcess, patchAddr, &originalValue, sizeof(originalValue)))
        {
          originalValue += delta;
          WriteRemoteMem(hProcess, patchAddr, &originalValue, sizeof(originalValue));
        }
      }
#else
      if(type == IMAGE_REL_BASED_HIGHLOW)
      {
        uint32_t originalValue = 0;
        if(ReadRemoteMem(hProcess, patchAddr, &originalValue, sizeof(originalValue)))
        {
          originalValue += (uint32_t)delta;
          WriteRemoteMem(hProcess, patchAddr, &originalValue, sizeof(originalValue));
        }
      }
#endif
      else if(type == IMAGE_REL_BASED_HIGH)
      {
        uint16_t originalValue = 0;
        if(ReadRemoteMem(hProcess, patchAddr, &originalValue, sizeof(originalValue)))
        {
          originalValue += (uint16_t)((delta >> 16) & 0xFFFF);
          WriteRemoteMem(hProcess, patchAddr, &originalValue, sizeof(originalValue));
        }
      }
      else if(type == IMAGE_REL_BASED_LOW)
      {
        uint16_t originalValue = 0;
        if(ReadRemoteMem(hProcess, patchAddr, &originalValue, sizeof(originalValue)))
        {
          originalValue += (uint16_t)(delta & 0xFFFF);
          WriteRemoteMem(hProcess, patchAddr, &originalValue, sizeof(originalValue));
        }
      }
    }

    pRelocData += pBlock->SizeOfBlock;
  }

  return true;
}

static bool CallDllMainRemote(HANDLE hProcess, uintptr_t remoteImageBase, uint32_t entryPointRVA)
{
  NtApi &ntapi = NtApi::GetInstance();
  if(!ntapi.Initialize())
    return false;

  DllMainContext ctx;
#if ENABLED(RDOC_X64)
  ctx.imageBase = remoteImageBase;
  ctx.entryPointRVA = entryPointRVA;
#else
  ctx.imageBase = (uint32_t)remoteImageBase;
  ctx.entryPointRVA = entryPointRVA;
#endif

  PVOID remoteCtx = NULL;
  SIZE_T ctxSize = sizeof(ctx);
  NTSTATUS status = ntapi.AllocateVirtualMemory(hProcess, &remoteCtx, 0, &ctxSize,
                                                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if(!NT_SUCCESS(status) || !remoteCtx)
    return false;

  if(!NT_SUCCESS(ntapi.WriteVirtualMemory(hProcess, remoteCtx, &ctx, ctxSize, NULL)))
  {
    PVOID tmp = remoteCtx;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    return false;
  }

  SIZE_T shellcodeSize = sizeof(g_DllMainShellcode);
  PVOID remoteShellcode = NULL;
  SIZE_T regionSize = shellcodeSize;
  status = ntapi.AllocateVirtualMemory(hProcess, &remoteShellcode, 0, &regionSize,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if(!NT_SUCCESS(status) || !remoteShellcode)
  {
    PVOID tmp = remoteCtx;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    return false;
  }

  if(!NT_SUCCESS(ntapi.WriteVirtualMemory(hProcess, remoteShellcode, (PVOID)g_DllMainShellcode,
                                          shellcodeSize, NULL)))
  {
    PVOID tmp = remoteCtx;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    tmp = remoteShellcode;
    tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    return false;
  }

  HANDLE hThread = NULL;
  status = ntapi.CreateThreadEx(&hThread, THREAD_ALL_ACCESS, NULL, hProcess,
                                (PVOID)remoteShellcode, remoteCtx, 0, 0, 0, 0, NULL);

  if(!NT_SUCCESS(status) || !hThread)
  {
    PVOID tmp = remoteCtx;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    tmp = remoteShellcode;
    tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    return false;
  }

  WaitForSingleObject(hThread, INFINITE);
  DWORD exitCode = 0;
  GetExitCodeThread(hThread, &exitCode);
  CloseHandle(hThread);

  {
    PVOID tmp = remoteCtx;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    tmp = remoteShellcode;
    tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
  }

  return exitCode == TRUE;
}

uintptr_t ManualMapDLL(HANDLE hProcess, const std::vector<uint8_t> &dllImage,
                       const ManualMapConfig &config)
{
  NtApi &ntapi = NtApi::GetInstance();
  if(!ntapi.Initialize())
  {
    RDCERR("NtApi not initialized, cannot manual map");
    return 0;
  }

  if(dllImage.empty())
  {
    RDCERR("DLL image is empty");
    return 0;
  }

  const uint8_t *pImageBase = dllImage.data();

  auto pDOS = (IMAGE_DOS_HEADER *)pImageBase;
  if(pDOS->e_magic != IMAGE_DOS_SIGNATURE)
  {
    RDCERR("Invalid DOS signature");
    return 0;
  }

  auto pNT = (IMAGE_NT_HEADERS *)(pImageBase + pDOS->e_lfanew);
  if(pNT->Signature != IMAGE_NT_SIGNATURE)
  {
    RDCERR("Invalid NT signature");
    return 0;
  }

  if(pNT->OptionalHeader.Magic
#if ENABLED(RDOC_X64)
     != IMAGE_NT_OPTIONAL_HDR64_MAGIC
#else
     != IMAGE_NT_OPTIONAL_HDR32_MAGIC
#endif
  )
  {
    RDCERR("DLL architecture mismatch");
    return 0;
  }

  DWORD imageSize = pNT->OptionalHeader.SizeOfImage;

  uintptr_t remoteImageBase;
  {
    PVOID remoteAddr = NULL;
    SIZE_T regionSize = imageSize;
    NTSTATUS status = ntapi.AllocateVirtualMemory(hProcess, &remoteAddr, 0, &regionSize,
                                                  MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if(!NT_SUCCESS(status) || !remoteAddr)
    {
      RDCERR("Failed to allocate remote memory: 0x%08X", status);
      return 0;
    }
    remoteImageBase = (uintptr_t)remoteAddr;
  }

  {
    DWORD headerSize = pNT->OptionalHeader.SizeOfHeaders;
    SIZE_T written = 0;
    NTSTATUS status = ntapi.WriteVirtualMemory(hProcess, (PVOID)remoteImageBase,
                                               (PVOID)pImageBase, headerSize, &written);
    if(!NT_SUCCESS(status) || written != headerSize)
    {
      RDCERR("Failed to write PE headers");
      PVOID tmp = (PVOID)remoteImageBase;
      SIZE_T tmpSize = 0;
      ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
      return 0;
    }
  }

  WORD numSections = pNT->FileHeader.NumberOfSections;
  auto pSectionHeader = IMAGE_FIRST_SECTION(pNT);

  for(WORD i = 0; i < numSections; i++)
  {
    IMAGE_SECTION_HEADER &sect = pSectionHeader[i];
    if(sect.SizeOfRawData > 0)
    {
      uintptr_t remoteSectionAddr = remoteImageBase + sect.VirtualAddress;
      const uint8_t *localSectionData = pImageBase + sect.PointerToRawData;

      SIZE_T written = 0;
      NTSTATUS status = ntapi.WriteVirtualMemory(hProcess, (PVOID)remoteSectionAddr,
                                                 (PVOID)localSectionData,
                                                 sect.SizeOfRawData, &written);
      if(!NT_SUCCESS(status))
      {
        RDCERR("Failed to write section %s", sect.Name);
        PVOID tmp = (PVOID)remoteImageBase;
        SIZE_T tmpSize = 0;
        ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
        return 0;
      }
    }
  }

  RDCDEBUG("Manual map: image mapped at 0x%p, applying relocations", (void *)remoteImageBase);
  if(!ApplyRelocations(hProcess, remoteImageBase, (uintptr_t)pImageBase, pNT))
  {
    RDCERR("Failed to apply relocations");
    PVOID tmp = (PVOID)remoteImageBase;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    return 0;
  }

  RDCDEBUG("Manual map: resolving imports");
  if(!ResolveImports(hProcess, remoteImageBase, pNT))
  {
    RDCERR("Failed to resolve imports");
    PVOID tmp = (PVOID)remoteImageBase;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    return 0;
  }

  if(config.callTlsCallbacks)
  {
    IMAGE_DATA_DIRECTORY &tlsDir =
        pNT->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if(tlsDir.VirtualAddress != 0 && tlsDir.Size != 0)
    {
#if ENABLED(RDOC_X64)
      IMAGE_TLS_DIRECTORY64 remoteTls = {};
      if(ReadRemoteMem(hProcess, remoteImageBase + tlsDir.VirtualAddress, &remoteTls,
                       sizeof(remoteTls)))
      {
        if(remoteTls.AddressOfCallBacks)
        {
          uintptr_t cbArray = (uintptr_t)remoteTls.AddressOfCallBacks;
          while(true)
          {
            uint64_t cbAddr = 0;
            if(!ReadRemoteMem(hProcess, cbArray, &cbAddr, sizeof(cbAddr)) || cbAddr == 0)
              break;

            RDCDEBUG("Manual map: calling TLS callback at 0x%p", (void *)cbAddr);
            DllMainContext tlsCtx = {remoteImageBase, 1};
            PVOID remoteTlsCtx = NULL;
            SIZE_T ctxSize = sizeof(tlsCtx);
            ntapi.AllocateVirtualMemory(hProcess, &remoteTlsCtx, 0, &ctxSize,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if(remoteTlsCtx)
            {
              ntapi.WriteVirtualMemory(hProcess, remoteTlsCtx, &tlsCtx, sizeof(tlsCtx), NULL);
              HANDLE hTlsThread = NULL;
              ntapi.CreateThreadEx(&hTlsThread, THREAD_ALL_ACCESS, NULL, hProcess,
                                   (PVOID)((uintptr_t)cbAddr),
                                   remoteTlsCtx, 0, 0, 0, 0, NULL);
              if(hTlsThread)
              {
                WaitForSingleObject(hTlsThread, INFINITE);
                CloseHandle(hTlsThread);
              }
              PVOID tmp = remoteTlsCtx;
              SIZE_T tmpSize = 0;
              ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
            }
            cbArray += sizeof(uint64_t);
          }
        }
      }
#else
      IMAGE_TLS_DIRECTORY32 remoteTls = {};
      if(ReadRemoteMem(hProcess, remoteImageBase + tlsDir.VirtualAddress, &remoteTls,
                       sizeof(remoteTls)))
      {
        if(remoteTls.AddressOfCallBacks)
        {
          uintptr_t cbArray = remoteImageBase + remoteTls.AddressOfCallBacks;
          while(true)
          {
            uint32_t cbAddr = 0;
            if(!ReadRemoteMem(hProcess, cbArray, &cbAddr, sizeof(cbAddr)) || cbAddr == 0)
              break;

            RDCDEBUG("Manual map: calling TLS callback at 0x%p", (void *)cbAddr);
            DllMainContext tlsCtx = {(uint32_t)remoteImageBase, 1};
            PVOID remoteTlsCtx = NULL;
            SIZE_T ctxSize = sizeof(tlsCtx);
            ntapi.AllocateVirtualMemory(hProcess, &remoteTlsCtx, 0, &ctxSize,
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if(remoteTlsCtx)
            {
              ntapi.WriteVirtualMemory(hProcess, remoteTlsCtx, &tlsCtx, sizeof(tlsCtx), NULL);
              HANDLE hTlsThread = NULL;
              ntapi.CreateThreadEx(&hTlsThread, THREAD_ALL_ACCESS, NULL, hProcess,
                                   (PVOID)((uintptr_t)cbAddr),
                                   remoteTlsCtx, 0, 0, 0, 0, NULL);
              if(hTlsThread)
              {
                WaitForSingleObject(hTlsThread, INFINITE);
                CloseHandle(hTlsThread);
              }
              PVOID tmp = remoteTlsCtx;
              SIZE_T tmpSize = 0;
              ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
            }
            cbArray += sizeof(uint32_t);
          }
        }
      }
#endif
    }
  }

  uint32_t entryPointRVA = pNT->OptionalHeader.AddressOfEntryPoint;
  if(entryPointRVA == 0)
  {
    RDCDEBUG("Manual map: no entry point, skipping DllMain");
    return remoteImageBase;
  }

  RDCDEBUG("Manual map: calling DllMain remotely");
  if(!CallDllMainRemote(hProcess, remoteImageBase, entryPointRVA))
  {
    RDCERR("DllMain returned FALSE or failed to execute");
    PVOID tmp = (PVOID)remoteImageBase;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    return 0;
  }

  return remoteImageBase;
}

extern void InjectFunctionCall(HANDLE hProcess, uintptr_t renderdoc_remote, const char *funcName,
                               void *data, const size_t dataLen);

static std::vector<uint8_t> ReadFileToVector(const wchar_t *path)
{
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if(!file.good())
    return {};

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> buffer((size_t)size);
  if(!file.read((char *)buffer.data(), size))
    return {};

  return buffer;
}

static void RunRemoteShellcode(HANDLE hProcess, const uint8_t *shellcode, size_t shellcodeSize,
                               uintptr_t parameter)
{
  NtApi &ntapi = NtApi::GetInstance();
  if(!ntapi.Initialize())
    return;

  PVOID remoteCode = NULL;
  SIZE_T regionSize = shellcodeSize;
  NTSTATUS status = ntapi.AllocateVirtualMemory(hProcess, &remoteCode, 0, &regionSize,
                                                MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
  if(!NT_SUCCESS(status) || !remoteCode)
    return;

  if(!NT_SUCCESS(ntapi.WriteVirtualMemory(hProcess, remoteCode, (PVOID)shellcode,
                                          shellcodeSize, NULL)))
  {
    PVOID tmp = remoteCode;
    SIZE_T tmpSize = 0;
    ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
    return;
  }

  HANDLE hThread = NULL;
  status = ntapi.CreateThreadEx(&hThread, THREAD_ALL_ACCESS, NULL, hProcess,
                                (PVOID)remoteCode, (PVOID)parameter, 0, 0, 0, 0, NULL);

  if(NT_SUCCESS(status) && hThread)
  {
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
  }

  PVOID tmp = remoteCode;
  SIZE_T tmpSize = 0;
  ntapi.FreeVirtualMemory(hProcess, &tmp, &tmpSize, MEM_RELEASE);
}

rdcpair<RDResult, uint32_t> Process::ManualMapInjectIntoProcess(
    uint32_t pid, const rdcarray<EnvironmentModification> &env, const rdcstr &capturefile,
    const CaptureOptions &opts, bool waitForExit, bool hideDll)
{
  NtApi &ntapi = NtApi::GetInstance();
  if(!ntapi.Initialize())
  {
    RDResult result;
    SET_ERROR_RESULT(result, ResultCode::InternalError, "Failed to initialize NtApi");
    return {result, 0};
  }

  HANDLE hProcess = OpenProcess(
      PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
          PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
      FALSE, pid);
  if(!hProcess)
  {
    RDResult result;
    SET_ERROR_RESULT(result, ResultCode::InjectionFailed,
                     "Failed to open process with PID %u", pid);
    return {result, 0};
  }

  wchar_t dllPath[MAX_PATH + 1] = {0};
  GetModuleFileNameW(GetModuleHandleA(STRINGIZE(RDOC_BASE_NAME) ".dll"), dllPath, MAX_PATH);

  std::vector<uint8_t> dllImage = ReadFileToVector(dllPath);
  if(dllImage.empty())
  {
    RDCERR("Failed to read DLL from '%ls'", dllPath);
    CloseHandle(hProcess);
    RDResult result;
    SET_ERROR_RESULT(result, ResultCode::FileIOFailed, "Failed to read renderdoc DLL from disk");
    return {result, 0};
  }

  ManualMapConfig mapConfig;
  mapConfig.hideDll = hideDll;

  uintptr_t remoteBase = ManualMapDLL(hProcess, dllImage, mapConfig);
  if(!remoteBase)
  {
    RDCERR("Manual map injection failed");
    CloseHandle(hProcess);
    RDResult result;
    SET_ERROR_RESULT(result, ResultCode::InjectionFailed,
                     "Failed to manual map renderdoc.dll into process %u", pid);
    return {result, 0};
  }

  rdcpair<RDResult, uint32_t> result = {ResultCode::Succeeded, 0};

  if(!capturefile.empty())
    InjectFunctionCall(hProcess, remoteBase, "INTERNAL_SetCaptureFile",
                       (void *)capturefile.c_str(), capturefile.size() + 1);

  rdcstr debugLogfile = RDCGETLOGFILE();
  InjectFunctionCall(hProcess, remoteBase, "INTERNAL_SetDebugLogFile",
                     (void *)debugLogfile.c_str(), debugLogfile.size() + 1);

  InjectFunctionCall(hProcess, remoteBase, "INTERNAL_SetCaptureOptions",
                     (CaptureOptions *)&opts, sizeof(CaptureOptions));

  InjectFunctionCall(hProcess, remoteBase, "INTERNAL_GetTargetControlIdent", &result.second,
                     sizeof(result.second));

  if(!env.empty())
  {
    for(const EnvironmentModification &e : env)
    {
      rdcstr name = e.name.trimmed();
      rdcstr value = e.value;
      EnvMod mod = e.mod;
      EnvSep sep = e.sep;

      if(name == "")
        break;

      InjectFunctionCall(hProcess, remoteBase, "INTERNAL_EnvModName", (void *)name.c_str(),
                         name.size() + 1);
      InjectFunctionCall(hProcess, remoteBase, "INTERNAL_EnvModValue", (void *)value.c_str(),
                         value.size() + 1);
      InjectFunctionCall(hProcess, remoteBase, "INTERNAL_EnvSep", &sep, sizeof(sep));
      InjectFunctionCall(hProcess, remoteBase, "INTERNAL_EnvMod", &mod, sizeof(mod));
    }

    void *dummy = NULL;
    InjectFunctionCall(hProcess, remoteBase, "INTERNAL_ApplyEnvMods", &dummy, sizeof(dummy));
  }

  if(hideDll)
  {
    RDCDEBUG("Manual map: running PEB unlink shellcode");
    RunRemoteShellcode(hProcess, g_PebUnlinkShellcode, sizeof(g_PebUnlinkShellcode), remoteBase);

    PVOID headerAddr = (PVOID)remoteBase;
    SIZE_T headerSize = 0x1000;
    ULONG oldProtect = 0;
    ntapi.ProtectVirtualMemory(hProcess, &headerAddr, &headerSize, PAGE_READWRITE, &oldProtect);

    std::vector<uint8_t> zeroHeader(0x1000, 0);
    ntapi.WriteVirtualMemory(hProcess, (PVOID)remoteBase, zeroHeader.data(), 0x1000, NULL);

    headerAddr = (PVOID)remoteBase;
    headerSize = 0x1000;
    ntapi.ProtectVirtualMemory(hProcess, &headerAddr, &headerSize, oldProtect, &oldProtect);
  }

  if(waitForExit)
    WaitForSingleObject(hProcess, INFINITE);

  CloseHandle(hProcess);

  return result;
}
