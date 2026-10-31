# ubdmctl CLI 使用指南

## 1. 简介

`ubdmctl` 是 ub-device-manager 提供的命令行工具，通过本机服务完成虚拟机和 NPU 设备管理。

本文介绍以下命令：

| 命令 | 作用 |
| --- | --- |
| `ubdmctl npu list` | 查询本机 NPU 设备信息及绑定状态。 |
| `ubdmctl npu bind` | 申请 NPU 对应的 UB VFE，并绑定到 bus instance。 |
| `ubdmctl npu unbind` | 解绑指定 bus instance 下的相关 UB 设备。 |
| `ubdmctl vm create` | 根据基础 XML 和 NPU 设备需求，定义并启动虚拟机。 |
| `ubdmctl vm delete` | 删除虚拟机，并解绑其关联的 UB 设备。 |

本文命令默认使用 root 用户执行。使用前，应已完成软件安装和服务授权，并启动 ub-device-manager。

### 内核模块检查

确认输出中同时包含 `vfio_ub` 和 `ubmempfd`：

```bash
lsmod | grep -E '^(vfio_ub|ubmempfd)[[:space:]]'
```

未加载时执行：

```bash
modprobe vfio-ub
modprobe ubmempfd
```

### 使用方式选择

通常使用 `ubdmctl vm create` 创建虚拟机、`ubdmctl vm delete` 删除虚拟机，组件自动处理设备绑定和解绑。只有需要自行配置虚拟机 XML 时，才单独使用 `ubdmctl npu bind` 和 `ubdmctl npu unbind`。

## 2. 查看帮助

可逐级查看命令分组和具体参数：

```bash
ubdmctl --help
ubdmctl npu --help
ubdmctl vm --help
ubdmctl npu bind --help
ubdmctl vm create --help
```

命令参数名称中的单词使用下划线连接，例如 `--npu_ids`、`--xml_path`、`--need_nic`。

## 3. NPU 管理命令

### 3.1 查询 NPU

#### 功能

查询本机 NPU 对应的设备 ID、GUID 和 bus instance 绑定信息。

#### 命令

```bash
ubdmctl npu list
```

#### 返回字段

| 字段 | 说明 |
| --- | --- |
| `id` | NPU 设备 ID。 |
| `guid` | 该设备对应的 UB 设备 GUID。 |
| `bus_guid` | 已绑定的 bus instance GUID；为空表示当前未绑定。 |

创建虚拟机或绑定设备时，应使用本命令返回的设备 ID，不要直接使用 npu-smi 中的设备编号。

### 3.2 绑定 NPU

#### 功能

按指定 ID 或数量申请 NPU 对应的 UB 设备，通过 UBSE 创建相应的 VFE 和 bus instance，并将 VFE 绑定到 bus instance。

该命令只完成设备分配和绑定，不创建虚拟机。

#### 命令格式

按 ID 申请：

```bash
ubdmctl npu bind --upi UPI --ids IDS
```

按数量申请：

```bash
ubdmctl npu bind --upi UPI --count COUNT
```

#### 参数说明

| 参数 | 是否必填 | 说明 |
| --- | --- | --- |
| `--upi` | 是 | 用户隔离标识，十六进制字符串，范围为 `0x0001`～`0x0fff`。 |
| `--ids` | 与 `--count` 二选一 | 指定设备 ID，多个 ID 使用逗号分隔，不允许重复。 |
| `--count` | 与 `--ids` 二选一 | 自动选择的空闲 NPU 数量，支持 `1`、`2`、`4`、`8`。 |
| `--need_nic` | 否 | 是否同时申请 NPU 配套的 Hi1825 网卡 VFE，默认 `false`。 |

说明：

- 指定 ID 时，设备数量也只能为 1、2、4 或 8 个。
- NPU 与配套的 Hi1825 网卡一一对应。
- `--need_nic` 需要显式传入布尔值，例如 `--need_nic true`。

#### 使用示例

绑定一张 NPU：

```bash
ubdmctl npu bind \
  --upi 0x000f \
  --ids 1-1
```

绑定两张指定 NPU：

```bash
ubdmctl npu bind \
  --upi 0x000f \
  --ids 1-1,1-2
```

自动选择 8 张空闲 NPU：

```bash
ubdmctl npu bind \
  --upi 0x000f \
  --count 8
```

同时申请配套智能网卡 VFE：

```bash
ubdmctl npu bind \
  --upi 0x000f \
  --ids 1-1 \
  --need_nic true
```

#### 返回信息

| 字段 | 说明 |
| --- | --- |
| `tid` | ubmempfd 驱动内存映射配置参数之一，用于补充虚拟机 XML。 |
| `uba` | ubmempfd 驱动内存映射配置参数之一，用于补充虚拟机 XML。 |
| `size` | ubmempfd 驱动内存映射配置参数之一，用于补充虚拟机 XML。 |
| `bus_guid` | 本次分配的 bus instance GUID，用于 XML 配置和后续解绑。 |
| `devices` | 已绑定的设备列表，每项包含设备 ID、GUID 和类型。 |

设备绑定成功后，应保存本次返回结果，并使用这些信息补充虚拟机 XML，完成 UB 直通虚拟机的创建。

申请配套智能网卡后，`devices` 中还会包含相应的 `NIC_VFE`。

### 3.3 解绑 NPU

#### 功能

根据 bus instance GUID 查询其下绑定的相关 UB 设备，并调用 UBSE 释放绑定。

解绑范围按 bus instance 确定，不是按单个 NPU ID 确定。

#### 命令格式

```bash
ubdmctl npu unbind --bus_guid BUS_GUID
```

#### 参数说明

| 参数 | 是否必填 | 说明 |
| --- | --- | --- |
| `--bus_guid` | 是 | 绑定时返回的 bus instance GUID。 |

将 `BUS_GUID` 替换为绑定时返回的实际值。解绑后检查：

```bash
ubdmctl npu list
```

#### 注意事项

- 该命令不会自动停止虚拟机，也不会取消虚拟机定义。
- 解绑前，应确认相关设备不再被运行中的虚拟机使用。虚拟机运行期间，对其正在使用的 NPU VFE 执行解绑会失败。
- 通过 `ubdmctl vm create` 创建的虚拟机，通常使用 `ubdmctl vm delete` 完成删除和解绑，无需再单独执行本命令。

## 4. 虚拟机管理命令

### 4.1 创建虚拟机

#### 功能

根据用户提供的基础 XML 和设备需求创建虚拟机。

请求 NPU 时，组件完成设备分配和绑定，补充 UB 直通与宿主 SMBIOS Type 131 配置，再调用 libvirt 定义并启动虚拟机。

不请求 NPU 时，使用基础 XML 定义并启动普通虚拟机。

#### 命令格式

```bash
ubdmctl vm create \
  --xml_path XML_PATH \
  --upi UPI \
  --npu_ids IDS
```

#### 参数说明

| 参数 | 是否必填 | 说明 |
| --- | --- | --- |
| `--xml_path` | 与 `--xml_text` 二选一 | 服务端可读取的基础 XML 文件路径。 |
| `--xml_text` | 与 `--xml_path` 二选一 | 基础 XML 文本。 |
| `--upi` | 是 | 用户隔离标识，范围为 `0x0001`～`0x0fff`。创建普通虚拟机时也需要提供。 |
| `--npu_ids` | 否 | 指定 NPU 设备 ID，多个 ID 使用逗号分隔。 |
| `--npu_count` | 否 | 自动选择的空闲 NPU 数量，支持 `1`、`2`、`4`、`8`。 |
| `--need_nic` | 否 | 是否同时申请配套智能网卡 VFE，默认 `false`。 |
| `--affinity_policy` | 否 | 内存与 NPU 的亲和策略：`required`、`socket`、`preferred`。省略时保持原有设备选择行为。 |

参数要求：

- `--xml_path` 与 `--xml_text` 必须二选一。
- 请求 NPU 时，`--npu_ids` 与 `--npu_count` 二选一。
- 指定设备 ID 时，不允许重复，数量只能为 1、2、4 或 8 个。
- 创建普通虚拟机时，省略 `--npu_ids` 和 `--npu_count`，不要传入 `--npu_count 0`。
- `--need_nic true` 随 NPU 请求使用，不是独立申请网卡的入口。

#### 基础 XML 要求

用户需要准备 CPU、内存、NUMA、磁盘、固件和普通网络等配置。

输入 XML 不应包含已有的 UB controller、UB hostdev 或 `ubmem_vmmu` 参数，这些内容由组件自动补充。

虚拟机名称由 XML 中的 `<name>` 指定，无需额外传入名称参数。

#### 可选内存亲和策略

`--affinity_policy` 根据虚拟机内存绑定的宿主 NUMA 选择 NPU，仅支持与 `--npu_count` 配合使用；与 `--npu_ids` 同传时返回 400。省略时保持原有选卡方式。

- **NPU 亲和 NUMA**：与该 NPU 具有亲和关系的宿主 NUMA 节点。
- **NPU 亲和 Socket**：该 NPU 亲和 NUMA 所属的宿主 Socket。
- **虚拟机内存绑定 NUMA 集合 `N`**：虚拟机内存允许使用的所有宿主 NUMA 编号，重复编号只保留一次。
- **对应的 Socket 集合 `S`**：`N` 中各 NUMA 所属的 Socket 编号，重复编号只保留一次。

| 策略 | 选择规则 |
| --- | --- |
| `required` | 仅选择**亲和 NUMA 属于集合 `N`** 的空闲 NPU；满足条件的数量不足时失败。 |
| `socket` | 仅选择**亲和 Socket 属于集合 `S`** 的空闲 NPU；满足条件的数量不足时失败。 |
| `preferred` | 优先选择**亲和 Socket 属于集合 `S`** 的空闲 NPU，不足时从**亲和 Socket 不属于集合 `S`** 的其他空闲 NPU 补足；总量仍不足时失败。 |

`preferred` 不额外优先选择亲和 NUMA 属于 `N` 的 NPU。三种策略均不保证各 NUMA 或 Socket 的 NPU 数量均衡。

XML 要求：**全部内存使用大页，并明确指定允许使用的宿主 NUMA**（`placement='static'`、`mode='strict'`），支持多 NUMA 和跨 Socket；暂不支持附加内存设备（`devices/memory`）。

在基础 XML 的 `<domain>` 下添加或修改以下配置，保存为 `/home/vm-affinity.xml`。示例使用 2 MiB 大页、宿主 NUMA 1 和 2（`N={1,2}`），请按实际资源调整：

```xml
<memoryBacking>
  <hugepages>
    <page size='2' unit='MiB'/>
  </hugepages>
</memoryBacking>
<numatune>
  <memory mode='strict' placement='static' nodeset='1,2'/>
</numatune>
```

若分别配置了多个虚拟机 NUMA 节点的内存绑定，单独的 `memnode` 设置优先于通用的 `memory` 设置；`N` 包含各部分内存允许使用的宿主 NUMA 编号。`numatune` 的 `nodeset` 表示宿主 NUMA 编号，`hugepages/page` 的 `nodeset` 表示虚拟机内部的 NUMA 编号。

按 `preferred` 策略自动选择 4 张 NPU：

```bash
ubdmctl vm create \
  --xml_path /home/vm-affinity.xml \
  --upi 0x000f \
  --npu_count 4 \
  --affinity_policy preferred
```

同时指定 `--need_nic true` 时，仅从有空闲配套 NIC_VFE 的 NPU 中选择。错误码见[本文常见错误码](#7-常见错误码)。

#### 使用示例

使用指定 NPU 创建虚拟机：

```bash
ubdmctl vm create \
  --xml_path /home/vm-base.xml \
  --upi 0x000f \
  --npu_ids 1-1
```

使用两张指定 NPU 创建虚拟机：

```bash
ubdmctl vm create \
  --xml_path /home/vm-base.xml \
  --upi 0x000f \
  --npu_ids 1-1,1-2
```

自动选择 8 张空闲 NPU：

```bash
ubdmctl vm create \
  --xml_path /home/vm-base.xml \
  --upi 0x000f \
  --npu_count 8
```

同时申请配套智能网卡 VFE：

```bash
ubdmctl vm create \
  --xml_path /home/vm-base.xml \
  --upi 0x000f \
  --npu_count 8 \
  --need_nic true
```

创建不带 NPU 的普通虚拟机：

```bash
ubdmctl vm create \
  --xml_path /home/vm-base.xml \
  --upi 0x000f
```

创建后检查：

```bash
virsh list --all
ubdmctl npu list
```

### 4.2 删除虚拟机

#### 功能

按名称删除虚拟机。

对于 NPU 直通虚拟机，组件会停止运行中的虚拟机、取消定义，并解绑其 bus instance 下的相关 UB 设备。

#### 命令格式

```bash
ubdmctl vm delete VM_NAME
```

虚拟机名称是位置参数，不使用 `--name`。

#### 使用示例

删除名称为 `vm-base` 的虚拟机：

```bash
ubdmctl vm delete vm-base
```

检查结果：

```bash
virsh list --all
ubdmctl npu list
```

#### 注意事项

- 删除运行中的虚拟机会强制停止虚拟机，建议先保存业务数据，再执行 `ubdmctl vm delete`。
- 仅使用 `virsh` 取消虚拟机定义，不会调用 UBSE 释放设备绑定。

## 5. 调试输出

业务命令支持 `--verbose` 或 `-v`，用于显示请求参数和更详细的错误信息。

例如：

```bash
ubdmctl npu list --verbose
```

```bash
ubdmctl vm create \
  --xml_path /home/vm-base.xml \
  --upi 0x000f \
  --npu_ids 1-1 \
  --verbose
```

`--verbose` 是具体业务命令的选项，应放在子命令后使用。

命令执行成功时显示成功提示，失败时显示错误信息并返回非零退出码。

## 6. 两种虚拟机操作方式

### 6.1 自动创建与删除

使用 `ubdmctl npu list` 查看设备，使用 `ubdmctl vm create` 创建虚拟机，使用 `ubdmctl vm delete` 删除虚拟机并解绑设备。参数和示例见[虚拟机管理命令](#4-虚拟机管理命令)。

### 6.2 手动绑定与创建

适用于需要自行配置 UB XML 的场景，操作流程如下：

1. 使用 `ubdmctl npu bind` 申请并绑定设备。
2. 保存返回的 bus instance GUID、设备 GUID 和映射参数。
3. 根据返回信息，手动补充基础 XML。
4. 使用 `virsh define` 和 `virsh start` 定义并启动虚拟机。
5. 不再使用时，先停止虚拟机、取消定义，再执行 `ubdmctl npu unbind` 解绑设备。

手动绑定后的设备，不要再次通过 `ubdmctl vm create` 请求；已经补充 UB 配置的 XML，也不要提交给自动创建接口。

绑定回显字段与 XML 的对应关系、UB 直通设备和 SMBIOS Type 131 的配置，以及手动创建步骤见[NPU 直通虚拟机 XML 配置指南](./npu_passthrough_xml.md)。由组件自动创建的流程见[快速开始](./quick_start.md)。

## 7. 常见错误码

命令失败时会显示服务端返回的错误码和错误信息，并以非零退出码结束。

| 错误码 | 说明 |
| --- | --- |
| 400 | 业务参数、XML 结构或内存绑定格式不符合要求，例如亲和策略与 `npu_ids` 同传、缺少 `npu_count`、缺少大页或内存绑定、绑定模式不受支持、`nodeset` 格式错误。 |
| 422 | 请求参数类型、格式或枚举值校验失败。 |
| 500 | 未被单独转换的内部错误。 |
| 503 | NPU 亲和拓扑信息不可用。 |
| 531 | 虚拟机删除失败。 |
| 532 | libvirt 连接失败。 |
| 533 | 虚拟机信息查询失败。 |
| 534 | 设备解绑失败。 |
| 543 | XML 构造相关错误或 Type 131 获取失败。 |
| 544 | 虚拟机定义或启动失败。 |
| 545 | 设备选择失败，包括符合亲和策略的空闲 NPU 不足、指定设备不存在或已绑定，以及 XML 内存绑定引用了宿主不存在的 NUMA（需修正 XML）。 |
| 548 | 指定 bus instance 下未找到可解绑设备。 |
