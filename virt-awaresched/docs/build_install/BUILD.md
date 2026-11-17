# 构建指导

virt-awaresched支持两种方式编译构建：

## 方式一：基于物理机环境编译

### 拉取源码

```shell
git clone https://gitcode.com/openeuler/ubs-virt.git
# 进入项目目录
cd virt-awaresched
```

### 构建工具链准备

```shell
sudo yum install gcc-c++ gcc cmake make -y
```

### 构建

#### 构建依赖

virt-awaresched构建依赖信息已经记录在spec文件（virt-awaresched.spec）中。

具体内容如下：

```shell
Requires: patch libvirt-devel libboundscheck
```

#### 安装依赖

yum命令手动安装

```shell
sudo yum install patch libvirt-devel libboundscheck -y
```

#### 执行构建

- 执行Release构建（无调试信息），产物输出到项目顶层目录的build/目录下。

    ```shell
    bash build.sh
    ```

- 执行Debug构建（包含调试信息），产物输出到项目顶层目录的build/目录下。

    ```shell
    bash build.sh -D
    ```

### 生成rpm包

构建项目，并打包成RPM文件输出到项目顶层目录下得output/目录下。

```shell
bash build.sh package
```

产物如下：

```filepath
└── output                                                            # 打包输出文件目录
    └── ubs-virt-awaresched-<version>-<release>.aarch64.rpm                           # virt-awaresched rpm安装包
```

## 方式二：基于预编译镜像环境编译

预编译镜像中已经安装 `virt-awaresched` 编译构建和单元测试所需的构建工具及系统依赖，可以直接基于该镜像启动容器并执行构建或单元测试，从而减少基础环境准备工作。

### 下载预编译镜像

该镜像允许匿名拉取，执行：

```shell
docker pull swr.cn-north-4.myhuaweicloud.com/ubscore/ubs-virt:oe2403-v1
```

> 若返回 `denied` 或 `unauthorized`，请向镜像仓库管理员申请读取权限后重试。

### 启动构建容器

执行以下命令启动容器：

```shell
docker run -it \
    --name=ubs-virt \
    swr.cn-north-4.myhuaweicloud.com/ubscore/ubs-virt:oe2403-v1 \
    /bin/bash
```

> 源码编译、单元测试不需要将宿主机的 `/lib/modules` 等目录挂载到容器，也不需要额外的内核能力。
>
> 首次构建 UT 时，构建系统会下载 GoogleTest、MockCpp 及 MockCpp ARM64 补丁，需确保容器可以访问 `cmake/gtest.cmake` 和 `cmake/mockcpp.cmake` 中配置的下载地址。

### 在容器内构建

进入容器后，下载源码并执行构建：

```shell
git clone -b openEuler-24.03-LTS-SP3 \
    https://gitcode.com/openeuler/ubs-virt.git \
    /home/ubs-virt-src
cd /home/ubs-virt-src/virt-awaresched
bash build.sh
```

编译完成之后，产物输出到项目顶层目录的`build/`目录下，与方式一的编译产物一致。

如需生成Debug版本，可执行：

```shell
bash build.sh -D
```

如需在同一容器中运行单元测试，可执行：

```shell
bash build.sh test
```

`bash build.sh test` 会完成 UT 的构建并运行全部用例，无需另行启动容器。

### 在容器内生成rpm包

如需生成RPM包，可执行：

```shell
bash build.sh package
```

`bash build.sh package`在执行编译的基础上，会额外将编译产物打包生成RPM安装包，产物与方式一"生成rpm包"的产物一致，输出到项目顶层目录的`output/`目录下，即`ubs-virt-awaresched-<version>-<release>.aarch64.rpm`。
