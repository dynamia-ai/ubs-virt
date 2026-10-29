# ub-device-manager Python SDK 使用指南

## 1. 简介

ub-device-manager 提供 Python SDK，进行虚拟机创建和 NPU 设备管理。

SDK 包路径：

```python
ub_device_manager.ub_dm_sdk
```

本文介绍以下接口：

| 接口 | 作用 |
| --- | --- |
| `get_host_npu_device()` | 查询本机 NPU 设备信息及绑定状态。 |
| `bind_npu_device()` | 申请 NPU 对应的 UB VFE，并绑定到 bus instance。 |
| `unbind_npu_device()` | 解绑指定 bus instance 下的相关 UB 设备。 |
| `create_vm()` | 根据基础 XML 和设备需求，定义并启动虚拟机。 |
| `delete_vm()` | 删除虚拟机，并解绑其关联的 UB 设备。 |

SDK 使用同步调用方式。函数调用会等待服务端返回结果，不会返回后台任务 ID。

普通用户可以直接使用 `create_vm()` 和 `delete_vm()` 完成虚拟机创建与删除，无需提前单独绑定设备。

`bind_npu_device()` 和 `unbind_npu_device()` 适用于需要自行配置 UB XML 或集成自定义设备管理流程的开发者。

## 2. 使用前提

- 已部署启动 ub-device-manager。
- 调用程序使用 Python 3.11，且能够导入 SDK。
- 调用用户具有 SDK 文件和服务 Socket 的访问权限。

检查 SDK 是否可导入：

```bash
python3.11 -c "import ub_device_manager.ub_dm_sdk; print('SDK available')"
```


## 3. 创建客户端

```python
from ub_device_manager.ub_dm_sdk import UbDeviceManagerClient

client = UbDeviceManagerClient(
    uds_path="/var/run/ub_device_manager/ub_device_manager.sock",
    timeout=1200.0,
)
```

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `uds_path` | 默认 Socket 路径 | 服务端 Unix Domain Socket 路径。 |
| `timeout` | `10.0` | HTTP 请求超时配置，单位为秒。创建、删除等耗时操作可适当调大。 |

未显式指定 `uds_path` 时，SDK 优先使用环境变量 `UBDM_UDS_PATH`，否则使用默认路径。

以下示例均复用上述 `client`。

## 4. NPU 管理接口

### 4.1 查询 NPU

#### 接口

```python
get_host_npu_device(client=None)
```

#### 示例

```python
from ub_device_manager.ub_dm_sdk import get_host_npu_device

devices = get_host_npu_device(client=client)

for device in devices:
    print(device.id, device.guid, device.bus_guid)
```

返回 `NpuDeviceInfo` 对象列表：

| 字段 | 说明 |
| --- | --- |
| `id` | NPU 设备 ID。 |
| `guid` | 对应的 UB 设备 GUID。 |
| `bus_guid` | 已绑定的 bus instance GUID；未绑定时可能为 `None` 或空字符串。 |

创建或绑定时，使用本接口返回的设备 ID，不要直接使用 npu-smi 中的设备编号。

### 4.2 绑定 NPU

#### 接口

```python
bind_npu_device(
    upi,
    ids=None,
    count=None,
    need_nic=False,
    client=None,
)
```

#### 参数

| 参数 | 是否必填 | 说明 |
| --- | --- | --- |
| `upi` | 是 | 十六进制用户隔离标识，范围为 `"0x0001"`～`"0x0fff"`。 |
| `ids` | 与 `count` 二选一 | 设备 ID 字符串列表，不允许重复。 |
| `count` | 与 `ids` 二选一 | 自动选择的空闲 NPU 数量，支持 1、2、4、8。 |
| `need_nic` | 否 | 是否同时申请配套 Hi1825 网卡 VFE，默认 `False`。 |
| `client` | 否 | 客户端对象；省略时使用默认客户端。 |

指定 ID 时，数量也只能为 1、2、4 或 8 个。

#### 示例

申请两张指定 NPU，并申请配套Hi1825网卡 VFE：

```python
from ub_device_manager.ub_dm_sdk import bind_npu_device

tid, uba, size, bus_guid, devices = bind_npu_device(
    upi="0x000f",
    ids=["1-1", "1-2"],
    need_nic=True,
    client=client,
)

print("tid:", tid)
print("uba:", uba)
print("size:", size)
print("bus_guid:", bus_guid)

for device in devices:
    print(device.id, device.guid, device.type)
```

按数量自动选择：

```python
tid, uba, size, bus_guid, devices = bind_npu_device(
    upi="0x000f",
    count=8,
    client=client,
)
```

#### 返回值

按以下顺序返回元组：

```text
(tid, uba, size, bus_guid, devices)
```

| 返回值 | 说明 |
| --- | --- |
| `tid`、`uba`、`size` | 用于补充虚拟机 XML 的设备映射参数。 |
| `bus_guid` | 本次分配的 bus instance GUID。 |
| `devices` | `BoundUbDevice` 对象列表，每项包含 `id`、`guid`、`type`。 |

该接口不创建虚拟机。调用成功后，应保存返回信息，用于手动配置 XML 和后续解绑。

### 4.3 解绑 NPU

#### 接口

```python
unbind_npu_device(bus_guid, client=None)
```

#### 示例

```python
from ub_device_manager.ub_dm_sdk import unbind_npu_device

unbind_npu_device(
    bus_guid="BUS_GUID",
    client=client,
)
```

`BUS_GUID` 替换为绑定时返回的实际值。

该函数成功时返回 `None`。

解绑范围按 bus instance 确定，不是按单个 NPU ID 确定。解绑前，应确认相关设备不再被运行中的虚拟机使用。

## 5. 虚拟机管理接口

### 5.1 创建虚拟机

#### 接口

```python
create_vm(
    upi,
    xml_text=None,
    xml_path=None,
    need_nic=False,
    npu_ids=None,
    npu_count=None,
    client=None,
    *,
)
```

#### 参数

| 参数 | 是否必填 | 说明 |
| --- | --- | --- |
| `upi` | 是 | 用户隔离标识，范围为 `"0x0001"`～`"0x0fff"`。 |
| `xml_text` | 与 `xml_path` 二选一 | 基础 XML 文本。 |
| `xml_path` | 与 `xml_text` 二选一 | 服务端可读取的基础 XML 文件路径。 |
| `need_nic` | 否 | 是否同时申请配套Hi1825网卡 VFE。 |
| `npu_ids` | 否 | NPU 设备 ID 字符串列表。 |
| `npu_count` | 否 | 自动选择的 NPU 数量，支持 1、2、4、8。 |
| `client` | 否 | 客户端对象。 |
| `affinity_policy` | 否 | 可选设备亲和性策略，支持 `"required"`、`"socket"`、`"preferred"`。 |

参数要求：

- `xml_text` 与 `xml_path` 必须二选一。
- 请求 NPU 时，`npu_ids` 与 `npu_count` 二选一。
- 指定设备 ID 时，不允许重复，数量只能为 1、2、4 或 8 个。
- 创建普通虚拟机时，省略 `npu_ids` 和 `npu_count`。
- `affinity_policy` 与 `npu_count` 配合使用，其前提和含义见 CLI 使用指南。
- 输入基础 XML 不应包含已有的 UB controller、UB hostdev 或 `ubmem_vmmu` 参数。

#### 示例

使用指定 NPU 创建虚拟机：

```python
from ub_device_manager.ub_dm_sdk import create_vm

result = create_vm(
    upi="0x000f",
    xml_path="/home/vm-base.xml",
    npu_ids=["1-1", "1-2"],
    client=client,
)

print(result)
```

自动选择 8 张 NPU，并申请配套Hi1825网卡：

```python
result = create_vm(
    upi="0x000f",
    xml_path="/home/vm-base.xml",
    npu_count=8,
    need_nic=True,
    client=client,
)
```

使用 XML 文本：

```python
from pathlib import Path

xml_text = Path("/home/vm-base.xml").read_text(encoding="utf-8")

result = create_vm(
    upi="0x000f",
    xml_text=xml_text,
    npu_count=2,
    client=client,
)
```

创建普通虚拟机：

```python
result = create_vm(
    upi="0x000f",
    xml_path="/home/vm-base.xml",
    client=client,
)
```

成功时返回字典：

```python
{"msg": "Create VM successfully"}
```

### 5.2 删除虚拟机

#### 接口

```python
delete_vm(vm_name, client=None)
```

#### 示例

```python
from ub_device_manager.ub_dm_sdk import delete_vm

result = delete_vm(
    vm_name="vm-base",
    client=client,
)

print(result)
```

当前实现返回服务端解析后的响应：

```python
{"msg": "Deleted vm-base successfully."}
```

删除运行中的虚拟机会强制停止虚拟机。删除会取消定义并解绑相关 UB 设备，但不删除磁盘文件。

## 6. 使用说明

- `ids`、`npu_ids` 使用 Python 字符串列表，不使用逗号拼接的单个字符串。
- `need_nic` 使用 Python 布尔值 `True` 或 `False`。
- 手动绑定设备后，使用返回信息配置 XML，并通过 libvirt 创建虚拟机；不要再请求自动创建接口分配同一设备。