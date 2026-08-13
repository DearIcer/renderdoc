# 3DMigoto 式图形 API 挂钩与注入兼容方案

## 背景

当前分支已集成 MinHook（inline hook，宏 `USE_MINHOOK`），但直接对
`d3d11.dll` / `dxgi.dll` 的导出函数做内联挂钩时，会与同样挂钩这些导出的
反作弊组件冲突：

- 双方改写同一个函数头部（`E9 jmp`），互相破坏对方的 trampoline / 链；
- 反作弊对系统 DLL 代码段做校验时，会发现"额外"的修改；
- 包装对象（WrappedIDXGIFactory 等）改变了对象身份，反作弊/Overlay 按真实
  对象 VTable 挂钩时看到的是另一个对象。

社区项目 3DMigoto（bo3b/3Dmigoto）的解决思路是：

1. **注入方式**：
   - Wrapper/Proxy DLL：把 `d3d11.dll`/`dxgi.dll` 放到游戏目录，由 Windows
     加载器优先加载，代理转发到系统 DLL，完全不依赖远程线程注入；
   - Loader：`CreateProcess(CREATE_SUSPENDED)` 挂起启动，再用
     `SetWindowsHookEx` / APC 注入后恢复线程。
2. **挂钩方式**：
   - 创建函数（`CreateDXGIFactory*`、`D3D11CreateDevice*`）仍需要拦截，但用
     Nektra 这类能链式处理"已被他人挂钩"的函数库，不重复改写同一字节；
   - 关键 COM 方法（`IDXGIFactory::CreateSwapChain`、
     `IDXGISwapChain::Present/Present1`）通过 **VTable 挂钩** 拦截：只改写
     对象 vtable 槽位，保留对象身份，且与已经存在的 vtable 钩子链式共存。

## 本次改动

### 1. 新增 VTable 挂钩工具

`renderdoc/os/win32/win32_vtablehook.{h,cpp}`

- `VTableHook::Install(obj, slot, detour, &orig, cloneVtable)`
  把 `obj` 的 vtable 第 `slot` 槽改写为 `detour`，原值写入 `orig`（调用
  `orig` 即可链到之前已存在的钩子/真实函数）。
- `VTableHook::Remove(...)` 恢复原槽位。
- `VTableHook::Scoped` RAII 封装，析构自动卸载。
- `cloneVtable=true` 时先为对象复制一份私有 vtable 再改写，不触碰模块共享
  vtable（可选，默认关闭，与 3DMigoto 一致直接改共享槽）。

### 2. win32_hook.cpp：inline hook 与反作弊共存

- IsInlineHookPresent(func)：检测目标导出函数头部是否已被他人内联挂钩
  （E9/EB/FF 25/48 FF 25 跳转，以及 48 B8 imm64; FF E0 绝对跳转）。
- `CreateMinHookForExport(...)`：MinHook 创建前先检测；若已被反作弊挂钩，
  **跳过内联挂钩**，改为对导入该函数的模块做 **IAT 回退挂钩**，同时保留
  既有的 `GetProcAddress` 钩子重定向动态查找。这样链是：
  游戏 → RenderDoc 钩子 → 反作弊钩子 → 真实函数，双方字节互不干扰。
- 修复 MinHook 模式下 **晚加载模块** 未被挂钩的问题：模块加载时（
  `ApplyHooks` 的 module-setup 分支）现在会为它的导出补建 MinHook 钩子，
  而不只是注册期已加载的模块。
- `RemoveHooks` 现在会恢复 IAT 回退条目（MinHook 模式不再提前 return）。

### 3. dxgi_hooks.cpp：`USE_VTABLE_HOOK` 模式（3DMigoto 式工厂挂钩）

定义 `USE_VTABLE_HOOK` 后：

- `CreateDXGIFactory*` 成功创建工厂后**不再包装**工厂对象，返回真实工厂
  （保留对象身份），而是用 `VTableHook` 挂钩工厂 vtable 的
  `CreateSwapChain`（槽 10）/ `CreateSwapChainForHwnd`（槽 15）/
  `CreateSwapChainForCoreWindow`（槽 16）/
  `CreateSwapChainForComposition`（槽 24）槽位；
- 钩子内逻辑与 `WrappedIDXGIFactory::CreateSwapChain*` 一致：解包设备、调用
  真实方法、把 swapchain 包装为 `WrappedIDXGISwapChain4`（swapchain 仍包装，
  因为 D3D11/D3D12 驱动需要它跟踪后备缓冲）；
- 若反作弊已经挂钩了同一 vtable 槽，`VTableHook` 会把它的钩子存为
  `orig`，我们的钩子调用 `orig` 即可链式通过。


### 4. win32_manualmap.cpp：手工映射注入修复

- 原 `RemoteGetProcAddress` 在 x64 上把双参数 `GetProcAddress` 直接作为线程
  入口调用（参数寄存器不对），且用 32 位线程退出码截断返回地址；现改为在
  注入器进程内用本地 `GetProcAddress` 解析导入（renderdoc.dll 的静态导入均
  为系统/CRT DLL，地址跨进程一致），同时仍通过 `RemoteLoadLibrary` 确保目
  标进程已加载对应模块。
- 修正 x64 `DllMain` shellcode：之前未把映像基址作为 `hModule` 传入，现按
  x64 调用约定设置 `rcx=hModule`、`rdx=1`、`r8=NULL` 后再调用入口点。

### 5. 新增 d3d11_proxy（3DMigoto Wrapper/Proxy DLL）

`renderdoc/os/win32/d3d11_proxy/`

- 输出 `d3d11_proxy64.dll` / `d3d11_proxy32.dll`，实际使用时改名为
  `d3d11.dll` 放到游戏目录。
- 除 `D3D11CreateDevice`、`D3D11CreateDeviceAndSwapChain`、
  `D3D11On12CreateDevice` 三个创建入口外，其余 d3d11.dll 导出全部通过 PE
  导出转发器转发到系统 `d3d11.dll`（保留系统导出序号）。
- 三个创建入口在首次调用时 `LoadLibrary` 真正的系统 d3d11.dll，再加载
  `rdhelper.dll`，并把 `D3D11CreateDevice*` 的调用转发给系统函数。此时
  RenderDoc 已注册自己的 D3D11/DXGI 钩子，设备/交换链会被 RenderDoc 正常
  包装。
- DllMain 会同步加载 RenderDoc（类似 3DMigoto 在 DLL attach 时初始化），确保游戏第一次调用
  `D3D11CreateDevice*` 前导出入口已被 RenderDoc 挂钩；创建入口仍带同步等待保护。
- `rdhelper.dll` 查找顺序：
  1. 环境变量 `RENDERDOC_RDOC_PATH`；
  2. 代理 DLL 同目录下的 `renderdoc_proxy_rdoc_path.txt`（UTF-8，可带 BOM）；
  3. 代理 DLL 同目录下的 `rdhelper.dll`；
  4. 代理 DLL 同目录下的 `renderdoc.dll`。
- 默认捕获选项设为：`allowVSync=1`、`allowFullscreen=1`、
  `debugOutputMute=1`，其余保持 RenderDoc 默认值。若需要自定义选项，可
  后续把选项写入代理旁的配置文件，再在 `LoadRenderDoc()` 里解析。
- 工程已加入 `renderdoc.sln`，随 x64/x86 Development/Release 一起构建。
- `EnsureRenderDocLoaded()` 用事件同步等待加载完成，避免多线程下 `D3D11CreateDevice*`
  在系统函数指针发布前返回 `E_FAIL`；已用合成 D3D11 进程重复 10 次验证。
- 合成进程里检测返回的设备/上下文 vtable 归属：代理模式下 vtable 位于
  `rdhelper.dll`，直连系统 d3d11 时位于 `C:\Windows\System32\d3d11.dll`，证明设备/上下文
  已被 RenderDoc 包装；`INTERNAL_GetTargetControlIdent` 也返回非零端口。
- 设置环境变量 `D3D11_PROXY_DEBUG=1` 后，代理会在自身目录写
  `d3d11_proxy.log`，并尝试把 RenderDoc 日志切到
  `d3d11_proxy_renderdoc.log`，便于排查游戏目录下的注入问题。


- 静态审计本机游戏目录：实际导入 `d3d11.dll` 的只有 `D3D11CreateDevice`；导入
  `dxgi.dll` 的只有 `CreateDXGIFactory` / `CreateDXGIFactory1`，均已被代理覆盖，且未
  发现 ordinal-only 导入。
### 5.1 新增 dxgi_proxy（3DMigoto Wrapper/Proxy DLL 备选入口）

`renderdoc/os/win32/dxgi_proxy/`

- 输出 `dxgi_proxy64.dll` / `dxgi_proxy32.dll`，使用时改名为 `dxgi.dll` 放
  到游戏目录。
- 全部 20 个 `dxgi.dll` 导出均转发到系统 `dxgi.dll`，不在代理中实现本地
  包装函数。
- DllMain 同步加载 `rdhelper.dll`；RenderDoc 会把 `CreateDXGIFactory*` 转
  发导出钩住，并在工厂创建时走 `USE_VTABLE_HOOK` 的 VTable 挂钩路径。
- 已用合成 DXGI 进程验证：`CreateDXGIFactory` 返回 `S_OK`，`rdhelper.dll`
  已加载。
- 设置 `DXGI_PROXY_DEBUG=1` 可写 `dxgi_proxy.log` 并尝试把 RenderDoc 日志
  切到 `dxgi_proxy_renderdoc.log`。


## 如何启用

- MinHook：`renderdoc.vcxproj` 已定义 `USE_MINHOOK`（全配置）。
- VTable 工厂挂钩：已在 `renderdoc/driver/dxgi/renderdoc_dxgi.vcxproj` 的
  `<PreprocessorDefinitions>` 中定义 `USE_VTABLE_HOOK;`（dxgi_hooks.cpp 所在
  静态库工程），因此默认启用；如需回退到包装工厂，删除该宏即可。
- Proxy DLL 已实现，见下文“Proxy DLL 使用方式”。


## 注意事项

- VTable 挂钩不改系统 DLL 代码段，但仍会改对象 vtable；对 vtable 校验严格
  的反作弊（校验 vtable 指针是否落在已知模块）建议开 `cloneVtable`。
- `USE_VTABLE_HOOK` 下工厂不再被包装，因此从真实工厂 `EnumOutputs` 拿到的
  output 是真实对象；`CreateSwapChainForHwnd` 等钩子里已通过
  `WrappedIDXGIOutput6::IsAlloc` 做解包判断。
- 本改动未包含 3DMigoto 的 `Present` vtable 挂钩：RenderDoc 通过包装
  swapchain 已经拦截 Present。若需要"完全不包装 swapchain、保持其身份"，
  需进一步把 `GetBuffer`/`GetDevice`/`QueryInterface` 也 vtable 挂钩，属于
  后续工作。

## Proxy DLL 使用方式

1. 构建 `d3d11_proxy` 工程（或直接构建 `renderdoc.sln`），得到
   `x64\Development\d3d11_proxy64.dll`。
2. 把该文件复制到《原神》目录并改名为 `d3d11.dll`。

备选入口：也可构建 `dxgi_proxy`，把 `x64\Development\dxgi_proxy64.dll` 改名
为 `dxgi.dll` 放入游戏目录；后续步骤相同。
3. 若 `rdhelper.dll` 不在游戏目录，设置环境变量
   `RENDERDOC_RDOC_PATH=D:\Github-WebProject\renderdoc\x64\Development\rdhelper.dll`。

更省事的自动部署脚本（会写 `renderdoc_proxy_rdoc_path.txt`）：
```powershell
util\deploy_genshin_proxy.ps1 -Apply -Configuration Release
```

带调试日志启动游戏（不会修改文件）：
```powershell
util\run_genshin_proxy_debug.ps1 -Configuration Release -Launch
```

4. 正常启动游戏。代理加载后，RenderDoc 会在设备创建前完成 D3D11/DXGI 钩子
   注册；用 RenderDoc 图形界面连接本机目标控制端口即可抓帧（RenderDoc
   启动时会自动打开 target-control 端口，无需远程线程注入）。
5. 抓帧结束或不需要代理时，删除/改名游戏目录中的 `d3d11.dll` 即可完全还原。

注意：该方式仍会让 RenderDoc 对 `D3D11CreateDevice*` 的代理导出做内联挂钩；
它不会修改系统 `d3d11.dll` 的代码段。若反作弊对游戏目录代理文件或代理导出
头部做完整性校验，需要额外做代理签名/白名单处理。



