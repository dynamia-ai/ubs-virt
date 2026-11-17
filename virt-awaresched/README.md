# Virtual Aware Scheduler

<p> 简体中文 | <a href="README_EN.md">English</a> </p>

> 本项目已在 [openEuler](https://www.openeuler.org/) 社区开源, 欢迎贡献与使用！

## 📍简介

Virtual Aware Scheduler(简称**VSched**)是基于鲲鹏cpu拓扑的虚拟化调度调优服务. 鲲鹏cpu拓扑层级为numa->cluster->core->cpu(smp), VSched旨在尽量减少范围绑核的虚拟机跨cluster, 以及vCPU线程在cpu间的抖动漂移, 从而提高LLC cache命中率, 减少vCPU无意义迁移的性能开销. 

## 📌 快速开始

### 环境要求

- 操作系统：推荐 **openEuler 24.03 LTS SP2或更高版本**
- 编译工具链：CMake ≥ 3.22, GCC ≥ 9.3
- 依赖库：详见 [构建指导文档](./docs/build_install/BUILD.md)

### 获取源码

```bash
git clone https://gitcode.com/openeuler/ubs-virt
cd virt-awaresched
```

### 构建项目

- 默认 Release 构建 RPM 包（生产环境编译）

    ```bash
    bash build.sh package
    ```

- 使用 Debug 构建 RPM 包（包含Debug信息）

    ```bash
    bash build.sh -D package
    ```

构建产物位于 `cmake-build-*` 目录下, RPM 包输出至 `output/`目录。

---

## 🧩 核心特性

- **采集虚拟机, CPU拓扑信息**<br>
    采集虚拟机信息并监听虚拟机生命周期事件, 同时采集CPU拓扑信息, 用于虚拟机vCPU调度。
- **CPU分配与整理**<br>
    VSched基于cluster拓扑在虚拟机绑核范围内为vCPU分配一个合适的CPU, 尽可能减少虚拟机跨cluster使用虚拟机. 在虚拟机销毁时, 回收CPU并整理CPU碎片。
- **支持静态/动态两种绑核模式**<br>
    VSched为vCPU线程绑核, 支持静态绑核与动态绑核两种：
    - 静态绑核下, vCPU将使用指定的CPU。
    - 动态绑核下, vCPU将使用一个优选核, 当优选核CPU使用率高于阈值后, vCPU将使用绑定范围中性能更好的CPU上. 动态绑核需要内核支持潮汐亲和特性。
- **支持CLI用户指令**<br>
    VSched提供给管理员运维指令, 方便进行vCPU绑定信息查询, 修改配置, 手动重调度。

> 详细功能说明请参阅 [CLI 文档](./docs/cli/cli_docs_reference.md) 和 [设计文档](./docs/design/ARCHITECTURE.md). 

---

## 📂 项目结构概览

```txt
virt-awaresched
├── cmake # cmake工具封装
├── docs # 文档
├── scripts # 开发使用脚本
│   └── ut_coverage
├── src # 源码目录
│   ├── cli # cli框架
│   ├── include # 头文件目录
│   ├── log # 日志模块
│   ├── util # 工具方法
│   ├── vasctl # vasctl
│   │   └── arg_parse # 二进制指令解析
│   └── vasd # vas-daemon
│       ├── acquire # 信息采集（含 libvirt 交互）
│       ├── api # api接口
│       ├── arg_parse # 二进制指令解析
│       ├── cluster_sched # 调度模块
│       ├── conf # 配置文件
│       ├── looper # 主循环
│       └── security # 安全模块
└── test # 测试目录
```

---

## 🧪 开发者测试

项目包含完整的单元测试(UT)。

- 仅运行UT

    ```bash
    bash build.sh test
    ```

- 运行特定测试用例

    ```bash
    bash build.sh test -- --gtest_filter="TestClusterSched.*"
    ```

- 生成代码覆盖率报告

    ```bash
    # 报告将生成与./coverage目录下
    bash build.sh test -C
    ```

> 测试开发指南见 [单元测试开发指南](./docs/test/TEST.md)。

---

## 📚 文档索引

所有技术文档位于 [`docs/`](./docs) 目录, 包括：

- **架构设计**：[architecture.md](./docs/design/ARCHITECTURE.md)
- **构建与安装**：[BUILD.md](./docs/build_install/BUILD.md)
- **配置说明**：[CONFIG.md](./docs/config/CONFIG.md)
- **CLI 接口规范**：[cli/](./docs/cli)
- **示例代码**：[example/](./docs/example)
- **测试指南**：[test/](./docs/test)

---

## 🤝 参与贡献

我们欢迎社区开发者提交 Issue、PR 或参与讨论！<br>
请先阅读 [贡献指南](docs/contributing/CONTRIBUTING.md)，并遵守 openEuler 社区行为准则。

---

## 📄 许可证

本项目采用 [Mulan PSL v2](https://license.coscl.org.cn/MulanPSL2) 开源许可证。

---
