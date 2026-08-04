# Probes Sigrok 项目：最小框架规划

## 1. 目标与边界

此目录是一个本地、独立、面向 Windows 的 sigrok 发行项目。它用于在
`libsigrok` 中新增或调整 driver（必要时也可调整协议 decoder 或 firmware），
并产出尽可能小、可再分发的运行目录。

第一版发行目标如下：

```text
probes-sigrok/
|-- sigrok-cli.exe
|-- *.dll
|-- decoders/
|-- firmware/
`-- source-disclosure/
```

`source-disclosure/` 是随发行包提供的源码披露和可复现性材料。其名称刻意不同于
项目开发源码目录 `src/`，避免二者混淆。它包含该发行版本对应的完整源码、上游
来源清单、许可证和第三方声明；它不是构建工作目录。

有一个必须正视的运行时约束：`libsigrokdecode` 需要兼容的 Python 运行时。只有
上述五类条目的包，只有在用户自行提供 Python 时才可运行。若要真正独立分发，
允许额外加入一个 `python/` 目录（嵌入式 Python 标准库），并在顶层加入所需的
`python3*.dll`。不能隐藏这项依赖。

项目应保持为普通本地项目。未有明确需求前，不引入 Node workspace、monorepo
包、数据库、服务或 CI 流水线。

## 2. 源码管理策略

本项目使用一个独立 Git 仓库，直接管理完整的项目源码。官方 sigrok 的三个组件
作为可追溯的源码基线一次性导入，不保留为嵌套 Git 仓库，也不以官方提交树、分支
或 patch 队列作为日常开发方式。

```text
probes-sigrok-project/.git/          # 唯一的项目 Git 历史
probes-sigrok-project/src/libsigrok/
probes-sigrok-project/src/libsigrokdecode/
probes-sigrok-project/src/sigrok-cli/
```

这样，driver、decoder、CLI、构建脚本和打包配置可以在同一个提交中修改和回滚，
适合日常定制开发。代价是上游更新不再通过直接 merge/rebase 完成；由于预期只在
官方重大更新时手动同步一次，这个取舍是合理的。

初次导入时必须从指定官方提交的干净源码导出，不得直接纳入当前 `.local/sigrok/`
工作树。当前参考副本中的 `libsigrok/Makefile.am` 有未提交修改，直接导入会使
基线不可追溯。

在项目根创建 `upstream-manifest.json`，至少记录每个组件的：

- 官方仓库 URL。
- 导入的 tag 或不可变 commit。
- 导入日期和源码归档 SHA-256。
- 适用许可证与版权声明位置。

初始提交建议命名为：

```text
vendor(sigrok): import upstream baseline
```

后续功能采用普通项目提交，例如：

```text
feat(driver): add probes-foo driver
fix(driver): handle probes-foo capture timeout
build: reduce runtime feature set
```

## 3. 最小项目目录

开始时只保留以下内容。标记为“生成”的目录是本地构建状态，不能成为权威源码。

```text
probes-sigrok-project/
|-- README.md                       # 本规划和简要操作说明
|-- .git/                           # 唯一的项目 Git 仓库
|-- src/                            # 已导入、直接维护的 sigrok 源码
|   |-- libsigrok/
|   |-- libsigrokdecode/
|   `-- sigrok-cli/
|-- config/
|   `-- release.psd1                # 版本、目标和选定功能
|-- scripts/
|   |-- requirements.ps1            # 只检查构建前置条件
|   |-- build.ps1
|   |-- package.ps1
|   `-- verify-package.ps1
|-- licenses/
|   `-- THIRD_PARTY_NOTICES.md
|-- upstream-manifest.json          # 初始上游基线的来源和校验信息
|-- .gitignore
|-- .work/                          # 生成：构建树、安装暂存区
|-- dist/                           # 生成：发行目录和 zip
`-- logs/                           # 生成：命令日志和包索引
```

父 Probes 仓库会忽略 `.local/`，因此这个嵌套仓库天然独立。项目自身的历史、标签和
发布元数据都保留在 `probes-sigrok-project/` 内。

建议 `.gitignore` 至少包含：

```gitignore
.work/
dist/
logs/
*.log
```

## 4. 日常开发方式

`src/` 是唯一的开发源码位置。所有源码修改都直接在其中完成，并由项目根的 Git
提交管理。

新增硬件 driver 时，主要修改通常位于：

```text
src/libsigrok/src/hardware/<driver-name>/
```

以及该 driver 所需的注册、构建列表和依赖配置。driver 的实现、测试样本、构建
调整和运行时打包调整应在同一个或一组语义清晰的 Git 提交中保存。

不要在 `.work/` 中直接开发。`.work/` 仅容纳由 `build.ps1` 生成的配置、编译和
安装暂存结果，随时可以删除并重建。

若未来需要向官方社区提交某项改动，可以从本项目的相关提交导出 patch；patch 是
对外协作产物或临时工具，而不是本项目的主开发存储方式。

## 5. 上游重大更新的手动同步

当需要同步官方重大更新时，按以下过程执行：

1. 在项目外的临时目录获取三个组件的干净官方源码，记录目标 tag/commit 和哈希。
2. 逐组件与 `src/libsigrok/`、`src/libsigrokdecode/`、`src/sigrok-cli/` 比较，
   将官方变化手动合入，并解决与本项目定制的冲突。
3. 更新 `upstream-manifest.json` 中的版本、日期和哈希。
4. 先完成未打包构建与核心 driver 测试，再更新发行包并做独立运行验证。
5. 以单独提交记录升级，例如 `vendor(sigrok): sync upstream 0.x`，不要把上游
   同步与功能修改混在同一个提交中。

每次发布都打项目 tag，例如 `v0.1.0`。这样即使没有保留官方提交树，也能准确
知道每一个发行版本采用的上游基线和本地改动。

## 6. 构建方式

以 PowerShell 作为唯一顶层接口。脚本可以调用上游 Autotools 构建系统和选定的
Windows 工具链，但调用者只需使用以下命令：

```powershell
./scripts/requirements.ps1
./scripts/build.ps1 -Configuration Release
./scripts/package.ps1 -Version 0.1.0
./scripts/verify-package.ps1 -PackagePath ./dist/probes-sigrok-0.1.0
```

`config/release.psd1` 保存少量策略值，避免把它们散落或硬编码到脚本中：

```powershell
@{
    Version = '0.1.0'
    Target = 'x86_64-w64-mingw32'
    IncludeDecoders = $true
    IncludeFirmware = $true
    PythonMode = 'embedded' # 只有明确声明非便携时才使用 'external'
    EnabledDrivers = @('probes-foo')
}
```

`build.ps1` 只读取 `src/` 中受 Git 管理的源码，并通过统一安装前缀将全部
`make install` 结果暂存至 `.work/stage/`。不要直接从任意编译器、MSYS2 或
Python 安装目录打包：那会混入无关库，且无法产出最小、可解释的发行包。

`requirements.ps1` 只负责检查工具链前置条件，不制作复杂安装器。它应验证选定
编译器、Autotools、pkg-config、Python 头文件和运行时、GLib、libusb，以及启用
driver 所需的设备库依赖。

## 7. 发行包组装

`package.ps1` 在 `dist/probes-sigrok-<version>/probes-sigrok/` 生成全新目录，
然后在旁边创建 zip。复制规则必须明确：

```text
probes-sigrok/
|-- sigrok-cli.exe                  # 命令行入口
|-- *.dll                           # 精确的传递运行时 DLL 闭包
|-- decoders/                       # 选定的 libsigrokdecode Python 模块
|-- firmware/                       # 仅启用 driver 所需的 firmware
|-- python/                         # 仅当 PythonMode = embedded 时存在
|-- source-disclosure/              # 完整发布源码、清单和许可证材料
|-- LICENSES/                       # 二进制分发所需许可证全文
|-- THIRD_PARTY_NOTICES.md
`-- VERSION.txt                     # 版本、目标、上游清单摘要
```

`source-disclosure/` 至少包含：

```text
source-disclosure/
|-- libsigrok/
|-- libsigrokdecode/
|-- sigrok-cli/
|-- upstream-manifest.json
|-- LICENSES/
`-- THIRD_PARTY_NOTICES.md
```

构建时应从当前发布 tag 的 `src/` 复制或导出这三份源码，排除 `.git/`、`.work/`、
`dist/`、目标文件、依赖缓存和其他构建产物。披露内容必须与本次二进制包实际使用的
源码一致。

预期的五类路径仍是主要结构。`python/`、`LICENSES/`、声明和版本文件是为便携性
和可追溯性增加的少量必要内容。只有当发行版明确声明外部 Python 前置条件时，
才可以省略 `python/`。

运行时 DLL 列表由暂存的可执行文件和库解析产生，再与白名单校验。它只能包含
启用功能所需的 DLL。不得把编译器 SDK、导入库（`.a`/`.lib`）、静态库、头文件、
目标文件、构建缓存或整个任意 MSYS2 目录复制进发行包。

最小化应首先发生在配置阶段：构建时禁用未使用的 driver、input、output 和可选
传输层。先进行宽泛构建、再删除 DLL，不是有效的最小化方法。

## 8. 验证门禁

`verify-package.ps1` 只能针对组装后的目录执行；它使用干净环境，并让包目录
优先出现在 `PATH` 中。初始门禁如下：

1. 校验必需条目存在，且没有开发产物。
2. 解析 `sigrok-cli.exe` 与已发布库的 DLL 依赖；若有依赖缺失，或依赖不在
   包/系统 DLL 白名单中，则失败。
3. 执行 `sigrok-cli.exe --version` 和 `sigrok-cli.exe --list-drivers`。
4. 断言自定义 driver 已列出，再针对 fixture 或已连接测试设备执行最小的、
   无破坏性的扫描/打开命令。
5. 加载一个随包发布的 decoder，并用已知正确的采样数据运行它。
6. 生成机器可读的 `package-manifest.json`，包括包文件哈希、项目 Git 提交、
   上游清单摘要、工具链版本和测试结果。

在另一台没有构建工具链、MSYS2、全局 sigrok 或全局 Python 依赖的 Windows 机器
或干净 Windows VM 上，使同一个组装目录通过测试之前，不应宣称该包可便携运行。

## 9. 渐进实施顺序

第一版实现应保持范围小且结果可观测：

1. 初始化项目 Git 仓库，从三个干净官方源码基线导入 `src/`，写入
   `upstream-manifest.json`，并提交导入结果。
2. 将未修改的 `libsigrok`、`libsigrokdecode` 和 `sigrok-cli` 构建到
   `.work/stage/`，验证 `sigrok-cli.exe --version`。
3. 使用一个 decoder 和必要的运行时闭包打包未修改运行时，并在干净环境验证。
4. 直接在 `src/libsigrok/` 中加入恰好一个自定义 driver，并加入基于 fixture 的
   冒烟测试。
5. 仅在此 driver 需要时加入 firmware，并在 `upstream-manifest.json` 或相邻清单
   中记录其来源、版本、许可证和校验和。
6. 收紧启用功能以及 DLL/decoder/firmware 白名单，直到包中只含已支持功能。

该顺序先建立构建、打包和运行时边界，再引入设备特有的 driver 不确定性。

## 10. 初始框架的非目标

- 不加入 GUI（PulseView）、安装器、更新器、服务或设备数据库。
- 不加入自动上游更新机器人，也不引入超出上游清单的依赖管理器。
- 不试图支持全部上游 driver 或 decoder。
- 正常项目构建不修改 `../sigrok/`。

## 11. 定制开发起始规则

每次新增或修改自定义 driver/decoder，先在 `src/` 完成代码，再更新
`config/custom-components.json` 和 `tests/fixtures/`。driver 条目必须提供
一个无破坏性的 CLI 命令及稳定输出断言；decoder 条目必须提供可再现的采样
fixture、CLI 参数和稳定输出断言。`verify-package.ps1` 会在打包运行时执行
所有登记的 fixture。

`build.ps1` 会在 `libsigrok` 和 `libsigrokdecode` 中执行有效的 `make check`。
若测试摘要显示 `TOTAL: 0`，构建将失败。发布前在构建机运行完整依赖扫描，
在没有 MSYS2 的干净机器上运行同一 `verify-package.ps1`，后者使用包内封存的
PE 依赖清单，不要求目标机安装构建工具链。
