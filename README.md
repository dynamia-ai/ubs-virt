# ubs-virt

<p> 简体中文 | <a href="README_EN.md">English</a> </p>

> 本项目面向 openEuler 社区开源。openEuler 官方网站：<https://www.openeuler.org/>，官方文档入口：<https://docs.openeuler.org/zh/>，UB 软件栈文档入口：<https://docs.openeuler.org/zh/docs/24.03_LTS_SP3/unifiedbus/unifiedbus/introduction/introduction.html>

## 介绍

Ubs-virt 包含多个虚拟化功能组件，分别提供NPU算力软切分，虚拟机线性度调优，NPU直通虚拟机大模型推理性能调优的功能。当前仅支持 aarch64 架构的编译与运行。

## 涉及组件

1. ubs-virt-enpu: ubs-virt提供NPU算力切分的服务，支持配置指定的算力和显存资源（独立交付，不在 RPM 打包范围内）。
2. virt-awaresched: virt-awaresched提供对虚拟化调度调优的服务，减少vCPU无意义迁移的性能开销，提升虚机的线性度。
3. virt-optimizer: virt-optimizer提供NPU直通虚拟机大模型推理性能瓶颈分析，性能调优建议的功能。

## 使用说明

1. ubs-virt-enpu: [ubs-virt-enpu使用说明](./ubs-virt-enpu/README.md)
2. virt-awaresched：[virt-awaresched使用说明](./virt-awaresched/README.md)
3. virt-optimizer: [virt-optimizer使用说明](./virt-optimizer/README.md)

## 说明

此开源项目非华为产品，仅提供有限支持。

## 组件文档

各组件详细文档见 [docs/zh](./docs/zh) 目录。

## 编译安装

### 构建依赖

```bash
yum install gcc gcc-c++ cmake make patch systemd \
    libvirt-devel libboundscheck \
    clang libbpf-devel bpftool rapidjson-devel
```

### 构建

```bash
# virt-awaresched（产物位于 virt-awaresched/build/bin/）
cd virt-awaresched && bash build.sh && cd ..

# virt-optimizer（产物位于 virt-optimizer/build/release/）
cd virt-optimizer && bash build.sh && cd ..
```

### RPM 打包

```bash
# 方式一：整仓打包（对应制品仓 ubs-virt.spec，当前打包 virt-awaresched）
rpmbuild -bb ubs-virt.spec

# 方式二：单组件本地打包
(cd virt-awaresched && bash build.sh package)
(cd virt-optimizer && bash build.sh)
```

virt-awaresched 的 RPM 安装后文件布局符合 openEuler 打包规范：可执行文件位于 `/usr/bin`，systemd 服务位于 `/usr/lib/systemd/system`。

## 目录说明

```
ubs-virt
├── docs               # 组件总览文档与各组件用户文档
├── virt-awaresched    # 虚拟化感知调度调优服务（含 src 源码与 test 测试用例）
├── virt-optimizer     # 性能瓶颈分析调优工具（含 ebpf/src 源码与 ebpf/tests 测试用例）
└── ubs-virt-enpu      # NPU 算力软切分服务（独立交付）
```

## 架构

各组件独立部署、互不依赖：

- virt-awaresched：vas-daemon 守护进程基于 libvirt 监听虚拟机事件，完成 vCPU 绑核调度与 CPU 碎片整理；vasctl 为运维命令行工具，通过 socket 与 vas-daemon 通信。
- virt-optimizer：基于 eBPF 采集虚拟机性能数据，分析 NPU 直通虚拟机推理性能瓶颈并给出调优建议。
- ubs-virt-enpu：NPU 算力软切分服务（独立交付）。

virt-awaresched 详细设计见 [ARCHITECTURE.md](./virt-awaresched/docs/design/ARCHITECTURE.md)。

## 参与开发

开发环境搭建、代码规范与 UT 编写规范见 [CONTRIBUTING.md](./virt-awaresched/docs/contributing/CONTRIBUTING.md)。

运行单元测试：

```bash
cd virt-awaresched && bash build.sh test
```

## 许可证

本项目基于 [木兰宽松许可证第 2 版（Mulan PSL v2）](./LICENSE) 开源。
