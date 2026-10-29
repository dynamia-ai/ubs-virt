# ub-device-manager HTTP API 使用指南

## 1. 简介

ub-device-manager 通过本机 Unix Domain Socket 提供 HTTP API，用于虚拟机和 NPU 设备管理。

默认 Socket 路径：

```text
/var/run/ub_device_manager/ub_device_manager.sock
```

当前接口不监听 TCP 端口。示例中的 `http://localhost` 只是 HTTP 请求中的逻辑地址，实际连接通过 Unix Domain Socket 建立。

本文介绍以下业务接口：

| 方法 | 路径 | 作用 |
| --- | --- | --- |
| GET | `/npu` | 查询本机 NPU 设备信息。 |
| POST | `/npu/bind` | 申请并绑定 NPU 对应的 UB 设备。 |
| POST | `/npu/unbind` | 解绑指定 bus instance 下的相关设备。 |
| POST | `/vm/create` | 定义并启动虚拟机。 |
| DELETE | `/vm/{name}` | 删除虚拟机并解绑相关 UB 设备。 |

## 2. 调用前提

- 已启动 ub-device-manager。
- 调用用户具有 Socket 访问权限。
- 已完成 UBSE、libvirt 及相关驱动环境的部署。

本文使用 curl 演示调用。curl 需要支持 `--unix-socket`。

示例使用 `--noproxy '*'`，使本机 UDS 请求不经过网络代理。

创建、绑定、删除等操作会等待服务端执行结果，不返回后台任务 ID。

## 3. 查询 NPU

### 接口

```text
GET /npu
```

无请求参数。

### 调用示例

```bash
curl --noproxy '*' \
  --unix-socket /var/run/ub_device_manager/ub_device_manager.sock \
  http://localhost/npu
```

### 成功响应

HTTP 状态码为 `200`，响应体为数组。以下仅为格式示例：

```json
[
  {
    "id": "1-1",
    "guid": "cc08-a002-0-2-000000-000000e70e940189",
    "bus_guid": ""
  }
]
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `id` | string | NPU 设备 ID。 |
| `guid` | string | 对应的 UB 设备 GUID。 |
| `bus_guid` | string 或 null | 已绑定的 bus instance GUID；未绑定时可能为空字符串或 null。 |

请求设备时使用 `id`，不要直接使用 npu-smi 中的设备编号。

## 4. 绑定 NPU

### 接口

```text
POST /npu/bind
```

### 请求参数

| 参数 | JSON 类型 | 是否必填 | 说明 |
| --- | --- | --- | --- |
| `upi` | string | 是 | 十六进制用户隔离标识，范围为 `"0x0001"`～`"0x0fff"`。 |
| `ids` | array[string] | 与 `count` 二选一 | 设备 ID 列表，不允许重复。 |
| `count` | integer | 与 `ids` 二选一 | 自动选择的空闲 NPU 数量，支持 1、2、4、8。 |
| `need_nic` | boolean | 否 | 是否同时申请配套 Hi1825 网卡 VFE，默认 false。 |

指定 ID 时，数量也只能为 1、2、4 或 8 个。

### 调用示例

申请两张指定 NPU，并申请配套智能网卡 VFE：

```bash
curl --noproxy '*' \
  --unix-socket /var/run/ub_device_manager/ub_device_manager.sock \
  -X POST \
  -H 'Content-Type: application/json' \
  -d '{
    "upi": "0x000f",
    "ids": ["1-1", "1-2"],
    "need_nic": true
  }' \
  http://localhost/npu/bind
```

按数量自动选择：

```bash
curl --noproxy '*' \
  --unix-socket /var/run/ub_device_manager/ub_device_manager.sock \
  -X POST \
  -H 'Content-Type: application/json' \
  -d '{
    "upi": "0x000f",
    "count": 8
  }' \
  http://localhost/npu/bind
```

### 成功响应

HTTP 状态码为 `200`，响应体为对象：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `tid` | integer | 用于虚拟机 XML 的设备映射参数。 |
| `uba` | integer | 用于虚拟机 XML 的设备映射参数。 |
| `size` | integer | 用于虚拟机 XML 的映射大小参数。 |
| `bus_guid` | string | 本次分配的 bus instance GUID。 |
| `devices` | array[object] | 已绑定的设备列表。 |

`devices` 中每个对象包含：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `id` | string | 设备 ID。 |
| `guid` | string | 设备对应的 UB GUID。 |
| `type` | string | 设备类型，例如 `NPU`、`NIC_VFE`。 |

该接口通过 UBSE 创建、分配并绑定相关 VFE，但不创建虚拟机。调用方应保存返回信息，用于手动配置 XML 和后续解绑。

## 5. 解绑 NPU

### 接口

```text
POST /npu/unbind
```

### 请求参数

| 参数 | 类型 | 是否必填 | 说明 |
| --- | --- | --- | --- |
| `bus_guid` | string | 是 | 绑定时返回的 bus instance GUID，不能为空。 |

### 调用示例

将 `BUS_GUID` 替换为实际值：

```bash
curl --noproxy '*' \
  --unix-socket /var/run/ub_device_manager/ub_device_manager.sock \
  -X POST \
  -H 'Content-Type: application/json' \
  -d '{
    "bus_guid": "BUS_GUID"
  }' \
  http://localhost/npu/unbind
```

### 成功响应

```json
{
  "msg": "Unbind NPU devices successfully"
}
```

该接口按 bus instance 解绑相关设备，不会自动停止虚拟机或取消定义。调用前应确认设备不再被运行中的虚拟机使用。

## 6. 创建虚拟机

### 接口

```text
POST /vm/create
```

### 请求参数

| 参数 | JSON 类型 | 是否必填 | 说明 |
| --- | --- | --- | --- |
| `xml_path` | string | 与 `xml_text` 二选一 | 服务端可读取的基础 XML 文件路径。 |
| `xml_text` | string | 与 `xml_path` 二选一 | 基础 XML 文本。 |
| `upi` | string | 是 | 用户隔离标识，范围为 `"0x0001"`～`"0x0fff"`。 |
| `npu_ids` | array[string] | 否 | 指定设备 ID 列表。 |
| `npu_count` | integer | 否 | 自动选择的 NPU 数量，支持 1、2、4、8。 |
| `need_nic` | boolean | 否 | 是否同时申请配套智能网卡 VFE，默认 false。 |

参数要求：

- XML 文本和路径必须二选一。
- 请求 NPU 时，`npu_ids` 与 `npu_count` 二选一。
- 指定 ID 时，不允许重复，数量只能为 1、2、4 或 8 个。
- 创建普通虚拟机时，省略 `npu_ids` 和 `npu_count`，不要传入 0。
- 基础 XML 不应包含已有的 UB controller、UB hostdev 或 `ubmem_vmmu` 参数。

### 调用示例

使用指定 NPU 创建虚拟机：

```bash
curl --noproxy '*' \
  --unix-socket /var/run/ub_device_manager/ub_device_manager.sock \
  -X POST \
  -H 'Content-Type: application/json' \
  -d '{
    "xml_path": "/home/vm-base.xml",
    "upi": "0x000f",
    "npu_ids": ["1-1", "1-2"]
  }' \
  http://localhost/vm/create
```

自动选择 8 张 NPU，并申请配套智能网卡：

```bash
curl --noproxy '*' \
  --unix-socket /var/run/ub_device_manager/ub_device_manager.sock \
  -X POST \
  -H 'Content-Type: application/json' \
  -d '{
    "xml_path": "/home/vm-base.xml",
    "upi": "0x000f",
    "npu_count": 8,
    "need_nic": true
  }' \
  http://localhost/vm/create
```

创建普通虚拟机：

```bash
curl --noproxy '*' \
  --unix-socket /var/run/ub_device_manager/ub_device_manager.sock \
  -X POST \
  -H 'Content-Type: application/json' \
  -d '{
    "xml_path": "/home/vm-base.xml",
    "upi": "0x000f"
  }' \
  http://localhost/vm/create
```

使用 `xml_text` 时，将完整 XML 作为 JSON 字符串传入，并正确转义其中的双引号和换行。

### 成功响应

```json
{
  "msg": "Create VM successfully"
}
```

接口完成设备分配、绑定、XML 补充及 libvirt 定义和启动。虚拟机名称由基础 XML 中的 `<name>` 指定。

## 7. 删除虚拟机

### 接口

```text
DELETE /vm/{name}
```

### 路径参数

| 参数 | 类型 | 说明 |
| --- | --- | --- |
| `name` | string | 虚拟机名称。 |

### 调用示例

```bash
curl --noproxy '*' \
  --unix-socket /var/run/ub_device_manager/ub_device_manager.sock \
  -X DELETE \
  http://localhost/vm/vm-base
```

### 成功响应

```json
{
  "msg": "Deleted vm-base successfully."
}
```

删除运行中的虚拟机会强制停止虚拟机。接口会取消定义并解绑相关 UB 设备，但不删除磁盘文件。

## 8. 响应与错误处理

### 8.1 查看状态码和响应头

curl 增加 `-i`，可以同时查看 HTTP 状态码、响应头和响应体：

```bash
curl --noproxy '*' \
  --unix-socket /var/run/ub_device_manager/ub_device_manager.sock \
  -i \
  http://localhost/npu
```

请求通常包含以下响应头：

| 响应头 | 说明 |
| --- | --- |
| `X-Request-ID` | 请求标识，可用于关联服务端日志。 |
| `X-Process-Time` | 服务端处理耗时，单位为秒。 |

curl 默认不会仅因 HTTP 4xx/5xx 状态而返回失败退出码。

### 8.2 常见状态码

| 状态码 | 说明 |
| --- | --- |
| 200 | 操作成功。 |
| 400 | 业务参数或 XML 输入不符合要求。 |
| 422 | 请求参数类型、格式或枚举值校验失败。 |
| 500 | 未被单独转换的内部错误。 |
| 531 | 虚拟机删除失败。 |
| 532 | libvirt 连接失败。 |
| 533 | 虚拟机信息查询失败。 |
| 534 | 设备解绑失败。 |
| 543 | XML 构造相关错误或 Type 131 获取失败。 |
| 544 | 虚拟机定义或启动失败。 |
| 545 | 设备选择失败。 |
| 548 | 指定 bus instance 下未找到可解绑设备。 |

其中部分状态码为本项目自定义状态码。


## 9. 使用注意事项

- 本文只介绍 VM 和 NPU 管理接口。
- 请求中的 `upi` 使用 JSON 字符串；`count`、`npu_count` 使用整数；`need_nic` 使用布尔值。
- 手动绑定设备后，不要再次通过自动创建接口请求同一设备。
- 请求超时或连接中断后，应先检查资源状态和日志，不要立即重复提交创建或绑定操作。