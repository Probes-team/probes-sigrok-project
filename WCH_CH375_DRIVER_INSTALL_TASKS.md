# WCH CH375 驱动自动安装改造任务清单

依据：`WCH_CH375_DRIVER_INSTALL_PLAN.md`。

范围：只修改自管 Sigrok；Extension 不增加驱动检测、安装代码或命令。目标平台
第一阶段为 Windows x64。所有任务默认未完成，完成后将对应 `[ ]` 改为 `[x]`，
并在任务下方补充验证结果或证据路径。

## A. 载荷、版本与再分发许可

- [x] 从 `D:\CSIETPP\mono-probes\worktrees\probes\topic-smart-kit\.local\U3LogicAnalyzer` 的官方安装目录提取与当前 `CH375DLL64.DLL` 同版本的 `CH375WDM.INF`、`CH375WDM.CAT`、`CH375W64.SYS`、`SETUP.EXE` 和 `DRVSETUP64\DRVSETUP64.EXE`（如安装器实际依赖）。
- [x] 将上述原始文件放入 `third_party/wch-ch375/driver-source/x64/`，保持 WCH 安装器要求的相对目录；禁止从可写临时目录或用户 PATH 取文件。
- [x] 在 `third_party/wch-ch375/driver-manifest.json` 建立载荷清单，记录每个文件的相对路径、大小、SHA-256、PE 架构/版本、WCH 版本、来源和签名要求。
- [x] 更新 `third_party/wch-ch375/PROVENANCE.md`，记录 INF/CAT/SYS/DLL/EXE 的来源路径、提取日期、版本、哈希及与 `VID_1A86&PID_5537`、`CH375_A64` 的对应关系。
- [ ] 在 `third_party/wch-ch375/LICENSE.txt` 或新增 `third_party/wch-ch375/REDISTRIBUTION.md` 中记录 WCH 对 `.sys`、`.cat`、`.dll`、安装器的再分发许可；若许可不明确，停止后续打包任务。
- [x] 使用 `signtool` 或等效工具验证 `CH375WDM.CAT` 的签名链、签名时间和签名状态，并将验证命令及输出保存到 `tests/evidence/wch-ch375/`（该目录如被忽略则保留本地证据）。
- [x] 确认 `CH375WDM.INF` 的服务名为 `CH375_A64`、硬件 ID 为 `USB\\VID_1A86&PID_5537`，并确认没有误用目标为 `VID_1A86&PID_8025` 的 `WCHU3Logic-libusb.inf`。

## B. 打包目录与发布清单

- [x] 修改 `scripts/package.ps1`：增加 `third_party/wch-ch375/driver-source/x64/` 和 `driver-manifest.json` 的输入存在性检查。
- [x] 修改 `scripts/package.ps1`：按 manifest 校验文件路径、大小、SHA-256、架构和目录布局，校验失败时停止打包。
- [x] 修改 `scripts/package.ps1`：将完整载荷复制到 `driver-install/wch-ch375/x64/`，保留 `DRVSETUP64/` 子目录和安装器需要的相邻文件。
- [x] 修改 `scripts/package.ps1`：将驱动 manifest 以固定路径复制进包，并把所有 INF/CAT/SYS/DLL/EXE 纳入 `package-manifest.json` 的 `files` 哈希清单。
- [x] 修改 `scripts/package.ps1`：在包 manifest 增加 driver 元数据（driver ID、目标架构、匹配硬件 ID、可接受服务、安装方式、版本和 replacement policy）。
- [x] 修改 `scripts/lib/source-disclosure.ps1`：披露完整 WCH 驱动载荷的来源、版本和哈希，不只披露现有 `CH375DLL64.DLL`。
- [ ] 修改 `scripts/lib/package-licenses.ps1`：将 WCH 驱动载荷的许可证/再分发说明复制到 `LICENSES/third_party/wch-ch375/`。
- [x] 修改 `tests/runtime-paths.ps1`：增加 `driver-install/wch-ch375/x64/` 的固定路径检查，验证运行时不依赖当前工作目录。
- [x] 修改 `tests/package-smoke.ps1`：增加 INF、CAT、SYS、安装器和 driver manifest 的静态存在性、哈希和布局检查；该测试不得触发 UAC 或实际安装。
- [x] 修改 `scripts/verify-package.ps1`：校验 driver manifest 与 package manifest 一致，检查 catalog 签名，并确保所有驱动载荷都已被 manifest 覆盖。
- [x] 为 `scripts/verify-package.ps1` 增加安装器/驱动 PE 架构检查，确认第一阶段只允许 x64 载荷进入 x64 包。
- [ ] 生成一次 `scripts/package.ps1` 产物，确认 ZIP 内包含 `driver-install/wch-ch375/x64/`，并用 `scripts/verify-package.ps1` 通过静态验证。

## C. Windows PnP 探测模块

- [x] 新增 `src/libsigrok/src/hardware/wch-ch32h417/wch_driver_windows.h`，定义 Windows 探测状态、错误码、设备匹配结果和安装接口；接口不得暴露任意路径执行能力。
- [x] 新增 `src/libsigrok/src/hardware/wch-ch32h417/wch_driver_windows.c`，使用 SetupAPI 或 Configuration Manager 枚举当前存在的 USB PnP 实例。
- [x] 在 `wch_driver_windows.c` 中大小写不敏感匹配 `USB\\VID_1A86&PID_5537`，允许实例 ID 的序列号后缀，并避免把 `VID_1A86&PID_8025` 误判为 CH375。
- [x] 在 `wch_driver_windows.c` 中读取服务名、驱动提供商/版本和 ConfigMgr problem code；只在服务为 `CH375_A64` 且设备状态正常时返回 `READY`。
- [x] 在 `wch_driver_windows.c` 中实现 `NO_DEVICE`、`READY`、`MISSING`、`OTHER` 和 `ERROR` 状态；查询失败或状态不确定时不得降级为 `MISSING`。
- [x] 对已绑定其它服务的设备返回 `OTHER`，禁止自动卸载、替换或重绑定现有第三方驱动。
- [x] 新增非 Windows 空实现（可放在同一文件的条件编译分支，或新增 `wch_driver_windows_stub.c`），保证 Linux/macOS 构建不引入 Windows 头文件或符号。
- [x] 为 SetupAPI、Configuration Manager 和安装器调用所需的 Windows 库补充构建链接声明，位置按当前工程约定修改 `src/libsigrok/Makefile.am` 或对应 configure 条件。
- [ ] 在 `src/libsigrok/src/hardware/wch-ch32h417/` 增加 mock/injected PnP 测试接口，避免普通单元测试依赖真实 USB 设备或管理员权限。
- [ ] 为无设备、缺少驱动、`CH375_A64` 正常、其它服务、problem code、API 错误和大小写/实例后缀分别增加测试用例，测试文件放在 `src/libsigrok/tests/` 或当前硬件测试约定目录。

## D. 扫描流程接入

- [x] 修改 `src/libsigrok/src/hardware/wch-ch32h417/api.c`：在 `ch375_init()` 之前调用 Windows PnP 探测；非 Windows 保持原有行为。
- [x] 修改 `api.c`：`NO_DEVICE` 直接结束该 driver 扫描，`READY` 进入现有 `CH375DLL64.DLL` 扫描，`OTHER`/`ERROR` 记录原因后结束。
- [x] 修改 `api.c`：只有 PnP 返回 `MISSING` 且设备确实在场时才调用安装接口；不得因为 `CH375OpenDevice()` 失败而无条件安装。
- [x] 修改 `api.c`：增加进程内或 `sr_context` 内的一次性安装尝试保护，确保同一次扫描流程最多触发一次 UAC；取消、超时或失败后不循环重试。
- [x] 修改 `api.c`：安装器返回后重新执行 PnP 探测；只有 `READY` 才继续枚举 CH375 设备，仍为 `MISSING`/`OTHER`/`ERROR` 则结束扫描并记录结果。
- [x] 修改 `api.c`：保持 `SR_CONF_CONN` 过滤语义；指定非 CH375 连接或没有匹配设备时不得触发安装器。
- [x] 检查 `src/libsigrok/src/hardware/wch-ch32h417/ch375_wrapper.c`：确认 DLL 仍从包内固定目录加载，且不把系统安装目录作为隐式驱动来源。
- [x] 检查 `src/libsigrok/src/hardware/wch-ch32h417/ch375_wrapper.h` 和 `protocol.h`：补充必要的错误/状态声明，但不把 Windows 安装逻辑塞入协议层。
- [x] 确认 `src/libsigrok/Makefile.am` 中 `wch-ch32h417` 源文件列表包含新增 Windows 模块，并且条件编译不会破坏其它平台。

## E. 受限提权安装器

- [x] 新增受限 helper 的源码目录，例如 `tools/wch-driver-installer/`，只接受内置 manifest entry ID，不接受任意外部 INF、目录或命令行拼接内容。
- [ ] 在 helper 中重新解析包根目录、读取 `driver-manifest.json`、校验载荷哈希和固定相对路径，再执行安装；校验失败不得调用 `pnputil` 或 WCH 安装器。
- [ ] 在 helper 中优先验证 WCH `SETUP.EXE /S` 的真实静默参数；若不能稳定安装 `CH375WDM.INF`，改用固定路径的 `pnputil.exe /add-driver ... /install`。
- [ ] 为 helper 增加 x64 构建目标、版本信息和最小权限设计；不得把 `sigrok-cli.exe` 本身作为提权后的长期运行进程。
- [x] 在 `src/libsigrok/src/hardware/wch-ch32h417/wch_driver_windows.c` 中使用 `ShellExecuteExW` 的 `runas` verb 启动 helper，只传递固定 entry ID/受保护参数。
- [x] 设置 helper 工作目录为包内 `driver-install/wch-ch375/x64/`，不依赖 PATH 或当前工作目录。
- [ ] 等待 helper 退出并实现明确超时；处理 `ERROR_CANCELLED`、启动失败、超时、非零退出码、签名失败和需要重启/重新插拔的返回状态。
- [ ] 记录安装器路径、entry ID、退出码和二次 PnP 探测结果；不得记录完整用户目录、序列号或其它敏感信息。
- [x] 验证 UAC 只由 Windows `runas` 产生，不增加 `MessageBox`、自定义 GUI、Webview、stdin 确认或 Extension 回调。

## F. 构建与本地验证

- [x] 更新 `src/libsigrok/Makefile.am`、configure 条件和 helper 构建脚本后，执行 Windows x64 完整构建，确认 `wch-ch32h417` 和 helper 均成功链接。
- [x] 执行现有 libsigrok 单元/fixture 测试，确认非 Windows stub、无设备扫描和其它硬件 driver 没有回归。
- [ ] 执行 `sigrok-cli.exe --version`、`sigrok-cli.exe --list-drivers`，确认不会触发 PnP 检查或 UAC。
- [ ] 在普通无硬件环境运行 `sigrok-cli.exe --scan`，确认无 CH375 设备时不弹 UAC、不崩溃、不循环，并输出可诊断日志。
- [ ] 运行 `scripts/package.ps1` 和 `scripts/verify-package.ps1`，保存 ZIP、manifest 和验证日志到 `.work/` 或 release 产物目录。
- [ ] 检查生成 ZIP 的 `package-manifest.json`，确认新增驱动载荷、helper、license、source disclosure 均有哈希记录且没有未 manifest 文件。

## G. 干净 Windows x64 验收矩阵

- [ ] 在没有安装 U3LogicAnalyzer 完整上位机的干净 VM 中插入 `VID_1A86&PID_5537`，确认初始没有 `CH375_A64`，并保存 `pnputil /enum-devices`/`Get-PnpDevice` 证据。
- [ ] 在上述状态执行 `sigrok-cli -d wch-ch32h417 --scan`，确认只出现一次 Windows UAC，且没有 Sigrok 自定义提示。
- [ ] 接受 UAC 后确认 `CH375_A64` 绑定成功、设备重新枚举，且同一次扫描能发现 `CH32H417 Logic Analyzer`；若系统要求重新插拔，记录实际行为并调整验收标准。
- [x] 取消 UAC，确认扫描干净退出、不重复弹窗、不死循环，日志包含取消/失败原因。
- [x] 再次扫描已成功安装的设备，确认不再启动安装器。
- [ ] 测试设备未插入，确认不启动安装器。
- [ ] 测试设备已绑定 `CH375_A64` 但存在 problem code，确认不把异常状态误判为正常或无限安装。
- [ ] 测试设备已绑定其它驱动，确认返回 `OTHER`、不替换现有绑定、不启动安装器。
- [ ] 删除/篡改包内一个驱动文件或 manifest 哈希，确认 helper 拒绝执行并记录校验失败。
- [ ] 测试 CAT 无效、驱动签名失败、权限不足、安装器非零退出、超时和需要重启等结果，确认扫描不会崩溃或重复提权。
- [ ] 在安装成功、取消和失败三种状态下分别执行 `pnputil /enum-drivers` 与 `Get-PnpDevice`，保存服务名、problem code、驱动版本和最终设备状态。

## H. 发布前门槛

- [ ] `third_party/wch-ch375/` 中完整驱动载荷、manifest、来源、哈希和再分发许可齐全。
- [ ] `driver-install/wch-ch375/x64/` 的所有文件均由 package manifest 覆盖，CAT 签名验证通过。
- [ ] Windows PnP 探测、一次性 UAC、安装后重新枚举和失败语义测试全部通过。
- [ ] 已确认不修改 Extension，且普通 `--version`/`--list-drivers` 不会触发安装流程。
- [ ] 干净 Windows x64 VM 通过 G 节全部验收项，测试证据可追溯。
- [ ] 更新 `README.md` 或新增面向维护者的文档，说明 CH375 驱动载荷、许可、安装行为和已知的重新插拔/重启要求。
- [ ] 将本清单中所有已完成项的证据路径补全，并在发布或合并前复核没有遗留未勾选的发布门槛。

## 本轮证据（2026-08-29）

- `dist/probes-sigrok-ch375-helper-dev/probes-sigrok.zip`：已生成，包含
  `driver-install/wch-ch375/x64/`、`driver-manifest.json` 与
  `wch-driver-installer.exe`。
- `tests/package-smoke.ps1`、`tests/runtime-paths.ps1`：针对上述包执行通过；
  测试只调用 `--version`、`--list-supported`、`demo` driver，不会触发 WCH UAC。
- `scripts/verify-package.ps1`：WCH manifest/hash/catalog 签名/PE 架构/包清单
  覆盖检查通过；随后在既有 `tests/run-custom-fixtures.ps1` 的 `c2` decoder
  fixture 因未生成 `decode.txt` 失败。因此完整验证和相关发布项仍未勾选。
- `.work/wch-driver-installer/wch-driver-installer.exe`：使用
  `x86_64-w64-mingw32-gcc` 生成；`objdump -f` 报告 `pei-x86-64`。

## 最新实机证据（2026-08-30）

- 测试包：`dist/probes-sigrok-ch375-test-20260830/probes-sigrok/`，由修复
  `CM_Get_DevNode_Status` 输出参数后的最新 Windows x64 构建生成。
- 初始设备状态：插入
  `USB\\VID_1A86&PID_5537\\0123456789`，状态为
  `CM_PROB_FAILED_INSTALL`，无 `CH375_A64` 服务。
- 自动安装触发：运行
  `sigrok-cli.exe -l 5 -d wch-ch32h417 --scan`，日志记录
  `WCH CH375 device is present without a function driver`，随后只触发一次
  `runas`；本次 UAC 被取消，扫描以退出码 0 干净结束且未循环。日志：
  `.work/evidence/wch-ch375/20260830/scan-auto-uac-cancel.log`。
- 安装结果：用同一包的 `wch-driver-installer.exe wch-ch375-x64` 通过
  `runas` 执行成功（退出码 0）。随后设备为 `Status=OK`、`Service=CH375_A64`、
  `Problem=CM_PROB_NONE`，服务处于 RUNNING，Driver Store 为 `oem202.inf`。
- 安装后扫描：最新包日志显示 `WCH driver is bound to CH375_A64`、
  `CH375DLL loaded successfully`、`Opened device at index 0`，发现
  `WCH CH32H417 Logic Analyzer`（16 通道），退出码 0。日志：
  `.work/evidence/wch-ch375/20260830/scan-after-install.log`。
- 实际采集：使用
  `-d wch-ch32h417:conn=ch375/0 --samples 1000 --output-mode stdout`，收到
  `SR_DF_HEADER` 和 `SR_DF_LOGIC (2000 bytes, unitsize = 2)`，输出 16 通道
  数据并正常结束，退出码 0。日志：
  `.work/evidence/wch-ch375/20260830/capture-1000.log`。
- 限制：本轮验证没有在“自动扫描弹出的 UAC 窗口中直接点击接受”后继续同一次
  扫描；安装接受和后续扫描/采集分两次执行。因此 G 节对应的干净 VM、同次扫描
  安装后枚举和完整矩阵仍未勾选。

## 交付包（2026-08-30）

- ZIP：`dist/probes-sigrok-ch375-delivery-20260830/probes-sigrok.zip`
- SHA-256：`767af79233e16511e3e0fa2154b8787febdb8281a8c0b0d064ceba8da620a725`
- 包内已确认存在 `sigrok-cli.exe`、`wch-driver-installer.exe`、完整
  `driver-install/wch-ch375/x64/` 载荷和 `driver-manifest.json`。
