# ubs-virt

<p> English | <a href="README.md">简体中文</a> </p>

> This project is open-sourced for the openEuler community. openEuler official website: <https://www.openeuler.org/>, official docs portal: <https://docs.openeuler.org/zh/>, UB software stack docs: <https://docs.openeuler.org/zh/docs/24.03_LTS_SP3/unifiedbus/unifiedbus/introduction/introduction.html>

## Overview

Ubs-virt includes multiple virtualization feature components, each offering capabilities like soft partitioning of NPU computing power, tuning VM linearity, and optimizing NPU passthrough for large model inference performance in VMs. Currently only the aarch64 architecture is supported for both build and runtime.

## Components

1. ubs-virt-enpu: ubs-virt enables NPU compute capability allocation, supporting configurable allocation of compute capability and graphics memory resources (delivered separately, not included in RPM packaging).
2. virt-awaresched: virt-awaresched optimizes virtualization scheduling, reducing the performance overhead of unnecessary vCPU migration and improving the linearity of VMs.
3. virt-optimizer: virt-optimizer provides NPU passthrough VM large model inference performance bottleneck analysis and performance optimization suggestions.

## Instruction

1. ubs-virt-enpu: [ubs-virt-enpu Instruction](./ubs-virt-enpu/README.md)
2. virt-awaresched: [virt-awaresched Instruction](./virt-awaresched/README.md)
3. virt-optimizer: [virt-optimizer Instruction](./virt-optimizer/README.md)

## Illustrate

This open-source project is not a Huawei product. Huawei offers only limited support.

## Component Documents

See the [docs/zh](./docs/zh) directory for detailed component documents.

## Build and Install

### Build dependencies

```bash
yum install gcc gcc-c++ cmake make patch systemd \
    libvirt-devel libboundscheck \
    clang libbpf-devel bpftool rapidjson-devel
```

### Build

```bash
# virt-awaresched (artifacts in virt-awaresched/build/bin/)
cd virt-awaresched && bash build.sh && cd ..

# virt-optimizer (artifacts in virt-optimizer/build/release/)
cd virt-optimizer && bash build.sh && cd ..
```

### RPM packaging

```bash
# Option 1: repository-wide spec (for the release repository ubs-virt.spec, currently packaging virt-awaresched)
rpmbuild -bb ubs-virt.spec

# Option 2: local per-component packaging
(cd virt-awaresched && bash build.sh package)
(cd virt-optimizer && bash build.sh)
```

The RPM layout of virt-awaresched complies with the openEuler packaging policy: executables under `/usr/bin`, systemd unit under `/usr/lib/systemd/system`.

## Directory Layout

```
ubs-virt
├── docs               # Component overview and per-component user documents
├── virt-awaresched    # VM aware scheduling service (src sources and test cases)
├── virt-optimizer     # Performance bottleneck analysis and tuning tool (ebpf/src sources and ebpf/tests test cases)
└── ubs-virt-enpu      # NPU compute partitioning service (delivered separately)
```

## Architecture

Components are deployed independently of each other:

- virt-awaresched: the vas-daemon service listens to VM events via libvirt to perform vCPU pinning and CPU compaction; vasctl is the CLI tool communicating with vas-daemon over socket.
- virt-optimizer: collects VM performance data via eBPF, analyzes inference performance bottlenecks of NPU passthrough VMs and provides tuning suggestions.
- ubs-virt-enpu: NPU compute partitioning service (delivered separately).

See [ARCHITECTURE.md](./virt-awaresched/docs/design/ARCHITECTURE.md) for the detailed design of virt-awaresched.

## Contributing

Refer to [CONTRIBUTING.md](./virt-awaresched/docs/contributing/CONTRIBUTING.md) for environment setup, coding style and UT guidelines.

Run unit tests:

```bash
cd virt-awaresched && bash build.sh test
```

## License

This project is licensed under [Mulan PSL v2](./LICENSE).
