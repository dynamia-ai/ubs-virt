# ENPU runtime image

`ghcr.io/dynamia-ai/enpu-runtime:1.0.0-cann9.1.0-r1` 保存预编译的 ARM64 ENPU 运行库，供 device-plugin 在构建时复制。镜像仅包含运行库产物，不包含 CANN SDK、编译器或驱动。插件 CI 无需重复编译 ENPU。

## 固定输入

| 输入 | 版本 |
| --- | --- |
| ubs-virt | `1.0.0`，提交 `46b6d55a892429008b1e02f96b0a0f81d1176db8` |
| CANN | `9.1.0` |
| 官方构建镜像 | `swr.cn-north-4.myhuaweicloud.com/ubscore/ubs-virt:oe2403-v1@sha256:f42bfc52ba01eaf1652421477b88425631a37ecd990191bf26c94084ce1a2c10` |
| 构建平台 | `linux/arm64` |

打包脚本从固定提交导出源码，与当前工作分支的运行库代码无关。构建过程使用镜像内的 CANN 和驱动 SDK，不需要 NPU、宿主机驱动或设备挂载；编译阶段禁用网络。

## 构建

需要 Git、Python 3 和支持 ARM64 的 Docker Buildx。推荐在 ARM64 Linux 上构建；AMD64 主机需要预先配置 QEMU。

```sh
bash packaging/enpu-runtime/prepare-context.sh
docker buildx build --platform linux/arm64 --provenance=false \
  --output type=oci,dest=enpu-runtime.oci.tar \
  -t ghcr.io/dynamia-ai/enpu-runtime:1.0.0-cann9.1.0-r1 \
  .build/enpu-runtime
```

脚本要求本地 Git 对象库包含固定提交。浅克隆需要先获取对应的 `1.0.0` 标签。产物位于镜像的 `/enpu-runtime`：

- `libvruntime.so`、`enpu-monitor` 和 `ld.so.preload`。
- `build-info.json`：源码提交、源码包校验值、构建镜像 digest、CANN 版本和产物校验值。
- `LICENSE.ubs-virt`：对应源码版本的 Mulan PSL v2 许可证。
- `SHA256SUMS`：包括构建信息和许可证在内的文件校验值。

构建会检查两个二进制文件的 AArch64 ELF 头并验证校验值。此验证不代表真实 Ascend 硬件验收，也不启用 AMD64 ENPU。

## 发布与消费

`ENPU runtime image` 工作流在相关文件变更时只构建。需要发布时，在 `master` 上手动运行并启用 `publish`。工作流拒绝覆盖已存在的版本；修改运行库、SDK 或打包方式时，应更新版本中的 `r1` 修订号或对应组件版本。

首次发布后，将 GHCR package 的可见性设为 Public，并在没有凭据的环境验证拉取。保留实际发布的 digest，供消费者固定使用；不要使用浮动的 `latest` 标签。

消费者显式选择 ARM64 资产阶段，即使最终镜像也提供 AMD64 版本：

```dockerfile
ARG ENPU_RUNTIME_IMAGE
FROM --platform=linux/arm64 ${ENPU_RUNTIME_IMAGE} AS enpu-runtime
FROM ubuntu:20.04
COPY --from=enpu-runtime /enpu-runtime/ /usr/local/enpu-runtime-assets/
RUN cd /usr/local/enpu-runtime-assets && sha256sum -c SHA256SUMS
```

构建时将 `ENPU_RUNTIME_IMAGE` 设为 `ghcr.io/dynamia-ai/enpu-runtime:<version>@sha256:<published-digest>`。更新消费者前，先确认新镜像可匿名拉取、架构和校验信息正确。此镜像不替代工作负载中的 CANN 环境或节点驱动。
