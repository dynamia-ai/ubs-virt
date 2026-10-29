# ub-device-manager 安装部署指导

## 1. 环境要求

本文命令默认使用 root 用户执行，适用于使用 Python 3.11 构建的 RPM 软件包。

| 项目                | 要求                                                                           |
|-------------------|------------------------------------------------------------------------------|
| 产品                | Atlas 850E 服务器 / Atlas 950 SuperPoD 服务器                                      |
| 操作系统              | openEuler 24.03 LTS SP4                                                      |
| 架构                | aarch64                                                                      |
| Python            | Python 3.11                                                                  |
| 软件包               | `ub-device-manager-*.aarch64.rpm`                                            |
| 依赖软件              | `libvirt虚拟化组件`, `qemu虚拟化组件`, `python3-libvirt`, `edk2-aarch64`, `ubs-engine` |

使用前，需要安装可用的 UBS Engine、支持 UB 直通的配套虚拟化组件 Qemu，Libvirt等。

请确保上述环境满足要求。本文介绍 ub-device-manager 及其依赖的安装，产品配套驱动和操作系统的安装请参阅相应文档。

## 2. 检查并安装依赖

### 2.1 检查依赖

#### 2.1.1 检查虚拟化组件

检查 QEMU、libvirt 虚拟化组件是否已安装，执行以下命令：

```bash
rpm -q qemu libvirt python3-libvirt libvirt-client libvirt-devel
```
回显示例：
```text
qemu-8.2.0-93.oe2403sp3.aarch64
libvirt-9.10.0-45.oe2403sp4.aarch64
python3-libvirt-9.10.0-2.oe2403sp4.aarch64
libvirt-client-9.10.0-45.oe2403sp4.aarch64
libvirt-devel-9.10.0-45.oe2403sp4.aarch64
```

#### 2.1.2 检查 UBS Engine

检查 UBS Engine 及相关软件包是否已安装，执行以下命令：

```bash
rpm -q ubs-engine ubs-engine-client-libs python3-ubs-engine ipmitool
```
回显示例：
```text
ubs-engine-1.0.1-1.aarch64
ubs-engine-client-libs-1.0.1-1.aarch64
python3-ubs-engine-1.0.1-1.noarch
ipmitool-1.8.18-24.oe2403sp4.aarch64
```

#### 2.1.3 检查基础工具
检查必要的基础工具是否安装，执行以下命令：
```bash
rpm -q sudo dmidecode
```

回显示例：
```text
sudo-1.9.15p5-8.oe2403sp4.aarch64
dmidecode-3.5-4.oe2403sp4.aarch64
```

#### 2.1.4 检查 Python 依赖
执行以下命令，检查ub-device-manager的python依赖是否安装：
```bash
python3.11 -c \
"import fastapi, uvicorn, pydantic, loguru, httpx, typer, rich, yaml, defusedxml; print('Python dependencies available')"
```
回显中打印'Python dependencies available'，则说明python依赖已安装

说明：如果2.1.1-2.1.4的检查完成后，均满足要求，则跳过2.2章节，否则，执行2.2章节操作，补齐依赖。

### 2.2 安装依赖

#### 2.2.1 配置 openEuler 软件源

编辑软件源配置文件：

```bash
vi /etc/yum.repos.d/openEuler.repo
```

写入以下内容：

```ini
[openEuler-everything]
name=openEuler-everything
baseurl=http://repo.openeuler.org/openEuler-24.03-LTS-SP4/everything/aarch64/
enabled=1
gpgcheck=0

[openEuler-EPOL]
name=openEuler-epol
baseurl=http://repo.openeuler.org/openEuler-24.03-LTS-SP4/EPOL/main/aarch64/
enabled=1
gpgcheck=0

[openEuler-update]
name=openEuler-update
baseurl=http://repo.openeuler.org/openEuler-24.03-LTS-SP4/update/aarch64/
enabled=1
gpgcheck=0
```

编辑完成并保存后，执行以下命令，重新建立软件源缓存：
```bash
dnf clean all
dnf makecache
```

#### 2.2.2 安装虚拟化组件
执行以下命令，进行虚拟化libvirt，qemu，python3-libvirt，edk2-aarch64的安装
```bash
dnf install -y  qemu* libvirt* python3-libvirt edk2-aarch64
```
执行以下命令，启动libvirtd服务
```bash
systemctl start libvirtd
```
验证基本功能，执行以下virsh命令：
```bash
virsh list --all
```
检查Libvirtd服务状态：
```bash
systemctl status libvirtd
```
服务状态为active (running)时，说明Libvirtd正常启动完成

#### 2.2.3 安装 UBS Engine

安装 UBS Engine 及相关依赖,执行以下命令：

```bash
export ENABLE_AI=true
env ENABLE_AI=true dnf install -y ubs-engine
dnf install -y ubs-engine-client-libs python3-ubs-engine ipmitool
```
UBS Engine 的访问授权在安装 ub-device-manager RPM、创建服务账户后配置，见第 3.3 节。

#### 2.2.4 安装基础工具
执行以下命令，下载dmidecode相关工具：
```bash
dnf install -y sudo dmidecode
```
执行以下命令，验证基本功能：
```bash
sudo dmidecode -t 131
```
确保命令有回显，且且回显字段的Header and Data内容中，第5个字节为00,而非FF;
回显示例：
```text
# sudo dmidecode -t 131
# dmidecode 3.5
Getting SMBIOS data from sysfs.
SMBIOS 3.6.0 present.
# SMBIOS implementations newer than version 3.5.0 are not
# fully supported by this version of dmidecode.

Handle 0x0032, DMI type 131, 77 bytes
OEM-specific Type
	Header and Data:
		83 4D 32 00 00 FF FF FF 08 02 00 00 00 FF FF 01
		00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
		00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
		00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
		00 00 00 00 00 00 00 00 00 00 00 00 00
	Strings:
		UB INFO
```
回显示例中，Header and Data内容中，第5个字节为00，说明
说明：smbios中的Header and Data，第5个字节为Flag字段，表示BMC获取情况，00表示获取成功，FF表示获取失败，需保证BMC获取成功，否则会影响后续NPU直通虚拟机流程；

#### 2.2.5 安装 Python 依赖
执行以下命令，下载ub-device-manager运行所需要的python依赖
```bash
python3.11 -m pip install -i https://pypi.org/simple \
  'fastapi>=0.115,<1' \
  'uvicorn>=0.30,<1' \
  'pydantic>=2.7,<3' \
  'loguru>=0.7,<1' \
  'httpx>=0.27,<1' \
  'typer>=0.12,<1' \
  'rich>=13,<15' \
  'PyYAML>=6,<7' \
  'defusedxml>=0.7,<1'
```
执行以下命令：
```bash
python3.11 -c \
"import fastapi, uvicorn, pydantic, loguru, httpx, typer, rich, yaml, defusedxml; print('Python dependencies available')"
```
若回显有'Python dependencies available'，说明python依赖安装完成

## 3. 安装软件包

### 3.1 安装 RPM

进入 ub-device-manager RPM 软件包所在目录，查看待安装的软件包：

```bash
ls -lh ub-device-manager-*.aarch64.rpm
```

确保匹配到的文件为本次需要安装的目标版本，然后执行：

```bash
rpm -ivh ub-device-manager-*.aarch64.rpm
```

检查安装结果：

```bash
rpm -q ub-device-manager
```

安装过程会完成以下操作：

- 创建服务账户和用户组 `ub-device-manager`。
- 安装服务端、CLI 和 Python SDK。
- 安装配置文件、systemd 服务文件和 SMBIOS 读取授权文件。
- 设置服务开机自启动，但不立即启动服务。

### 3.2 安装路径

| 内容 | 路径 |
| --- | --- |
| 服务端、CLI 和 SDK 代码 | `/usr/lib/python3.11/site-packages/ub_device_manager/` |
| CLI 入口 | `/usr/bin/ubdmctl` |
| 配置文件 | `/etc/ub_device_manager/ub_device_manager.toml` |
| systemd 服务文件 | `/etc/systemd/system/ub-device-manager.service` |
| SMBIOS 读取授权 | `/etc/sudoers.d/ub-device-manager-smbios` |
| 日志目录 | `/var/log/ub_device_manager/` |

安装完成后，按需参阅[配置文件说明](./configuration.md)调整配置，并完成以下访问授权后再启动服务。

### 3.3 配置 UBS Engine 访问权限

以下步骤在安装 ub-device-manager RPM 后执行。即使依赖已经安装、跳过了第 2.2 节，也需要检查本节授权配置。

将 `ub-device-manager` 服务账户加入 `ubse` 组：

```bash
usermod -aG ubse ub-device-manager
id ub-device-manager
```

编辑 UBS Engine 权限配置文件：

```bash
vi /etc/ubse/ubse_auth_default.conf
```

在已有的 `[auth.user.default]` 配置段中添加以下内容；如果已存在该用户，应检查并修改原配置，避免重复添加：

```ini
ub-device-manager = default_op
```

保留其他用户的原有配置。等待正在执行的 UBS Engine 业务操作结束后，重启服务使授权生效：

```bash
systemctl restart ubse
systemctl status ubse --no-pager
```

确认状态为 `active (running)`，再启动或重启 ub-device-manager，使服务进程使用更新后的用户组权限。

## 4. 启动并验证

### 4.1 启动服务

确认依赖服务和访问权限已配置完成后，启动 ub-device-manager：

```bash
systemctl daemon-reload
systemctl restart ub-device-manager
```

### 4.2 检查服务状态

```bash
systemctl status ub-device-manager --no-pager
```

若状态为 `active (running)`，说明服务进程正在运行。

### 4.3 验证 NPU 查询功能

```bash
ubdmctl npu list
```

回显示例：

```text
✔ Execution successful.
┏━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━┓
┃ id  ┃ guid                                  ┃ bus_guid ┃
┡━━━━━╇━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╇━━━━━━━━━━┩
│ 1-1 │ cc08-a002-0-2-000000-000000e70e940189 │          │
│ 1-2 │ cc08-a002-0-2-000000-000000e70e94018a │          │
│ 1-3 │ cc08-a002-0-2-000000-000000e70e94018b │          │
│ 1-4 │ cc08-a002-0-2-000000-000000e70e94018c │          │
│ 1-5 │ cc08-a002-0-2-000000-000000e70e940089 │          │
│ 1-6 │ cc08-a002-0-2-000000-000000e70e94008a │          │
│ 1-7 │ cc08-a002-0-2-000000-000000e70e94008b │          │
│ 1-8 │ cc08-a002-0-2-000000-000000e70e94008c │          │
└─────┴───────────────────────────────────────┴──────────┘
```

实际设备 ID、GUID 和绑定状态以当前环境的查询结果为准。

## 5. 升级

本文采用先卸载旧版本、再安装新版本的方式进行升级。

升级前，等待正在执行的虚拟机创建、删除和设备操作结束，并按需备份配置和日志。

### 5.1 卸载旧版本

```bash
rpm -e ub-device-manager
```

### 5.2 安装新版本

进入新版本 RPM 软件包所在目录，确认目录下匹配到的文件为目标版本，然后执行：

```bash
rpm -ivh ub-device-manager-*.aarch64.rpm
```

检查安装版本：

```bash
rpm -q ub-device-manager
```

### 5.3 检查配置并启动

按照新版本要求检查依赖、配置和访问权限，然后执行：

```bash
systemctl daemon-reload
systemctl start ub-device-manager
systemctl status ub-device-manager --no-pager
```

验证 NPU 查询功能：

```bash
ubdmctl npu list
```

## 6. 卸载

### 6.1 卸载前准备

卸载前，等待正在执行的操作结束，并按需备份配置和日志。

卸载软件包不会自动删除虚拟机、虚拟机磁盘或释放已绑定的 UB 设备。如果不再使用这些业务资源，应在卸载前通过 ub-device-manager 完成删除和解绑，避免资源长期占用。

仅通过 `virsh` 取消 NPU 直通虚拟机的定义，不会调用 UBSE 释放其设备绑定。需要通过 ub-device-manager 的虚拟机删除流程完成虚拟机删除和相关设备解绑；如果已通过其他方式取消定义，则需要另行执行设备解绑。

### 6.2 卸载软件包

```bash
rpm -e ub-device-manager
```

### 6.3 检查卸载结果

```bash
rpm -q ub-device-manager
```

卸载成功后，会提示该软件包未安装。
