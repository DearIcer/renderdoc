# RenderDoc 表层特征修改计划

## 目标
修改 RenderDoc 的硬编码特征字符串，以规避简单的特征码扫描。重点修改反作弊系统最常扫描的字符串，包括：
- 动态库文件名（renderdoc.dll）
- 产品名、窗口类名、互斥体（Mutex）名称、命名管道（Named Pipe）名称
- 导出函数名

## 总体策略
1. **保持功能不变**：仅修改字符串常量，不改变逻辑和接口（除非导出函数名需要变更）
2. **系统性替换**：全局搜索并替换源代码中的关键字符串
3. **避免引入新问题**：确保修改后的字符串长度与原来相同或类似，防止缓冲区溢出等问题
4. **测试验证**：修改后必须通过编译和基本功能测试

## 需要修改的关键字符串列表
### 1. 动态库及相关文件名
- `renderdoc.dll` → 改为 `rdhelper.dll`（示例，可自定义）
- `librenderdoc.so` → `librdhelper.so`
- `librenderdoc.dylib` → `librdhelper.dylib`
- `renderdoc`（内部名称、原始文件名等） → `rdhelper`

### 2. 产品名、公司名等资源字符串
- `RenderDoc` → `GraphHelper`
- `Baldur Karlsson` → `GraphHelper Team`（可选，不影响扫描）
- `Core DLL for RenderDoc` → `Core DLL for GraphHelper`

### 3. 窗口类名
- `renderdoccmd` → `rdcmd`
- 其他可能的窗口类名（需进一步搜索）

### 4. 互斥体、事件、信号量名称
- `RENDERDOC_CRASHHANDLE` → `GRAPHHELPER_CRASHHANDLE`
- 其他以 `RENDERDOC_` 开头的内核对象名

### 5. 命名管道名称
- 搜索 `\\\\\\.\\\\pipe\\\\` 或 `NamedPipe` 相关的字符串，替换其中的 `renderdoc` 部分

### 6. 导出函数名
- `RENDERDOC_GetAPI` → `GRAPHHELPER_GetAPI`
- 所有以 `RENDERDOC_` 为前缀的导出函数（见 `renderdoc_app.h` 和 `apidefs.h`）
- 注意：修改导出函数名会影响使用 GetProcAddress 加载的应用程序，但这是规避扫描的必要步骤。

### 7. 预处理器宏前缀
- 宏前缀 `RENDERDOC_` 改为 `GRAPHHELPER_`（例如 `RENDERDOC_API`、`RENDERDOC_CC` 等）
- 注意：这些宏在编译后不会留下字符串，但为了代码一致性可以修改。不过修改宏可能影响大量代码，需谨慎评估。

## 实施步骤
### 第一阶段：代码库分析
1. 使用 grep/ripgrep 搜索所有硬编码字符串，生成详细列表。
2. 确定每个字符串的出现位置（源代码、资源文件、构建脚本等）。
3. 分类字符串，区分必须修改的和可选的（如注释、文档）。

### 第二阶段：字符串替换
1. **动态库文件名**：替换所有 `renderdoc.dll`、`librenderdoc.so`、`librenderdoc.dylib` 的引用。
   - 注意：构建脚本（CMakeLists.txt、.vcxproj、.wxs 等）中的名称也需要修改。
2. **资源字符串**：修改 `.rc` 文件中的 `VALUE` 字段。
3. **窗口类名**：修改 `lpszClassName` 等赋值。
4. **内核对象名**：替换 `CreateEvent`、`CreateMutex`、`CreateNamedPipe` 等调用中的名称。
5. **导出函数名**：
   - 修改 `apidefs.h` 中 `RENDERDOC_EXPORT_API` 的定义？不需要，只需修改函数名本身。
   - 修改 `renderdoc_app.h` 中的函数声明和 `app_api.cpp` 中的定义。
   - 注意：函数名修改后，需要同步更新所有使用 `GetProcAddress` 加载该函数的地方（如测试代码、工具代码）。这部分可能较多，但为了规避扫描是必要的。
6. **预处理器宏**：如果决定修改，则全局替换 `RENDERDOC_` 为 `GRAPHHELPER_`。

### 第三阶段：构建配置更新
1. 修改 CMake 项目名称、输出文件名。
2. 修改安装程序脚本（.wxs）中的产品名称、安装目录等。
3. 更新版本信息文件（`renderdoc.version`、`rdocself.version`）中的字符串。

### 第四阶段：编译测试
1. 执行完整构建（Debug/Release），确保无编译错误。
2. 运行单元测试和基础功能测试，验证修改后的 DLL 能否正常加载、捕获、回放。
3. 检查生成的二进制文件是否包含原特征字符串（使用 strings 工具）。

## 风险与注意事项
1. **兼容性破坏**：修改导出函数名会导致现有应用程序无法加载修改后的 RenderDoc。但这是规避扫描的代价，用户应了解此限制。
2. **字符串长度**：确保新字符串长度不超过原字符串分配的缓冲区大小（特别是固定大小的数组）。
3. **第三方代码**：注意不要修改第三方库（如 breakpad、zstd、lz4）中的字符串，除非它们引用了 RenderDoc。
4. **平台差异**：Windows、Linux、macOS、Android 等平台下的字符串可能需要分别处理。
5. **调试信息**：编译生成的 PDB 文件可能仍包含原字符串，需考虑是否移除或混淆。

## 后续工作
1. 如果希望进一步隐藏，可以考虑：
   - 加壳或压缩 DLL
   - 动态构造字符串（运行时拼接）
   - 加密敏感字符串
2. 但这些属于更深层的混淆，超出本次“表层”修改的范围。

## 计划输出
完成上述步骤后，将得到一个“改头换面”的 RenderDoc 版本，其二进制文件不再包含明显的 `RenderDoc`、`renderdoc.dll`、`RENDERDOC_` 等特征字符串，从而能够绕过简单的特征码扫描。