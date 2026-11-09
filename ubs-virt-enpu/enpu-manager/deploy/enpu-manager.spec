# enpu-manager RPM Spec
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# Licensed under Mulan PSL v2.

%global _hardened_build 1
%define debug_package %{nil}

# ====================================================================
# Package Information
# ====================================================================
Name:           enpu-manager
Version:        1.0.0
Release:        1%{?dist}
Summary:        Ascend NPU Management System

License:        MulanPSL-2.0
URL:            https://github.com/enpu-manager
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.15
BuildRequires:  gcc >= 9.3
BuildRequires:  systemd
BuildRequires:  cjson-devel
BuildRequires:  libuuid-devel
Requires:       cjson
Requires:       libuuid
Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd

# NOTE: CANN Toolkit + Driver are NOT declared as RPM dependencies.

%description
ENPU Manager is a management system for Huawei Ascend NPU devices.
It supports device discovery, vNPU partitioning, elastic memory management,
memory swapping, and both Kubernetes and standalone deployment scenarios.

%prep
%setup -q

%build
# Validate required macros
%{!?_ascend_home_path: %{error: "_ascend_home_path must be defined. Use --define '_ascend_home_path /path/to/cann'"}}
%{!?_enpu_ascend_driver_path: %{error: "_enpu_ascend_driver_path must be defined. Use --define '_enpu_ascend_driver_path /path/to/ascend'"}}

# Set CANN environment
export ASCEND_HOME_PATH="%{_ascend_home_path}"
export ENPU_ASCEND_DRIVER_PATH="%{_enpu_ascend_driver_path}"

# Step 1: Build core shared library
pushd enpu-manager/core
bash make_build.sh
popd

# Step 2: Build standalone binaries (enpu-manager + enpu-cli)
pushd enpu-manager/standalone
echo "[INFO] Building standalone binaries..."
bash build.sh
popd

%install
install -D enpu-manager/standalone/build/output/enpu-manager %{buildroot}%{_bindir}/enpu-manager
install -D enpu-manager/standalone/build/output/enpu-cli %{buildroot}%{_bindir}/enpu-cli

# Shared library — versioned and soversion symlinks
install -D enpu-manager/core/build/output/libenpu_manager.so.1.0.0 %{buildroot}%{_libdir}/libenpu_manager.so.1.0.0
ln -sf libenpu_manager.so.1.0.0 %{buildroot}%{_libdir}/libenpu_manager.so.1

# Configuration file
install -D enpu-manager/config/enpu-manager.conf %{buildroot}%{_sysconfdir}/enpu/enpu-manager.conf

# Systemd service unit
install -D enpu-manager/deploy/enpu-manager.service %{buildroot}%{_unitdir}/enpu-manager.service

# State / log / data directories
install -d %{buildroot}%{_localstatedir}/lib/enpu-manager
install -d %{buildroot}%{_localstatedir}/log/enpu-manager
install -d %{buildroot}/opt/enpu

# ====================================================================
# Pre/Post Scripts — Security & Lifecycle
# ====================================================================

%pre
# Verify CANN runtime dependency
if [ ! -f "%{_ascend_home_path}/lib64/libascendcl.so" ]; then
    echo "[WARNING] libascendcl.so not found. Please install Ascend CANN Toolkit."
fi

# Verify NPU driver runtime dependency
if [ ! -f "%{_enpu_ascend_driver_path}/driver/lib64/driver/libdcmi.so" ]; then
    echo "[WARNING] libdcmi.so not found. Please install Ascend NPU Driver."
fi

%post
%systemd_post enpu-manager.service
/sbin/ldconfig

%preun
# Stop & disable on uninstall (not upgrade)
if [ $1 -eq 0 ]; then
    systemctl stop enpu-manager.service >/dev/null 2>&1 || :
fi
%systemd_preun enpu-manager.service

%postun
# On upgrade, restart if was running
%systemd_postun_with_restart enpu-manager.service
/sbin/ldconfig

# ====================================================================
# File Lists
# ====================================================================
%files
%doc enpu-manager/README.md

%attr(0500, root, root) %{_bindir}/enpu-manager
%attr(0500, root, root) %{_bindir}/enpu-cli

%{_libdir}/libenpu_manager.so.1
%attr(0500, root, root) %{_libdir}/libenpu_manager.so.1.0.0

%config(noreplace) %attr(0400, root, root) %{_sysconfdir}/enpu/enpu-manager.conf

%attr(0400, root, root) %{_unitdir}/enpu-manager.service

%attr(0700, root, root) %dir %{_localstatedir}/lib/enpu-manager
%attr(0700, root, root) %dir %{_localstatedir}/log/enpu-manager
%attr(0700, root, root) %dir /opt/enpu

%changelog
* Sun Jul 19 2026 enpu-manager maintainer <maintainer@enpu-manager> - 1.0.0-1
- Initial RPM packaging
