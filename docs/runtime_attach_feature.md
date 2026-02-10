# RenderDoc Runtime Attach Feature

## Overview

This feature adds runtime attach capability to RenderDoc, allowing you to attach to any running process that uses graphics APIs (Direct3D, OpenGL, Vulkan, etc.) without needing to pre-inject or restart the application.

## Features

- **Process Enumeration**: List all running processes or filter for those with graphics APIs
- **Runtime Attachment**: Attach to any running process and inject RenderDoc
- **Automatic Hook Initialization**: Hooks are initialized after attachment using MinHook
- **Capture Configuration**: Set capture files and options during attachment
- **Cross-architecture Support**: Works with both 32-bit and 64-bit processes

## Architecture

### Components

1. **RuntimeAttach Namespace** (`os/os_specific.h`, `os/win32/win32_specific.h`)
   - `EnumerateProcesses()`: List all running processes
   - `EnumerateProcessesWithGraphicsAPI()`: List processes with graphics APIs loaded
   - `AttachToProcess(pid, capturefile, opts)`: Attach to a process
   - `IsAttached(pid)`: Check if RenderDoc is attached
   - `DetachFromProcess(pid)`: Detach from a process

2. **INTERNAL_InitializeRuntimeHooks** (`os/win32/win32_libentry.cpp`)
   - Exported function that initializes RenderDoc hooks when called in a remote process
   - Called via CreateRemoteThread after DLL injection

3. **Process Injection** (`os/win32/win32_process.cpp`)
   - Uses CreateRemoteThread + LoadLibraryW for DLL injection
   - Allocates memory in target process for DLL path and parameters
   - Calls remote functions to configure capture settings

### How It Works

1. **Process Selection**: User selects a target process by PID
2. **Process Opening**: OpenProcess with required permissions
3. **DLL Injection**: 
   - Allocate memory in target process
   - Write renderdoc.dll path to target process
   - Create remote thread calling LoadLibraryW
4. **Hook Initialization**:
   - Locate renderdoc.dll in target process
   - Calculate address of INTERNAL_InitializeRuntimeHooks
   - Create remote thread to initialize hooks
5. **Configuration**:
   - Call INTERNAL_SetCaptureFile if capture file specified
   - Call INTERNAL_SetDebugLogFile
   - Call INTERNAL_SetCaptureOptions
   - Call INTERNAL_GetTargetControlIdent to get connection port

## Usage

### Using the Example Program

```bash
# List all processes
runtime_attach_example.exe list

# List processes with graphics APIs
runtime_attach_example.exe list-graphics

# Attach to a process
runtime_attach_example.exe attach 1234

# Attach with capture file
runtime_attach_example.exe attach 1234 "C:\captures\my_capture.rdc"
```

### Programmatic Usage

```cpp
#include "renderdoc/os/os_specific.h"
#include "renderdoc/api/replay/capture_options.h"

// Enumerate processes with graphics APIs
rdcarray<RuntimeAttach::ProcessInfo> processes = 
    RuntimeAttach::EnumerateProcessesWithGraphicsAPI();

// Find target process
uint32_t targetPid = 0;
for(const auto& proc : processes) {
    if(proc.name == "target_game.exe") {
        targetPid = proc.pid;
        break;
    }
}

// Attach to process
CaptureOptions opts;
opts.captureAllCmdLists = true;
opts.hookIntoChildren = true;

rdcpair<RDResult, uint32_t> result = 
    RuntimeAttach::AttachToProcess(targetPid, "", opts);

if(result.first.code == ResultCode::Succeeded) {
    printf("Attached successfully! Ident: %u\n", result.second);
    // Connect using RenderDoc UI with this ident
}
```

## Integration with MinHook

The runtime attach feature leverages RenderDoc's existing MinHook integration:

- **IAT Patching**: Default hooking method using Import Address Table patching
- **MinHook Fallback**: Can use MinHook for direct function hooking
- **Dynamic Hooking**: Hooks are applied at runtime when DLL is loaded

## Limitations

1. **Admin Privileges**: May require administrator privileges for some processes
2. **Process Architecture**: 64-bit RenderDoc can only attach to 64-bit processes, and vice versa
3. **Already Hooked**: Processes with other hooking libraries may have conflicts
4. **System Processes**: System processes are excluded for safety
5. **Anti-Cheat**: Games with anti-cheat may block injection

## Building

The runtime attach feature is automatically included when building RenderDoc with `USE_MINHOOK` defined:

```cmake
add_definitions(-DUSE_MINHOOK)
```

## Security Considerations

- Process injection requires appropriate Windows permissions
- System processes are automatically excluded
- The feature respects RenderDoc's existing security model
- User must have permission to attach to target process

## Troubleshooting

### "Failed to open process"
- Ensure you have sufficient permissions
- Try running as administrator
- Check if the process is protected

### "Failed to inject renderdoc.dll"
- Verify renderdoc.dll exists in the expected location
- Check if the target process is 64-bit/32-bit compatible
- Ensure no other debuggers are attached

### "Failed to locate renderdoc.dll"
- Wait a moment after injection and try again
- Check if the process crashed during DLL load
- Verify the DLL path is correct

## Future Enhancements

- Detach functionality to cleanly remove hooks
- Process monitoring for automatic re-attachment
- Enhanced error reporting and diagnostics
- Support for additional platforms (Linux, macOS)

## Credits

This feature extends RenderDoc's existing injection mechanism to support runtime attachment, similar to how debuggers like x64dbg can attach to running processes.
