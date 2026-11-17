# Virtual Aware Scheduler

<p> English | <a href="README.md">简体中文</a>

> This project has been open-sourced in the [openEuler](https://www.openeuler.org/) community. You are welcome to use and contribute to it.

## 📍Overview

Virtual Aware Scheduler (VSched) is a virtualization scheduling optimization service based on the Kunpeng CPU topology. The Kunpeng CPU topology is NUMA > Cluster > Core > CPU (SMP). VSched is designed to minimize cross-cluster scheduling of core-bound virtual machines and reduce vCPU thread drift between CPUs, thereby improving the LLC cache hit ratio and reducing the performance overhead caused by unnecessary vCPU migrations.

## 📌 Quick Start

### Environment Requirements

- OS: openEuler 24.03 LTS SP2 or later is recommended.
- Compilation toolchain: CMake ≥ 3.22, GCC ≥ 9.3
- Dependency library: For details, see [Build Guide](./docs/build_install/BUILD.md).

### Get Code

```bash
git clone https://gitcode.com/openeuler/ubs-virt
cd virt-awaresched
```

### Build the Project

- Build the RPM package in release mode by default (for production deployment).

    ```bash
    bash build.sh package
    ```

- Build the RPM package in debug mode (including debug information).

    ```bash
    bash build.sh -D package
    ```

The build artifacts are located in the `cmake-build-*` directory, and the RPM package is generated in the `output/` directory.

---

## 🧩 Core Features

- **Collection of information about VMs and CPU topology**<br>
    VSched collects virtual machine information, monitors virtual machine lifecycle events, while also collecting CPU topology information for vCPU scheduling.
- **CPU allocation and defragmentation**<br>
    VSched allocates suitable CPUs to vCPUs within the core binding range of virtual machines based on the cluster topology, minimizing cross-cluster scheduling. When a virtual machine is destroyed, VSched reclaims CPUs and defragments CPU resources.
- **Support for static and dynamic core binding**<br>
    VSched binds vCPU threads to cores statically or dynamically.
    - In static core binding, vCPUs run on designated CPUs.
    - In dynamic core binding, vCPUs run on a preferred core. If the CPU usage of the preferred core exceeds a specified threshold, vCPUs are migrated a better-performing core within the specified range. Dynamic core binding requires the kernel to support the tidal affinity feature.
- **CLI command support**<br>
    VSched provides O&M commands for administrators to query vCPU binding details, modify configurations, and manually trigger rescheduling.

> For details, see [CLI Document](./docs/cli/cli_docs_reference.md) and [Design Document](./docs/design/ARCHITECTURE.md).

---

## 📂 Project Structure

```txt
virt-awaresched
├── cmake # CMake tool encapsulation
├── docs # Documents
├── scripts # Scripts for development
│   └── ut_coverage
├── src # Source code directory
│   ├── cli # CLI framework
│   ├── include # Header file directory
│   ├── log # Log module
│   ├── util # Tool methods
│   ├── vasctl # vasctl
│   │   └── arg_parse # Binary instruction parsing
│   └── vasd # vas-daemon
│       ├── acquire # Information collection (incl. libvirt interaction)
│       ├── api # API
│       ├── arg_parse # Binary instruction parsing
│       ├── cluster_sched # Scheduling module
│       ├── conf # Configuration file
│       ├── looper # Main loop
│       └── security # Security module
└── test # Test directory
```

---

## 🧪 Developer Testing

The project contains complete unit tests (UTs).

- Run UTs only.

    ```bash
    bash build.sh test
    ```

- Run specific test cases.

    ```bash
    bash build.sh test -- --gtest_filter="TestClusterSched.*"
    ```

- Generate a code coverage report.

    ```bash
    # The report will be generated in the ./coverage directory.
    bash build.sh test -C
    ```

> For details about the test development guide, see [Unit Test Development Guide](./docs/test/TEST.md).

---

## 📚 Documentation Index

All technical documents are located in the [`docs/`](./docs) directory, including:

- **Architecture design**: [architecture.md](./docs/design/ARCHITECTURE.md)
- **Build and installation**: [BUILD.md](./docs/build_install/BUILD.md)
- **Configuration description**: [CONFIG.md](./docs/config/CONFIG.md)
- **CLI specifications**: [cli/](./docs/cli)
- **Example code**: [example/](./docs/example)
- **Test guide**: [test/](./docs/test)

---

## 🤝 Contributing

We welcome community developers to submit issues, PRs, or participate in discussions.<br>
Please read [Contribution Guide](docs/contributing/CONTRIBUTING.md) and comply with the openEuler community code of conduct.

---

## 📄 License

This project is licensed under [Mulan PSL v2](https://license.coscl.org.cn/MulanPSL2).

---
