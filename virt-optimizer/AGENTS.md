# AGENTS.md

## 适用范围

本文件中的规则适用于 `virt-optimizer/` 及其所有子目录。除非必须修改仓库级构建或代码风格文件，否则应将改动限制在本组件内。当文档与实现不一致时，以构建脚本、CMake 文件和源代码为准。

## 项目概述

UBS Optimizer（UB Service Optimizer）是一款面向昇腾虚拟机推理场景的离线性能分析与调优工具。它使用 eBPF 采集虚拟机内核指标，通过 VSOCK 将指标发送到物理机，与物理机侧的 QEMU/vCPU 观测数据合并，并通过交互式调优器提供优化建议。

- 目标操作系统：openEuler 24.03 LTS SP4
- 支持的架构：`x86_64`/`AMD64` 和 `aarch64`/`arm64`
- 编程语言：eBPF 程序使用 C11，用户态代码使用 C++17
- 构建系统：CMake，由 Bash 脚本统一驱动
- 源文件许可证：木兰宽松许可证第 2 版（Mulan PSL v2）；保留现有版权和许可证声明
- 使用定位：开发阶段的离线性能调优，不用于生产环境持续运行

受支持的编译器及软件包版本记录在 `README_EN.md` 和 `docs/build_install/构建指导.md` 中。主要构建依赖包括 CMake、GCC/G++、Clang、bpftool、libbpf/libelf/zlib 开发包、RapidJSON、tar 和 rpm-build。

## 构建命令

在受支持的 openEuler 主机上，从 `virt-optimizer/` 目录执行构建命令：

```shell
# Release 构建（默认）并生成 RPM 包
bash build.sh

# Debug 构建（包含单元测试目标）并生成 RPM 包
bash build.sh -t debug

# 清理并重新构建 Release 目录
bash build.sh -c

# 清理并重新构建 Debug 目录
bash build.sh -t debug -c
```

构建说明：

- Release 构建产物和 `compile_commands.json` 位于 `build/release/`；Debug 对应文件位于 `build/debug/`。
- 每次成功执行 `build.sh` 后都会调用 `build_rpm.sh`。RPM 包会被复制到 `output/release/` 或 `output/debug/`。
- 当 `ebpf/src/client/bpfs/vmlinux.h` 和 `/usr/include/vmlinux.h` 均不存在时，`build.sh` 可能使用 `sudo bpftool` 生成前者。
- 不要虚构脚本未支持的参数或子命令，例如 `-D`、`-T`、`package` 或 `ut`。
- 不要编辑或提交自动生成的 BPF skeleton、`vmlinux.h`、构建目录、RPM 临时目录、覆盖率报告或打包产物。

## 测试命令

完整单元测试的正式入口为：

```shell
bash ebpf/run_ut.sh
```

`run_ut.sh` 会执行 Debug 构建并运行以下 GoogleTest 程序：

```shell
./build/debug/tests/client/test-client
./build/debug/tests/common/test-common
./build/debug/tests/server/test-server
./build/debug/tests/optimizer/test-optimizer
```

已有 Debug 构建后，可以直接运行指定测试，例如：

```shell
./build/debug/tests/optimizer/test-optimizer --gtest_filter='CPUBoundAnalyzerTest.*'
./build/debug/tests/client/test-client --gtest_filter='CollectorManagerTest.LaunchReceiverTest'
```

测试约束：

- 测试仅适用于 Linux/openEuler，需要可执行 `sudo` 的权限以及 eBPF、VSOCK 和 RPM 构建依赖。
- 首次配置 Debug 构建时，CMake 会从 GitCode 下载 GoogleTest 和 mockcpp；如果本地没有缓存，则需要网络访问权限。
- `run_ut.sh` 会在 `/var/ubs-opt/` 和 `/usr/local/sbin/ubs-optimizer/` 下创建文件，结束时会递归删除这两个目录。若其中存在有价值的数据，绝对不要运行该脚本。在共享主机或非一次性环境中运行前，必须检查脚本并获得用户明确批准。
- 如果同时安装了 `lcov` 和 `genhtml`，脚本会将覆盖率报告写入 `build/debug/tests/gcover_report/`；否则只跳过覆盖率生成，不影响单元测试执行。
- 项目没有注册 CTest 测试。应直接运行上述四个测试程序或 `run_ut.sh`，不要假设可以使用 `ctest`。

## 代码风格

- 遵循仓库根目录的 `.clang-format` 和 `.clang-tidy` 配置。
- 使用 4 个空格缩进，不使用 Tab，单行不超过 120 个字符。函数定义的左大括号另起一行，控制语句的左大括号与语句保持同一行。
- C 代码必须兼容 C11，C++ 代码必须兼容 C++17。
- 保持现有 include 分组，并按照仓库配置交由 clang-format 排序。
- 新增代码注释应简洁，默认使用英文；中文文档中的说明继续使用中文。
- 只格式化本次修改的 C/C++ 文件，不要重新格式化无关模块。
- 源码的 clang-tidy 检查使用 `build/release/compile_commands.json`，测试代码检查使用 `build/debug/compile_commands.json`。

## 项目架构

```text
virt-optimizer/
├── build.sh                    # 配置和构建入口；成功后总会触发 RPM 打包
├── build_rpm.sh                # 暂存程序及配置并调用 rpmbuild
├── ubs_optimizer.spec          # RPM 元数据、安装路径及内核/VSOCK 前置检查
├── docs/                       # 构建、部署、CLI、配置、测试、安全和调优文档
└── ebpf/
    ├── CMakeLists.txt          # C11/C++17 工程及 Release/Debug 构建选择
    ├── run_ut.sh               # 需要高权限的单元测试及可选覆盖率流程
    ├── src/
    │   ├── client/             # 虚拟机侧 `ubs-opt` 采集守护进程和 CLI
    │   │   ├── bpfs/           # 采集 IPI、调度和 NUMA 事件的 eBPF C 程序
    │   │   └── collector/      # 指标采集、聚合、本地降级存储和 VSOCK 客户端
    │   ├── server/             # 物理机侧 `ubs-opt-guard` VSOCK 接收服务和主机采集器
    │   │   ├── collector/      # QEMU 迁移及 vCPU 被抢占情况观测
    │   │   └── control/        # 磁盘监控及虚拟机采集器控制
    │   ├── optimizer/          # `ubs-opt-tuner` 分析器、调优器、文件读取和终端界面
    │   │   ├── analyzer/       # 面向 CPU、I/O 和 IRQ 的优化建议筛选
    │   │   ├── tuner/          # 各项优化的检查、建议和应用逻辑
    │   │   └── util/           # JSON Lines 输入及交互式显示和选择工具
    │   ├── common/             # 公共数据模型、命令解析/执行、日志和工具函数
    │   └── default_config.json # RPM 安装的默认配置
    └── tests/                  # 与 client/common/server/optimizer 对应的 GoogleTest 测试
```

运行时数据流：

1. 在虚拟机中，`ubs-opt start_ebpf` 加载 JSON 配置，并启动已启用的 IPI、调度和 NUMA 采集器。
2. 每到一个采样周期，虚拟机通过 VSOCK 将 `DataTable` 发送到物理机 CID 2。如果发送失败，则在本地追加写入 `/var/ubs-opt/data/data.json`。
3. 在物理机上，`ubs-opt-guard start` 接收虚拟机数据，补充 QEMU 迁移和物理机抢占虚拟机 vCPU 的观测结果，并以 JSON Lines 格式写入相同的数据路径。
4. `ubs-opt-tuner start` 读取采集文件，运行 CPU/I/O/IRQ 分析器，展示适用的调优项，并可应用用户选中的系统更改。

架构相关的差异属于预期行为：部分 CPU 拓扑、隔离和 GIC 调优器只会在 `aarch64` 上编译；NPU 拓扑、大页、Write Combining 和 Halt Polling 逻辑还包含适用的跨架构路径。

## 运行时配置与路径

- 安装后的配置文件：`/usr/local/sbin/ubs-optimizer/config.json`
- 默认配置源文件：`ebpf/src/default_config.json`
- 采集数据（JSON Lines）：`/var/ubs-opt/data/data.json`
- 日志：`/var/ubs-opt/log/ubs_optimizer_client.log`、`ubs_optimizer_server.log` 和 `ubs_optimizer_tuner.log`
- PID 文件：`/usr/local/sbin/ubs-optimizer/ubs-opt.pid` 和 `ubs-opt-guard.pid`
- 安装后的可执行程序：`/usr/local/sbin/ubs-opt`、`ubs-opt-guard` 和 `ubs-opt-tuner`

以下配置键必须在代码、`default_config.json`、`docs/config/配置说明.md` 和测试之间保持同步：

- `sampling_interval`：1～600 秒的整数
- `bind_port`：1024～49151 的整数
- `vm_name`：虚拟机名称/libvirt 域名
- `npu_type`：`d802` 或 `d803`
- `system.ipi_collector`、`system.sched_collector`、`system.numa_collector`：`enable` 或 `disable`

虚拟机采集器运行期间修改安装后的配置文件，应使用 `ubs-opt reload_ebpf` 重新加载配置。

## 修改指南

- 源码改动应放入对应的 `ebpf/src/<module>/`，测试应放入对应的 `ebpf/tests/<module>/`。
- 修改 eBPF 事件结构时，必须确保 BPF C 程序、生成的 skeleton 使用方、`DataTable`、VSOCK 传输、JSON 序列化、分析器和测试之间保持 ABI 兼容。
- 新增采集器时，应同步更新 `CollectorManager`、`system` 配置结构及默认值、相关文档和 client 测试。
- 新增分析器或调优器时，应将其接入对应的 analyzer/`OPTEngine`，增加有针对性的 optimizer 测试，并记录所有高权限或持久化系统影响。
- 修改 CLI 命令、安装路径、配置键、输出路径、依赖或软件包名称时，应在同一改动中同步更新相关 README/文档、构建脚本、RPM spec 和测试。
- 保持 `/var/ubs-opt/data/data.json` 的 JSON Lines 兼容性；不要在没有兼容方案的情况下重命名分析器或外部工具使用的现有字段。
- 没有明确的安全理由和代码审查，不得削弱编译器或链接器的安全加固参数。

## 安全指南

- 禁止提交密码、Token、API 密钥、私钥、证书、数据库凭证，以及从物理机或虚拟机采集的敏感数据。
- 不得为了通过测试而关闭 TLS/SSL 校验、身份认证、权限校验、内核安全检查或编译/链接安全加固。
- 日志中不得记录凭证、个人信息、原始密钥或非必要的物理机/虚拟机工作负载数据。
- 将 Shell 命令构造、`popen`/`system`、SSH 执行、虚拟机名称、文件路径和 JSON 配置视为安全边界。新增外部输入必须进行校验，并防止命令注入。
- 不得自动运行已安装的守护进程、`ubs-opt-tuner start`、RPM 安装/卸载、GRUB 修改、sysfs/procfs 写入、`virsh`、远程 SSH 命令或调优器的 `apply()` 路径。这些操作可能修改物理机、虚拟机、启动配置、CPU 亲和性或设备拓扑；执行前必须取得用户明确批准，并确认环境适合。
- 遵循 `docs/security/安全声明.md`：本工具用于离线调优，不应在生产环境周期运行；获得调优建议后应停止并卸载本程序。

## 提交指南

- 保持改动范围集中，不要包含生成产物或无关格式化修改。
- 行为发生变化时，增加或更新有针对性的 GoogleTest 测试。
- 对修改过的 C/C++ 文件执行 clang-format，并在环境允许时运行相关的指定测试。
- 提交代码前，仅可在确认安全且受支持的主机上执行完整的 `bash ebpf/run_ut.sh`，并提前考虑其高权限目录清理行为。如果没有合适环境，应明确说明哪些检查没有运行及其原因。
- 修改构建、打包、配置或文档时，检查相关命令、路径以及中英文 README/文档是否保持一致。
