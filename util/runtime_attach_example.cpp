#include <windows.h>
#include <stdio.h>
#include "renderdoc/os/os_specific.h"
#include "renderdoc/api/replay/capture_options.h"

void PrintProcessInfo(const RuntimeAttach::ProcessInfo &proc)
{
  printf("PID: %-8u  Name: %-20s  Path: %s\n",
         proc.pid, proc.name.c_str(), proc.path.c_str());
  printf("      64-bit: %-5s  System: %-5s\n",
         proc.is64Bit ? "Yes" : "No",
         proc.isSystemProcess ? "Yes" : "No");
  printf("\n");
}

int main(int argc, char *argv[])
{
  printf("RenderDoc Runtime Attacher Example\n");
  printf("================================\n\n");

  if(argc < 2)
  {
    printf("Usage:\n");
    printf("  %s list                    - List all processes\n", argv[0]);
    printf("  %s list-graphics          - List processes with graphics APIs\n", argv[0]);
    printf("  %s attach <pid>           - Attach to process by PID\n", argv[0]);
    printf("  %s attach <pid> <file>   - Attach and set capture file\n", argv[0]);
    printf("\n");
    return 1;
  }

  rdcstr command = argv[1];

  if(command == "list")
  {
    printf("Enumerating all processes...\n\n");
    rdcarray<RuntimeAttach::ProcessInfo> processes = RuntimeAttach::EnumerateProcesses();

    printf("Found %zu processes:\n\n", processes.size());
    for(const RuntimeAttach::ProcessInfo &proc : processes)
    {
      PrintProcessInfo(proc);
    }
  }
  else if(command == "list-graphics")
  {
    printf("Enumerating processes with graphics APIs...\n\n");
    rdcarray<RuntimeAttach::ProcessInfo> processes =
        RuntimeAttach::EnumerateProcessesWithGraphicsAPI();

    printf("Found %zu processes with graphics APIs:\n\n", processes.size());
    for(const RuntimeAttach::ProcessInfo &proc : processes)
    {
      PrintProcessInfo(proc);
    }
  }
  else if(command == "attach")
  {
    if(argc < 3)
    {
      printf("Error: PID required for attach command\n");
      printf("Usage: %s attach <pid> [capture_file]\n", argv[0]);
      return 1;
    }

    uint32_t pid = atoi(argv[2]);
    if(pid == 0)
    {
      printf("Error: Invalid PID '%s'\n", argv[2]);
      return 1;
    }

    printf("Attaching to process %u...\n", pid);

    rdcstr captureFile;
    if(argc >= 4)
    {
      captureFile = argv[3];
      printf("Capture file: %s\n", captureFile.c_str());
    }

    CaptureOptions opts;
    opts.captureAllCmdLists = true;
    opts.apiValidation = false;
    opts.captureCallstacks = false;
    opts.captureCallstackOnlyDraws = false;
    opts.debugOutputMute = false;
    opts.hookIntoChildren = true;
    opts.refAllResources = false;
    opts.saveAllInitials = false;
    opts.delayForDebugger = 0;

    rdcpair<RDResult, uint32_t> result = RuntimeAttach::AttachToProcess(pid, captureFile, opts);

    if(result.first.code == ResultCode::Succeeded)
    {
      printf("\nSuccessfully attached to process %u\n", pid);
      printf("Target control ident: %u\n", result.second);
      printf("\nYou can now connect to this process using RenderDoc UI.\n");
    }
    else
    {
      printf("\nFailed to attach to process %u\n", pid);
      printf("Error: %s\n", result.first.message.c_str());
      return 1;
    }
  }
  else
  {
    printf("Unknown command: %s\n", command.c_str());
    printf("Run '%s' without arguments for usage information.\n", argv[0]);
    return 1;
  }

  return 0;
}
