# enpu-manager

## 介绍

`enpu-manager`是 ubs-virt-enpu 提供 NPU 显存交换（超分）服务的管理进程，运行在宿主机侧。它在 vCANN-RT 已有的算力切分和显存控制能力之上，引入节点级显存超分机制：允许将冷模型的显存换出到 CPU 侧 swap buffer，把腾出的显存让给其他容器使用，从而提升整卡显存利用率。

enpu-manager 通过 REST API 接收 vNPU 分配/释放/查询请求，生成容器侧 vCANN-RT 所需的 `npu_info.config` 配置文件，并配合 vCANN-RT 完成显存换出/换入的协调。

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

手动安装时系统未自动装入 `libcjson` 等运行时依赖，需自行解决（安装 `libcjson` 包或将 `.so` 路径加入 `LD_LIBRARY_PATH`）。

## 配置文件

配置文件路径：`/etc/enpu/enpu-manager.conf`。

**表 4 配置文件字段**

| 字段                       | 类型     | 默认值       | 是否可修改 | 说明                                                                                                                                                          |
|:---------------------------|:---------|:-------------|:-----------|:--------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `rest_port`                | int      | 8080         |  是       | REST API 监听端口。                                                                                                                                            |
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
enpu-manager [--port 8080]
```

启动成功后日志输出 `REST API listening on 0.0.0.0:8080` 即可对外提供服务。

### 验证部署成功

- 通过 `systemctl status enpu-manager` 确认服务状态为 `active (running)`，并查看日志中是否出现 `REST API listening on 0.0.0.0:8080`。如出现 `configured oversub-ratio too large`，需调小 `/etc/enpu/enpu-manager.conf` 中的 `oversub-ratio` 后重启。

- 调用 REST API 查询设备列表，确认 enpu-manager 已正确识别节点上的物理 NPU：

  ```bash
  curl http://localhost:8080/api/v1/devices
  ```

  正常返回 JSON，`devices` 数组中应包含每张可用物理卡的 `phy_id / minor_name / aicore_available / hbm_available / oversub_ratio` 等字段。

- 分配一个测试 vNPU 验证分配链路：

  ```bash
  curl -X POST http://localhost:8080/api/v1/allocate \
    -H "Content-Type: application/json" \
    -d '{"aicore_quota":10,"hbm_quota":1024,"hbm_limit":1024,"sched_policy":1}'
  ```

  正常返回包含 `phy_id / vnpu_id / pod_uid / container_name` 等字段，`result=0` 表示分配成功。

- 查询当前分配记录，确认上一步分配的 vNPU 已登记：

  ```bash
  curl http://localhost:8080/api/v1/allocations
  ```

- 在宿主机侧确认 vCANN-RT 配置文件已生成：

  ```bash
  ls /etc/enpu/vcann-rt/
  # 应能看到以 pod_uid 命名的目录，其下有 container_name 命名的子目录，里面包含 npu_info.config
  ```

- 释放测试 vNPU 完成清理（其中 `pod-0` / `container-0-X` 替换为上一步返回的实际值）：

  ```bash
  curl -X DELETE http://localhost:8080/api/v1/allocations/pod-0/container-0-0
  ```

## REST API 使用

enpu-manager 通过 HTTP REST API 接收请求，默认端口 8080，返回 JSON 格式。当前对外开放的接口包含**分配、释放、查询、清理**四类；服务端口上另有若干仅供内部调试使用的接口，不属于对外开放范围，不提供兼容性保证，请勿依赖。

### API 总览

**表 6 REST API 列表**

| 端点                                  | 方法   | 说明                                       |
|:--------------------------------------|:-------|:-------------------------------------------|
| `/api/v1/devices`                     | GET    | 列出所有 NPU 设备及其可用资源。             |
| `/api/v1/devices/{phy_id}`            | GET    | 查询指定物理卡详情（含 oversub_ratio）。    |
| `/api/v1/allocate`                    | POST   | 分配 vNPU。                                |
| `/api/v1/allocations/{pod_uid}/{container_name}` | DELETE | 释放指定 vNPU。                            |
| `/api/v1/allocations`                 | GET    | 列出当前所有 vNPU 分配。                    |
| `/api/v1/clean`                       | DELETE | 清理当前所有 vNPU 分配。                    |

### 分配 vNPU

`POST /api/v1/allocate`

**表 7 请求字段**

| 字段               | 类型    | 必填 | 说明                                                                                       |
|:-------------------|:--------|:-----|:-------------------------------------------------------------------------------------------|
| `aicore_quota`     | int     | 普通优先级必填 | AICore 配额，普通优先级范围 1~100；高优先级可省略，实际使用 0，表示不预留软件算力配额。 |
| `hbm_quota`        | int | 是   | HBM Request（保底预留），单位 MB，等价别名 `hbm_request`。                                            |
| `hbm_limit`        | int | 否   | HBM Limit（硬上限），单位 MB。未填时默认等于 `hbm_quota`。                                              |
| `sched_policy`     | int     | 普通优先级必填 | 调度策略：`1`=fixed-share，`2`=elastic，`3`=best-effort；高优先级可省略，实际使用 `3`。 |
| `vnpu_priority`    | int     | 否   | vNPU 优先级：`0`=普通优先级（默认），`1`=高优先级。 |
| `swap_priority`    | int     | 否   | 换出优先级：`0`=HIGH（最后换出），`1`=MEDIUM（默认），`2`=LOW（最先换出）。                  |
| `phy_id`           | int     | 否   | 指定物理卡：`-1`=由 enpu-manager 自动选择（默认）；`>=0`=直接分配到该卡（跳过自动决策）。     |
| `pod_uid`          | string  | 否   | Pod 标识符。未填时默认 `pod-<phy_id>`。                                                     |
| `container_name`   | string  | 否   | 容器名。未填时默认 `container-<phy_id>-<vnpu_id>`。                                          |

> `pod_uid` 和 `container_name` 共同构成 vNPU 的复合主键，后续释放/查询时需要提供。未指定时使用默认值，响应体中会回显实际使用的值。
>
> 未填 `hbm_limit` 时默认等于 `hbm_quota`，相当于 `hbm_request == hbm_limit`。**普通优先级采用 `sched_policy=1`（fixed-share）时建议显式传 `hbm_limit` 与 `hbm_quota` 相等**，避免误解。

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
curl -X POST http://localhost:8080/api/v1/allocate \
  -H "Content-Type: application/json" \
  -d '{
    "aicore_quota": 20,
    "hbm_quota": 10240,
    "hbm_limit": 32768,
    "sched_policy": 2
  }'
```

响应：

```json
{
  "phy_id": 0,
  "vnpu_id": 5,
  "pod_uid": "pod-0",
  "container_name": "container-0-5",
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

**示例 2：指定物理卡，使用自定义 pod_uid**

```bash
curl -X POST http://localhost:8080/api/v1/allocate \
  -H "Content-Type: application/json" \
  -d '{
    "aicore_quota": 20,
    "hbm_quota": 10240,
    "hbm_limit": 10240,
    "sched_policy": 1,
    "phy_id": 0,
    "pod_uid": "pod-abc",
    "container_name": "container-1"
  }'
```

**示例 3：fixed-share（不支持显存超分，`hbm_quota == hbm_limit`）**

```bash
curl -X POST http://localhost:8080/api/v1/allocate \
  -H "Content-Type: application/json" \
  -d '{
    "aicore_quota": 30,
    "hbm_quota": 16384,
    "hbm_limit": 16384,
    "sched_policy": 1
  }'
```

### 释放 vNPU

```bash
curl -X DELETE http://localhost:8080/api/v1/allocations/pod-0/container-0-5
```

### 清理所有 vNPU 分配

一次性释放当前所有 vNPU 分配（释放 shm / quota / 配置文件），适用于环境重置、批量回收等场景：

```bash
curl -X DELETE http://localhost:8080/api/v1/clean
```

返回示例：`{"released_count":N,"result":0,"error_msg":""}`，其中 `released_count` 表示本次清理的 vNPU 数量，该接口不会让 enpu-manager 进程退出。

### 查询设备

```bash
# 列出所有设备
curl http://localhost:8080/api/v1/devices

# 查询单卡详情（含该 die 的 oversub_ratio）
curl http://localhost:8080/api/v1/devices/0
```

### 查询分配

```bash
curl http://localhost:8080/api/v1/allocations
```

## vNPU 分配约束

分配 vNPU 时，请求需同时满足以下约束，否则分配失败并返回 `error_msg` 说明原因。

**表 8 vNPU 分配约束**

| 类别             | 约束                                                                                                                                                                                                |
|:-----------------|:----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| AICore          | `aicore_quota` ∈ [1, 100]。                                                                                                                                                                          |
| HBM 基础        | `0 ≤ hbm_request ≤ hbm_limit`。                                                                                                                                                                     |
| HBM 上限        | `hbm_limit` 不得超过单 DIE HBM 总量。                                                                                                                                                                |
| DIE 超分        | 同 DIE 上所有 vNPU 的 `hbm_limit` 之和 ≤ `(1 + oversub_ratio[phy_id]) × die_hbm_total`。                                                                                                            |
| 调度策略        | `sched_policy=1`（fixed-share）：要求 `hbm_request > 0` 且 `hbm_request == hbm_limit`，**不支持显存超分**。                                                                                            |
| 调度策略        | `sched_policy=2`（elastic）/ `sched_policy=3`（best-effort）：允许任意 `0 ≤ hbm_request ≤ hbm_limit` 组合，**支持显存超分**。                                                                                  |
| 同卡策略一致性  | 同一 DIE 上所有 vNPU 必须使用相同 `sched_policy`。                                                                                                                                                  |
| 超分 die 匹配   | vNPU 需要超分（`hbm_request < hbm_limit`）时，**只能**分配到 `oversub_ratio > 0` 的 die。                                                                                                            |
| 非超分优先      | vNPU 不需要超分（`hbm_request == hbm_limit`）时，**优先**分配到 `oversub_ratio == 0` 的 die；非超分 die 资源不足时才会回退到超分 die。                                                                          |
| 跨 DIE          | 不支持跨 DIE 分配（一个 vNPU 只能占用单 DIE 资源）。                                                                                                                                                  |
| 物理卡指定      | `phy_id >= 0` 时直接分配到该卡（跳过自动决策），仍受以上所有约束。                                                                                                                                    |

## 与 vCANN-RT 集成

vNPU 分配成功后，enpu-manager 会在宿主机侧生成 vCANN-RT 所需的配置文件：

**路径**：`/etc/enpu/vcann-rt/{pod_uid}/{container_name}/npu_info.config`

**表 9 npu_info.config 字段**

| 字段                | 说明                                                                                          |
|:--------------------|:----------------------------------------------------------------------------------------------|
| `physical-npu-id`   | 物理 NPU id（即 phy_id）。                                                                     |
| `virtual-npu-id`    | vNPU id（同一 phy 下唯一）。                                                                   |
| `aicore-quota`      | AICore 配额（%）。                                                                              |
| `memory-request`    | HBM 保底预留（MB）。                                                                            |
| `memory-limit`      | HBM 硬上限（MB）。                                                                              |
| `shm-id`            | 共享内存标识。                              |
| `scheduling-policy` | 调度策略（1/2/3，含义同 `sched_policy`）。                                                       |

业务容器启动时需要把这个文件挂载到容器内的固定路径 `/etc/enpu/vcann-rt/npu_info.config`，vCANN-RT 据此完成算力切分与显存控制。具体挂载方式参见 [vCANN-RT 使用说明](../vcann-rt/README.md)。

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
   curl http://localhost:8080/api/v1/allocations

   # 查询所有设备资源
   curl http://localhost:8080/api/v1/devices
   ```

6. **`GET /api/v1/allocations` 返回结果中出现 `error_msg` 字段**

   正常情况下不会出现。如果出现，表示当前节点上 vNPU 分配数量过多，返回结果被截断，本次返回**不完整**。实际报错形如 `json buffer (65535B) overflow at item %d (total %d), only first %d items returned; raise MAX_ALLOC_COUNT or split the query` 或 `registry count %d exceeds MAX_ALLOC_COUNT %d (truncated, please report)`。请按报错提示，精确释放不再使用的 vNPU 分配后重试：

   ```bash
   # 仅释放指定的 vNPU 分配。请勿使用 DELETE /api/v1/clean 全量清理，
   # clean 会释放节点上所有 vNPU 分配，包括正在运行业务的分配
   curl -X DELETE http://localhost:8080/api/v1/allocations/pod-0/container-0-0
   ```
