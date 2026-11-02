# ubs-virt-enpu

<p> 简体中文 | <a href="README_EN.md">English</a> </p>

## 介绍

ubs-virt-enpu 支持基于硬件和软件的动态分配、多任务并行执行与资源隔离，显著提升 AI 硬件资源利用率与性能。

## 涉及组件

1. vCANN-RT: ubs-virt-enpu提供NPU算力软切分的服务，支持根据算力和显存资源配额信息，进行算力控制和显存控制。
2. MCS: ubs-virt-enpu提供NPU算力软切分服务，支持根据算力和显存资源配额信息进行算力控制和显存控制，实现多vNPU分核共享NPU算力。
3. enpu-manager: ubs-virt-enpu提供NPU显存交换（超分）服务的管理进程，配合vCANN-RT实现冷模型换出/换入，提升显存利用率。

## 使用说明

1. vCANN-RT: [vCANN-RT使用说明](./vcann-rt/README.md)
2. MCS: [MCS使用说明](./mcs/README.md)
3. enpu-manager: [enpu-manager使用说明](./enpu-manager/README.md)
