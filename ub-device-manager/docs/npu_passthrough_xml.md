# NPU 直通虚拟机 XML 配置指南

本文介绍 NPU 直通虚拟机的 XML 配置方法，包括根据设备绑定结果配置 UB 直通设备，以及向虚拟机传递宿主机 SMBIOS Type 131 信息。

本文适用于需要自行配置 XML 的开发者，按照“准备基础 XML → 绑定设备 → 配置 UB 直通与 Type 131 → 定义并启动虚拟机”的顺序说明，并提供验证和清理步骤。

使用 `ubdmctl vm create` 自动创建虚拟机的用户，可直接参考[快速开始](./quick_start.md)。组件会自动完成设备绑定和相关 XML 配置，无需执行本文的手动流程。

## 1. 准备基础 XML

先按照[普通虚拟机创建指导](./vm_basics.md)准备已验证可用的基础 XML、独立磁盘、网络和大页资源，保存为 `/home/vm-npu-manual.xml`，将虚拟机名称设为 `vm-npu-manual`。

基础 XML 暂不包含 UB controller、UB hostdev 或 `ubmem_vmmu` 参数。

## 2. 绑定设备并保存返回信息

查询可用 NPU：

```bash
ubdmctl npu list
```

选择未绑定的设备，例如 `1-1`，执行绑定：

```bash
ubdmctl npu bind --upi 0x000f --ids 1-1
```

如需同时申请配套 Hi1825 网卡 VFE，增加 `--need_nic true`。参数说明见[CLI 使用指南](cli_userguide.md)。

保存本次返回的 `bus_guid`、`tid`、`uba`、`size` 和完整的 `devices` 列表。以下是单设备返回数据示例：

```json
{
  "bus_guid": "cc08-ffff-0-0-000000-011101ff7e2c0080",
  "tid": 1,
  "uba": 3298534883328,
  "size": 49152,
  "devices": [
    {
      "id": "1-1",
      "guid": "cc08-a002-0-2-000000-0000008405480188",
      "type": "NPU"
    }
  ]
}
```

后续示例只说明上述数据如何映射到 XML，所有分配值必须替换为本次绑定的实际回显。

## 3. 配置字段对应关系

| 来源 | XML 配置 | 填写方法                                                            |
| --- | --- |-----------------------------------------------------------------|
| `bus_guid` | UB controller 的 `source/businstance/@guid` | 原样填写本次绑定返回值。                                                    |
| `bus_guid` | UB controller 的 `address/@guid` | 将 `bus_guid` 按 `-` 分隔后的第 4 段改为 `2`，其他段保持不变，与当前 UBDM XML 生成逻辑一致。 |
| `devices` 的条目数 | UB controller 的 `ports/@num` | 填写实际直通的设备条目数；申请配套网卡时，`NIC_VFE`条目数也要计入。                          |
| 每个 `devices[i].guid` | 对应 hostdev 的 `source/address/@guid` 和 `address/@guid` | 两处均使用该条设备的 GUID，不使用设备 ID 或 bus instance GUID。                   |
| 设备顺序 `i`，从 0 开始 | hostdev 的 `address/@eid` | 从 `0x2` 开始依次递增；controller 已使用 `0x1`。                            |
| 设备顺序 `i`，从 0 开始 | hostdev 的 `ports/port/@tport` | 从 `0` 开始依次递增。`index` 固定为 `0`，`teid` 固定为 `0x1`。                  |
| 本例固定值 `1` | `<iommufds>` 和 hostdev 的 `iommufd` 属性 | 两处必须对应。该值不等同于绑定回显中的 `tid`。                                      |
| `tid`、`uba`、`size` | `ubmem_vmmu` 的同名参数 | 绑定接口返回的uba,tid,size                                             |
| 当前宿主机 SMBIOS Type 131 | `sysinfo/type131/entry` | 从当前宿主机读取，并根据返回值根据规则进行转换，参考 SMBIOS Type 131 说明                   |

## 4. 配置 UB 直通与 SMBIOS Type 131

以下节点添加到同一份 `/home/vm-npu-manual.xml` 中。已有相应节点时，应修改或合并，不要重复添加同一控制器、设备或参数。

### 4.1 声明命名空间和 IOMMUFD

在根节点声明 `qemu` 命名空间：

```xml
<domain type='kvm' xmlns:qemu='http://libvirt.org/schemas/domain/qemu/1.0'>
```

在 `<domain>` 内、与 `<devices>` 同级添加：

```xml
<iommufds>1</iommufds>
```

### 4.2 添加 UB controller、hostdev 和 UMMU

在已有的 `<devices>` 中添加以下内容，保留磁盘、网络和控制台等原有配置：

```xml
<controller type='ub' index='0'>
  <model name='ubc'/>
  <ports num='1'/>
  <alias name='ubc.0'/>
  <source>
    <businstance guid='cc08-ffff-0-0-000000-011101ff7e2c0080'/>
  </source>
  <address type='ub' eid='0x1' guid='cc08-ffff-0-2-000000-011101ff7e2c0080'/>
</controller>

<hostdev mode='subsystem' type='ub' managed='yes' iommufd='1'>
  <driver name='vfio'/>
  <source>
    <address guid='cc08-a002-0-2-000000-0000008405480188'/>
  </source>
  <address type='ub' eid='0x2' guid='cc08-a002-0-2-000000-0000008405480188'/>
  <alias name='ub-0'/>
  <ports num='1'>
    <port index='0' teid='0x1' tport='0'/>
  </ports>
</hostdev>

<iommu model='ummu'/>
```

每条 `devices` 记录对应一个 hostdev，配套 `NIC_VFE` 使用相同的节点结构。多设备时，按返回列表顺序设置各自 GUID、递增的 `eid` 和 `tport`，使用不同的 alias，并同步调整 controller 的 `ports num`。例如返回 8 条设备时，controller 的端口数为 `8`，hostdev 的 `eid` 为 `0x2`～`0x9`，`tport` 为 `0`～`7`。

### 4.3 添加设备映射参数

在 `<domain>` 内、与 `<devices>` 同级添加：

```xml
<qemu:commandline>
  <qemu:arg value='-device'/>
  <qemu:arg value='ubmem_vmmu,tid=1,uba=3298534883328,size=49152'/>
</qemu:commandline>
```

将 `tid`、`uba`、`size` 替换为本次绑定返回值。如果已有其他 `qemu:commandline` 参数，应合并到同一节点中。

### 4.4 添加宿主机 Type 131 信息

#### 4.4.1 用途与前提

SMBIOS Type 131 是产品使用的 OEM 扩展记录，其中包含 `UB INFO` 及服务器组网信息。将这些信息提供给虚拟机后，配套 NPU 驱动安装程序可据此感知服务器类型，按产品规则选择 UB 或 PCIe 驱动安装路径。 
本节适用于 openEuler 24.03 LTS SP4 的配套环境，需要 libvirt 和 QEMU 已适配 Type 131 XML 字段及其客体呈现功能。 
使用 `ubdmctl vm create` 自动创建 NPU 直通虚拟机时，当前 UBDM 已负责读取 、解析并补充这些字段，用户无需手动添加。
仅绑定NPU进行XML配置的用户，需要对宿主机type 131信息字段进行转换，并进行xml字段补充；

#### 4.4.2 读取宿主机记录

在宿主机执行：

```bash
dmidecode -t 131 -u
```

以下为示例，实际字段应以当前宿主机回显为准：

```text
Handle 0x8300, DMI type 131, 77 bytes
OEM-specific Type
    Header and Data:
        83 4D 00 83 00 01 00 02 00 00 00 00 00 03 00 01
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
        00 00 00 00 00 00 00 00 00 00 00 00 00
    Strings:
        UB INFO
```

#### 4.4.3 字节与 XML 属性对应关系

偏移从 `Header and Data` 的第一个字节开始，以 0 为起点。原始数据以十六进制显示，多字节数值采用小端序，填写 XML 时转换为十进制字符串。

| 偏移 | 示例字节 | 字段 | 示例数值或含义 | XML 属性                            |
| --- | --- | --- | --- |-----------------------------------|
| `00h` | `83` | Type | `0x83`，即 Type 131。 | 不单独填写，由 `<type131>` 表示            |
| `01h` | `4D` | Length | 表总长度77字节 | 不填写                               |
| `02h`～`03h` | `00 83` | Handle | 表标识符 | 不填写                               |
| `04h` | `00` | Flag | 本例表示 BMC 信息获取成功；当前 UBDM 只接受值 `00`。 | `flag='0'`                        |
| `05h`～`06h` | `01 00` | PodId | `0x0001`，即组网 ID `1`。 | `pod_id='1'`                      |
| `07h` | `02` | SlotId | 服务器在组网内的 ID 为 `2`。 | `slot_id='2'`                     |
| `08h` | `00` | Mesh_Type | 本例对应 8P 智算场景，其他取值以产品定义为准。 | `mesh_type='0'`                   |
| `09h`～`0Ch` | `00 00 00 00` | SuperPodId | 预留字段| `super_pod_id='0'`                |
| `0Dh`～`0Eh` | `03 00` | ServedIndex | `0x0003`，即超节点全局编号 `3`。 | `served_index='3'`                |
| `0Fh` | `01` | TypeName | 字符串索引 `1`，指向字符串区的 `UB INFO`。 | 固定写`type_name='UB INFO'`，不填写索引数字。 |
| `10h`～`4Ch` | 全 `00` | Reserved | 61 字节预留数据。 | 当前 UBDM 不映射到 XML 属性。              |

例如 `PodId` 的 `01 00` 应按小端序解释为 `0x0001`，转换为十进制 `1`。本例 `Mesh_Type` 的原始值为 `00`，对应 `mesh_type='0'`。


#### 4.4.4 填写 XML

根据上述回显，在 `<domain>` 内添加以下 SMBIOS 信息，并在已有的 `<os>` 中设置 `smbios` 模式。

```xml
<domain type='kvm'>
  <sysinfo type='smbios'>
    <type131>
      <entry flag='0' pod_id='1' slot_id='2' mesh_type='0'
             super_pod_id='0' served_index='3' type_name='UB INFO'/>
    </type131>
  </sysinfo>
  <os>
    <smbios mode='sysinfo'/>
  </os>
</domain>
```

如果已有 SMBIOS `sysinfo`，只合并其中的 `<type131>`，保留原有其他字段。不要替换整个 `<os>`，应保留已经验证的机器类型、固件、启动顺序等配置。

#### 4.4.5 在虚拟机内验证

定义并启动虚拟机后，在虚拟机内使用 root 用户执行：

```bash
dmidecode -t 131 -u
```

确认存在 Type 131 记录，并核对 Flag、PodId、SlotId、Mesh_Type、SuperPodId、ServedIndex 和 `UB INFO` 是否与所填写的宿主机字段对应。数值按小端序解释，不要求 Handle、预留区等未映射内容逐字节一致。

仅在宿主机执行 `virsh dumpxml` 看见 `<type131>`，还不能代替虚拟机内的实际读取验证。如果虚拟机内没有记录，应先检查 XML 配置和配套 libvirt/QEMU 是否支持该扩展；如果记录正确但驱动安装路径仍不符合预期，应检查 HDK 版本和产品配套要求。

#### 4.4.6 OpenStack 场景说明

本小节仅适用于已集成 Type 131 读取与 XML 注入适配的 Nova 版本，UBDM 手动创建流程无需配置 Nova。Nova 必须具有读取宿主机记录并生成相应 XML 的代码，libvirt/QEMU 也必须支持 Type 131 扩展；仅新增 rootwrap 过滤规则，不会使未经适配的 Nova 自动注入 Type 131。

按所提供的配套 Nova 部署方式，可配置读取命令的 rootwrap 授权：

```bash
mkdir -p /etc/nova/rootwrap.d
vi /etc/nova/rootwrap.d/ubs_dmidecode.filters
```

写入以下内容：

```ini
[Filters]
dmidecode: CommandFilter, dmidecode, root
```

确认宿主机已安装 `dmidecode`，Nova 的 rootwrap 配置包含上述过滤文件目录，且命令调用方式与配套适配一致。保存后，在允许重启计算服务的维护时段执行：

```bash
systemctl restart openstack-nova-compute
```

以上服务名称以配套部署为例。后续新建虚拟机时，由适配后的 Nova 读取并注入 Type 131；已有虚拟机不会仅因重启计算服务而自动更新。创建后仍应按第 4.4.5 节在虚拟机内验证。

## 5. 定义、启动和验证

保存 XML 后执行：

```bash
virsh define /home/vm-npu-manual.xml
virsh start vm-npu-manual
virsh list --all
virsh dumpxml vm-npu-manual
ubdmctl npu list
```

确认虚拟机运行、设备仍绑定到本次的 `bus_guid`，并登录虚拟机，按产品配套说明安装驱动、验证 NPU 功能。

## 6. 清理虚拟机与设备绑定

如果定义或启动失败，应先查看虚拟机及设备状态，修正后重试，或按本节清理，不要重复申请已经绑定的设备。

不再使用时，正常关闭虚拟机，并确认状态为 `shut off` 后取消定义：

```bash
virsh shutdown vm-npu-manual
virsh domstate vm-npu-manual
virsh undefine vm-npu-manual
```

如果虚拟机有独立 NVRAM，按提示选择保留或删除。

确认虚拟机不再使用设备后，使用绑定时保存的原始 `bus_guid` 解绑。将以下 `BUS_GUID` 替换为实际值：

```bash
ubdmctl npu unbind --bus_guid BUS_GUID
ubdmctl npu list
```
