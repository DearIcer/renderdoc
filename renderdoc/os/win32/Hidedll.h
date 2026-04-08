﻿﻿#pragma once
#include <windows.h>
#include "xorstr.h"

// 禁用 C4201 警告（匿名 union）
#pragma warning(push)
#pragma warning(disable: 4201)

// 定义必要的 NT 结构体
typedef struct _UNICODE_STRING
{
    USHORT    Length;
    USHORT    MaximumLength;
    PWSTR     Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _CLIENT_ID
{
    PVOID UniqueProcess;
    PVOID UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _PEB_LDR_DATA
{
    ULONG                   Length;
    BOOLEAN                 Initialized;
    HANDLE                  SsHandle;
    LIST_ENTRY              InLoadOrderModuleList;
    LIST_ENTRY              InMemoryOrderModuleList;
    LIST_ENTRY              InInitializationOrderModuleList;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _LDR_DATA_TABLE_ENTRY
{
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG Flags;
    USHORT LoadCount;
    USHORT TlsIndex;
    union
    {
        LIST_ENTRY HashLinks;
        struct
        {
            PVOID SectionPointer;
            ULONG CheckSum;
        };
    };
    ULONG TimeDateStamp;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef struct _PEB
{
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    union
    {
        BOOLEAN BitField;
        struct
        {
            BOOLEAN ImageUsesLargePages : 1;
            BOOLEAN IsProtectedProcess : 1;
            BOOLEAN IsImageDynamicallyRelocated : 1;
            BOOLEAN SkipPatchingUser32Forwarders : 1;
            BOOLEAN IsPackagedProcess : 1;
            BOOLEAN IsAppContainer : 1;
            BOOLEAN IsProtectedProcessLight : 1;
            BOOLEAN IsLongPathAwareProcess : 1;
        };
    };
    HANDLE Mutant;
    PVOID ImageBaseAddress;
    PPEB_LDR_DATA Ldr;
    // ... 其他字段不需要
} PEB, *PPEB;

typedef struct _TEB
{
    PVOID Reserved1[12];
    PPEB ProcessEnvironmentBlock;
    // ... 其他字段不需要
} TEB, *PTEB;

class HideDll
{
public:
	HideDll(HMODULE hModule)
	{
		m_hModule = hModule;
	};

	BOOL RemoveMAP()
	{
		auto Len = GetModuleLen();
		auto DllHandle = m_hModule;
		auto hNtdll = GetModuleHandleA("ntdll.dll");
		if (!hNtdll) return false;
		auto ZwUnmapViewOfSection = GetProcAddress(hNtdll, "ZwUnmapViewOfSection");
		if (!ZwUnmapViewOfSection)return false;
		auto ZwAllocateVirtualMemory = GetProcAddress(hNtdll, "ZwAllocateVirtualMemory");
		if (!ZwAllocateVirtualMemory)return false;
		auto DllAddr = new BYTE[Len];
		BYTE shellcode[256] =
		{
			0x4C, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48,
			0x89, 0x4C, 0x24, 0x08, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x54, 0x24, 0x58, 0x48,
			0x8B, 0x4C, 0x24, 0x50, 0xFF, 0x54, 0x24, 0x70, 0x48, 0x85, 0xC0, 0x74, 0x04, 0x32, 0xC0, 0xEB,
			0x75, 0x48, 0xC7, 0x44, 0x24, 0x28, 0x40, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x44, 0x24, 0x20, 0x00,
			0x20, 0x00, 0x00, 0x4C, 0x8D, 0x4C, 0x24, 0x60, 0x45, 0x33, 0xC0, 0x48, 0x8D, 0x54, 0x24, 0x58,
			0x48, 0x8B, 0x4C, 0x24, 0x50, 0xFF, 0x54, 0x24, 0x78, 0x48, 0x85, 0xC0, 0x74, 0x04, 0x32, 0xC0,
			0xEB, 0x44, 0x48, 0xC7, 0x44, 0x24, 0x28, 0x40, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x44, 0x24, 0x20,
			0x00, 0x10, 0x00, 0x00, 0x4C, 0x8D, 0x4C, 0x24, 0x60, 0x45, 0x33, 0xC0, 0x48, 0x8D, 0x54, 0x24,
			0x58, 0x48, 0x8B, 0x4C, 0x24, 0x50, 0xFF, 0x54, 0x24, 0x78, 0x48, 0x85, 0xC0, 0x74, 0x04, 0x32,
			0xC0, 0xEB, 0x13, 0x48, 0x8B, 0x7C, 0x24, 0x58, 0x48, 0x8B, 0x74, 0x24, 0x68, 0x48, 0x8B, 0x4C,
			0x24, 0x60, 0xF3, 0xA4, 0xB0, 0x01, 0x48, 0x83, 0xC4, 0x38, 0x5F, 0x5E, 0xC3
		};

		memcpy(DllAddr, DllHandle, Len);
		DWORD OldProtect = 0;
		VirtualProtect(&shellcode, sizeof(shellcode), 64, &OldProtect);
		auto result = reinterpret_cast<BOOL(__cdecl*)(INT64, HMODULE, DWORD64, PVOID64, FARPROC, FARPROC)>(&shellcode)(-1, DllHandle, Len, DllAddr, ZwUnmapViewOfSection, ZwAllocateVirtualMemory);
		VirtualProtect(&shellcode, sizeof(shellcode), OldProtect, &OldProtect);
		delete[] DllAddr;
		return result;
	}
	BOOL RemovePEH()
	{
		auto DllHandle = m_hModule;
		unsigned char* ImageBase = reinterpret_cast<unsigned char*>(DllHandle);
		auto DosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(ImageBase);
		if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
			return false;
		auto NTHeader = reinterpret_cast<IMAGE_NT_HEADERS*>(ImageBase + DosHeader->e_lfanew);
		if (NTHeader->Signature != IMAGE_NT_SIGNATURE)
			return false;
		for (int i = 0; i < 0x1000; i++)
			ImageBase[i] = 0;
		return true;
	}
	BOOL RemoveLDR()
	{
		auto hModule = m_hModule;
		PTEB pTeb = reinterpret_cast<PTEB>(NtCurrentTeb());
		PPEB pPeb = pTeb->ProcessEnvironmentBlock;
		if (!pPeb || !pPeb->Ldr)
			return false;
		
		PPEB_LDR_DATA pLdr = pPeb->Ldr;
		PLDR_DATA_TABLE_ENTRY pLdrData = (PLDR_DATA_TABLE_ENTRY)pLdr->InLoadOrderModuleList.Flink;
		
		do
		{
			if (pLdrData->DllBase == hModule)
			{
				pLdrData->InLoadOrderLinks.Blink->Flink = pLdrData->InLoadOrderLinks.Flink;
				pLdrData->InLoadOrderLinks.Flink->Blink = pLdrData->InLoadOrderLinks.Blink;
				pLdrData->InMemoryOrderLinks.Blink->Flink = pLdrData->InMemoryOrderLinks.Flink;
				pLdrData->InMemoryOrderLinks.Flink->Blink = pLdrData->InMemoryOrderLinks.Blink;
				pLdrData->InInitializationOrderLinks.Blink->Flink = pLdrData->InInitializationOrderLinks.Flink;
				pLdrData->InInitializationOrderLinks.Flink->Blink = pLdrData->InInitializationOrderLinks.Blink;
				return true;
			}
			pLdrData = (PLDR_DATA_TABLE_ENTRY)(pLdrData->InLoadOrderLinks.Flink);
		} while (pLdrData->DllBase);
		return false;
	}
	~HideDll() {};
private:

	DWORD64 GetModuleLen()
	{
		auto hModule = m_hModule;
		auto pImage = (PBYTE)hModule;
		PIMAGE_DOS_HEADER pImageDosHeader;
		PIMAGE_NT_HEADERS pImageNtHeader;
		pImageDosHeader = (PIMAGE_DOS_HEADER)pImage;
		if (pImageDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
			return 0;
		pImageNtHeader = (PIMAGE_NT_HEADERS)&pImage[pImageDosHeader->e_lfanew];
		if (pImageNtHeader->Signature != IMAGE_NT_SIGNATURE)
			return 0;
		return pImageNtHeader->OptionalHeader.SizeOfImage;
	}

	HMODULE m_hModule;
};

// 恢复警告
#pragma warning(pop)