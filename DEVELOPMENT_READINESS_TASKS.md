# 完整上游 Sigrok 基线可开发化执行清单

## 1. 本阶段目标

本阶段只完成“自定义 driver 开始之前”的开发基线，不编写任何自定义 driver 或设备
协议代码。完成状态是：当前项目可修改、可重新构建、可生成独立 Windows 包，且包
保留所选 Windows 平台上游 sigrok 的完整可用功能。

本阶段产物必须满足：

1. 构建 `libsigrok` 时不主动禁用上游 driver、input、output、transform 或 decoder。
2. `decoders/` 包含 `libsigrokdecode` 上游安装逻辑产生的完整 decoder 集和完整
   `common/` 共享模块。
3. `firmware/` 包含固定版本 `sigrok-firmware` 的完整上游 firmware 集。
4. `demo` 仅作为不连接硬件的烟测对象，不是唯一构建或发行 driver。
5. 包内资产可直接被程序发现，且不删除上游已有的环境变量扩展能力。

“最小”只指项目机制简单：一个 Git 仓库、一种 Windows 工具链、一种 Python ABI、
一套构建脚本和一个发行包。它不指裁剪 sigrok 原有的协议 decoder、可用 driver 或
firmware。

## 2. 单一路径决策

本项目只使用下列组合，不保留多工具链或多 Python 兼容分支：

```text
开发主机: Windows x86_64
构建环境: C:\msys64 的 MSYS2 MINGW64
目标: x86_64-w64-mingw32
编译器: MSYS2 mingw-w64-x86_64-gcc
Python ABI: MSYS2 mingw-w64-x86_64-python 3.14
产品入口: sigrok-cli.exe
```

不支持 W64DevKit、MSYS2 UCRT64/CLANG64/MINGW32、系统 Python 或外部 Python。
这是构建与 ABI 的单一路径约束，不是对 sigrok 运行功能的裁剪。

## 3. 对上一版计划的修正

| 上一版问题 | 为什么错误 | 本版修正 |
|---|---|---|
| 仅打包 `uart + common/srdhelper` | 人为拆分 decoder 依赖树，增加维护工作并丢失官方 decoder | 删除 decoder 白名单；整体安装、整体复制上游 decoders |
| `--disable-all-drivers --enable-demo` | 将 `demo` 烟测错误地变成唯一 driver | 使用上游默认 `--enable-all-drivers=check`；`demo` 仅参与测试 |
| `firmware/` 保持空目录 | 已启用 driver 可能依赖官方 firmware | 导入完整 `sigrok-firmware` 基线并整体发布 |
| 期望 driver 文件只含 `demo` | 会把其他已构建 driver 误判为不应存在 | 从构建摘要生成已构建 driver 清单，包验证与其精确比较 |
| 不检测可用 driver 依赖 | 会因本机漏装依赖而静默失去本可支持的上游 driver | 安装并验证 MINGW64 仓库可提供的相关依赖；平台本身无实现的功能按上游 `check` 行为记录 |
| CLI 强制覆盖 decoder/firmware 目录变量 | 破坏上游的用户扩展搜索路径 | 在库中加入包目录作为默认搜索路径，保留既有环境变量优先级和扩展语义 |

`--disable-bindings` 仍保留，但不属于上述问题：本发行包的契约是 `sigrok-cli.exe`
及其运行时，而不是 C++、Python、Ruby、Java SDK 发行包。它不会减少 CLI 的
driver、decoder、input/output、transform 或 firmware 功能。若将来需要分发 SDK，
应另行扩展发行包契约，而不是在本阶段混入 bindings。

## 4. 最终包基线

```text
probes-sigrok/
|-- sigrok-cli.exe
|-- *.dll
|-- decoders/                       # 全部上游 decoder 和 common 模块
|-- firmware/                       # 全部固定版本上游 sigrok-firmware 内容
|-- python/
|   `-- lib/python3.14/
|-- source-disclosure/
|   |-- libsigrok/
|   |-- libsigrokdecode/
|   |-- sigrok-cli/
|   `-- sigrok-firmware/
|-- LICENSES/
|-- THIRD_PARTY_NOTICES.md
|-- VERSION.txt
`-- package-manifest.json
```

包内的 driver 集取决于上游 `configure` 在 MINGW64 上发现的依赖。脚本不得以
`--disable-*` 主动排除 driver；因 Windows 平台或 MINGW64 仓库没有对应依赖而
自然无法构建的 driver，必须在构建清单中明确记录原因。

## 5. 执行任务

按 T01-T08 执行。期间不提交；只有 T08 通过后创建一个最终提交。

### T01：固定 MSYS2 MINGW64 工具链和全部可用驱动依赖

**文件变更**

- 新增 `config/toolchain.psd1`
- 新增 `scripts/invoke-mingw64.ps1`
- 修改 `scripts/requirements.ps1`
- 修改 `README.md`

**具体改造**

1. `config/toolchain.psd1` 固定：

   ```powershell
   @{
       MsysRoot = 'C:\msys64'
       Environment = 'MINGW64'
       Target = 'x86_64-w64-mingw32'
       PythonExecutable = 'python3.14'
       PythonPkgConfig = 'python-3.14-embed'
   }
   ```

2. `scripts/invoke-mingw64.ps1` 是唯一外部命令入口。它只经
   `<MsysRoot>\usr\bin\bash.exe` 在 `MSYSTEM=MINGW64` 环境执行命令，不读取
   调用它的 PowerShell PATH。
3. `requirements.ps1` 验证 Autotools、GCC、pkgconf、GLib、GIO、zlib、libzip、
   Python 3.14，以及 MINGW64 可提供的 driver 依赖：libusb、libserialport、
   libftdi、hidapi、nettle。
4. 对 MINGW64 无实现的上游依赖，例如 BlueZ、librevisa、libgpib、libieee1284，
   输出“平台不可用”记录而不尝试引入另一套工具链或第三方兼容层。
5. `README.md` 记录唯一工具链和“全部 MINGW64 可支持的上游功能”原则。

**验收**

```powershell
./scripts/requirements.ps1
```

命令成功，并输出固定目标、Python 3.14 与所有已安装/平台不可用的依赖状态。

### T02：补入完整上游 firmware 基线

**文件变更**

- 新增 `src/sigrok-firmware/`
- 修改 `upstream-manifest.json`
- 修改 `licenses/THIRD_PARTY_NOTICES.md`
- 修改 `README.md`

**具体改造**

1. 从官方 `https://github.com/sigrokproject/sigrok-firmware.git` 的固定 commit 导入
   干净源码到 `src/sigrok-firmware/`；不保留嵌套 `.git/`。
2. 在 `upstream-manifest.json` 增加第四个组件，记录 URL、commit、tree、归档
   SHA-256 和许可证文件位置。
3. 将 firmware 的版权与许可证材料加入 `THIRD_PARTY_NOTICES.md`。
4. 不挑选、不重命名 firmware 文件；发布时整体复制此固定基线内容。

**验收**

导入目录没有嵌套 Git 元数据；清单中的归档哈希可复算；许可证路径均存在。

### T03：使 libsigrokdecode 固定使用 Python 3.14

**文件变更**

- 修改 `src/libsigrokdecode/configure.ac`
- 修改 `README.md`

**具体改造**

1. 在 `SR_PKG_CHECK([python3], ...)` 中只检查 `python-3.14-embed`。
2. 在 `AC_CHECK_PROGS([PYTHON3], ...)` 中只检查 `python3.14`。
3. 不保留 Python 3.12、3.13 或更早 ABI 的检测与回退。

**验收**

`libsigrokdecode` 的隔离 `configure` 摘要显示 Python 3.14，且没有其他候选 ABI。

### T04：实现完整上游功能的隔离构建

**文件变更**

- 修改 `config/release.psd1`
- 新增 `scripts/lib/release-config.ps1`
- 修改 `scripts/build.ps1`
- 新增 `scripts/build-mingw64.sh`
- 修改 `README.md`

**具体改造**

1. `config/release.psd1` 只保留产品与烟测事实，不定义自定义 driver 或 decoder
   白名单：

   ```powershell
   @{
       PackageName = 'probes-sigrok'
       Version = '0.1.0-dev'
       Target = 'x86_64-w64-mingw32'
       SmokeDriver = 'demo'
       PythonMode = 'embedded'
   }
   ```

2. `build.ps1` 每次将当前 `src/` 复制到 `.work/autotools-src/`，并只在该副本中
   运行 `autoreconf`。受 Git 管理的 `src/` 不得出现生成的 `configure`、
   `aclocal.m4` 或 libtool 文件。
3. `build-mingw64.sh` 固定构建顺序：

   ```text
   libsigrok -> libsigrokdecode -> sigrok-cli
   ```

4. `libsigrok` 使用上游 `--enable-all-drivers=check`，不传递
   `--disable-all-drivers` 或任意单 driver `--enable-*` 参数；保留所有被 MINGW64
   依赖解析支持的上游 driver。
5. `libsigrokdecode` 执行原有 `make install`，使全部 decoder 和 `common/` 模块
   进入 `.work/stage/`；不得增加 decoder 选择逻辑。
6. 语言 bindings 使用 `--disable-bindings`，原因仅为 CLI 发行包不含 SDK；其他
   CLI 运行功能不得关闭。
7. 三个组件使用同一个安装前缀和 `DESTDIR=.work/stage`。构建后续组件时，
   `PKG_CONFIG_PATH` 只指向 `.work/stage` 中的 pkg-config 文件，禁止链接全局
   sigrok 库。
8. 写入 `.work/build-manifest.json`，包括 `configure` 摘要、每个已构建 driver、
   因平台不可用而未构建的 driver、完整 decoder 目录清单、工具链版本和命令。

**验收**

```powershell
./scripts/build.ps1 -Configuration Release
```

`src/` 保持干净；`.work/stage/` 含 CLI、两个库、全部安装 decoder；构建清单中没有
任何由脚本主动禁用的上游 driver。

### T05：增加包目录作为默认资源路径，保留上游扩展路径

**文件变更**

- 修改 `src/libsigrok/src/resource.c`
- 修改 `src/libsigrokdecode/srd.c`
- 新增 `src/sigrok-cli/runtime_paths.c`
- 新增 `src/sigrok-cli/runtime_paths.h`
- 修改 `src/sigrok-cli/main.c`
- 修改 `src/sigrok-cli/Makefile.am`
- 新增 `tests/runtime-paths.ps1`

**具体改造**

1. `runtime_paths.c` 只解决包内 Python 3.14：使用 `GetModuleFileNameW(NULL, ...)`
   计算 `<package-root>/python`，在 Python 初始化前设置 `PYTHONHOME`。这是固定 ABI
   的产品要求，不使用系统 Python。
2. `resource.c` 在既有 `SIGROK_FIRMWARE_DIR`、`SIGROK_FIRMWARE_PATH`、用户目录、
   编译期目录和系统目录搜索链中，加入 `<package-root>/firmware` 作为额外默认路径；
   不删除或覆盖任何原有环境变量路径。
3. `srd.c` 在既有 XDG、调用者参数、`SIGROKDECODE_DIR`、`SIGROKDECODE_PATH` 搜索链中，
   加入 `<package-root>/decoders` 和 Windows `%LOCALAPPDATA%\\Probes\\sigrok\\decoders`
   作为额外默认路径；不删除、覆盖或重排原有用户提供路径的优先级。
4. `main.c` 只在初始化嵌入式 Python 前调用 runtime path 初始化。它不得设置或覆盖
   `SIGROKDECODE_*`、`SIGROK_FIRMWARE_*`。
5. `tests/runtime-paths.ps1` 在清除相关环境变量后验证包内 decoder 和 firmware
   默认路径，以及 Windows 用户 decoder 根目录；再设置测试扩展路径，验证原有环境
   变量路径仍可生效。

**验收**

无环境变量时包可找到自身完整 decoders、firmware 和用户目录中的 decoder；设置上游
支持的环境变量后，额外路径仍被保留。用户目录不属于版本化包内容。

### T06：实现完整上游发行包组装

**文件变更**

- 修改 `scripts/package.ps1`
- 新增 `scripts/lib/dll-closure.ps1`
- 新增 `scripts/lib/python-runtime.ps1`
- 新增 `scripts/lib/source-disclosure.ps1`
- 修改 `README.md`

**具体改造**

1. `package.ps1` 只从 `.work/stage/`、`src/sigrok-firmware/` 和当前 Git `HEAD`
   获取输入，在 `dist/probes-sigrok-<Version>/probes-sigrok/` 创建新目录。
2. 整体复制 `.work/stage/` 安装出的 decoder 目录，包含全部 decoder 和 `common/`；
   不存在 `decoder-allowlist.json`、裁剪脚本或手工依赖表。
3. 整体复制 `src/sigrok-firmware/` 的有效 firmware 内容到顶层 `firmware/`，排除
   其 `.git/` 和发布无关元数据，但不按 driver 选择文件。
4. `dll-closure.ps1` 使用 MINGW64 `objdump -p` 递归解析 CLI 和已复制 DLL，仅从
   `.work/stage/`、`C:\msys64\mingw64\bin/` 与 Windows API DLL 白名单解析依赖。
   所有非系统依赖都必须复制到包顶层。
5. `python-runtime.ps1` 从同一 MINGW64 前缀复制 `libpython3.14.dll` 到包顶层，
   并复制完整 `lib/python3.14/` 到 `<package-root>/python/lib/python3.14/`。
6. `source-disclosure.ps1` 从 Git `HEAD` 导出四份完整源码：`libsigrok`、
   `libsigrokdecode`、`sigrok-cli`、`sigrok-firmware`，以及上游清单、许可证和
   第三方声明。不得带入 `.git/`、`.work/`、`dist/` 或构建产物。
7. 生成 `VERSION.txt` 与 `package-manifest.json`，记录项目 commit、四个上游基线、
   所有 decoder/firmware 文件哈希、DLL 闭包和工具链版本。

**验收**

```powershell
./scripts/package.ps1
```

日常构建默认使用版本名 `current`；只有发布或归档等明确场景才显式传入其它版本。

包内完整保留上游 decoder 和 firmware；不存在编译器、SDK、`.a`、`.lib`、`.h`、
`.o`、pkg-config 文件或构建缓存。

### T07：实现不裁剪功能的发行包验证

**文件变更**

- 修改 `scripts/verify-package.ps1`
- 新增 `tests/package-smoke.ps1`
- 新增 `tests/expected-build-manifest.ps1`

**具体改造**

1. `verify-package.ps1` 只运行已组装包，禁止访问 `.work/stage/`、MSYS2 bin、
   系统 Python 或全局 sigrok。
2. 它对比 `package-manifest.json` 与包内实际文件，验证完整 decoder 和 firmware
   清单、递归 DLL 闭包及 source-disclosure 哈希。
3. `expected-build-manifest.ps1` 从 `.work/build-manifest.json` 生成期望的 driver
   列表；不再存在只包含 `demo` 的静态 `expected-drivers.txt`。
4. `package-smoke.ps1` 从包外临时工作目录执行：

   ```text
   sigrok-cli.exe --version
   sigrok-cli.exe --list-supported
   sigrok-cli.exe -d demo --scan
   sigrok-cli.exe -d demo --show
   ```

   `demo` 只证明无设备扫描可运行。`--list-supported` 中的 driver 和 decoder 集
   必须与构建/包清单一致，而不是与人为裁剪列表一致。

**验收**

所有清单比对和无设备命令通过；验证脚本没有要求连接设备，也没有只允许 `demo`
存在。

### T08：干净环境最终验证与单次提交

**文件变更**

- 修改 `README.md`
- 修改 `DEVELOPMENT_READINESS_TASKS.md`，记录实际验证环境与结果
- 仅当发现确定性问题时修改对应构建、打包或验证脚本

**具体改造**

1. 在没有 MSYS2、W64DevKit、Python 和全局 sigrok 的 Windows x86_64 VM 或机器中
   解压发行包。
2. 不设置 `PYTHONHOME`、`SIGROKDECODE_*`、`SIGROK_FIRMWARE_*` 环境变量，执行
   T07 验证。
3. 记录 VM 版本、包 SHA-256、通过时间和结果。
4. 仅当 T01-T07 本机验证与干净环境验证均通过时，创建一个提交：

   ```text
   build: establish the complete portable sigrok baseline
   ```

**验收**

解压包保留其完整上游可用功能并独立通过验证。此时才进入下一阶段：在该基线上
新增或修改自定义 driver。

## 6. 本阶段禁止项

- 不新增 `src/libsigrok/src/hardware/<自定义-driver>/`。
- 不修改 `src/libsigrok/configure.ac` 增加自定义 `SR_DRIVER(...)`。
- 不引入自定义 VID/PID、设备协议、设备固件或真实硬件 fixture。
- 不为压缩体积选择部分 decoder、部分官方 firmware 或部分已可构建 driver。
- 不增加第二套工具链、第二种 Python ABI 或外部运行包装器。
