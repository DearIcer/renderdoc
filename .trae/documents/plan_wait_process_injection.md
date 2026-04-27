# 计划：为 renderdoccmd 添加等待进程启动后注入的功能

## 目标
根据参考代码（`参考代码.md`）中的模式，为 renderdoccmd 项目的 inject 命令添加等待目标进程启动后再进行注入的功能。

## 背景
当前 `inject` 命令需要用户提供进程ID (PID) 并立即注入。用户希望添加类似参考代码的功能：通过进程名指定目标进程，并等待该进程启动后再注入。

参考代码关键功能：
1. 通过进程名查找进程（使用 `CreateToolhelp32Snapshot`）
2. 循环等待直到目标进程出现
3. 注入 DLL

## 实施步骤

### 1. 分析现有代码结构
- 阅读 `renderdoccmd.cpp` 中的 `InjectCommand` 类（第281-330行）
- 理解现有选项：`--PID`、`--capture-file`、`--wait-for-exit`
- 查看 `RENDERDOC_InjectIntoProcess` 函数的调用方式

### 2. 设计新选项
扩展 `InjectCommand` 类，添加以下选项：
- `--process-name` 或 `--wait-for-process`：指定目标进程的可执行文件名（例如 "Endfield.exe"）
- `--timeout`（可选）：设置最大等待时间（秒），超时后退出
- 保持向后兼容：如果同时提供 PID 和进程名，优先使用 PID

### 3. 实现进程查找函数
在 Windows 平台（`renderdoccmd_win32.cpp`）实现进程查找功能：

```cpp
// 参考参考代码中的 find_process 函数
DWORD FindProcessByName(const char* processName);
```

函数逻辑：
1. 使用 `CreateToolhelp32Snapshot` 获取进程快照
2. 遍历进程列表，比较 `szExeFile` 与目标进程名（不区分大小写）
3. 返回找到的进程ID，未找到返回0

### 4. 修改 InjectCommand 类
#### 4.1 添加私有成员
```cpp
private:
  uint32_t PID = 0;
  std::string captureFile;
  bool wait_for_exit = false;
  std::string processName;  // 新增
  int timeout = 0;          // 新增，0表示无限等待
```

#### 4.2 修改 AddOptions 方法
添加新选项：
```cpp
parser.add<uint32_t>("PID", 0, "The process ID of the process to inject.", true);
parser.add<std::string>("process-name", '\0', 
                       "The name of the process to inject into (e.g., MyGame.exe). "
                       "If specified, will wait for the process to start.", false, "");
parser.add<int>("timeout", '\0', 
               "Maximum time to wait for process to start (seconds). 0 = wait indefinitely.",
               false, 0);
```

#### 4.3 修改 Parse 方法
解析新选项：
```cpp
PID = parser.get<uint32_t>("PID");
captureFile = parser.get<std::string>("capture-file");
wait_for_exit = parser.exist("wait-for-exit");
processName = parser.get<std::string>("process-name");
timeout = parser.get<int>("timeout");

// 验证：至少提供 PID 或 process-name 之一
if(PID == 0 && processName.empty()) {
  std::cerr << "Error: must specify either --PID or --process-name" << std::endl;
  return false;
}
```

#### 4.4 修改 Execute 方法
实现等待逻辑：
```cpp
// 如果指定了进程名但未指定PID，则等待进程启动
if(!processName.empty() && PID == 0) {
  std::cout << "Waiting for process \"" << processName << "\" to start..." << std::endl;
  
  DWORD startTime = GetTickCount();
  while((PID = FindProcessByName(processName.c_str())) == 0) {
    // 检查超时
    if(timeout > 0) {
      DWORD elapsed = (GetTickCount() - startTime) / 1000;
      if(elapsed >= timeout) {
        std::cerr << "Timeout: Process \"" << processName << "\" did not start within " 
                  << timeout << " seconds." << std::endl;
        return 1;
      }
    }
    
    // 短暂休眠后继续检查
    Sleep(100);
  }
  
  std::cout << "Process found (PID: " << PID << ")" << std::endl;
}

// 原有的注入逻辑...
```

### 5. 跨平台考虑
- Windows：使用 `CreateToolhelp32Snapshot` API
- Linux：通过 `/proc` 文件系统查找进程
- macOS：使用 `sysctl` 或 `proc_pidinfo` API
- 其他平台：如果无法实现进程名查找，则仅支持 PID 模式

建议先实现 Windows 版本，其他平台可后续添加或显示警告信息。

### 6. 错误处理
- 进程未找到时的超时处理
- 权限不足时的错误提示（可能需要管理员权限）
- 注入失败的错误信息

### 7. 更新帮助文本
更新 `Description()` 方法，说明新功能：
```cpp
virtual const char *Description() { 
  return "Injects RenderDoc into a given running process. "
         "Can wait for a process to start by name."; 
}
```

### 8. 测试计划
1. 编译项目确保无错误
2. 测试现有功能（通过 PID 注入）仍正常工作
3. 测试新功能：
   - 启动一个测试进程（如 notepad.exe）
   - 使用 `renderdoccmd inject --process-name notepad.exe` 命令
   - 验证注入成功
4. 测试超时功能
5. 测试同时提供 PID 和进程名的情况

## 文件修改清单
1. `renderdoccmd/renderdoccmd.cpp`：修改 `InjectCommand` 类
2. `renderdoccmd/renderdoccmd_win32.cpp`：添加 `FindProcessByName` 函数实现
3. 可能需要修改 `renderdoccmd.h` 添加函数声明

## 注意事项
1. 保持代码风格与项目一致
2. 避免不必要的注释（根据用户要求）
3. 考虑安全性：进程名匹配应不区分大小写
4. 注入可能需要管理员权限，参考代码中有 UAC 提权逻辑，但 renderdoccmd 可能已处理

## 后续优化（可选）
1. 添加 `--interval` 选项控制检查间隔
2. 支持通配符匹配进程名
3. 添加 `--list-processes` 选项列出所有运行中的进程
4. 支持同时注入多个同名进程实例