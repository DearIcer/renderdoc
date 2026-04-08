#include <Windows.h>
#include <iostream>

#pragma comment(lib, "ntdll.lib")

typedef LONG NTSTATUS;
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

typedef DWORD KPRIORITY;
typedef WORD UWORD;

typedef struct _CLIENT_ID
{
    PVOID UniqueProcess;
    PVOID UniqueThread;
} CLIENT_ID, * PCLIENT_ID;

typedef struct _THREAD_BASIC_INFORMATION
{
    NTSTATUS                ExitStatus;
    PVOID                   TebBaseAddress;
    CLIENT_ID               ClientId;
    KAFFINITY               AffinityMask;
    KPRIORITY               Priority;
    KPRIORITY               BasePriority;
} THREAD_BASIC_INFORMATION, * PTHREAD_BASIC_INFORMATION;

enum THREADINFOCLASS
{
    ThreadBasicInformation,
};

typedef enum _MEMORY_INFORMATION_CLASS {
    MemoryBasicInformation = 0,
    MemoryAllocationInformation = 1,
    MemoryProtectInformation = 2
} MEMORY_INFORMATION_CLASS;


typedef NTSTATUS(NTAPI* _ZwReadVirtualMemory)(
    IN HANDLE ProcessHandle,
    IN PVOID BaseAddress,
    OUT PVOID Buffer,
    IN SIZE_T NumberOfBytesToRead,
    OUT PSIZE_T NumberOfBytesReaded OPTIONAL
    );

typedef NTSTATUS(NTAPI* _ZwQueryInformationThread)(
    IN HANDLE ThreadHandle,
    IN THREADINFOCLASS ThreadInformationClass,
    PVOID ThreadInformation,
    IN ULONG ThreadInformationLength,
    OUT PULONG ReturnLength OPTIONAL
    );

typedef NTSTATUS(NTAPI* _ZwProtectVirtualMemory)(
    IN HANDLE ProcessHandle,
    IN PVOID* BaseAddress,
    IN SIZE_T* NumberOfBytesToProtect,
    IN ULONG NewAccessProtection,
    OUT PULONG OldAccessProtection
    );

typedef NTSTATUS(NTAPI* _NtQueryVirtualMemory)(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    MEMORY_INFORMATION_CLASS Type,
    PVOID Out,
    ULONG Length,
    ULONG* NumberOfBytesRead
    );

_ZwReadVirtualMemory        pZwReadVirtualMemory = nullptr;
_ZwQueryInformationThread   pZwQueryInformationThread = nullptr;
_ZwProtectVirtualMemory     pZwProtectVirtualMemory = nullptr;
_NtQueryVirtualMemory       pNtQueryVirtualMemory = nullptr;

bool InitNtApis() {
    HMODULE hNtdll = LoadLibraryA("ntdll.dll");
    if (!hNtdll) {
        printf("加载ntdll.dll失败，错误码：%d\n", GetLastError());
        return false;
    }

    pZwReadVirtualMemory = (_ZwReadVirtualMemory)GetProcAddress(hNtdll, "ZwReadVirtualMemory");
    pZwQueryInformationThread = (_ZwQueryInformationThread)GetProcAddress(hNtdll, "ZwQueryInformationThread");
    pZwProtectVirtualMemory = (_ZwProtectVirtualMemory)GetProcAddress(hNtdll, "ZwProtectVirtualMemory");
    pNtQueryVirtualMemory = (_NtQueryVirtualMemory)GetProcAddress(hNtdll, "NtQueryVirtualMemory");

    if (!pZwReadVirtualMemory || !pZwQueryInformationThread || !pZwProtectVirtualMemory || !pNtQueryVirtualMemory) {
        printf("获取API地址失败\n");
        FreeLibrary(hNtdll);
        return false;
    }
    return true;
}

void TestZwReadVirtualMemory(DWORD dwProcessId, PVOID lpBaseAddr, PVOID lpBuffer, ULONG uSize) {
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ, FALSE, dwProcessId);
    if (!hProcess) {
        printf("OpenProcess失败，错误码：%d\n", GetLastError());
        return;
    }

    SIZE_T uRead = 0; 
    NTSTATUS status = pZwReadVirtualMemory(hProcess, lpBaseAddr, lpBuffer, uSize, &uRead);

    if (NT_SUCCESS(status)) {
        printf("读取内存成功，实际读取：%zu字节\n", uRead);
    }
    else {
        printf("读取内存失败，NTSTATUS：0x%08X\n", status);
    }

    CloseHandle(hProcess);
}

void TestZwQueryInformationThread(DWORD dwThreadId) {
    HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, dwThreadId);
    if (!hThread) {
        printf("OpenThread失败，错误码：%d\n", GetLastError());
        return;
    }

    THREAD_BASIC_INFORMATION tbi = { 0 };
    ULONG uReturnLen = 0;

    NTSTATUS status = pZwQueryInformationThread(hThread, ThreadBasicInformation, &tbi, sizeof(tbi), &uReturnLen);

    if (NT_SUCCESS(status)) {
        printf("线程基本信息：\n");
        printf("  所属进程ID：%d\n", (DWORD)tbi.ClientId.UniqueProcess);
        printf("  线程ID：%d\n", (DWORD)tbi.ClientId.UniqueThread);
        printf("  退出状态：0x%08X\n", tbi.ExitStatus);
    }
    else {
        printf("查询线程信息失败，NTSTATUS：0x%08X\n", status);
    }

    CloseHandle(hThread);
}

int main() {
    if (!InitNtApis()) {
        return -1;
    }

    DWORD dwTestProcessId = GetCurrentProcessId();
    BYTE buf[1024] = { 0 };

    // 用合法地址测试
    int test_value = 123456;
    TestZwReadVirtualMemory(dwTestProcessId, &test_value, buf, sizeof(test_value));

    DWORD dwTestThreadId = GetCurrentThreadId();
    TestZwQueryInformationThread(dwTestThreadId);

    system("pause");
    return 0;
}