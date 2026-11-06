# MCS

## 介绍

`MCS`是ubs-virt-enpu提供NPU算力软切分，以实现多vNPU分核共享同一物理NPU算力资源的方案。本方案需要依赖Linux系统Preload Hook的能力，通过预加载MCS动态库，拦截部分runtime API的调用，根据配置文件中的算力和显存资源配额信息进行算力控制和显存控制，并基于共享内存实现同一物理NPU上多个vNPU之间的优先级调度。

## 环境准备

### 软件版本

#### Atlas A2 推理系列产品、Atlas 950 SuperPoD 产品

**表 1 软件版本**

| 软件 | 版本 |
|:-----|:-----|
| CANN | 9.1.0 |
| HDK  | 与CANN配套的驱动版本 |


### 主机侧环境配置

主机侧通过`npu-smi`工具开启容器共享模式，可支持多个容器挂载同一设备。若设备未开启容器共享模式，则只能挂载到单个容器。若配合MindCluster使用，要求整节点开启容器共享模式。

```bash
# Atlas A2 推理系列产品：设置容器共享模式
npu-smi set -t device-share -i ${id} -c ${chip_id} -d ${value}

# Atlas 950 SuperPoD 产品：设置容器共享模式
npu-smi set -t device-share -i ${id} -d ${value}

# 查询设备容器共享模式
npu-smi info -t device-share -i ${id}
```

**表 2 参数说明**

|参数|参数选项|说明|
|:---|:---|:---|
|id|设备id|通过`npu-smi info -l`命令查询获取的NPU ID即为设备id。|
|chip_id|芯片id|通过`npu-smi info -m`命令查询获取的Chip ID即为芯片id。|
|value|<ul>默认值：禁用<li>禁用(0)</li><li>使能(1)</li></ul>|容器共享模式使能状态。|

示例：
开启设备0所有芯片的容器共享模式，查询设备0容器共享模式。

  ```bash
  npu-smi set -t device-share -i 0 -d 1
  npu-smi info -t device-share -i 0
  ```

主机侧可设置容器共享持久化使能状态。若持久化功能为开启状态，则重启系统后设备的容器共享模式使能状态与重启前保持一致。

```bash
npu-smi set -t device-share-cfg-recover -d ${value}
```

**表 3 参数说明**

|参数|参数选项|说明|
|:---|:---|:---|
|value|<ul>默认值：禁用<li>禁用(0)</li><li>使能(1)</li></ul>|容器共享持久化使能状态。|

## 源码获取

可以使用如下方式获取`MCS`源码。

```bash
git clone https://gitcode.com/openeuler/ubs-virt.git
cd ubs-virt-enpu/mcs
```

### 源码目录结构

`MCS`源码的主要目录结构如下所示：

```bash
.
├── common    // 公共模块：runtime hook框架、显存配额检查（guard_memory）
├── scripts   // 存放项目中编译构建使用的cmake脚本文件
└── src       // 存放MCS功能实现源码：API拦截、配置解析、优先级调度、显存控制、日志
```

## 编译

编译前先加载所选 CANN 版本的环境。以下路径请替换为实际安装路径：

```bash
source /path/to/cann/set_env.sh
```

`MCS`在代码仓中提供了统一的编译构建脚本（即`make_build.sh`文件），可以直接执行该脚本文件进行编译构建。

```bash
bash make_build.sh
```

编译完成之后，会在`build`目录下产生软切分动态库`libmcs.so`。

## 部署

### 创建vNPU配置文件

针对每个vNPU，需要创建一个配置文件，默认路径为`/etc/enpu/mcs/mcs_config.config`（可通过环境变量`MCS_CONFIG`覆盖）。配置文件格式和字段示例如下：

```bash
physical-npu-id=0
virtual-npu-id=0
aicore-quota=20
memory-quota=8192
shm-id=11111111-22222222-33333333-44444444-55555555
vnpu_priority=0
```

**表 4 配置项说明**

| 参数 | 是否必填 | 参数选项                                             | 说明                                                       |
|:-----|:---|:-------------------------------------------------|:---------------------------------------------------------|
| physical-npu-id | 是 | 物理NPU id，>=0                                     | 表示使用第几张物理NPU。                                            |
| virtual-npu-id | 是 | vNPU id                                          | 同一物理NPU下的vNPU不允许重复。                                      |
| aicore-quota | 是 | 算力资源配额，单位为%，整数1~100                              | 非法值（缺失、0、负数、非数字）将导致MCS拒绝启动。                              |
| memory-quota | 是 | HBM显存配额，单位为MB，>0                                 | 当前进程使用的设备显存总量不能超过该配额。                                    |
| shm-id | 是 | 共享内存标识                                           | 同一物理NPU上所有vNPU必须配置相同的shm-id，否则优先级调度不生效。                  |
| vnpu_priority | 否 | <ul>默认0<li>0: 普通优先级</li><li>1: 高优先级</li></ul>    | 本vNPU的调度优先级；配置为其他值将导致MCS拒绝启动。 |

此外，需要设置配置文件具有合适的权限，建议为644。

### 启动业务容器（docker方式部署）

启动业务容器时，需要将NPU设备、驱动、MCS动态库和配置文件挂载到容器，并共享`/dev/shm`（优先级调度依赖POSIX共享内存）。容器启动命令可参考（假如使用第0张NPU卡）：

```bash
docker run -it --name=${container_name} \
  --device=/dev/davinci0:/dev/davinci0 \
  --device=/dev/davinci_manager \
  --device=/dev/hisi_hdc:/dev/hisi_hdc \
  -v /usr/local/Ascend/driver:/usr/local/Ascend/driver \
  -v /xxx/libmcs.so:/opt/enpu/mcs/lib/libmcs.so \
  -v /xxx/mcs_config.config:/etc/enpu/mcs/mcs_config.config \
  -v ${preload_path}/ld.so.preload:/etc/ld.so.preload \
  -v /dev/shm:/dev/shm \
  ${image_name} /bin/bash
```

    注：若启动容器时，不映射ld.so.preload文件时，可通过`export LD_PRELOAD=/opt/enpu/mcs/lib/libmcs.so`命令加载动态库。

**表 5 参数说明**

| 文件/工具 | 路径 |
|:-----|:-----|
| MCS动态库 | 容器内建议路径：`/opt/enpu/mcs/lib/libmcs.so` |
| 物理NPU设备 | 主机侧和容器内均为固定路径：`/dev/davinci0` |
| 共享内存设备 | 主机侧和容器内均为固定路径：`/dev/shm` |
| 配置文件 | <ul><li>主机侧可存放在自定义路径。</li><li>容器内为固定路径：`/etc/enpu/mcs/mcs_config.config`</li></ul> |

### 启动业务，使用MCS服务

- 拉起业务时，可通过环境变量`ENPU_LOG_LEVEL`配置日志级别，例如：

    ```bash
    export ENPU_LOG_LEVEL=3
    ```
    日志级别由高到低分别是FATAL(0), ERROR(1), WARN(2), INFO(3), DEBUG(4)。默认日志级别为INFO，设置日志级别为DEBUG后，可在日志文件（日志文件路径：`/var/log/enpu/mcs/`）中查看DEBUG日志，屏显仅打印INFO及以上级别日志。

  - 在容器内可查询配置文件获取vNPU资源配额等信息：

    ```bash
    cat /etc/enpu/mcs/mcs_config.config
    ```

## 环境变量汇总

**表 6 环境变量列表**

| 环境变量 | 范围 | 默认值 | 说明 |
|:---------|:-----|:-------|:-----|
| `LD_PRELOAD` | 运行 | 无 | 预加载MCS动态库`libmcs.so`，使能MCS能力。 |
| `MCS_CONFIG` | 运行 | `/etc/enpu/mcs/mcs_config.config` | vNPU配置文件路径。 |
| `ENPU_LOG_LEVEL` | 运行 | `3` | 日志级别，FATAL(0), ERROR(1), WARN(2), INFO(3), DEBUG(4)。 |
| `MCS_STARVATION_LIMIT` | 运行 | `300` | 反饿死突发上限，高优先级vNPU连续发射多少个kernel后放行一个普通优先级launch，需配置为>=1的整数。 |
| `ASCEND_HOME_PATH` | 编译 | `/usr/local/Ascend/cann` | CANN安装路径，提供libboundscheck头文件与库，可通过`source /path/to/cann/set_env.sh`设置。 |

## 约束

- 同一物理NPU上所有vNPU必须配置相同的`shm-id`，且`virtual-npu-id`互不相同（范围[0, 100)），否则优先级调度行为异常。
- `aicore-quota`必须配置为1~100的整数；`vnpu_priority`只能为0或1。配置缺失或非法时MCS初始化失败、`rtSetDevice`返回错误，不会退化为无限制直通。
- MCS不修改kernel启动参数，算力配额通过"上报受限核数"实现，需要上层框架按`rtGetAiCoreCount`/`rtGetDeviceInfo`的返回值进行host侧tiling和并发控制才会生效。
- 显存配额按进程粒度统计，不做跨进程聚合；如需容器级强隔离，需配合其他机制。
- MCS动态库运行依赖libboundscheck，需确保业务环境可正常链接该库。
- 优先级调度依赖`/dev/shm`共享内存，日志落盘依赖`/var/log/enpu/mcs/`目录写权限，部署时需保证进程具有相应权限。

## FAQ

1. 业务调用`rtSetDevice`时报错`[MCS] FATAL: MCS init failed - refusing rtSetDevice`：

    检查vNPU配置文件：必填字段是否齐全、`aicore-quota`是否在[1, 100]区间内、`vnpu_priority`是否为0或1、`shm-id`是否与其他vNPU一致。具体原因可查看`/var/log/enpu/mcs/`目录下的日志。

2. 业务分配显存时报错`ACL_ERROR_STORAGE_OVER_LIMIT`：

    进程当前占用的设备显存总量已超过配置文件中的`memory-quota`，可适当调大配额或降低业务显存占用。配额按实时占用统计：分配计入口径，释放（`rtFree`/`rtDvppFree`/`rtMemFree`）相应扣减。

3. 配置了高优先级但调度不生效：

    确认同一物理NPU上各vNPU容器的`shm-id`配置一致、`/dev/shm`已正确挂载，并通过`ENPU_LOG_LEVEL=4`查看DEBUG日志确认初始化时注册的优先级。
