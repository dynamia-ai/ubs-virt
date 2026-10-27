# syntax=docker/dockerfile:1
ARG BUILDER_IMAGE=swr.cn-north-4.myhuaweicloud.com/ubscore/ubs-virt:oe2403-v1@sha256:f42bfc52ba01eaf1652421477b88425631a37ecd990191bf26c94084ce1a2c10
ARG CANN_VERSION=9.1.0
ARG SOURCE_REVISION=46b6d55a892429008b1e02f96b0a0f81d1176db8
ARG IMAGE_VERSION=1.0.0-cann9.1.0-r1
FROM --platform=linux/arm64 ${BUILDER_IMAGE} AS build
ARG BUILDER_IMAGE
ARG CANN_VERSION
ARG SOURCE_REVISION
COPY source.tar source.json LICENSE.ubs-virt build-runtime.sh /input/
RUN --network=none bash /input/build-runtime.sh "$CANN_VERSION" "$BUILDER_IMAGE" "$SOURCE_REVISION"

# Asset image only: no SDK, compiler or host driver is distributed.
FROM scratch
ARG CANN_VERSION
ARG SOURCE_REVISION
ARG IMAGE_VERSION
LABEL org.opencontainers.image.source="https://github.com/dynamia-ai/ubs-virt" \
      org.opencontainers.image.description="ARM64 ENPU runtime assets built with CANN ${CANN_VERSION}" \
      org.opencontainers.image.licenses="MulanPSL-2.0" \
      org.opencontainers.image.version="${IMAGE_VERSION}" \
      org.opencontainers.image.revision="${SOURCE_REVISION}"
COPY --from=build /output/ /enpu-runtime/
