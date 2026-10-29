# ub-device-manager 快速开始

本文介绍从源码构建到创建第一台 NPU 直通虚拟机的基本流程。

示例虚拟机规格如下：

| 项目 | 配置 |
| --- | --- |
| 操作系统 | openEuler 24.03 LTS SP4 aarch64 |
| CPU | 16 vCPU |
| 内存 | 16 GiB，使用 2 MiB 大页 |
| 磁盘 | qcow2 系统盘 |
| 网络 | libvirt `default` 网络 |
| NPU | 1 张 |

除特别说明外，本文命令均在宿主机上使用 root 用户执行。

## 1. 前置条件

宿主机应满足以下条件：

- 使用 openEuler 24.03 LTS SP3 或以上版本，架构为 aarch64。
- 已安装 Python 3.11，且可使用对应的 pip。
- 已安装 Git 和 xz 等基础工具。
- 已安装产品配套的 NPU 驱动。
- 如需同时直通 Hi1825 网卡，已安装产品配套的网卡驱动。
- 已获取 UBS Engine 及其配套软件。
- 已获取与操作系统配套、支持 UB 直通的 QEMU、libvirt 及 aarch64 UEFI 固件。
- 已配置与操作系统版本对应的软件源。

## 2. 构建并安装

### 2.1 获取源码

获取 ubs-virt 仓库的 `br_UnifiedBus` 分支，并进入 ub-device-manager 目录：

```bash
git clone -b br_UnifiedBus https://gitcode.com/openeuler/ubs-virt.git
cd ubs-virt/ub-device-manager
```

### 2.2 安装依赖

安装构建及基础工具：

```bash
dnf install -y rpm-build tar gzip sudo dmidecode
```

安装 Python 运行依赖：

```bash
python3.11 -m pip install \
  -i https://pypi.org/simple \
  -r requirements.txt
```

### 2.3 构建 RPM

执行构建：

```bash
bash build_rpm.sh
```

构建完成后，查看生成的软件包：

```bash
ls -lh output/ub-device-manager-*.aarch64.rpm
```

### 2.4 安装 RPM

确认匹配到的软件包为目标版本，然后执行安装：

```bash
rpm -ivh output/ub-device-manager-*.aarch64.rpm
```

检查安装结果：

```bash
rpm -q ub-device-manager
```

安装过程会创建 `ub-device-manager` 服务账户，并安装程序、配置文件和 systemd 服务文件。

详细说明见[构建指导](./build.md)和[安装部署指导](./install.md)。

## 3. 准备驱动和依赖服务

### 3.1 检查宿主机内核驱动

检查 NPU 驱动：

```bash
npu-smi info
```

确认能够正常显示宿主机 NPU 信息。

加载 UB 直通相关内核模块：

```bash
modprobe ubmempfd
modprobe vfio_ub
```

检查模块是否已加载：

```bash
lsmod | grep -E 'ubmempfd|vfio_ub'
```

### 3.2 安装并配置 libvirt 和 QEMU

安装虚拟化组件及 UEFI 固件：

```bash
dnf install -y 'libvirt*' 'qemu*' edk2-aarch64
```

编辑 libvirt 配置文件：

```bash
vi /etc/libvirt/libvirtd.conf
```

设置以下配置项。已有同名配置时，修改原配置，避免重复添加：

```ini
log_level = 2
log_outputs = "2:file:/var/log/libvirt/libvirtd.log"
```

编辑 QEMU 配置文件：

```bash
vi /etc/libvirt/qemu.conf
```

设置以下配置项，保留本例所需的设备访问配置：

```ini
user = "root"
group = "root"
seccomp_sandbox = 0
namespaces = []

cgroup_device_acl = [
    "/dev/null", "/dev/full", "/dev/zero",
    "/dev/random", "/dev/urandom",
    "/dev/ptmx", "/dev/kvm",
    "/dev/vfio/vfio", "/dev/ubmempfd",
    "/dev/rtc"
]
```

配置完成后，启动 libvirtd 服务；如果服务已经运行，应在允许重启的维护时段重启以加载配置：

```bash
systemctl restart libvirtd
```

检查服务状态：

```bash
systemctl status libvirtd --no-pager
```

确认状态为 `active (running)`。执行以下命令检查基本管理功能：

```bash
virsh list --all
```

命令能够正常返回虚拟机列表（允许为空），说明 libvirt 基本管理功能可用。

### 3.3 安装 UBS Engine 及其依赖

安装昇腾场景所需的 UBS Engine 软件包：

```bash
export ENABLE_AI=true
env ENABLE_AI=true dnf install -y ubs-engine
```

安装客户端运行时库、UBS Engine Python API 及依赖工具：

```bash
dnf install -y ubs-engine-client-libs python3-ubs-engine ipmitool
```

### 3.4 配置访问权限并启动 ub-device-manager

ub-device-manager 默认配置文件为：

```text
/etc/ub_device_manager/ub_device_manager.toml
```

配置项说明见[配置文件说明](./configuration.md)。libvirt 访问权限的配置和检查方法见[安装部署指导](./install.md)。

将 `ub-device-manager` 服务账户加入 `ubse` 组：

```bash
usermod -aG ubse ub-device-manager
```

编辑 UBS Engine 权限配置文件：

```bash
vi /etc/ubse/ubse_auth_default.conf
```

在已有的 `[auth.user.default]` 配置段中添加以下内容；如果已存在该用户，检查并修改原配置，避免重复添加：

```ini
ub-device-manager = default_op
```

配置完成后，重启 UBS Engine 服务，使授权配置生效：

```bash
systemctl restart ubse
systemctl status ubse --no-pager
```

启动 ub-device-manager，并设置开机自启动：

```bash
systemctl daemon-reload
systemctl enable --now ub-device-manager
systemctl status ub-device-manager --no-pager
```

如果 ub-device-manager 已经运行，修改配置或用户组后需执行 `systemctl restart ub-device-manager`。

查询 NPU 设备：

```bash
ubdmctl npu list
```

确认命令执行成功，并能够查看设备 ID、GUID 和绑定状态。`bus_guid` 为空表示设备当前未绑定。

### 3.5 检查SMBIOS 表的type 131信息
说明：NPU直通虚拟机前，需要确保宿主机的SMBIOS 表的type 131信息符合要求
执行以下dmidecode命令，读取SMBIOS 表中类型编号为 131 的记录
```text
dmidecode -t 131
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

## 4. 准备镜像、网络和基础 XML

### 4.1 准备系统镜像

根据虚拟机操作系统要求，从 openEuler 官网获取对应版本的 qcow2 镜像压缩包。本例使用 [openEuler 24.03 LTS SP4 aarch64 镜像](https://repo.openeuler.org/openEuler-24.03-LTS-SP4/virtual_machine_img/aarch64/openEuler-24.03-LTS-SP4-aarch64.qcow2.xz)。

创建镜像存放目录：

```bash
mkdir -p /home/image
```

将下载的镜像压缩包上传至该目录，确认文件存在后解压：

```bash
ls -lh /home/image/openEuler-24.03-LTS-SP4-aarch64.qcow2.xz
xz -d /home/image/openEuler-24.03-LTS-SP4-aarch64.qcow2.xz
```

将解压后的镜像复制为虚拟机独立磁盘：

```bash
mkdir -p /var/lib/libvirt/images
cp /home/image/openEuler-24.03-LTS-SP4-aarch64.qcow2 \
  /var/lib/libvirt/images/vm-npu.qcow2
```

镜像的初始登录账户、密码或 SSH 密钥配置方式，以 openEuler 官方镜像说明为准。

### 4.2 准备虚拟网络

查看虚拟网络：

```bash
virsh net-list --all
```

本例使用 `default` 网络。回显示例如下，表示该网络已启动并设置为自动启动：

```text
 Name      State    Autostart   Persistent
--------------------------------------------
 default   active   yes         yes
```

如果网络不存在或尚未启动，按照[普通虚拟机创建指导](./vm_basics.md)完成网络准备。

### 4.3 准备基础 XML

本例的基础 XML 配置如下：

| 配置项 | 值 |
| --- | --- |
| 虚拟机名称 | `vm-npu` |
| vCPU 数量 | `16` |
| 内存 | `16 GiB` |
| 大页规格 | `2 MiB` |
| 磁盘路径 | `/var/lib/libvirt/images/vm-npu.qcow2` |
| 网络 | `default` |

创建并编辑虚拟机 XML 文件：

```bash
vi /home/vm-npu.xml
```

写入以下内容并保存：

```xml
<domain type='kvm'>
  <name>vm-npu</name>
  <seclabel type="none"></seclabel>
  <memory unit='GiB'>16</memory>
  <currentMemory unit='GiB'>16</currentMemory>
  <memoryBacking>
    <hugepages>
      <page size='2' unit='MiB'/>
    </hugepages>
  </memoryBacking>

  <vcpu placement='static'>16</vcpu>

  <os>
    <type arch='aarch64' machine='virt'>hvm</type>
    <boot dev='hd'/>
  </os>

  <features>
    <acpi/>
    <gic version='3'/>
  </features>

  <cpu mode='host-passthrough'/>

  <clock offset='utc'/>

  <devices>
    <emulator>/usr/libexec/qemu-kvm</emulator>

    <disk type='file' device='disk'>
      <driver name='qemu' type='qcow2'/>
      <source file='/var/lib/libvirt/images/vm-npu.qcow2'/>
      <target dev='vda' bus='virtio'/>
    </disk>

    <interface type='network'>
      <source network='default'/>
      <model type='virtio'/>
    </interface>

    <serial type='pty'>
      <target type='system-serial' port='0'/>
    </serial>

    <console type='pty'>
      <target type='serial' port='0'/>
    </console>

    <graphics type='vnc' port='-1' autoport='yes'/>
  </devices>
</domain>
```

## 5. 分配 2 MiB 大页

16 GiB 内存需要 8192 个 2 MiB 大页。在没有其他大页业务的示例环境中，执行：

```bash
echo 8192 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
cat /sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages
```

确认空闲大页数量不少于 8192。`nr_hugepages` 设置的是目标总页数；如果宿主机已有其他大页业务，应合并计算需求。

## 6. 创建 NPU 直通虚拟机

### 6.1 查询可用设备

```bash
ubdmctl npu list
```

从返回结果中选择未绑定的设备，例如 `1-1`。

### 6.2 创建虚拟机

以下两种方式二选一。

指定一张 NPU：

```bash
ubdmctl vm create \
  --xml_path /home/vm-npu.xml \
  --upi 0x000f \
  --npu_ids 1-1
```

将 `1-1` 替换为实际设备 ID。

也可以不指定设备 ID，由系统自动选择一张空闲 NPU：

```bash
ubdmctl vm create \
  --xml_path /home/vm-npu.xml \
  --upi 0x000f \
  --npu_count 1
```

`--upi` 为用户隔离标识，应按实际业务分配，取值范围为 `0x0001`～`0x0fff`。

如需同时申请 NPU 配套的 Hi1825 智能网卡 VFE，在创建命令中增加以下参数：

```text
--need_nic true
```

## 7. 验证创建结果

查看虚拟机状态：

```bash
virsh list --all
```

查看设备绑定状态：

```bash
ubdmctl npu list
```

确认已直通设备的 `bus_guid` 字段非空。

连接虚拟机控制台：

```bash
virsh console vm-npu
```

以下操作在虚拟机内完成：等待操作系统启动完成，按回车查看登录提示，再使用镜像配置的账户登录。初始账户和认证方式以 openEuler 官方镜像说明为准。使用 `Ctrl+]` 退出控制台。

如需使用 SSH 登录，应按实际环境配置虚拟机网络、SSH 服务和认证方式。

虚拟机创建成功并不代表虚拟机内的 NPU 驱动已就绪。登录虚拟机后，还需要安装与服务器产品配套的 HDK 驱动，并按配套说明验证 NPU 功能。

## 8. 删除示例虚拟机

不再使用时，在宿主机执行：

```bash
ubdmctl vm delete vm-npu
```

应通过 ub-device-manager 完成虚拟机删除和相关设备解绑。仅执行 `virsh undefine` 不会调用 UBSE 释放设备绑定。
