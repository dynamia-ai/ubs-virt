# ub-device-manager 构建指导

## 1. 构建说明

本文介绍 ub-device-manager 的构建依赖、构建方法和构建产物。

ub-device-manager 使用 Python 实现，构建过程将服务端、Python SDK、CLI 和配置文件打包为 RPM。

本文安装构建依赖的命令默认使用 root 用户执行。

默认构建的软件包使用 Python 3.11 的安装路径和启动命令。构建过程不安装 Python 运行依赖，也不验证 libvirt、UBS Engine 或 NPU 驱动是否可用；运行环境准备见[安装部署指导](./install.md)。

| 项目 | 要求 |
| --- | --- |
| 操作系统 | openEuler 24.03 LTS SP4 |
| CPU 架构 | aarch64 |
| 源码位置 | ubs-virt 仓库中的 `ub-device-manager` 目录 |

## 2. 构建依赖

安装构建依赖：
```bash
dnf install -y rpm-build tar gzip git
```

## 3. 构建方法

### 3.1 获取源码
拉取开源 ubs-virt 项目的 `br_UnifiedBus` 分支，并进入 ub-device-manager 源码目录：
```bash
git clone -b br_UnifiedBus https://gitcode.com/openeuler/ubs-virt.git
cd ubs-virt/ub-device-manager
```

主要构建文件如下：

| 文件或目录 | 说明 |
| --- | --- |
| `build_rpm.sh` | RPM 构建入口 |
| `ub_device_manager.spec` | 定义软件包信息、安装路径和安装脚本 |

### 3.2 执行构建

在 `ub-device-manager` 目录执行：

> 每次构建会清理 `output/` 中已有的 `ub-device-manager-*.rpm`。需要保留历史产物时，请提前备份。

```bash
bash build_rpm.sh
```

构建成功后，终端会显示类似信息：

```text
==== Build finished, RPM located at: ....../ubs-virt/ub-device-manager/output ====
```

## 4. 构建产物


构建完成后，从 `output/` 目录获取用于安装和部署的 RPM 软件包：

| 路径 | 产物                                      | 用途 |
| --- |-----------------------------------------| --- |
| `output/` | `ub-device-manager-*.aarch64.rpm` | 用于安装和部署 |

查看生成的软件包：

```bash
ls -lh output/ub-device-manager-*.aarch64.rpm
```

RPM 安装、服务授权与启动步骤见[安装部署指导](./install.md)。
