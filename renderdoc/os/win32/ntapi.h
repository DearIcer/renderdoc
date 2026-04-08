#pragma once

#include <windows.h>

typedef LONG NTSTATUS;
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#endif

typedef DWORD KPRIORITY;
typedef ULONG_PTR KAFFINITY;

typedef struct _CLIENT_ID
{
  PVOID UniqueProcess;
  PVOID UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _THREAD_BASIC_INFORMATION
{
  NTSTATUS ExitStatus;
  PVOID TebBaseAddress;
  CLIENT_ID ClientId;
  KAFFINITY AffinityMask;
  KPRIORITY Priority;
  KPRIORITY BasePriority;
} THREAD_BASIC_INFORMATION, *PTHREAD_BASIC_INFORMATION;

typedef enum _THREADINFOCLASS
{
  ThreadBasicInformation = 0,
} THREADINFOCLASS;

typedef enum _MEMORY_INFORMATION_CLASS
{
  MemoryBasicInformation = 0,
  MemoryWorkingSetInformation,
  MemoryMappedFilenameInformation,
  MemoryRegionInformation,
  MemoryWorkingSetExInformation,
  MemoryVdmBitsetInformation,
  MemoryNotNeededInformation,
  MemoryExeBitsetInformation,
  MemoryExeBitsetInformationEx,
  MemoryPagePriorityInformation,
  MemoryAllocationInformation,
  MemoryProtectInformation = 2,
} MEMORY_INFORMATION_CLASS;

// Use custom name to avoid conflict with Windows headers
typedef struct _MEMORY_BASIC_INFORMATION_NT
{
  PVOID BaseAddress;
  PVOID AllocationBase;
  DWORD AllocationProtect;
  SIZE_T RegionSize;
  DWORD State;
  DWORD Protect;
  DWORD Type;
} MEMORY_BASIC_INFORMATION_NT, *PMEMORY_BASIC_INFORMATION_NT;

typedef NTSTATUS(NTAPI *_ZwReadVirtualMemory)(IN HANDLE ProcessHandle, IN PVOID BaseAddress,
                                              OUT PVOID Buffer, IN SIZE_T NumberOfBytesToRead,
                                              OUT PSIZE_T NumberOfBytesReaded OPTIONAL);

typedef NTSTATUS(NTAPI *_ZwWriteVirtualMemory)(IN HANDLE ProcessHandle, IN PVOID BaseAddress,
                                               IN PVOID Buffer, IN SIZE_T NumberOfBytesToWrite,
                                               OUT PSIZE_T NumberOfBytesWritten OPTIONAL);

typedef NTSTATUS(NTAPI *_ZwQueryInformationThread)(IN HANDLE ThreadHandle,
                                                   IN THREADINFOCLASS ThreadInformationClass,
                                                   OUT PVOID ThreadInformation,
                                                   IN ULONG ThreadInformationLength,
                                                   OUT PULONG ReturnLength OPTIONAL);

typedef NTSTATUS(NTAPI *_ZwProtectVirtualMemory)(IN HANDLE ProcessHandle, IN PVOID *BaseAddress,
                                                 IN SIZE_T *NumberOfBytesToProtect,
                                                 IN ULONG NewAccessProtection,
                                                 OUT PULONG OldAccessProtection);

typedef NTSTATUS(NTAPI *_NtQueryVirtualMemory)(IN HANDLE ProcessHandle, IN PVOID BaseAddress,
                                               IN MEMORY_INFORMATION_CLASS Type, OUT PVOID Out,
                                               IN ULONG Length, OUT PULONG NumberOfBytesRead);

typedef NTSTATUS(NTAPI *_NtCreateThreadEx)(
    OUT PHANDLE ThreadHandle, IN ACCESS_MASK DesiredAccess, IN PVOID ObjectAttributes OPTIONAL,
    IN HANDLE ProcessHandle, IN PVOID StartAddress, IN PVOID Parameter OPTIONAL,
    IN ULONG CreateFlags, IN SIZE_T ZeroBits, IN SIZE_T StackSize, IN SIZE_T MaximumStackSize,
    IN PVOID AttributeList OPTIONAL);

typedef NTSTATUS(NTAPI *_NtAllocateVirtualMemory)(IN HANDLE ProcessHandle, IN OUT PVOID *BaseAddress,
                                                  IN ULONG_PTR ZeroBits, IN OUT PSIZE_T RegionSize,
                                                  IN ULONG AllocationType, IN ULONG Protect);

typedef NTSTATUS(NTAPI *_NtFreeVirtualMemory)(IN HANDLE ProcessHandle, IN OUT PVOID *BaseAddress,
                                              IN OUT PSIZE_T RegionSize, IN ULONG FreeType);

typedef NTSTATUS(NTAPI *_NtQuerySystemInformation)(IN ULONG SystemInformationClass,
                                                   OUT PVOID SystemInformation,
                                                   IN ULONG SystemInformationLength,
                                                   OUT PULONG ReturnLength OPTIONAL);

typedef NTSTATUS(NTAPI *_NtQueryInformationProcess)(IN HANDLE ProcessHandle,
                                                     IN ULONG ProcessInformationClass,
                                                     OUT PVOID ProcessInformation,
                                                     IN ULONG ProcessInformationLength,
                                                     OUT PULONG ReturnLength OPTIONAL);

class NtApi
{
public:
  static NtApi &GetInstance();

  bool Initialize();
  void Shutdown();

  bool IsInitialized() const { return m_Initialized; }

  NTSTATUS ReadVirtualMemory(HANDLE hProcess, PVOID baseAddress, PVOID buffer,
                             SIZE_T bytesToRead, PSIZE_T bytesRead = NULL);
  NTSTATUS WriteVirtualMemory(HANDLE hProcess, PVOID baseAddress, PVOID buffer,
                              SIZE_T bytesToWrite, PSIZE_T bytesWritten = NULL);
  NTSTATUS ProtectVirtualMemory(HANDLE hProcess, PVOID *baseAddress, SIZE_T *numBytesToProtect,
                                ULONG newProtection, PULONG oldProtection);
  NTSTATUS QueryVirtualMemory(HANDLE hProcess, PVOID baseAddress,
                              MEMORY_INFORMATION_CLASS memoryInfoClass, PVOID memoryInfo,
                              ULONG memoryInfoLength, PULONG bytesWritten = NULL);
  NTSTATUS AllocateVirtualMemory(HANDLE hProcess, PVOID *baseAddress, ULONG_PTR zeroBits,
                                 PSIZE_T regionSize, ULONG allocationType, ULONG protect);
  NTSTATUS FreeVirtualMemory(HANDLE hProcess, PVOID *baseAddress, PSIZE_T regionSize,
                             ULONG freeType);
  NTSTATUS CreateThreadEx(HANDLE *threadHandle, ACCESS_MASK desiredAccess,
                          PVOID objectAttributes, HANDLE processHandle, PVOID startAddress,
                          PVOID parameter, ULONG createFlags, SIZE_T zeroBits, SIZE_T stackSize,
                          SIZE_T maximumStackSize, PVOID attributeList);
  NTSTATUS QueryInformationThread(HANDLE threadHandle, THREADINFOCLASS threadInfoClass,
                                  PVOID threadInformation, ULONG threadInformationLength,
                                  PULONG returnLength = NULL);

private:
  NtApi();
  ~NtApi();
  NtApi(const NtApi &) = delete;
  NtApi &operator=(const NtApi &) = delete;

  HMODULE m_hNtdll;
  bool m_Initialized;

  _ZwReadVirtualMemory m_pZwReadVirtualMemory;
  _ZwWriteVirtualMemory m_pZwWriteVirtualMemory;
  _ZwProtectVirtualMemory m_pZwProtectVirtualMemory;
  _NtQueryVirtualMemory m_pNtQueryVirtualMemory;
  _NtAllocateVirtualMemory m_pNtAllocateVirtualMemory;
  _NtFreeVirtualMemory m_pNtFreeVirtualMemory;
  _NtCreateThreadEx m_pNtCreateThreadEx;
  _NtQuerySystemInformation m_pNtQuerySystemInformation;
  _NtQueryInformationProcess m_pNtQueryInformationProcess;
  _ZwQueryInformationThread m_pZwQueryInformationThread;
};
