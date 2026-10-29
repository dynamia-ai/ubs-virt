# ub-device-manager 设计说明

本文说明 ub-device-manager 的整体架构、框架选型、对外接口实现、UBS Engine（UBSE）调用，以及设备返回值到虚拟机 XML 的转换流程，供开发和维护人员理解当前实现。

本文以当前源码中的 NPU 与配套 Hi1825 网卡直通流程为主。服务同时注册了 SSU 应用，但SSU相关功能还在开发阶段，本文不展开讨论。NPU直通的操作步骤见[快速开始](./quick_start.md)，手动填写 XML 的方法见[NPU 直通虚拟机 XML 配置指南](./npu_passthrough_xml.md)。

## 1. 设计目标

组件接收用户准备的基础虚拟机 XML，将设备选择、UBSE 分配、直通 XML 生成和 libvirt 虚拟机管理连接起来，对外提供统一的查询、绑定、创建和删除入口。

| 参与方 | 职责                                                 |
| --- |----------------------------------------------------|
| 调用方 | 提供基础 XML、UPI 和设备需求；准备 CPU、内存、磁盘、固件、网络和大页等资源。       |
| ub-device-manager | 校验请求、选择设备、编排外部调用、生成 UB 和 Type 131 配置、处理异常。         |
| UBSE | 提供宿主 UB 设备信息，分配和绑定设备，提供 bus instance 及设备，释放绑定。 |
| libvirt | 接收完整 XML，定义、启动、查询和删除虚拟机。                           |
| 配套 QEMU 与内核驱动 | 实现 UB、UMMU、设备映射和虚拟机运行能力。                           |

## 2. 整体架构与框架

![ub-device-manager 整体架构](./images/architecture.png)

### 2.1 框架与用途

| 框架或机制 | 当前用途 |
| --- | --- |
| Python 3.11、`asyncio` | 组织异步请求和任务；使用 `asyncio.to_thread` 调用同步的 UBSE、libvirt 和 SMBIOS 读取逻辑。 |
| FastAPI、Uvicorn | 定义 HTTP 路由并运行 ASGI 服务，通过本地 Unix Domain Socket 接收请求。 |
| Pydantic | 定义请求、响应和内部数据模型，校验类型、枚举及字段格式。跨字段和设备状态校验由业务层补充。 |
| 自定义 `AsyncTaskChain` | 将一次操作拆为顺序执行的任务，共享请求上下文并支持命名锁和异常处理。 |
| HTTPX | SDK 和 CLI 的同步 HTTP 客户端，通过 UDS 访问服务。 |
| Typer、Rich、PyYAML | 根据本地 OpenAPI 契约生成 CLI，解析命令参数并展示表格、树形结果和错误信息。 |
| defusedxml、ElementTree | 前者解析并检查输入 XML，后者构建和序列化输出 XML。 |
| Loguru、ContextVar | 输出日志，并在异步请求中传递请求标识和任务上下文。 |
| systemd、TOML | 管理服务进程和运行参数。 |

`AsyncTaskChain` 在当前 HTTP 请求内执行，接口等待任务完成后返回结果。它不是后台任务队列，不产生可供后续轮询的任务 ID，也没有持久化任务状态。

### 2.2 模块分工

| 模块 | 主要职责 |
| --- | --- |
| `run.py` | 初始化日志和 UDS，启动 Uvicorn，设置 Socket 权限。 |
| `common/factory.py` | 合并 VM、NPU、SSU 路由，注册请求标识、耗时和异常处理中间件。 |
| `app/` | 声明接口模型及路由，组装对应任务链。 |
| `domain/npu/` | 设备库存转换、设备选择、UBSE 分配与释放。 |
| `domain/vm/` | 读取基础 XML、转换 UB 配置、解析 Type 131、调用 libvirt。 |
| `common/async_task_framework.py` | 任务生命周期、请求上下文、命名锁及退出清理。 |
| `ub_dm_sdk/` | 提供 Python 调用函数、客户端模型及 UDS HTTP 客户端。 |
| `ub_device_manager_cli.py`、`openapi.yaml` | 动态生成命令并转换命令参数与 HTTP 请求。 |

## 3. HTTP、SDK 与 CLI 的实现

### 3.1 HTTP 服务

`run.py` 将 `common.factory:final_app` 交给 Uvicorn，监听 `/var/run/ub_device_manager/ub_device_manager.sock`。当前服务入口不配置 TCP 监听；HTTP 客户端中的主机名只是逻辑地址，实际通过 UDS 建立连接。

`factory.py` 使用 `new_app.router.routes.extend(...)` 合并各业务应用的路由。NPU 和 VM 的主要接口如下：

| HTTP 接口 | 业务入口 | 结果 |
| --- | --- | --- |
| `GET /npu` | `QueryNpuTask` | NPU 设备列表。 |
| `POST /npu/bind` | `BindNpuDeviceTask` | `bus_guid`、`tid`、`uba`、`size` 和设备列表。 |
| `POST /npu/unbind` | `UnbindNpuDeviceTask` | 解绑完成消息。 |
| `POST /vm/create` | XML 准备、设备绑定、XML 构建与启动任务链。 | 创建完成消息。 |
| `DELETE /vm/{name}` | 虚拟机删除、设备解绑任务链。 | 删除完成消息。 |

FastAPI 使用服务端 `app/models.py` 校验请求。任务层进一步校验 XML 输入二选一、设备 ID/数量互斥、重复 ID、允许数量以及设备是否空闲。当前 NPU 数量支持 1、2、4、8，UPI 范围为 `0x0001`～`0x0fff`。

响应模型决定成功结果的 JSON 结构。异常处理器将 Pydantic 校验错误转换为 422，将业务 `HTTPException` 转换为对应状态码，将其他未处理异常转换为 500。当前异常响应体为 JSON 字符串，不统一包装成 `{code, msg}` 对象。响应头包含 `X-Request-ID` 和 `X-Process-Time`，用于关联日志和处理耗时。

### 3.2 Python SDK

SDK 是独立的客户端封装，不直接调用 UBSE 或 libvirt。调用过程为：

```text
SDK 函数参数
  → SDK Pydantic 请求模型
  → model_dump(exclude_none=True)
  → HTTPX / UDS
  → 服务端路由与业务任务
  → JSON 响应
  → Python 对象或返回值
```

`UbDeviceManagerClient` 使用 `httpx.HTTPTransport(uds=...)`，每次请求创建并关闭同步 HTTPX 客户端，设置 `trust_env=False`。UDS 路径优先使用显式参数，其次为 `UBDM_UDS_PATH`，最后为默认路径。请求默认超时为 10 秒，调用方可为创建等耗时操作设置更大的值。

客户端先调用 `raise_for_status()`，成功后解析 JSON；非成功状态通过 HTTPX 异常反馈给调用方。各封装函数继续整理返回值：

| SDK 函数 | HTTP 映射 | 返回值处理 |
| --- | --- | --- |
| `get_host_npu_device()` | `GET /npu` | 将每项转换为 `NpuDeviceInfo`。 |
| `bind_npu_device()` | `POST /npu/bind` | 将设备转换为 `BoundUbDevice`，返回 `(tid, uba, size, bus_guid, devices)`。 |
| `unbind_npu_device()` | `POST /npu/unbind` | 成功后不返回业务数据。 |
| `create_vm()` | `POST /vm/create` | 返回服务端创建结果字典。 |
| `delete_vm()` | `DELETE /vm/{name}` | 当前实现返回服务端删除结果字典。 |

SDK 模型与服务端模型分别定义，因此 SDK 侧检查不能替代服务端校验。

### 3.3 CLI

CLI 读取程序目录内的 `openapi.yaml`，使用 YAML SafeLoader 解析，并解析 schema 引用、HTTP 方法、路径参数、请求体属性、类型和必填项。

生成规则如下：

| OpenAPI 内容 | CLI 表现 |
| --- | --- |
| 路径第一段，如 `/npu`、`/vm` | Typer 命令分组 `npu`、`vm`。 |
| `GET /npu` | `ubdmctl npu list`。 |
| `POST /npu/bind`、`POST /vm/create` | `ubdmctl npu bind`、`ubdmctl vm create`。 |
| `DELETE /vm/{name}` | `ubdmctl vm delete NAME`，路径字段作为位置参数。 |
| 请求体和查询字段 | `--字段名` 选项，例如 `--npu_ids`。 |

`build_executor()` 使用 `inspect.Signature` 生成 Typer 可识别的函数签名。原始选项先作为字符串接收，再按契约转换为整数、布尔值、列表或对象；路径值填入 URL，查询值放入查询参数，请求体字段组成 JSON。未识别的额外参数会被拒绝。

CLI 直接通过 HTTPX 访问服务，没有调用 Python SDK 函数。当前超时为 1200 秒。成功响应按数据形态展示为 Rich 表格或树；失败时显示 HTTP 状态和错误内容，并以非零退出码结束。`--verbose` 展示请求参数和更详细的错误信息。

CLI 使用随包提供的静态 OpenAPI 文件，不会在启动时自动向服务端获取契约。新增接口或修改字段时，应同步维护服务端模型、`openapi.yaml`、SDK 和对应测试，避免客户端与服务端不一致。

## 4. UBSE 接口与绑定结果的形成

### 4.1 调用封装

`NpuClient` 封装 `ubse.ubs_engine_npu` 中的同步接口，使用 `asyncio.to_thread` 在线程中调用，使同步外部操作不直接阻塞 ASGI 事件循环。以下是当前 UBDM 使用的调用形式，不代表 UBSE 接口的全部能力：

| UBSE 接口 | 当前传入参数 | 当前接收结果及用途 |
| --- | --- | --- |
| `get_host_ub_devices()` | 无。 | 设备库存对象列表，用于查询、选择和解绑前定位设备。 |
| `alloc_devices(...)` | `upi`、`bus_guid`、`device_list`。 | 以 `bus_guid, _` 接收，使用第一个返回值作为本次 bus instance GUID，第二个返回值当前不使用。 |
| `query_uba_tid_size(...)` | `bus_instance_guid=bus_guid`。 | 接收 `(tid, uba, size)`，用于生成 `ubmem_vmmu` 参数。 |
| `free_devices(...)` | `bus_instance_guid`、`device_list`。 | 不使用业务返回值；异常向上反馈，用于解绑和失败补偿。 |

`NpuClient` 初始化时还会注册一次进程级 UBSE 日志回调，当前回调不转发日志内容；UBDM 在自身调用和业务层记录操作日志。

### 4.2 库存字段与设备选择

库存查询结果先转换为内部 `UbDeviceInfo`：

| UBSE 库存字段 | UBDM 字段 | 用途 |
| --- | --- | --- |
| `device_id` | `id` | 用户选择设备，以及构造分配、释放请求。 |
| `device_type` | `type` | 区分 NPU、NIC_PFE 和 NIC_VFE。 |
| `guid` | `guid` | 后续 hostdev XML 中的设备 GUID。 |
| `bus_instance` | `bus_guid` | 判断是否已绑定，或按 bus instance 定位待释放设备。 |
| `affinity_devs` | `affinity_devs` | 查找 NPU 配套的 NIC_VFE。 |

适配层保留 NPU、NIC_PFE 和 NIC_VFE 库存，`GET /npu` 只返回其中的 NPU。选择器以 `(type, id)` 建立索引，避免仅凭 ID 混淆不同类型的设备。

指定 ID 时，检查目标 NPU 存在且 `bus_guid` 为空。指定数量时，按库存遍历顺序选择前若干个空闲 NPU。`need_nic=True` 时，仅选择具有可用配套 NIC_VFE 的 NPU，并将 NIC_VFE 追加到设备列表；不会把 NIC_PFE 当作网卡直通对象。

这里的配套网卡关联来自 UBSE 的 `affinity_devs`，不是按宿主 NUMA 或 Socket 选择 NPU 的策略。当前请求没有 `affinity_policy` 参数。

### 4.3 从分配请求到绑定结果

假设用户指定 `upi='0x000f'` 和 NPU `1-1`，流程如下：

1. 查询库存，选择对应设备，并保留其 GUID 和类型。
2. 构造 `device_list`，每项仅包含 `device_id` 和 `device_type`。
3. 去掉 UPI 的 `0x` 前缀，以 `upi='000f'` 调用 UBSE。
4. 新分配流程默认传入 `bus_guid=''`，由 UBSE 返回本次 bus instance GUID。
5. 使用该 GUID 调用 `query_uba_tid_size()`，取得映射参数。
6. 将分配结果与所选库存设备组合为 `BindNpuDeviceResult`。

```text
get_host_ub_devices() ──→ 所选设备的 id / type / guid
alloc_devices()      ──→ bus_guid
query_uba_tid_size()  ──→ tid / uba / size
                         ↓
                BindNpuDeviceResult
```

因此，绑定结果的 `devices[].guid` 来自库存查询，`bus_guid` 来自分配接口，`tid/uba/size` 来自后续映射查询。它们不是单次 `alloc_devices()` 直接返回的完整对象，也不是 UBDM 自行生成的值。

## 5. 绑定结果如何转换为 XML

`VmXmlBuilder` 使用用户的基础 XML、`BindNpuDeviceResult` 和宿主机 `SmbiosType131` 生成最终 domain XML。用户已有的普通虚拟机配置作为基础保留，组件补充 UB 直通和服务器类型信息。

### 5.1 转换规则

| 输入或生成规则 | XML 位置 | 处理方式 |
| --- | --- | --- |
| XML 命名空间常量 | `qemu:commandline` | 使用 `http://libvirt.org/schemas/domain/qemu/1.0` 命名空间。 |
| 固定标识 `1` | `<iommufds>1</iommufds>` | 为 UB hostdev 提供一致的 IOMMUFD 引用；当前拒绝其他非空标识值。 |
| `bus_guid` | `devices/controller/source/businstance/@guid` | 使用原始 bus instance GUID。 |
| `bus_guid` 派生值 | UB controller 的 `address/@guid` | 按 `-` 分隔，将第 4 段改为 `2`，其他段不变。 |
| `len(devices)` | UB controller 的 `ports/@num` | 包含所有待直通设备，配套 NIC_VFE 也计入端口数。 |
| 固定配置 | UB controller | `type='ub'`、`index='0'`、子节点 `<model name='ubc'/>`、地址 `eid='0x1'`。 |
| 每项 `devices[i].guid` | hostdev 的 source 地址及 UB 地址 | 两处均写该设备 GUID；driver 为 `vfio`，`managed='yes'`，`iommufd='1'`。 |
| 设备顺序 `i`，从 0 开始 | hostdev 地址、端口和 alias | `eid=hex(i+2)`，`tport=str(i)`，`teid='0x1'`，端口 `index='0'`，alias 为 `ub-i`。 |
| 固定模型 | `devices/iommu` | 添加 `<iommu model='ummu'/>`；已有该模型时复用。 |
| `tid`、`uba`、`size` | `qemu:commandline` | 生成 `-device` 和 `ubmem_vmmu,tid=...,uba=...,size=...` 两个参数。 |
| 宿主机 Type 131 解析结果 | `sysinfo/type131/entry` | 数值转为十进制字符串，写入服务器组网信息。 |
| SMBIOS 使用模式 | `os/smbios/@mode` | 设置为 `sysinfo`，让配套虚拟化组件使用所提供的信息。 |

IOMMUFD 标识 `1` 与 UBSE 返回的 `tid` 属于不同配置，不能互相替代。设备 ID 用于查询和分配，设备 GUID 用于 XML；原始 bus GUID 用于绑定关系和释放，controller 的派生地址 GUID 不能用来解绑。

### 5.2 转换示例

以下是便于说明的单设备示例，所有值仅用于展示映射关系：

```json
{
  "bus_guid": "cc08-ffff-0-0-000000-011101ff7e2c0080",
  "tid": 1,
  "uba": 3298534883328,
  "size": 49152,
  "devices": [
    {"id": "1-1", "guid": "cc08-a002-0-2-000000-0000008405480188", "type": "NPU"}
  ]
}
```

对应的关键 XML 片段如下；hostdev 和 controller 位于 `<devices>` 内，命令行位于 `<domain>` 下：

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
```

```xml
<qemu:commandline>
  <qemu:arg value='-device'/>
  <qemu:arg value='ubmem_vmmu,tid=1,uba=3298534883328,size=49152'/>
</qemu:commandline>
```

### 5.3 Type 131 的来源

Type 131 信息来自宿主机 SMBIOS，用于让虚拟机内的 NPU 驱动识别服务器类型。`VmSmbios` 通过以下命令读取，超时为 10 秒：

```bash
/usr/bin/sudo -n /usr/sbin/dmidecode -t 131 -u
```

字段对应关系如下。偏移从 `Header and Data` 的第一个字节开始，以 0 为起点；多字节数值按小端序读取，写入 XML 时使用十进制字符串。

| 偏移 | 原始字段 | XML 属性 | 读取与转换方式 | 说明 |
| --- | --- | --- | --- | --- |
| `00h` | Type | 不填写。 | 单字节，`83` 表示十进制 `131`。 | 用于识别 Type 131 记录。 |
| `01h` | Length | 不填写。 | 单字节，`4D` 表示十进制 `77`。 | 格式化数据区长度，用于检查记录完整性。 |
| `02h`～`03h` | Handle | 不填写。 | 不转换为 XML 属性。 | 宿主机记录标识符。 |
| `04h` | Flag | `flag` | 读取单字节，`00` 转为 `0`。 | 当前组件只接受 Flag 为 `00` 的记录。 |
| `05h`～`06h` | PodId | `pod_id` | 读取 2 字节，按小端序转换；如 `01 00` 转为 `1`。 | 组网 ID。 |
| `07h` | SlotId | `slot_id` | 读取单字节；如 `02` 转为 `2`。 | 服务器在组网内的 ID。 |
| `08h` | Mesh_Type | `mesh_type` | 读取单字节；如 `00` 转为 `0`。 | 组网类型，具体取值含义以产品定义为准。 |
| `09h`～`0Ch` | SuperPodId | `super_pod_id` | 读取 4 字节，按小端序转换。 | 按宿主机实际值填写。 |
| `0Dh`～`0Eh` | ServedIndex | `served_index` | 读取 2 字节，按小端序转换；如 `03 00` 转为 `3`。 | 超节点全局编号。 |
| `0Fh` | TypeName | `type_name` | 当前组件固定填写 `UB INFO`，不解析字符串索引。 | 不将索引数字写入 XML。 |
| `10h`～`4Ch` | Reserved | 不填写。 | 当前组件不转换这些字节。 | 77 字节示例记录中的预留区。 |

解析完成后，`VmXmlBuilder` 将表中的 XML 属性写入 `sysinfo/type131/entry`，并在已有的 `<os>` 中设置 `<smbios mode='sysinfo'/>`。如果已有 SMBIOS 配置，则替换其中的 Type 131，保留其他字段。

以下示例展示转换后的 XML 结构。其中 `pod_id='1'`、`slot_id='2'`、`mesh_type='0'`、`super_pod_id='0'`、`served_index='3'` 为示例值，实际应使用当前宿主机的解析结果：

```xml
<domain type='kvm'>
  <!-- 保留虚拟机原有的其他配置 -->
  <sysinfo type='smbios'>
    <type131>
      <entry flag='0' pod_id='1' slot_id='2' mesh_type='0'
             super_pod_id='0' served_index='3' type_name='UB INFO'/>
    </type131>
  </sysinfo>
  <os>
    <!-- 保留原有的机器类型、固件和启动顺序等配置 -->
    <smbios mode='sysinfo'/>
  </os>
</domain>
```

`<sysinfo>` 与 `<os>` 同级，`<smbios mode='sysinfo'/>` 位于 `<os>` 内。上例只展示 Type 131 相关配置，合并时应保留虚拟机原有的其他配置。

虚拟机启动后，在虚拟机内使用 root 用户执行：

```bash
dmidecode -t 131 -u
```

确认回显中存在 Type 131 记录，并按表中的转换规则核对 Flag、PodId、SlotId、Mesh_Type、SuperPodId、ServedIndex 和 `UB INFO`。各字段应与 XML 中填写的值一致。

如果虚拟机内没有相应记录，应检查 XML 是否生效，以及配套 libvirt 和 QEMU 是否支持 Type 131 扩展。仅在宿主机查看 XML，不能代替虚拟机内的读取验证。

## 6. 创建流程与异常补偿

### 6.1 正常创建

![NPU 直通虚拟机创建流程](./images/vm_create_sequence.png)

创建路由顺序执行 `PrepareVmXmlTask → 加锁 → BindNpuDeviceTask → 解锁 → BuildAndStartVmTask`：

1. 先处理 `xml_text` 或服务端 `xml_path`，按配置限制字节数，使用 defusedxml 解析。根节点必须为 `domain`，不能已有 UB controller、UB hostdev 或 `ubmem_vmmu` 参数。先检查输入，减少无效请求占用设备的机会。
2. 请求 NPU 时，准备绑定请求，选择设备并取得完整绑定结果。
3. 读取 Type 131 并调用 `VmXmlBuilder.build()`，将最终 XML 交给 `VMClient.define_and_start()`。
4. `VMClient` 通过 libvirt Python 绑定连接本地 `qemu:///system`，调用 `defineXML()` 和 `domain.create()`，最后关闭连接。

不请求 NPU 时，绑定任务跳过，直接使用基础 XML 定义并启动普通虚拟机，不补充 UB 和宿主机 Type 131。该分支不表示服务可以不安装其整体运行依赖。

### 6.2 任务上下文与锁

每条任务链有独立 `Context`，通过 ContextVar 传递；任务按上下文键读取请求、基础 XML、绑定结果和待解绑 GUID。任务的 `should_run()` 决定是否执行，`execute()` 执行业务，`when_raise_exception()` 提供异常处理入口。

创建路由在库存查询、选择和绑定阶段持有 `vm_bind_npu_device` 命名锁，绑定完成后释放，避免把后续虚拟机启动耗时包含在锁内。任务链结束时，`finally` 会释放尚未主动释放的锁并恢复上下文。

该锁是当前进程内的 `asyncio.Lock`，不是跨进程或分布式锁；独立 `/npu/bind` 路由没有使用这把创建锁。因此不能把它描述为所有资源操作的全局互斥保证。

### 6.3 失败处理

| 失败阶段 | 当前处理 |
| --- | --- |
| 输入校验或设备选择失败 | 直接返回错误，尚未完成设备分配。 |
| `alloc_devices()` 抛出异常 | 异常向上反馈；UBDM 没有在此分支额外执行库存核对或释放。 |
| 分配成功，但 `query_uba_tid_size()` 失败 | `NpuClient.bind_devices()` 使用分配所得 GUID 和设备列表调用 `free_devices()`，随后抛出异常。 |
| Type 131 读取或 XML 构建失败 | `BuildAndStartVmTask` 尝试释放本次绑定设备，再向上抛出错误。 |
| libvirt 已定义虚拟机，但启动失败 | `VMClient` 尝试用 `VIR_DOMAIN_UNDEFINE_NVRAM` 取消定义；外层创建任务再尝试释放设备。 |
| 创建阶段释放设备也失败 | 记录释放失败日志；`BuildAndStartVmTask` 保留其捕获的原始创建异常。资源状态需进一步核查。 |

这是对外部副作用的补偿流程，不是数据库事务，也不保证补偿在所有故障下必然成功。映射查询失败后的 `free_devices()`，以及 libvirt 启动失败后的取消定义调用，如果再次抛错，当前实现可能暴露后续清理异常。

调用超时、服务进程中断或外部接口出现部分成功时，不应仅凭客户端失败就认定资源未分配。当前没有持久化恢复队列和请求幂等键，处理前应结合 UBSE 库存、libvirt 定义和日志确认实际状态。

## 7. 删除与解绑设计

删除路由执行 `DeleteVmTask → UnbindNpuDeviceTask`，顺序如下：

1. 通过虚拟机名称查询 libvirt domain，读取 XML 中 UB controller 的 `source/businstance/@guid`。当前删除逻辑取第一个 UB controller，与自动生成一个 controller 的流程对应。
2. 虚拟机运行时调用 `destroy()` 强制停止，再通过 `undefineFlags(VIR_DOMAIN_UNDEFINE_NVRAM)` 取消定义并处理 NVRAM。
3. 将原始 bus instance GUID 写入任务上下文。
4. 重新查询 UBSE 库存，选择 `bus_guid` 与该 GUID 相同的设备，构造 `device_id/device_type` 列表并调用 `free_devices()`。

普通虚拟机没有 UB controller 时，待解绑 GUID 为空，解绑任务跳过。删除流程不删除虚拟机磁盘文件。

独立的 `/npu/unbind` 只按 bus instance 释放设备，不停止或取消定义虚拟机。虚拟机已经取消定义但解绑失败时，需保留原始 bus GUID，核对设备状态后单独处理解绑，不能指望再次按名称删除已不存在的虚拟机完成恢复。

当前 UBDM 没有自己的持久化设备映射数据库：设备归属来自 UBSE 库存，虚拟机与 bus instance 的关联来自 libvirt XML。请求上下文仅保存本次操作中的中间数据。

## 8. 运行配置、日志与源码入口

RPM 将服务配置为 `ub-device-manager` 用户运行。systemd 创建运行目录，程序启动后将 UDS 权限设置为 `0660`。调用用户需具备该 Socket 的访问权限；服务账户访问 UBSE、libvirt 和通过 sudo 读取 Type 131 的权限分别由对应组件配置。

TOML 配置在进程启动时读取，主要控制日志级别、轮转和 XML 输入大小；修改后需要重启。路径加载顺序为存在的 `UBDM_CONFIG_PATH` 文件、`/etc/ub_device_manager/ub_device_manager.toml`、程序包内默认副本，不合并多个文件。具体操作见[配置文件说明](./configuration.md)和[安装部署指导](./install.md)。

日志默认写入 `/var/log/ub_device_manager/ub_device_manager.log`，支持轮转、压缩及可选标准输出。请求 ID 通过 ContextVar 关联同次请求中的日志，日志消息和 CLI 展示内容还会过滤控制字符。

| 源码入口 | 对应设计 |
| --- | --- |
| [run.py](../ub_device_manager/run.py)、[factory.py](../ub_device_manager/common/factory.py) | 服务启动、路由组合、中间件和错误处理。 |
| [app/models.py](../ub_device_manager/app/models.py)、[vm_app.py](../ub_device_manager/app/vm_app.py)、[npu_app.py](../ub_device_manager/app/npu_app.py) | 服务端模型与业务任务链入口。 |
| [async_task_framework.py](../ub_device_manager/common/async_task_framework.py) | 任务链、上下文和进程内锁。 |
| [npu_client.py](../ub_device_manager/domain/npu/npu_client.py)、[device_selector.py](../ub_device_manager/domain/npu/device_selector.py)、[npu_tasks.py](../ub_device_manager/domain/npu/npu_tasks.py) | UBSE 适配、设备选择、绑定与解绑。 |
| [vm_tasks.py](../ub_device_manager/domain/vm/vm_tasks.py)、[vm_client.py](../ub_device_manager/domain/vm/vm_client.py) | 创建、删除、libvirt 调用和补偿。 |
| [vm_xml_builder.py](../ub_device_manager/domain/vm/vm_xml_builder.py)、[vm_smbios.py](../ub_device_manager/domain/vm/vm_smbios.py) | XML 转换和 Type 131 读取、解析。 |
| [ub_dm_sdk](../ub_device_manager/ub_dm_sdk)、[ub_device_manager_cli.py](../ub_device_manager/ub_device_manager_cli.py)、[openapi.yaml](../ub_device_manager/openapi.yaml) | SDK 封装、CLI 生成与接口契约。 |

现有单元测试通过 Stub 和 Mock 隔离 UBSE、libvirt 及外部命令，用于验证参数处理、设备选择、XML 转换和异常分支；真实驱动、固件、设备访问与客体 NPU 功能仍需在配套服务器上验证。测试执行方法见[测试指导](./test.md)。
