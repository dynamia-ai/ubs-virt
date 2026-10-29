# 使用 libvirt 创建普通虚拟机参考指导
### 说明：该文档旨在帮助对虚拟化零基础的用户，快速上手基础的虚拟化软件libvirt，有经验的虚拟化开发者和运维可跳过该章节
本文以 openEuler 24.03 LTS SP4 aarch64 为例，介绍如何使用 libvirt 创建普通虚拟机，以及如何配置大页内存和 NUMA。

先完成普通虚拟机创建、登录和网络验证，再参考 NPU 直通指南添加 UB 设备。

本文命令默认使用 root 用户执行

## 1. 准备环境

### 1.1 检查虚拟化环境

宿主机需要安装配套的 QEMU、libvirt、virsh 和 aarch64 UEFI 固件。

在宿主机执行：

```bash
qemu-system-aarch64 --version
virsh --version

systemctl start libvirtd
virsh -c qemu:///system list --all
```

后续 `virsh` 命令统一连接系统级 libvirt：

```bash
export LIBVIRT_DEFAULT_URI=qemu:///system
```

检查示例使用的 QEMU 和固件路径：

```bash
ls -l /usr/bin/qemu-system-aarch64
ls -l /usr/share/edk2/aarch64/QEMU_EFI-pflash.raw
```

如果实际安装路径不同，应同步修改后文 XML。

### 1.2 准备系统镜像

自行下载 openEuler 24.03 LTS SP4 aarch64 的 qcow2 系统镜像，上传到宿主机 `/home/images/`。

以下示例假设镜像文件为：

```text
/home/images/openEuler-24.03-LTS-SP4-aarch64.qcow2
```

如果下载的是 `.xz` 压缩文件，先解压：

```bash
xz -dk /home/images/openEuler-24.03-LTS-SP4-aarch64.qcow2.xz
```

为虚拟机复制一份独立磁盘，避免直接修改原始镜像：

```bash
mkdir -p /var/lib/libvirt/images

cp /home/images/openEuler-24.03-LTS-SP4-aarch64.qcow2 \
  /var/lib/libvirt/images/vm-base.qcow2
```

查看磁盘信息：

```bash
qemu-img info /var/lib/libvirt/images/vm-base.qcow2
```

如需将虚拟磁盘容量扩大到 100 GiB，在虚拟机启动前执行：

```bash
qemu-img resize /var/lib/libvirt/images/vm-base.qcow2 100G
```

说明：可根据实际情况调整磁盘容量，该命令只扩展虚拟磁盘容量，虚拟机内部的分区和文件系统需要另行扩容。

### 1.3 准备虚拟网络

本文使用 libvirt 的 `default` NAT 网络：虚拟机通过虚拟网桥与宿主机通信，并通过宿主机转发访问外部网络。

检查网络：

```bash
virsh net-list --all
```

如果存在 `default` 网络但未启动，执行：

```bash
virsh net-start default
virsh net-autostart default
```

如果不存在，可以新建 `/home/default-network.xml`：

```xml
<network>
  <name>default</name>
  <forward mode='nat'/>
  <bridge name='virbr0' stp='on' delay='0'/>
  <ip address='192.168.44.1' netmask='255.255.255.0'>
    <dhcp>
      <range start='192.168.44.100' end='192.168.44.200'/>
    </dhcp>
  </ip>
</network>
```

确认 `192.168.44.0/24` 与现有网络不冲突，且没有其他网络占用 `virbr0`，然后执行：

```bash
virsh net-define /home/default-network.xml
virsh net-start default
virsh net-autostart default
```

已有网络正常时，无需重复创建，也不要直接修改正在使用的网桥。

## 2. 准备基础 XML

先创建一台 16 vCPU、16 GiB 普通内存的虚拟机。确认能够启动、登录后，再配置大页和 NUMA。

### 2.1 基本概念

| 概念 | 简要说明 |
| --- | --- |
| QEMU/KVM | QEMU 提供虚拟硬件，KVM 提供内核中的硬件虚拟化能力。 |
| libvirt / virsh | libvirt 管理虚拟机，`virsh` 是调用 libvirt 的命令行工具。 |
| XML | 描述虚拟机 CPU、内存、磁盘、网络等配置的文件。 |
| vCPU | 虚拟机看到的逻辑 CPU；配置数量不等于自动独占同等数量的宿主机物理核心。 |
| Socket / Core / Thread | 分别表示处理器插槽、核心和硬件线程；虚拟机中显示的拓扑可以与宿主机不同。 |
| NUMA | 将 CPU 和内存划分为多个节点；CPU 访问本节点内存通常更有利于性能。 |

### 2.2 XML 示例

在宿主机创建 `/home/vm-base.xml`：

```xml
<domain type='kvm'>
  <name>vm-base</name>

  <!-- 16 GiB 内存 -->
  <memory unit='GiB'>16</memory>
  <currentMemory unit='GiB'>16</currentMemory>

  <!-- 16 个 vCPU -->
  <vcpu placement='static'>16</vcpu>

  <os>
    <type arch='aarch64' machine='virt'>hvm</type>
    <loader readonly='yes' type='pflash'>/usr/share/edk2/aarch64/QEMU_EFI-pflash.raw</loader>
    <boot dev='hd'/>
  </os>

  <features>
    <acpi/>
    <gic version='3'/>
  </features>

  <cpu mode='host-passthrough' check='none'/>

  <clock offset='utc'/>

  <on_poweroff>destroy</on_poweroff>
  <on_reboot>restart</on_reboot>
  <on_crash>destroy</on_crash>

  <devices>
    <emulator>/usr/bin/qemu-system-aarch64</emulator>

    <disk type='file' device='disk'>
      <driver name='qemu' type='qcow2'/>
      <source file='/var/lib/libvirt/images/vm-base.qcow2'/>
      <target dev='vda' bus='virtio'/>
    </disk>

    <interface type='network'>
      <source network='default'/>
      <model type='virtio'/>
    </interface>

    <serial type='pty'>
      <target type='system-serial' port='0'>
        <model name='pl011'/>
      </target>
    </serial>

    <console type='pty'>
      <target type='serial' port='0'/>
    </console>
  </devices>
</domain>
```

主要配置说明：

| 配置 | 说明 |
| --- | --- |
| `<name>` | 虚拟机名称，同一宿主机上不能重复。 |
| `<memory>`、`<currentMemory>` | 本例均设置为 16 GiB。 |
| `<vcpu>` | 虚拟机的 vCPU 数量。 |
| `host-passthrough` | 向虚拟机暴露宿主机 CPU 特性。 |
| `<loader>` | 宿主机上的 UEFI 固件路径。 |
| `<disk>` | 虚拟机磁盘，本例使用独立的 qcow2 文件。 |
| `<interface>` | 普通 virtio 虚拟网卡，接入 `default` 网络，不涉及智能网卡直通。 |
| `<console>` | 用于通过 `virsh console` 连接串口控制台。 |

创建多台虚拟机时，需要使用不同的名称和独立磁盘文件。

## 3. 创建、登录和验证

### 3.1 定义并启动虚拟机

在宿主机执行：

```bash
virsh define /home/vm-base.xml
virsh start vm-base
virsh list --all
```

`define` 保存虚拟机配置，`start` 启动虚拟机。

### 3.2 使用控制台登录

```bash
virsh console vm-base
```

连接后按回车，使用镜像已配置的账户登录。

退出控制台使用 `Ctrl+]`，该操作不会关闭虚拟机。

控制台显示依赖虚拟机系统已启用相应串口输出和登录服务；仅添加 XML 控制台节点并不能保证出现登录提示。

### 3.3 查看 IP 并使用 SSH

镜像内启用了 DHCP 客户端后，通常可以从 `default` 网络获取地址。

在宿主机查看 DHCP 租约：

```bash
virsh net-dhcp-leases default
```

也可以在虚拟机内查看：

```bash
ip -br addr
ip route
```

假设虚拟机获得 `192.168.44.100`，在宿主机连接：

```bash
ssh root@192.168.44.100
```

实际用户名以镜像配置为准。使用 SSH 还需要虚拟机内的 SSH 服务已启动、认证方式有效，并允许相应网络流量。

此类 NAT 网络地址通常用于宿主机连接虚拟机。PC 直接连接该地址需要额外的路由、跳板或端口转发配置。

### 3.4 验证网络

在虚拟机内执行：

```bash
ip route
getent hosts repo.openeuler.org
curl -I https://repo.openeuler.org
```

如果宿主机访问外网需要代理，虚拟机也需要配置可访问的代理地址，例如：

```bash
export http_proxy=http://代理地址:端口
export https_proxy=http://代理地址:端口
```

代理地址必须能够从虚拟机访问，不能直接照搬宿主机上的 `127.0.0.1`。宿主机设置的代理环境变量不会自动传入虚拟机。

## 4. 配置大页内存和 NUMA

以下操作在普通虚拟机验证成功后进行。

先正常关闭虚拟机，并确认状态为 `shut off`：

```bash
virsh shutdown vm-base
virsh domstate vm-base
```

修改 `/home/vm-base.xml` 后，需要重新执行 `virsh define`，再启动虚拟机。

### 4.1 配置大页内存

大页需要在宿主机提前预留。建议优先使用 1 GiB 大页，环境不支持或无法满足分配要求时使用 2 MiB 大页。

下面两种规格二选一，以 16 GiB 内存为例：

| 大页规格 | 所需页数 |
| --- | --- |
| 1 GiB | 16 |
| 2 MiB | 8192 |

以下设置为指定页池的目标总页数，不是追加数量。宿主机已有其他大页虚拟机时，需要合并计算资源需求。

#### 方案一：1 GiB 大页

在宿主机执行：

```bash
echo 16 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages

cat /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages
cat /sys/kernel/mm/hugepages/hugepages-1048576kB/free_hugepages
```

在 XML 中，与 `<memory>` 同级添加：

```xml
<memoryBacking>
  <hugepages>
    <page size='1048576' unit='KiB'/>
  </hugepages>
</memoryBacking>
```

#### 方案二：2 MiB 大页

在宿主机执行：

```bash
echo 8192 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
cat /sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages
```

在 XML 中添加：

```xml
<memoryBacking>
  <hugepages>
    <page size='2048' unit='KiB'/>
  </hugepages>
</memoryBacking>
```

如果实际预留数量不足，不能直接启动。尤其是 1 GiB 大页，运行时可能因连续内存不足而分配失败，需要调整资源或在开机阶段预留。

上述命令为运行时配置，重启宿主机后需要重新配置或设置持久化预留。

#### 检查大页挂载

宿主机需要提供对应规格的 hugetlbfs 挂载：

```bash
findmnt -t hugetlbfs
```

如果尚未挂载所选规格，可按需执行其中一组：

```bash
# 1 GiB 大页
mkdir -p /dev/hugepages-1G
mount -t hugetlbfs -o pagesize=1G none /dev/hugepages-1G
```

```bash
# 2 MiB 大页
mkdir -p /dev/hugepages-2M
mount -t hugetlbfs -o pagesize=2M none /dev/hugepages-2M
```

确保 libvirt/QEMU 能发现并访问对应挂载点。挂载完成后仍提示找不到大页挂载时，应检查配套 libvirt 的大页目录配置。

### 4.2 配置双 NUMA

NUMA 配置需要结合宿主机实际拓扑，不能直接照搬 CPU 编号。

在宿主机查看：

```bash
lscpu
lscpu -e=CPU,NODE,SOCKET,CORE
numactl --hardware
```

下面仅演示配置方法，假设：

- 宿主机 NUMA 0 包含 CPU 0～7。
- 宿主机 NUMA 1 包含 CPU 8～15。
- 两个节点均有足够的可用内存。
- 虚拟机使用 16 vCPU、16 GiB 内存，每个虚拟机 NUMA 分配 8 vCPU 和 8 GiB 内存。

将原 XML 中的 `<cpu .../>` 替换为：

```xml
<cpu mode='host-passthrough' check='none'>
  <numa>
    <cell id='0' cpus='0-7' memory='8' unit='GiB'/>
    <cell id='1' cpus='8-15' memory='8' unit='GiB'/>
  </numa>
</cpu>
```

这里的 `cpus` 是虚拟机 vCPU 编号。

与 `<cpu>` 同级添加宿主内存绑定：

```xml
<numatune>
  <memory mode='strict' nodeset='0-1'/>
  <memnode cellid='0' mode='strict' nodeset='0'/>
  <memnode cellid='1' mode='strict' nodeset='1'/>
</numatune>
```

其中，`cellid` 是虚拟机 NUMA 编号，`nodeset` 是宿主机 NUMA 编号。

再添加 CPU 绑定：

```xml
<cputune>
  <vcpupin vcpu='0' cpuset='0'/>
  <vcpupin vcpu='1' cpuset='1'/>
  <vcpupin vcpu='2' cpuset='2'/>
  <vcpupin vcpu='3' cpuset='3'/>
  <vcpupin vcpu='4' cpuset='4'/>
  <vcpupin vcpu='5' cpuset='5'/>
  <vcpupin vcpu='6' cpuset='6'/>
  <vcpupin vcpu='7' cpuset='7'/>
  <vcpupin vcpu='8' cpuset='8'/>
  <vcpupin vcpu='9' cpuset='9'/>
  <vcpupin vcpu='10' cpuset='10'/>
  <vcpupin vcpu='11' cpuset='11'/>
  <vcpupin vcpu='12' cpuset='12'/>
  <vcpupin vcpu='13' cpuset='13'/>
  <vcpupin vcpu='14' cpuset='14'/>
  <vcpupin vcpu='15' cpuset='15'/>
</cputune>
```

`vcpu` 为虚拟机 CPU 编号，`cpuset` 为宿主机逻辑 CPU 编号。绑核本身不会阻止其他宿主机进程使用这些 CPU。

双 NUMA 同时使用大页时，需要在对应宿主机节点分别预留足够的大页。例如，每个节点提供 8 GiB：

```bash
# 使用 1 GiB 大页：每个节点 8 页
echo 8 > /sys/devices/system/node/node0/hugepages/hugepages-1048576kB/nr_hugepages
echo 8 > /sys/devices/system/node/node1/hugepages/hugepages-1048576kB/nr_hugepages
```

或：

```bash
# 使用 2 MiB 大页：每个节点 4096 页
echo 4096 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
echo 4096 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
```

两种规格二选一，并检查相应节点的 `free_hugepages`。严格绑定内存时，宿主机其他节点有空闲大页，也不能替代目标节点的大页。

### 4.3 应用并检查配置

在宿主机执行：

```bash
virsh define /home/vm-base.xml
virsh start vm-base

virsh vcpupin vm-base
virsh numatune vm-base
```

在虚拟机内检查：

```bash
lscpu
free -h
```

宿主机为虚拟机内存使用大页，并不意味着虚拟机内部也自动配置了大页。

## 5. 常用操作与问题排查

### 5.1 常用命令

| 操作 | 命令 |
| --- | --- |
| 查看全部虚拟机 | `virsh list --all` |
| 查看状态 | `virsh domstate vm-base` |
| 查看当前 XML | `virsh dumpxml vm-base` |
| 启动 | `virsh start vm-base` |
| 正常关机 | `virsh shutdown vm-base` |
| 强制停止 | `virsh destroy vm-base` |
| 取消定义 | `virsh undefine vm-base` |

`shutdown` 需要等待虚拟机完成关机。`destroy` 相当于强制断电，可能造成未保存数据丢失。

取消定义前应先关闭虚拟机。对于带有独立 NVRAM 的虚拟机，删除时可能还需要按提示处理 NVRAM。

上述取消定义命令不会删除 qcow2 磁盘文件。

普通虚拟机能够正常启动、登录和联网后，即可将相应 XML 作为 NPU 直通的基础配置。

NPU 直通由 ub-device-manager 补充 bus instance 和相关 UB VFE 的直通配置，具体操作见 NPU 直通指南。删除由 ub-device-manager 创建的 NPU 直通虚拟机时，应使用其删除命令，完成虚拟机和 UB 设备绑定资源的清理。