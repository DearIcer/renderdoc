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

#include "ntapi.h"
#include <stdio.h>

NtApi::NtApi() : m_hNtdll(NULL), m_Initialized(false)
{
  memset(&m_pZwReadVirtualMemory, 0, sizeof(m_pZwReadVirtualMemory));
  memset(&m_pZwWriteVirtualMemory, 0, sizeof(m_pZwWriteVirtualMemory));
  memset(&m_pZwProtectVirtualMemory, 0, sizeof(m_pZwProtectVirtualMemory));
  memset(&m_pNtQueryVirtualMemory, 0, sizeof(m_pNtQueryVirtualMemory));
  memset(&m_pNtAllocateVirtualMemory, 0, sizeof(m_pNtAllocateVirtualMemory));
  memset(&m_pNtFreeVirtualMemory, 0, sizeof(m_pNtFreeVirtualMemory));
  memset(&m_pNtCreateThreadEx, 0, sizeof(m_pNtCreateThreadEx));
  memset(&m_pNtQuerySystemInformation, 0, sizeof(m_pNtQuerySystemInformation));
  memset(&m_pNtQueryInformationProcess, 0, sizeof(m_pNtQueryInformationProcess));
  memset(&m_pZwQueryInformationThread, 0, sizeof(m_pZwQueryInformationThread));
}

NtApi::~NtApi()
{
  Shutdown();
}

NtApi &NtApi::GetInstance()
{
  static NtApi instance;
  return instance;
}

bool NtApi::Initialize()
{
  if(m_Initialized)
    return true;

  m_hNtdll = GetModuleHandleA("ntdll.dll");
  if(!m_hNtdll)
  {
    fprintf(stderr, "[NtApi] Failed to load ntdll.dll: %u\n", GetLastError());
    return false;
  }

  m_pZwReadVirtualMemory =
      (_ZwReadVirtualMemory)GetProcAddress(m_hNtdll, "ZwReadVirtualMemory");
  m_pZwWriteVirtualMemory =
      (_ZwWriteVirtualMemory)GetProcAddress(m_hNtdll, "ZwWriteVirtualMemory");
  m_pZwProtectVirtualMemory =
      (_ZwProtectVirtualMemory)GetProcAddress(m_hNtdll, "ZwProtectVirtualMemory");
  m_pNtQueryVirtualMemory =
      (_NtQueryVirtualMemory)GetProcAddress(m_hNtdll, "NtQueryVirtualMemory");
  m_pNtAllocateVirtualMemory =
      (_NtAllocateVirtualMemory)GetProcAddress(m_hNtdll, "NtAllocateVirtualMemory");
  m_pNtFreeVirtualMemory =
      (_NtFreeVirtualMemory)GetProcAddress(m_hNtdll, "NtFreeVirtualMemory");
  m_pNtCreateThreadEx = (_NtCreateThreadEx)GetProcAddress(m_hNtdll, "NtCreateThreadEx");
  m_pNtQuerySystemInformation =
      (_NtQuerySystemInformation)GetProcAddress(m_hNtdll, "NtQuerySystemInformation");
  m_pNtQueryInformationProcess =
      (_NtQueryInformationProcess)GetProcAddress(m_hNtdll, "NtQueryInformationProcess");
  m_pZwQueryInformationThread =
      (_ZwQueryInformationThread)GetProcAddress(m_hNtdll, "ZwQueryInformationThread");

  if(!m_pZwReadVirtualMemory || !m_pZwWriteVirtualMemory || !m_pZwProtectVirtualMemory ||
     !m_pNtQueryVirtualMemory || !m_pNtAllocateVirtualMemory || !m_pNtFreeVirtualMemory ||
     !m_pNtCreateThreadEx)
  {
    fprintf(stderr, "[NtApi] Failed to get required Nt/Zw function addresses\n");
    return false;
  }

  m_Initialized = true;
  fprintf(stdout, "[NtApi] Initialized successfully\n");
  return true;
}

void NtApi::Shutdown()
{
  m_Initialized = false;
  m_hNtdll = NULL;
}

NTSTATUS NtApi::ReadVirtualMemory(HANDLE hProcess, PVOID baseAddress, PVOID buffer,
                                  SIZE_T bytesToRead, PSIZE_T bytesRead)
{
  if(!m_Initialized || !m_pZwReadVirtualMemory)
    return STATUS_UNSUCCESSFUL;

  SIZE_T localBytesRead = 0;
  NTSTATUS status = m_pZwReadVirtualMemory(hProcess, baseAddress, buffer, bytesToRead,
                                           bytesRead ? bytesRead : &localBytesRead);
  return status;
}

NTSTATUS NtApi::WriteVirtualMemory(HANDLE hProcess, PVOID baseAddress, PVOID buffer,
                                   SIZE_T bytesToWrite, PSIZE_T bytesWritten)
{
  if(!m_Initialized || !m_pZwWriteVirtualMemory)
    return STATUS_UNSUCCESSFUL;

  SIZE_T localBytesWritten = 0;
  NTSTATUS status = m_pZwWriteVirtualMemory(hProcess, baseAddress, buffer, bytesToWrite,
                                            bytesWritten ? bytesWritten : &localBytesWritten);
  return status;
}

NTSTATUS NtApi::ProtectVirtualMemory(HANDLE hProcess, PVOID *baseAddress, SIZE_T *numBytesToProtect,
                                     ULONG newProtection, PULONG oldProtection)
{
  if(!m_Initialized || !m_pZwProtectVirtualMemory)
    return STATUS_UNSUCCESSFUL;

  return m_pZwProtectVirtualMemory(hProcess, baseAddress, numBytesToProtect, newProtection,
                                   oldProtection);
}

NTSTATUS NtApi::QueryVirtualMemory(HANDLE hProcess, PVOID baseAddress,
                                   MEMORY_INFORMATION_CLASS memoryInfoClass, PVOID memoryInfo,
                                   ULONG memoryInfoLength, PULONG bytesWritten)
{
  if(!m_Initialized || !m_pNtQueryVirtualMemory)
    return STATUS_UNSUCCESSFUL;

  ULONG localBytesWritten = 0;
  NTSTATUS status = m_pNtQueryVirtualMemory(hProcess, baseAddress, memoryInfoClass, memoryInfo,
                                            memoryInfoLength,
                                            bytesWritten ? bytesWritten : &localBytesWritten);
  return status;
}

NTSTATUS NtApi::AllocateVirtualMemory(HANDLE hProcess, PVOID *baseAddress, ULONG_PTR zeroBits,
                                      PSIZE_T regionSize, ULONG allocationType, ULONG protect)
{
  if(!m_Initialized || !m_pNtAllocateVirtualMemory)
    return STATUS_UNSUCCESSFUL;

  return m_pNtAllocateVirtualMemory(hProcess, baseAddress, zeroBits, regionSize, allocationType,
                                    protect);
}

NTSTATUS NtApi::FreeVirtualMemory(HANDLE hProcess, PVOID *baseAddress, PSIZE_T regionSize,
                                  ULONG freeType)
{
  if(!m_Initialized || !m_pNtFreeVirtualMemory)
    return STATUS_UNSUCCESSFUL;

  return m_pNtFreeVirtualMemory(hProcess, baseAddress, regionSize, freeType);
}

NTSTATUS NtApi::CreateThreadEx(HANDLE *threadHandle, ACCESS_MASK desiredAccess,
                               PVOID objectAttributes, HANDLE processHandle, PVOID startAddress,
                               PVOID parameter, ULONG createFlags, SIZE_T zeroBits,
                               SIZE_T stackSize, SIZE_T maximumStackSize, PVOID attributeList)
{
  if(!m_Initialized || !m_pNtCreateThreadEx)
    return STATUS_UNSUCCESSFUL;

  return m_pNtCreateThreadEx(threadHandle, desiredAccess, objectAttributes, processHandle,
                             startAddress, parameter, createFlags, zeroBits, stackSize,
                             maximumStackSize, attributeList);
}

NTSTATUS NtApi::QueryInformationThread(HANDLE threadHandle, THREADINFOCLASS threadInfoClass,
                                       PVOID threadInformation, ULONG threadInformationLength,
                                       PULONG returnLength)
{
  if(!m_Initialized || !m_pZwQueryInformationThread)
    return STATUS_UNSUCCESSFUL;

  return m_pZwQueryInformationThread(threadHandle, threadInfoClass, threadInformation,
                                     threadInformationLength, returnLength);
}
