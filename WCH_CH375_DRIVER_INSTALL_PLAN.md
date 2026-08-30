# WCH CH375 驱动随 Sigrok 自动安装方案

D:\CSIETPP\mono-probes\worktrees\probes\topic-smart-kit\.local\U3LogicAnalyzer里是这个逻辑分析仪官方上位机的安装目录。
D:\CSIETPP\mono-probes\worktrees\probes\topic-toolchain\.local\probes-sigrok-project是自管sigrok的一个本地签出。

## 1. 目标与边界

本方案只改造自管 Sigrok 项目，不新增 Extension 逻辑、不新增 Extension
命令、不由 Extension 检查或安装驱动。

目标是：用户运行自管 `sigrok-cli` 并触发 WCH CH32H417 driver 扫描时，程序能够：

1. 在没有 WCH 内核驱动的情况下，仍通过 Windows PnP 发现是否插入了
   `VID_1A86&PID_5537` 设备；
2. 设备存在且没有已绑定的驱动时，调用随包提供的 WCH 驱动安装器；
3. 通过 Windows 标准 `runas` 机制触发 UAC 管理员确认；
4. 安装成功后重新枚举设备并继续原有 CH375DLL/Sigrok 扫描；
5. 不创建 Sigrok 自己的 MessageBox、Webview、终端交互或额外确认提示。

UAC 确认是 Windows 安全边界，不能被消除。用户取消 UAC、权限不足、驱动签名
失败或安装失败时，扫描应返回无设备/设备不可用，并保留可诊断的日志。

## 2. 现有链路与必须补上的环节

现有 driver 在 `src/libsigrok/src/hardware/wch-ch32h417/api.c` 的 `scan()` 中
直接加载 `CH375DLL64.DLL`，然后调用 `CH375OpenDevice()`。没有
`CH375W64.sys` 时，DLL 无法列出未绑定的 USB 设备，因而不能仅依赖 CH375 API
判断“设备已插入”。

扫描顺序应改为：

```text
wch-ch32h417 scan()
  -> Windows PnP 枚举 VID_1A86&PID_5537
  -> 查询设备服务/问题码
  -> 已绑定 CH375_A64 且状态正常：继续现有 CH375DLL 扫描
  -> 设备在场但无驱动：runas 启动 WCH SETUP.EXE /S
  -> 安装进程结束后重新枚举
  -> 驱动正常：继续现有 CH375DLL 扫描
  -> 仍不可用：结束扫描并记录原因
```

该检查只放在 `wch-ch32h417` driver 的 Windows 路径中，不改变其它 Sigrok
driver 的扫描行为。

## 3. 发行包新增内容

### 3.1 推荐目录

在自管 Sigrok 包中增加独立的驱动安装目录：

```text
probes-sigrok/
|-- sigrok-cli.exe
|-- CH375DLL64.DLL                 # 现有运行时 DLL，继续保留
|-- driver-install/
|   `-- wch-ch375/
|       `-- x64/
|           |-- SETUP.EXE          # WCH 安装器，使用 /S
|           |-- DRVSETUP64/
|           |   `-- DRVSETUP64.EXE # SETUP.EXE 需要时的 64 位辅助程序
|           |-- CH375WDM.INF
|           |-- CH375WDM.CAT
|           |-- CH375W64.SYS
|           |-- CH375DLL64.DLL
|           |-- driver-manifest.json
|           `-- PROVENANCE.md
|-- LICENSES/third_party/wch-ch375-driver/LICENSE.txt
`-- ...
```

`CH375DLL64.DLL` 在包根目录的现有副本不能直接替代安装目录中的副本：WCH
`SETUP.EXE` 会按 INF 的相邻文件规则查找驱动文件，且安装后的系统 DLL 位置由
INF 决定。两个文件都必须来自同一份 WCH 驱动版本并分别校验哈希。

### 3.2 文件选择

第一阶段只发布 Windows x64：

- `CH375WDM.INF`
- `CH375WDM.CAT`
- `CH375W64.SYS`
- `CH375DLL64.DLL`
- `SETUP.EXE`
- `DRVSETUP64/DRVSETUP64.EXE`（若实测 `SETUP.EXE /S` 依赖它）

不要加入 `WCHU3Logic-libusb` 目录中的 WinUSB/libusb 文件；它匹配的是
`VID_1A86&PID_8025`，不是本 driver 使用的 `VID_1A86&PID_5537`。

每个二进制文件都要在 `driver-manifest.json` 记录：文件路径、大小、SHA-256、
文件版本、目标架构和来源。`CH375WDM.CAT` 还要在构建和验证阶段检查签名。

现有 `third_party/wch-ch375/` 只记录 DLL，实施时应扩展为完整的驱动来源记录，
并确认 WCH 驱动文件的再分发许可。没有明确许可时不能把 `.sys`、`.cat` 和安装器
作为发布资产。

## 4. Sigrok 源码改造

### 4.1 Windows PnP 探测模块

新增 Windows-only 模块，例如：

```text
src/libsigrok/src/hardware/wch-ch32h417/wch_driver_windows.c
src/libsigrok/src/hardware/wch-ch32h417/wch_driver_windows.h
```

模块提供以下内部接口：

```c
enum wch_driver_state {
    WCH_DRIVER_NO_DEVICE,
    WCH_DRIVER_READY,
    WCH_DRIVER_MISSING,
    WCH_DRIVER_OTHER,
    WCH_DRIVER_ERROR,
};

enum wch_driver_state wch_driver_probe(void);
int wch_driver_install(void);
```

实现要求：

1. 使用 SetupAPI 或 Configuration Manager 枚举当前存在的 USB PnP 设备。
2. 以大小写不敏感方式匹配 `USB\\VID_1A86&PID_5537`，允许实例序列号后缀。
3. 读取设备的服务名、驱动信息和 ConfigMgr problem code。
4. `CH375_A64` 且设备状态正常才返回 `WCH_DRIVER_READY`。
5. 设备存在但没有服务/没有有效驱动时返回 `WCH_DRIVER_MISSING`。
6. 设备已绑定其它服务时返回 `WCH_DRIVER_OTHER`，不得自动替换第三方驱动。
7. 没有匹配设备时返回 `WCH_DRIVER_NO_DEVICE`。
8. API 错误、权限错误或状态不确定时返回 `WCH_DRIVER_ERROR`，不得误判为可安装。

libsigrok 的 Windows 构建需要补充 SetupAPI/Configuration Manager 的链接依赖，
并保持非 Windows 平台的空实现，不影响现有跨平台编译。

### 4.2 扫描时机与状态机

在 `api.c::scan()` 中，调用 `ch375_init()` 之前执行一次 PnP 检查：

```text
NO_DEVICE  -> 直接结束本 driver 扫描
READY      -> 进入现有 DLL 扫描
OTHER      -> 记录“已有其它驱动”，直接结束，不安装
ERROR      -> 记录原始错误，直接结束
MISSING    -> 尝试一次安装
```

安装尝试应有进程内的一次性保护，避免同一次扫描流程重复弹出 UAC。安装器结束
后再次运行 `wch_driver_probe()`：

- 返回 `READY`：继续现有扫描；
- 仍为 `MISSING`：结束扫描并记录安装器退出码；
- 返回 `OTHER` 或 `ERROR`：结束扫描并记录状态变化。

不要因为 `CH375OpenDevice()` 失败而无条件再次启动安装器。只有 PnP 明确发现
设备在场且没有驱动时才允许调用安装器。

### 4.3 安装器调用

安装目录和文件路径必须由当前 `sigrok-cli.exe` 的绝对路径推导，不能依赖当前
工作目录或 PATH。调用方式建议为：

```text
ShellExecuteExW(
    lpVerb = L"runas",
    lpFile = <package-root>\\driver-install\\wch-ch375\\x64\\SETUP.EXE,
    lpParameters = L"/S",
    lpDirectory = <package-root>\\driver-install\\wch-ch375\\x64
)
```

实现细节：

1. 使用 `runas`，让 Windows 产生唯一的管理员 UAC 提示。
2. 不调用 `MessageBox`、VS Code API、GUI 自定义安装页或 stdin 询问。
3. 等待安装器退出，设置明确的超时时间，防止扫描永久阻塞。
4. 处理 `ERROR_CANCELLED`、启动失败、超时和非零退出码。
5. 安装器的工作目录必须是包含 INF、CAT、SYS 和 DLL 的 x64 目录。
6. 安装器实际的静默参数必须在实现阶段用干净 Windows VM 验证；若 WCH
   `SETUP.EXE /S` 不能稳定安装 `CH375WDM.INF`，应改为用同一 UAC 机制调用
   `pnputil.exe /add-driver <INF> /install`，而不是增加自定义 UI。

安装器本身不能被放在可写的临时目录中执行。包解压目录必须经过现有包哈希校验，
安装前再次校验 `driver-manifest.json` 中的文件哈希，避免从被篡改的路径提权执行。

## 5. 打包脚本与清单改动

### 5.1 `scripts/package.ps1`

增加驱动源目录和文件白名单：

1. 校验 `CH375WDM.INF`、`CH375WDM.CAT`、`CH375W64.SYS`、`CH375DLL64.DLL`、
   `SETUP.EXE` 和可选 `DRVSETUP64.EXE` 存在。
2. 校验驱动 manifest 中的哈希、大小和架构。
3. 将它们复制到 `driver-install/wch-ch375/x64/`，保留安装器要求的相对布局。
4. 把驱动文件加入 `package-manifest.json`，不把安装器或 SYS 文件排除在清单外。
5. 将来源、许可证和供应商版本写入 `VERSION.txt` 或 package manifest 的 driver
   节点。

### 5.2 源码披露与许可证

扩展 `scripts/lib/source-disclosure.ps1` 和 `package-licenses.ps1`，披露：

- WCH INF/CAT/SYS/DLL/安装器的来源路径；
- 每个文件的 SHA-256；
- WCH 版本和发布日期；
- 适用的再分发条款；
- 当前包只支持 `x86_64-w64-mingw32`，驱动只包含 x64。

如果 WCH 许可不允许独立再分发安装器或内核驱动，应停止该方案，改为要求用户
自行安装 WCH 驱动，不能只分发一个未经授权的安装包。

### 5.3 包验证

扩展以下验证：

- `verify-package.ps1`：驱动文件完整性、manifest 一致性、CAT 签名和 PE 依赖；
- `package-smoke.ps1`：验证驱动载荷静态存在，但不在普通 CI 中触发 UAC 或安装；
- 新增 Windows 驱动集成测试：在干净 VM 中分别验证无驱动、安装取消、安装成功、
  已有 `CH375_A64`、已有其它驱动和设备未插入六种状态。

普通无硬件测试只能证明安装器可启动和包内容完整，不能宣称设备扫描成功。

## 6. 日志和失败语义

不增加用户提示窗口，但应在 Sigrok 日志中保留可诊断信息：

- 匹配到的 VID/PID 和 PnP 实例 ID 的脱敏形式；
- 当前服务名和 problem code；
- 安装器路径、参数、启动错误或退出码；
- 安装后的二次探测结果；
- 最终未返回设备的原因。

日志不得把完整用户目录、序列号或安全令牌写入默认输出。UAC 被取消应作为普通
的安装未完成结果，而不是崩溃或无限重试。

## 7. 实施顺序

### 阶段 A：载荷和许可

1. 从官方安装目录提取 x64 驱动文件和安装器。
2. 验证 WCH 再分发许可、CAT 签名和版本一致性。
3. 建立 `driver-manifest.json`、provenance 和 license。
4. 修改 package 脚本，生成含 `driver-install/` 的 Sigrok ZIP。
5. 先通过静态包验证。

### 阶段 B：PnP 探测

1. 实现 Windows PnP 枚举和服务/problem code 分类。
2. 为无设备、未绑定设备、`CH375_A64`、其它驱动和 API 错误增加单元测试。
3. 将探测接入 `wch-ch32h417` 扫描前，但暂时只记录状态，不启动安装器。

### 阶段 C：提权安装

1. 验证 `SETUP.EXE /S` 的工作目录、退出码和实际安装结果。
2. 实现 `runas` 启动、等待、超时和一次性保护。
3. 安装后重新探测，成功才进入 CH375 DLL 扫描。
4. 增加取消 UAC、拒绝权限、缺文件和安装失败测试。

### 阶段 D：干净环境验收

在没有完整 U3LogicAnalyzer 上位机的 Windows x64 VM 中验证：

1. 插入 `VID_1A86&PID_5537`，确认初始没有 `CH375_A64`。
2. 执行 `sigrok-cli -d wch-ch32h417 --scan`，只出现 Windows UAC，不出现自定义提示。
3. 接受 UAC 后设备绑定 `CH375_A64`，扫描返回 CH32H417 Logic Analyzer。
4. 取消 UAC 后扫描干净退出，不重复弹出、不死循环。
5. 第二次扫描不再启动安装器。
6. 已安装驱动、未插设备和绑定其它驱动时均不触发安装器。
7. 用 `pnputil /enum-devices` 和 `Get-PnpDevice` 保存机器级验收证据。

## 8. 验收标准

方案只有在以下条件全部满足后才可发布：

- Extension 没有新增驱动安装代码或命令；
- 自管 Sigrok ZIP 包含完整、已授权、已签名的 WCH x64 驱动载荷；
- 未安装驱动时，Sigrok 能通过 PnP 发现设备并只调用一次安装器；
- UAC 由 Windows `runas` 产生，Sigrok 不显示自定义弹窗或提示页；
- 安装成功后同一次扫描可以继续并发现设备，或明确要求重新插拔后再扫描；
- 用户取消或安装失败不会造成无限重试、崩溃或替换其它驱动；
- 干净 Windows VM 通过完整测试矩阵；
- 包清单、许可证、来源、哈希和安装行为均可追溯。

