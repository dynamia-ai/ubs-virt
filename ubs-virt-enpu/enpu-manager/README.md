# enpu-manager

## 介绍

`enpu-manager`是 ubs-virt-enpu 提供 NPU 显存交换（超分）服务的管理进程，运行在宿主机侧。它在 vCANN-RT 已有的算力切分和显存控制能力之上，引入节点级显存超分机制：允许将冷模型的显存换出到 CPU 侧 swap buffer，把腾出的显存让给其他容器使用，从而提升整卡显存利用率。

enpu-manager 通过 HTTP over UDS 的 REST API 接收 vNPU 分配/释放/查询请求，生成容器侧 vCANN-RT 所需的 `npu_info.config` 配置文件，并配合 vCANN-RT 完成显存换出/换入的协调。

> 本文的 UDS、allocation UUID 和资源查询接口按目标协议描述，联调使用配套实现版本。

显存超分面向冷模型共享场景：多个模型部署在同一 DIE 上，显存需求总量可超过该 DIE 的 HBM 容量，但模型主要交替、间歇访问，同时活跃模型的显存需求之和不超过 HBM 容量。业务还需能够接受换入、换出的传输时延。**不支持多个大模型持续并发推理、同时活跃的显存需求超过该 DIE 的 HBM 容量，并依靠频繁交换维持运行。**此类场景可能出现时延波动或业务异常。详见 [vCANN-RT 显存超分适用场景与使用限制](../vcann-rt/README.md#适用场景与使用限制)。

## 环境准备

### 软件版本

#### Atlas A2 推理系列产品

**表 1 软件版本**

| 软件                | 版本                                                                        |
|:---------------------|:-----------------------------------------------------------------------------|
| CANN                | 8.5.0~9.1.0                                                                |
| HDK                 | 25.5.0 及以上版本                                                           |

#### Atlas A3 推理系列产品

**表 2 软件版本**

| 软件                | 版本                                                                        |
|:---------------------|:-----------------------------------------------------------------------------|
| CANN                | 8.5.0~9.1.0                                                                |
| HDK                 | 26.0.0 及以上版本                                                           |

#### Atlas A5 推理系列产品（Ascend 950）

**表 3 软件版本**

| 软件                | 版本                                                                        |
|:---------------------|:-----------------------------------------------------------------------------|
| CANN                | 8.5.0~9.1.0                                                                |
| HDK                 | 配套 Ascend 950 的 HDK 版本                                                 |

> enpu-manager 自身运行在宿主机侧，不依赖 Docker；Docker 仅用于拉起业务容器（参见 [vCANN-RT 使用说明](../vcann-rt/README.md)）。

### 系统要求

- **硬件**：Ascend NPU 设备（910 系列 / Ascend 950 系列）
- **操作系统**：openEuler 24.03 LTS、Ubuntu 22.04 或兼容系统
- **CANN**：8.5.0~9.1.0

## 源码获取

```shell
git clone <ubs-virt-enpu-url>
cd ubs-virt-enpu/enpu-manager
```

## 编译

> 仅**源码部署**需要执行本节；通过 RPM / DEB 包安装可跳过本节，直接看[部署](#部署)。

编译前需要设置 CANN 环境变量:

```shell
source /usr/local/Ascend/cann/set_env.sh

# 构建核心库
cd core
bash make_build.sh

# 构建独立部署二进制（enpu-manager 主进程）
cd ../standalone
bash build.sh
```

构建产物位于 `standalone/build/output/enpu-manager`（主进程二进制）。

## 部署

### 方式一：RPM 包安装（openEuler）

获取或构建 RPM 包后执行：

```shell
sudo rpm -ivh enpu-manager-*.rpm
```

RPM 安装会自动完成：
- 把 `enpu-manager` 二进制部署到 `/usr/bin/`
- 把默认配置文件部署到 `/etc/enpu/enpu-manager.conf`
- 把 systemd service 部署到 `/usr/lib/systemd/system/enpu-manager.service`
- **无需手动配置 `LD_LIBRARY_PATH`**

如需从源码自行构建 RPM 包：

```shell
sudo dnf install cmake gcc gcc-c++ cjson-devel rpm-build
cd deploy && bash build_rpm.sh
# 产物位于 rpmbuild/RPMS/
```

### 方式二：DEB 包安装（Ubuntu/Debian）

获取或构建 DEB 包后执行：

```shell
sudo dpkg -i enpu-manager_*.deb
sudo apt-get install -f    # 自动补齐依赖
```

DEB 安装的能力与 RPM 一致。

如需从源码自行构建 DEB 包：

```shell
sudo apt-get install build-essential cmake debhelper libcjson-dev
cd deploy && bash build_deb.sh
# 产物位于 debbuild/output/
```

### 方式三：手动安装（开发调试用）

适合开发阶段反复迭代，不推荐生产环境。

```shell
sudo cp standalone/build/output/enpu-manager /usr/bin/
sudo cp config/enpu-manager.conf /etc/enpu/
sudo cp deploy/enpu-manager.service /usr/lib/systemd/system
sudo systemctl daemon-reload
```

手动安装时系统未自动装入 `libcjson` 等运行时依赖，需自行解决（安装对应软件包或将 `.so` 路径加入 `LD_LIBRARY_PATH`）。

## 配置文件

配置文件路径：`/etc/enpu/enpu-manager.conf`。

**表 4 配置文件字段**

| 字段                       | 类型     | 默认值       | 是否可修改 | 说明                                                                                                                                                          |
|:---------------------------|:---------|:-------------|:-----------|:--------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `socket_path` | string | /run/enpu/enpu-manager.sock | 是 | HTTP over UDS 监听路径。                                                                                                                                            |
| `config_dir`               | string   | /etc/enpu    |  否       | vCANN-RT 配置根目录（生成 `npu_info.config` 的基础路径）。                                                          |
| `state_dir`               | string   | /var/lib/enpu-manager |  否       | 状态持久化目录（`checkpoint.json`）。                                                                                                       |
| `share-strategy`           | string   | compact      |  是       | vNPU 分配策略，可选 `compact` / `anti-fragment`，详见下方表 5。                                                                                              |
| `oversub-ratio`            | string   | 0（全禁用）  |  是       | per-die 超分比例，多个值按 phy_id 顺序逗号分隔（CSV 格式），`0` 表示该 die 不参与超分。单值写法表示所有 die 都填这个值。示例：`oversub-ratio=0.2,0.2,0.0,0.3` 表示 phy0/phy1 各 0.2、phy2 禁用、phy3 为 0.3。<br>**注意**：启用显存超分的卡（`oversub-ratio > 0`）上运行的容器必须启用软切分（vCANN-RT），否则 vCANN-RT 不会上报 `hbm_used`，超分比例计算不准确。 |
| `swap-pre-watermark`       | int      | 80           |  是       | 预换出水水位，单位为百分比（%），即 die 的 HBM 使用率。使用率达到该值时触发预换出（提前腾出显存）。                                                            |
| `log_level`                | string   | info         | 是        | 日志级别：`debug(4)` / `info(3)` / `warn(2)` / `error(1)` / `fatal(0)`。                                                                                    |
| `log_dir`                  | string   | /var/log/enpu-manager | 是 | 日志文件输出目录。                                                                                                                                         |

**节点级 swap buffer 大小**自动计算为 `Σ (1 + oversub_ratio[phy_id]) × die_hbm_total[phy_id]`。启动时若该值超过宿主机 `/proc/meminfo` 的 `MemFree` 字段，enpu-manager 会拒绝启动并在日志中提示 `configured oversub-ratio too large`，需调小 `oversub-ratio` 后重试。

**表 5 分配策略对比**

通过 `share-strategy` 字段配置，影响 vNPU 自动分配时 die 的选择倾向（指定 `phy_id` 的强制分配路径不走此排序）。

| 策略名 | 特点描述 |
|:---|:---|
| `compact`（集中分配，默认） | 优先把新 vNPU 分配到 **vNPU 数量多、AICore 占用多** 的 die，让新任务加入已经较忙的卡，逐步把卡填满。<br>**目的**：腾出完整的空卡，便于后续大块资源分配或硬件维护。<br>**适用场景**：节点上负载相对稳定，需要预留整张空卡给突发大任务的场景。 |
| `anti-fragment`（分散分配） | 优先把新 vNPU 分配到 **vNPU 数量少、AICore 占用少** 的 die，把新任务分散到空闲卡上。<br>**目的**：避免单 die 过载，让负载在多卡间均衡，降低单卡故障/换出对整体的影响。<br>**适用场景**：高并发、强调负载均衡、单 die 失败影响大的生产场景。 |

## 启动服务

### 方式一：Systemd 服务（推荐）

```shell
sudo systemctl enable --now enpu-manager

# 查看状态与日志
systemctl status enpu-manager.service
journalctl -u enpu-manager.service -n 100
```

### 方式二：手动启动

```shell
sudo install -d -m 0750 /run/enpu
enpu-manager
```

使用自定义 socket 路径时，先创建其父目录，并确保运行用户具有写权限。

启动后通过下方 UDS 请求确认服务可用。

### 验证部署成功

- 通过 `systemctl status enpu-manager` 确认服务状态为 `active (running)`，并检查监听 socket 和服务日志。如出现 `configured oversub-ratio too large`，需调小 `/etc/enpu/enpu-manager.conf` 中的 `oversub-ratio` 后重启。

- 调用 REST API 查询设备列表，确认 enpu-manager 已正确识别节点上的物理 NPU：

  ```bash
  curl --unix-socket /run/enpu/enpu-manager.sock http://localhost/api/v1/devices
  ```

  正常返回 JSON，`devices` 数组中应包含每张可用物理卡的 `phy_id / minor_name / aicore_available / hbm_available / oversub_ratio` 等字段。

- 分配一个测试 vNPU 验证分配链路：

  ```bash
  curl --unix-socket /run/enpu/enpu-manager.sock -X POST http://localhost/api/v1/allocate \
    -H "Content-Type: application/json" \
    -d '{"allocation_id":"550e8400-e29b-41d4-a716-446655440000","aicore_quota":10,"hbm_quota":1024,"hbm_limit":1024,"sched_policy":1}'
  ```

  正常返回包含 `allocation_id / phy_id / vnpu_id / config_path` 等字段，`result=0` 表示分配成功。

- 查询当前分配记录，确认上一步分配的 vNPU 已登记：

  ```bash
  curl --unix-socket /run/enpu/enpu-manager.sock http://localhost/api/v1/allocations
  ```

- 在宿主机侧确认 vCANN-RT 配置文件已生成：

  ```bash
  ls /etc/enpu/vcann-rt/allocations/
  # 每个 allocation_id 对应一个目录，包含 npu_info.config
  ```

- 释放测试 vNPU 完成清理（UUID 替换为上一步返回的 allocation_id）：

  ```bash
  curl --unix-socket /run/enpu/enpu-manager.sock -X DELETE http://localhost/api/v1/allocations/5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4
  ```

## REST API 使用

enpu-manager 通过 HTTP over Unix Domain Socket 接收请求，默认 socket 为 `/run/enpu/enpu-manager.sock`。
除 `/metrics` 返回 Prometheus 文本外，其余接口返回 JSON。调用者需具备 socket 连接权限；容器调用方挂载 socket 所在目录。
示例中的 `http://localhost` 用于构造 HTTP 请求，实际连接由 `--unix-socket` 指定。

请求头和正文合计最多 4095 字节；请求超限返回 HTTP 413，接收超时（5 秒）返回 408。
正文使用 `Content-Length`，不支持 `Transfer-Encoding`；报文不完整或长度字段非法返回 400。

### API 总览

**表 6 REST API 列表**

| 端点                                  | 方法   | 说明                                       |
|:--------------------------------------|:-------|:-------------------------------------------|
| `/api/v1/devices`                     | GET    | 列出所有 NPU 设备及其可用资源。             |
| `/api/v1/devices/{phy_id}`            | GET    | 查询指定物理卡详情（含 oversub_ratio）。    |
| `/api/v1/allocate`                    | POST   | 分配 vNPU。                                |
| `/api/v1/allocations/{allocation_id}` | DELETE | 按 UUID 释放指定分配。 |
| `/api/v1/allocations?pod_uid={pod_uid}&container_name={container_name}` | DELETE | 按两个标签唯一匹配后释放。 |
| `/api/v1/allocations`                 | GET    | 列出当前所有 vNPU 分配。                    |
| `/api/v1/resources` | GET | 查询配额、HBM 用量及物理 NPU/vNPU 利用率。 |
| `/metrics` | GET | 批量返回 Prometheus 指标及 allocation 关联信息。 |
| `/api/v1/clean`                       | DELETE | 清理当前所有 vNPU 分配。                    |

### 分配 vNPU

`POST /api/v1/allocate`

**表 7 请求字段**

| 字段               | 类型    | 必填 | 说明                                                                                       |
|:-------------------|:--------|:-----|:-------------------------------------------------------------------------------------------|
| `allocation_id` | string | 否 | 未传时由 manager 生成；传入时检查重复，已有分配返回 409，不比较其他参数。 |
| `aicore_quota`     | int     | 普通优先级必填 | 普通优先级范围 1~100；策略 `1`/`2` 下用于 AICore 配额限制，策略 `3` 下仍需填写但不用于限制算力。高优先级可省略，实际使用 0。 |
| `hbm_quota`        | int | 是   | HBM Request（保底预留），单位 MB，等价别名 `hbm_request`。                                            |
| `hbm_limit`        | int | 否   | HBM Limit（硬上限），单位 MB。未填时默认等于 `hbm_quota`。                                              |
| `sched_policy`     | int     | 普通优先级必填 | 调度策略：`1`=fixed-share，`2`=elastic，`3`=best-effort；高优先级可省略，实际使用 `3`。 |
| `vnpu_priority`    | int     | 否   | vNPU 优先级：`0`=普通优先级（默认），`1`=高优先级。 |

| `swap_order` | int | 否 | 换出顺序：`0` 最先换出，`1` 默认，`2` 最后换出。同一顺序先换出最久未执行的候选。 |
| `phy_id`           | int     | 否   | 指定物理卡：`-1`=由 enpu-manager 自动选择（默认）；`>=0`=直接分配到该卡（跳过自动决策）。     |
| `pod_uid` | string | 否 | Pod 元数据，可重复，未填为空字符串。 |
| `container_name` | string | 否 | 容器名元数据，可重复，未填为空字符串。 |

创建成功后返回 `allocation_id`（UUID），用于查询或释放；可选标签在 `metadata` 中回显。未传 allocation_id 时每次创建独立分配；重试时复用同一个 allocation_id，重复请求返回 HTTP 409，可通过分配列表查询已有记录。CLI 支持 `--allocation-id`。

分配接口的 MB 按 1024 × 1024 bytes 换算。`hbm_quota` 与 `hbm_request` 任选一个传入；同时提供时优先使用 `hbm_request`。
best-effort 仍需提供 `aicore_quota`，但不启用算力限流。`config_path` 是宿主机路径，调用方负责将配置挂载给目标业务容器。

CLI 分配时可通过 `--swap-order 0|1|2` 指定换出顺序，未指定时为 1。
当`vnpu_priority=1`时，若用户显式传入非`0`的`aicore_quota`或非`3`的`sched_policy`，分配成功的响应会通过`warnings`数组对相应字段返回英文提示，并回显最终生效值。未传入的字段、已使用正确值的字段不产生提示；没有提示时省略`warnings`字段。分配失败仍通过`error_msg`说明原因。

例如，同时传入`vnpu_priority=1`、`aicore_quota=20`和`sched_policy=2`时，成功响应中包含：

```json
{
  "warnings": [
    "aicore_quota is ignored when vnpu_priority=1; the effective value is 0.",
    "sched_policy is ignored when vnpu_priority=1; the effective value is 3 (best-effort)."
  ]
}
```

**示例 1：自动选择物理卡，分配 elastic 模式 vNPU**

```bash
curl --unix-socket /run/enpu/enpu-manager.sock -X POST http://localhost/api/v1/allocate \
  -H "Content-Type: application/json" \
  -d '{
    "allocation_id": "550e8400-e29b-41d4-a716-446655440001",
    "aicore_quota": 20,
    "hbm_quota": 10240,
    "hbm_limit": 32768,
    "sched_policy": 2
  }'
```

响应：

```json
{
  "allocation_id": "5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",
  "config_path": "/etc/enpu/vcann-rt/allocations/5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4/npu_info.config",
  "phy_id": 0,
  "vnpu_id": 5,
  "metadata": {"pod_uid": "", "container_name": ""},
  "die_id": "14422CC3-...",
  "shm_id": "14422CC3-...",
  "aicore_quota": 20,
  "hbm_quota": 10240,
  "hbm_limit": 32768,
  "sched_policy": 2,
  "minor_name": "Ascend910-0",
  "result": 0,
  "error_msg": ""
}
```

**示例 2：指定物理卡和业务标签，分配 fixed-share 模式 vNPU**

```bash
curl --unix-socket /run/enpu/enpu-manager.sock -X POST http://localhost/api/v1/allocate \
  -H "Content-Type: application/json" \
  -d '{
    "allocation_id": "550e8400-e29b-41d4-a716-446655440002",
    "aicore_quota": 20,
    "hbm_quota": 10240,
    "hbm_limit": 10240,
    "sched_policy": 1,
    "phy_id": 0,
    "pod_uid": "pod-abc",
    "container_name": "container-1"
  }'
```

### 释放 vNPU

`DELETE /api/v1/allocations/{allocation_id}`

停止使用该配置的业务后，按创建结果中的 UUID 释放配额和配置文件。请求无需正文，也无需 Pod/容器标签。

```bash
curl --unix-socket /run/enpu/enpu-manager.sock -X DELETE http://localhost/api/v1/allocations/5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4
```

成功响应：

```json
{"result": 0, "error_msg": ""}
```

UUID 格式非法返回 HTTP 400，分配不存在返回 HTTP 404。

按标签释放：`DELETE /api/v1/allocations?pod_uid={pod_uid}&container_name={container_name}`。
两个标签均须提供，按精确匹配定位 allocation 后执行释放：匹配一条时成功，无匹配返回 HTTP 404，匹配多条返回 HTTP 409 Conflict 并要求改用 UUID。

```bash
curl --unix-socket /run/enpu/enpu-manager.sock -X DELETE \
  'http://localhost/api/v1/allocations?pod_uid=pod-abc&container_name=container-1'
```

自动回收及重试建议使用创建时返回的 UUID，避免同名业务重新分配后误释放新资源。

### 清理所有 vNPU 分配

`DELETE /api/v1/clean`

一次性释放当前所有 vNPU 分配及其共享内存、配额和配置文件，适用于环境重置、批量回收。
请求无需参数和正文。调用前应停止使用这些配置的业务。

```bash
curl --unix-socket /run/enpu/enpu-manager.sock -X DELETE http://localhost/api/v1/clean
```

成功响应示例：

```json
{
  "released_count": 2,
  "result": 0,
  "error_msg": ""
}
```

`released_count` 表示本次释放的 vNPU 分配数量，无分配时为 0；`result=0` 表示清理成功。

### 查询设备列表

`GET /api/v1/devices`

返回所有 NPU 设备及其可分配资源，无需请求参数。

```bash
curl --unix-socket /run/enpu/enpu-manager.sock http://localhost/api/v1/devices
```

响应示例：

```json
{
  "devices": [
    {
      "phy_id": 0,
      "minor_name": "Ascend910-0",
      "uuid": "14422CC3-...",
      "aicore_available": 80,
      "hbm_available": 32768,
      "vnpu_available": 63,
      "oversub_ratio": 0.5
    }
  ]
}
```

`aicore_available` 为剩余可分配算力配额（百分数），`hbm_available` 为可分配 HBM 配额（MB），
`vnpu_available` 为剩余可分配 vNPU 数量，`oversub_ratio` 为显存超分比例（例如 0.5 表示允许超分 50%）。
无设备时返回 `{"devices":[]}`。实际用量及利用率通过 `/api/v1/resources` 查询。

### 查询设备详情

`GET /api/v1/devices/{phy_id}`

返回指定物理卡的可分配资源、总显存及显存超分比例。

| 参数 | 类型 | 必填 | 说明 |
|:---|:---|:---|:---|
| `phy_id` | int | 是 | 路径参数，物理资源 ID，须为非负整数。 |

```bash
curl --unix-socket /run/enpu/enpu-manager.sock http://localhost/api/v1/devices/0
```

响应示例：

```json
{
  "phy_id": 0,
  "die_id": "14422CC3-...",
  "aicore_available": 80,
  "hbm_available": 32768,
  "vnpu_available": 63,
  "total_memory": 65536,
  "oversub_ratio": 0.5
}
```

可分配资源字段与设备列表含义一致；`die_id` 为 DIE 标识，`total_memory` 为设备总显存，单位为 MB。
指定设备不存在返回 HTTP 404。

### 查询分配

`GET /api/v1/allocations`

返回当前分配清单，供下游建立 allocation 与设备、业务标签的映射。`config_path` 为该分配在宿主机上的配置文件路径，与创建响应一致，支持自定义配置目录。

```bash
curl --unix-socket /run/enpu/enpu-manager.sock http://localhost/api/v1/allocations
```

响应示例：

```json
{
  "allocations": [
    {
      "allocation_id": "5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",
      "config_path": "/etc/enpu/vcann-rt/allocations/5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4/npu_info.config",
      "metadata": {"pod_uid": "pod-abc", "container_name": "container-1"},
      "phy_id": 0,
      "vnpu_id": 5,
      "die_id": "14422CC3-...",
      "aicore_quota": 20,
      "hbm_quota": 10240,
      "hbm_limit": 10240,
      "sched_policy": 1,
      "swap_order": 1
    }
  ]
}
```

无分配时返回 `{"allocations":[]}`。此处 HBM 配额沿用分配接口的 MB 单位。

### 查询资源

`GET /api/v1/resources`

返回算力配额、HBM request/limit 和实际用量、六项物理利用率及两项 vNPU 利用率。

| 参数 | 类型 | 必填 | 说明 |
|:---|:---|:---|:---|
| `allocation_id` | string | 否 | 按 UUID 查询单个分配。 |
| `phy_id` | int | 否 | 按物理资源过滤。 |
| `vnpu_id` | int | 否 | 查询指定 vNPU，须同时提供 phy_id。 |
| `pod_uid`、`container_name` | string | 否 | 按非空元数据精确匹配，同时提供时为 AND。 |
| `detail` | bool | 否 | 默认 false；true 时增加采样时间和数据源返回码等诊断信息。 |

UUID、资源坐标、标签三种选择器互斥；不传选择器查询全部。结果统一放在 `items` 数组中，
UUID 查询成功时为一项，标签查询可返回多项。资源响应的显存单位为 bytes，利用率为 0～100 百分数。

```bash
# 查询单个 allocation
curl --unix-socket /run/enpu/enpu-manager.sock \
  'http://localhost/api/v1/resources?allocation_id=5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4'

# 按标签查询并附加诊断信息
curl --unix-socket /run/enpu/enpu-manager.sock \
  'http://localhost/api/v1/resources?pod_uid=pod-abc&container_name=container-1&detail=true'
```

单个 allocation 的响应示例（数值为示意）：

```json
{
  "items": [
    {
      "allocation_id": "5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",
      "phy_id": 0,
      "vnpu_id": 5,
      "metadata": {"pod_uid": "pod-abc", "container_name": "container-1"},
      "quota": {
        "aicore_percent": 20,
        "core_limit_enabled": true,
        "scheduling_policy": "fixed-share"
      },
      "hbm": {
        "request_bytes": 10737418240,
        "limit_bytes": 10737418240,
        "usage": {"used_bytes": 2147483648, "status": "ok"}
      },
      "physical": {
        "npu_utilization": {"value": 80.0, "status": "ok"},
        "aicore_utilization": {"value": 75.0, "status": "ok"},
        "aicpu_utilization": {"value": 10.0, "status": "ok"},
        "ctrlcpu_utilization": {"value": 5.0, "status": "ok"},
        "cube_utilization": {"value": 70.0, "status": "ok"},
        "vector_utilization": {"value": 65.0, "status": "ok"}
      },
      "vnpu": {
        "utilization": {"value": 24.0, "status": "ok"},
        "aicore_utilization": {"value": 22.5, "status": "ok"}
      }
    }
  ],
  "result": 0,
  "error_msg": ""
}
```

`quota` 和 HBM 配额来自分配记录；`physical` 为绑定卡的整体指标，`vnpu` 为按工作量归因的估算值，
不直接由配额推算。HBM 用量为来源容器在绑定设备上的 DCMI 可见进程内存汇总。
best-effort 下 `core_limit_enabled=false`、`aicore_percent=null`，`scheduling_policy="best-effort"`。

观测指标通过 `status` 表示有效性：`ok/idle` 为有效，`unsupported` 为不支持，`stale` 为过期，
`unavailable` 或统计异常状态表示数据不可用。无效数值为 null，例如：

```json
{"used_bytes": null, "status": "stale"}
```

统计异常状态包括 `inconsistent`、`stats_shm_missing`、`stats_version_mismatch`、`stats_unavailable`、
`avg_duration_missing`、`dcmi_unavailable` 和 `vnpu_id_out_of_range`。下游仅将 `ok/idle` 当作有效数据，未知状态按不可用处理。
单项指标失败不影响其他指标返回，此时 HTTP 200、`result=0` 仍表示查询成功。

`detail=true` 时，每个 item 额外增加 `detail`，以下省略部分同类指标诊断字段：

```json
{
  "detail": {
    "window_ms": 1000,
    "collected_at_ns": 12100000000,
    "die_id": "14422CC3-...",
    "shm_id": "14422CC3-...",
    "raw": {
      "sum_block_dim": 300,
      "launch_count": 30,
      "avg_kernel_duration_ns": 1000000
    },
    "calculated": {"work": 300000000, "weight": 0.3},
    "hbm": {"source_result": 0, "sampled_at_monotonic_ns": 12000000000},
    "physical": {
      "npu_utilization": {"source_result": 0, "sampled_at_monotonic_ns": 12090000000}
    },
    "vnpu": {
      "utilization": {"source_result": 0, "sampled_at_monotonic_ns": 12080000000}
    }
  }
}
```

detail 时间使用单调时钟纳秒，0 表示无对应时间；`window_ms` 仅表示 vNPU 统计窗口。
这些时间用于诊断，不是 metrics 样本末尾的 Unix 毫秒时间戳。

参数非法返回 HTTP 400；指定 UUID 或物理卡加 vNPU 不存在返回 404；
集合或标签无匹配返回 200 和空 items；整体查询失败返回 500。

### 查询 Prometheus 指标

`GET /metrics`

一次返回当前节点全部 allocation 及其关联物理卡的指标，不接受请求体和查询参数。

| 项目 | 约定 |
| --- | --- |
| Content-Type | `text/plain; version=0.0.4; charset=utf-8` |
| 编码 | UTF-8，以 LF 分隔，正文末尾保留换行 |
| 正文上限 | 8 MiB，超限返回失败 |
| 指标类型 | 全部为 Gauge |

```bash
curl --fail-with-body -sS --max-time 10 \
  --unix-socket /run/enpu/enpu-manager.sock \
  http://localhost/metrics
```

正常查询、空 allocation 集合或部分指标不可用时返回 HTTP 200；方法错误返回 405；整体查询或格式化失败返回 500。

#### 标签

| 标签 | 含义 |
| --- | --- |
| `allocation_id` | manager 生成的分配 UUID，在本次分配生命周期内稳定 |
| `phy_id` | 节点内物理资源 ID |
| `vnpu_id` | 物理资源下的 vNPU ID |
| `pod_uid` | 可选业务元数据，未填写时为空字符串 |
| `container_name` | 可选业务元数据，未填写时为空字符串 |
| `metric` | valid 指标所描述的指标名称 |

物理卡指标仅携带 `phy_id`，每张卡每类指标输出一次；allocation 指标携带
`allocation_id`、`phy_id`、`vnpu_id`、`pod_uid` 和 `container_name`。

`enpu_allocation_info` 每个活动 allocation 输出一条，值固定为 1，用于关联业务元数据、
vNPU 和物理卡。下游以 `allocation_id` 定位一次分配，以 `phy_id` 关联同一 manager 的物理指标。
一个物理卡可能关联多个 allocation，关联后的物理利用率不能按 allocation 求和。

#### 指标清单

利用率单位为百分数 0～100，显存单位为 bytes。

| 指标 | 标签 | 含义 |
| --- | --- | --- |
| `enpu_npu_utilization_percent` | `phy_id` | 物理 NPU 整体利用率 |
| `enpu_npu_aicore_utilization_percent` | `phy_id` | 物理 NPU AI Core 利用率 |
| `enpu_npu_aicpu_utilization_percent` | `phy_id` | 物理 NPU AI CPU 利用率 |
| `enpu_npu_ctrlcpu_utilization_percent` | `phy_id` | 物理 NPU Ctrl CPU 利用率 |
| `enpu_npu_cube_utilization_percent` | `phy_id` | 物理 NPU AI Cube 利用率 |
| `enpu_npu_vector_utilization_percent` | `phy_id` | 物理 NPU Vector Core 利用率 |
| `enpu_npu_metric_valid` | `phy_id, metric` | 对应物理指标是否有效 |
| `enpu_allocation_info` | allocation 标签 | allocation 元数据和设备关联，固定为 1 |
| `enpu_vnpu_aicore_quota_percent` | allocation 标签 | 启用算力限流时的时间共享配额 |
| `enpu_vnpu_core_limit_enabled` | allocation 标签 | 是否启用算力限流：1 开启，0 关闭 |
| `enpu_vnpu_hbm_request_bytes` | allocation 标签 | 配置的 HBM 申请量 |
| `enpu_vnpu_hbm_limit_bytes` | allocation 标签 | 配置的 HBM 上限 |
| `enpu_vnpu_hbm_used_bytes` | allocation 标签 | 来源业务容器在绑定设备上的 HBM 用量 |
| `enpu_vnpu_utilization_percent` | allocation 标签 | vNPU 的利用率估算值                   |
| `enpu_vnpu_aicore_utilization_percent` | allocation 标签 | vNPU 的 AI Core 利用率估算值 |
| `enpu_vnpu_metric_valid` | allocation 标签、`metric` | 对应 vNPU 观测指标是否有效 |

`enpu_npu_metric_valid` 的 `metric` 可为 `utilization_percent`、
`aicore_utilization_percent`、`aicpu_utilization_percent`、`ctrlcpu_utilization_percent`、
`cube_utilization_percent` 或 `vector_utilization_percent`；`enpu_vnpu_metric_valid` 的
`metric` 可为 `hbm_used_bytes`、`utilization_percent` 或 `aicore_utilization_percent`。

- vNPU 利用率根据近期 block 规模和抽样平均耗时计算工作量权重，再乘对应物理指标；它不是配额使用率。
- HBM 用量是容器内 DCMI 可见进程在绑定设备上的内存之和。
- best-effort 模式输出 `enpu_vnpu_core_limit_enabled=0`，省略 `enpu_vnpu_aicore_quota_percent`，其他可用指标照常输出。

#### 有效性与时间戳

数据有效时输出数值样本及 `valid=1`，有效值为 0 时仍明确输出 0。指标不支持、采集失败、
样本过期或快照异常时省略对应数值样本并输出 `valid=0`。allocation 释放后，其 info、配置和
观测样本均停止输出。整轮抓取失败由 Prometheus 的 `up=0` 表示。

每条数值样本的格式为：

```text
指标名{标签} 数值 Unix毫秒时间戳
```

时间戳表示本次查询完成时间，同一响应统一使用一个时间戳；各数据源的实际采样时间通过 JSON `detail` 查询。

#### 完整响应示例

下面示例覆盖全部基础指标族：同一物理卡上的两个 elastic allocation，算力配额分别为 40% 和 60%。
各示例独立展示，UUID、资源坐标和数值均为示意值。

```http
HTTP/1.1 200 OK
Content-Type: text/plain; version=0.0.4; charset=utf-8
```

```text
# HELP enpu_allocation_info allocation 元数据及设备关联。
# TYPE enpu_allocation_info gauge
enpu_allocation_info{allocation_id="45d75bc4-9c12-4c5e-87fb-197895599ae1",phy_id="0",vnpu_id="1",pod_uid="pod-a",container_name="infer"} 1 1793066400000
enpu_allocation_info{allocation_id="5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",phy_id="0",vnpu_id="2",pod_uid="",container_name=""} 1 1793066400000

# HELP enpu_npu_utilization_percent 物理 NPU 整体利用率，单位为百分数。
# TYPE enpu_npu_utilization_percent gauge
enpu_npu_utilization_percent{phy_id="0"} 80 1793066400000

# HELP enpu_npu_aicore_utilization_percent 物理 NPU 的 AI Core 利用率，单位为百分数。
# TYPE enpu_npu_aicore_utilization_percent gauge
enpu_npu_aicore_utilization_percent{phy_id="0"} 72 1793066400000

# HELP enpu_npu_aicpu_utilization_percent 物理 NPU 的 AI CPU 利用率，单位为百分数。
# TYPE enpu_npu_aicpu_utilization_percent gauge
enpu_npu_aicpu_utilization_percent{phy_id="0"} 15 1793066400000

# HELP enpu_npu_ctrlcpu_utilization_percent 物理 NPU 的 Ctrl CPU 利用率，单位为百分数。
# TYPE enpu_npu_ctrlcpu_utilization_percent gauge
enpu_npu_ctrlcpu_utilization_percent{phy_id="0"} 8 1793066400000

# HELP enpu_npu_cube_utilization_percent 物理 NPU 的 AI Cube 利用率，单位为百分数。
# TYPE enpu_npu_cube_utilization_percent gauge
enpu_npu_cube_utilization_percent{phy_id="0"} 65 1793066400000

# HELP enpu_npu_vector_utilization_percent 物理 NPU 的 Vector Core 利用率，单位为百分数。
# TYPE enpu_npu_vector_utilization_percent gauge
enpu_npu_vector_utilization_percent{phy_id="0"} 32 1793066400000

# HELP enpu_vnpu_utilization_percent vNPU 的 NPU 利用率估算值，单位为百分数。
# TYPE enpu_vnpu_utilization_percent gauge
enpu_vnpu_utilization_percent{allocation_id="45d75bc4-9c12-4c5e-87fb-197895599ae1",phy_id="0",vnpu_id="1",pod_uid="pod-a",container_name="infer"} 24 1793066400000
enpu_vnpu_utilization_percent{allocation_id="5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",phy_id="0",vnpu_id="2",pod_uid="",container_name=""} 56 1793066400000

# HELP enpu_vnpu_aicore_utilization_percent vNPU 的 AI Core 利用率估算值，单位为百分数。
# TYPE enpu_vnpu_aicore_utilization_percent gauge
enpu_vnpu_aicore_utilization_percent{allocation_id="45d75bc4-9c12-4c5e-87fb-197895599ae1",phy_id="0",vnpu_id="1",pod_uid="pod-a",container_name="infer"} 21.6 1793066400000
enpu_vnpu_aicore_utilization_percent{allocation_id="5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",phy_id="0",vnpu_id="2",pod_uid="",container_name=""} 50.4 1793066400000

# HELP enpu_vnpu_aicore_quota_percent 启用算力限流时配置的 AI Core 时间共享配额，单位为百分数。
# TYPE enpu_vnpu_aicore_quota_percent gauge
enpu_vnpu_aicore_quota_percent{allocation_id="45d75bc4-9c12-4c5e-87fb-197895599ae1",phy_id="0",vnpu_id="1",pod_uid="pod-a",container_name="infer"} 40 1793066400000
enpu_vnpu_aicore_quota_percent{allocation_id="5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",phy_id="0",vnpu_id="2",pod_uid="",container_name=""} 60 1793066400000

# HELP enpu_vnpu_core_limit_enabled 是否启用 AI Core 算力限流，1 表示开启，0 表示关闭。
# TYPE enpu_vnpu_core_limit_enabled gauge
enpu_vnpu_core_limit_enabled{allocation_id="45d75bc4-9c12-4c5e-87fb-197895599ae1",phy_id="0",vnpu_id="1",pod_uid="pod-a",container_name="infer"} 1 1793066400000
enpu_vnpu_core_limit_enabled{allocation_id="5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",phy_id="0",vnpu_id="2",pod_uid="",container_name=""} 1 1793066400000

# HELP enpu_vnpu_hbm_request_bytes 配置的 HBM 申请量，单位为 bytes。
# TYPE enpu_vnpu_hbm_request_bytes gauge
enpu_vnpu_hbm_request_bytes{allocation_id="45d75bc4-9c12-4c5e-87fb-197895599ae1",phy_id="0",vnpu_id="1",pod_uid="pod-a",container_name="infer"} 4294967296 1793066400000
enpu_vnpu_hbm_request_bytes{allocation_id="5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",phy_id="0",vnpu_id="2",pod_uid="",container_name=""} 4294967296 1793066400000

# HELP enpu_vnpu_hbm_limit_bytes 配置的 HBM 上限，单位为 bytes。
# TYPE enpu_vnpu_hbm_limit_bytes gauge
enpu_vnpu_hbm_limit_bytes{allocation_id="45d75bc4-9c12-4c5e-87fb-197895599ae1",phy_id="0",vnpu_id="1",pod_uid="pod-a",container_name="infer"} 6442450944 1793066400000
enpu_vnpu_hbm_limit_bytes{allocation_id="5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",phy_id="0",vnpu_id="2",pod_uid="",container_name=""} 8589934592 1793066400000

# HELP enpu_vnpu_hbm_used_bytes 来源业务容器在绑定设备上的 HBM 用量，单位为 bytes。
# TYPE enpu_vnpu_hbm_used_bytes gauge
enpu_vnpu_hbm_used_bytes{allocation_id="45d75bc4-9c12-4c5e-87fb-197895599ae1",phy_id="0",vnpu_id="1",pod_uid="pod-a",container_name="infer"} 2147483648 1793066400000
enpu_vnpu_hbm_used_bytes{allocation_id="5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",phy_id="0",vnpu_id="2",pod_uid="",container_name=""} 3221225472 1793066400000

# HELP enpu_npu_metric_valid 物理 NPU 指标是否具有有效样本，1 表示有效，0 表示无效。
# TYPE enpu_npu_metric_valid gauge
enpu_npu_metric_valid{phy_id="0",metric="utilization_percent"} 1 1793066400000
enpu_npu_metric_valid{phy_id="0",metric="aicore_utilization_percent"} 1 1793066400000
enpu_npu_metric_valid{phy_id="0",metric="aicpu_utilization_percent"} 1 1793066400000
enpu_npu_metric_valid{phy_id="0",metric="ctrlcpu_utilization_percent"} 1 1793066400000
enpu_npu_metric_valid{phy_id="0",metric="cube_utilization_percent"} 1 1793066400000
enpu_npu_metric_valid{phy_id="0",metric="vector_utilization_percent"} 1 1793066400000

# HELP enpu_vnpu_metric_valid vNPU 指标是否具有有效样本，1 表示有效，0 表示无效。
# TYPE enpu_vnpu_metric_valid gauge
enpu_vnpu_metric_valid{allocation_id="45d75bc4-9c12-4c5e-87fb-197895599ae1",phy_id="0",vnpu_id="1",pod_uid="pod-a",container_name="infer",metric="hbm_used_bytes"} 1 1793066400000
enpu_vnpu_metric_valid{allocation_id="45d75bc4-9c12-4c5e-87fb-197895599ae1",phy_id="0",vnpu_id="1",pod_uid="pod-a",container_name="infer",metric="utilization_percent"} 1 1793066400000
enpu_vnpu_metric_valid{allocation_id="45d75bc4-9c12-4c5e-87fb-197895599ae1",phy_id="0",vnpu_id="1",pod_uid="pod-a",container_name="infer",metric="aicore_utilization_percent"} 1 1793066400000
enpu_vnpu_metric_valid{allocation_id="5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",phy_id="0",vnpu_id="2",pod_uid="",container_name="",metric="hbm_used_bytes"} 1 1793066400000
enpu_vnpu_metric_valid{allocation_id="5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",phy_id="0",vnpu_id="2",pod_uid="",container_name="",metric="utilization_percent"} 1 1793066400000
enpu_vnpu_metric_valid{allocation_id="5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4",phy_id="0",vnpu_id="2",pod_uid="",container_name="",metric="aicore_utilization_percent"} 1 1793066400000
```

下游按指标名和标签解析，勿依赖样本顺序或 `HELP` 文案。

#### 下游查询示例

以下 PromQL 假设下游已采集 `/metrics`，抓取任务名为 `enpu`：

```promql
# 指定业务的 vNPU 利用率
enpu_vnpu_utilization_percent{job="enpu", pod_uid="pod-a", container_name="infer"}

# HBM 实际用量占配置上限的百分比
100 * enpu_vnpu_hbm_used_bytes{job="enpu"}
  / on(job, instance, allocation_id)
  (enpu_vnpu_hbm_limit_bytes{job="enpu"} > 0)

# allocation 所绑定物理卡的整体利用率
enpu_allocation_info{job="enpu", pod_uid="pod-a"}
  * on(job, instance, phy_id) group_left
  enpu_npu_utilization_percent{job="enpu"}

```

利用率为 Gauge，使用原值或时间聚合，不使用 `rate()`。

## vNPU 分配约束

分配 vNPU 时，请求需同时满足以下约束，否则分配失败并返回 `error_msg` 说明原因。

**表 8 vNPU 分配约束**

| 类别             | 约束                                                                                                                                                                                                |
|:-----------------|:----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| AICore          | 普通优先级的 `aicore_quota` ∈ [1, 100]；策略 `3`（best-effort）下仍需提供该值，但 vCANN-RT 不按此值限制算力。高优先级的生效值固定为 `0`。                                                     |
| HBM 基础        | `0 ≤ hbm_request ≤ hbm_limit`。                                                                                                                                                                     |
| HBM 上限        | `hbm_limit` 不得超过单 DIE HBM 总量。                                                                                                                                                                |
| DIE 超分        | 同 DIE 上所有 vNPU 的 `hbm_limit` 之和 ≤ `(1 + oversub_ratio[phy_id]) × die_hbm_total`。                                                                                                            |
| 调度策略        | `sched_policy=1`（fixed-share）：要求 `hbm_request > 0` 且 `hbm_request == hbm_limit`，**不支持显存超分**。                                                                                            |
| 调度策略        | `sched_policy=2`（elastic）/ `sched_policy=3`（best-effort）：允许任意 `0 ≤ hbm_request ≤ hbm_limit` 组合，**支持显存超分**。                                                                                  |
| 同卡策略一致性  | 同一 DIE 上的普通优先级 vNPU 必须使用相同 `sched_policy`；高优先级固定使用 `3`，不参与普通优先级策略一致性校验。                                                                                       |
| 超分 die 匹配   | vNPU 需要超分（`hbm_request < hbm_limit`）时，**只能**分配到 `oversub_ratio > 0` 的 die。                                                                                                            |
| 非超分优先      | vNPU 不需要超分（`hbm_request == hbm_limit`）时，**优先**分配到 `oversub_ratio == 0` 的 die；非超分 die 资源不足时才会回退到超分 die。                                                                          |
| 跨 DIE          | 不支持跨 DIE 分配（一个 vNPU 只能占用单 DIE 资源）。                                                                                                                                                  |
| 物理卡指定      | `phy_id >= 0` 时直接分配到该卡（跳过自动决策），仍受以上所有约束。                                                                                                                                    |

## 与 vCANN-RT 集成

vNPU 分配成功后，enpu-manager 会在宿主机侧生成 vCANN-RT 所需的配置文件：

**默认路径**：`/etc/enpu/vcann-rt/allocations/{allocation_id}/npu_info.config`，以创建响应中的 `config_path` 为准。

**表 9 npu_info.config 字段**

| 字段                | 说明                                                                                          |
|:--------------------|:----------------------------------------------------------------------------------------------|
| `allocation-id` | 分配 UUID。 |
| `physical-npu-id`   | 物理 NPU id（即 phy_id）。                                                                     |
| `virtual-npu-id`    | vNPU id（同一 phy 下唯一）。                                                                   |
| `aicore-quota`      | AICore 配额（%）；高优先级写入 `0`。                                                              |
| `memory-request`    | HBM 保底预留（MB）。                                                                            |
| `memory-limit`      | HBM 硬上限（MB）。                                                                              |
| `shm-id`            | 共享内存标识。                              |
| `scheduling-policy` | 调度策略（1/2/3，含义同 `sched_policy`）；高优先级写入 `3`。                                      |
| `vnpu_priority`     | vNPU 算力调度优先级（0=普通，1=高）；由分配请求写入。                                             |

业务容器启动时需要把这个文件挂载到容器内的固定路径 `/etc/enpu/vcann-rt/npu_info.config`，vCANN-RT 据此完成算力切分与显存控制。高优先级请求即使显式填写其他 AICore 配额或策略，生成文件中仍写入生效值 `0` 和 `3`；若绕过 enpu-manager 直接编写配置文件，则必须自行满足高优先级配置约束。具体挂载方式参见 [vCANN-RT 使用说明](../vcann-rt/README.md)。

## 环境变量汇总

**表 10 环境变量列表**

| 环境变量                  | 范围 | 默认值             | 说明                                                                                          |
|:--------------------------|:-----|:-------------------|:----------------------------------------------------------------------------------------------|
| `ASCEND_HOME_PATH`        | 编译 | /usr/local/Ascend/cann | CANN 安装路径，可通过 `source /path/to/cann/set_env.sh` 设置。                                  |
| `ENPU_ASCEND_DRIVER_PATH` | 编译 | /usr/local/Ascend  | HDK driver 安装路径。                                                                          |

> 通过 RPM / DEB 包安装的用户无需配置 `LD_LIBRARY_PATH`，运行时依赖已在包安装时部署到系统标准路径。仅手动安装方式下需自行保证 `libenpu_manager.so`、`libcjson.so.1` 等可被加载。

## 约束

- 单节点最大 NPU 数：16。
- 单 DIE AICore 配额上限：100。
- 由于硬件设备的限制（可以参考昇腾社区[使用约束](https://www.hiascend.com/document/detail/zh/canncommercial/850/appdevg/acldevg/aclcppdevg_000222.html)），不同代际产品单 device 支持的最大用户进程数不一样，建议单 DIE 上 vNPU 切分数量不超过对应产品支持的最大用户进程数。
- 当前版本仅支持 Docker 独立部署场景，暂不支持 Kubernetes 编排。
- 节点级 swap buffer 大小受宿主机 `MemFree` 限制；配置 `oversub-ratio` 过大致使 swap buffer 超过 `MemFree` 时，enpu-manager 拒绝启动。
- 启用显存超分的卡（`oversub_ratio > 0`）上所有容器必须启用软切分（vCANN-RT），否则 vCANN-RT 不会上报 HBM 使用量，enpu-manager 无法准确计算超分比例与触发换出。

## FAQ

1. **启动报错 `libcjson.so.1: cannot open shared object file`**

   enpu-manager 运行时依赖 cJSON 动态库。请通过 `find / -name "libcjson.so*"` 定位后，将其所在目录加入 `LD_LIBRARY_PATH`，或拷贝到 `/usr/local/lib/` 后执行 `ldconfig`。

2. **启动报错 `configured oversub-ratio too large`**

   配置的 `oversub-ratio` 计算出的 swap buffer 超过了宿主机 `MemFree`。请编辑 `/etc/enpu/enpu-manager.conf` 调小 `oversub-ratio` 后重启。

3. **分配 vNPU 返回 `card X rejected: sched_policy=N conflicts with existing vNPUs on the DIE`**

   同一 DIE 上不允许混用不同 `sched_policy`。请换一张卡，或调整请求使 `sched_policy` 与该 DIE 上已有 vNPU 一致。

4. **分配 vNPU 返回 `vNPU requires oversubscription (hbm_request<hbm_limit) but die has oversub_ratio=0`**

   请求要求显存超分（`hbm_request < hbm_limit`），但目标 die 在 `enpu-manager.conf` 中 `oversub_ratio` 配置为 0。请改用 `hbm_request == hbm_limit`（不超分），或在配置文件中给该 die 设置非 0 的 `oversub_ratio`。

5. **如何排查 vNPU 状态异常**

   ```bash
   # 查询当前所有分配
   curl --unix-socket /run/enpu/enpu-manager.sock http://localhost/api/v1/allocations

   # 查询所有设备资源
   curl --unix-socket /run/enpu/enpu-manager.sock http://localhost/api/v1/devices
   ```

6. **`GET /api/v1/allocations` 返回结果中出现 `error_msg` 字段**

   正常情况下不会出现。如果出现，表示当前节点上 vNPU 分配数量过多，返回结果被截断，本次返回**不完整**。实际报错形如 `json buffer (65535B) overflow at item %d (total %d), only first %d items returned; raise MAX_ALLOC_COUNT or split the query` 或 `registry count %d exceeds MAX_ALLOC_COUNT %d (truncated, please report)`。请按报错提示，精确释放不再使用的 vNPU 分配后重试：

   ```bash
   # 仅释放指定的 vNPU 分配。请勿使用 DELETE /api/v1/clean 全量清理，
   # clean 会释放节点上所有 vNPU 分配，包括正在运行业务的分配
   curl --unix-socket /run/enpu/enpu-manager.sock -X DELETE http://localhost/api/v1/allocations/5d861bbe-0c3e-4b10-84ab-4c60c53ad2d4
   ```
