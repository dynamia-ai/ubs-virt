# ubs-virt-enpu

<p> 简体中文 | <a href="README_EN.md">English</a> </p>

## 介绍

ubs-virt-enpu提供NPU分时/分核算力切分、vNPU资源管理和显存超分能力，使多个容器高效、灵活地共享同一张物理NPU，显著提升单卡资源利用率与性能，适用于多容器共卡部署、按配额隔离算力和显存的AI推理/训练场景。ubs-virt-enpu由三个组件构成：

1. vCANN-RT：以so形式加载到业务进程，负责算力时分复用，各vNPU按时间片轮流占用整张物理NPU的AI Core，实现算力按配额比例共享；同时根据显存资源配额信息进行显存控制。
2. MCS (Multi-container Sharing)：以so形式加载到业务进程，负责算力分核共享，控制各容器可使用的AI Core核数不超过其配额，实现多容器分核共享一张物理NPU；同时根据显存资源配额信息进行显存控制。
3. eNPU Manager：独立进程，提供vNPU资源管理和显存超分服务，配合vCANN-RT实现冷模型换出/换入，提升显存利用率。

> 说明：vCANN-RT和MCS为两种不同的算力切分方案，部署时只能二选一；当前eNPU Manager仅支持配合vCANN-RT使用，暂不支持MCS模式。

## 使用说明

1. vCANN-RT: [vCANN-RT使用说明](./vcann-rt/README.md)
2. MCS: [MCS使用说明](./mcs/README.md)
3. eNPU Manager: [eNPU Manager使用说明](./enpu-manager/README.md)
