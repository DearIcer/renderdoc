#pragma once

#include <windows.h>
#include <cstdint>
#include <vector>

struct ManualMapConfig
{
  bool hideDll = false;         // Remove from PEB LDR list and zero PE headers after init
  bool callTlsCallbacks = true; // Call TLS callbacks before DllMain
};

uintptr_t ManualMapDLL(HANDLE hProcess, const std::vector<uint8_t> &dllImage,
                       const ManualMapConfig &config = ManualMapConfig());