Name:           ubs-virt
Version:        1.0.0
Release:        1%{?dist}
Summary:        UBS Virt virtualization performance components
License:        Mulan PSL v2
URL:            https://gitcode.com/openeuler/ubs-virt
Source0:        %{name}-%{version}.tar.gz

ExclusiveArch:  aarch64
# Keep the package arch marked as aarch64 even when building on a cross (x86_64) host
BuildArch:      aarch64

# Subpackage binaries are stripped (-s) at build stage; no debug info to extract
%global debug_package %{nil}

# The main package is a meta package that aggregates all subpackages
Requires:       %{name}-awaresched = %{version}-%{release}

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  make
BuildRequires:  patch
BuildRequires:  systemd
BuildRequires:  libvirt-devel
BuildRequires:  libboundscheck

# Component documentation entry (openEuler official website):
# https://docs.openeuler.org/zh/docs/24.03_LTS_SP3/unifiedbus/unifiedbus/introduction/introduction.html

%description
ubs-virt contains multiple virtualization feature components.
This spec packages the virt-awaresched component: a VM aware scheduling and
tuning service that reduces the overhead of unnecessary vCPU migration and
improves VM linearity. Only the aarch64 architecture is supported for both
build and runtime.

%package awaresched
Summary:        Virtual machine aware scheduling and tuning service
License:        Mulan PSL v2
Requires:       libvirt
Requires:       libboundscheck
Requires:       systemd

%description awaresched
virt-awaresched (VSched) is a VM aware scheduling and tuning service based on
Kunpeng CPU topology. It provides the vas_daemon daemon and the vasctl
command-line tool, supporting static/dynamic vCPU pinning and CPU compaction,
reducing cross-cluster vCPU migration and improving VM linearity.

%prep
%autosetup -p1

%build
# build virt-awaresched (vas_daemon, vasctl)
pushd virt-awaresched
bash build.sh
popd

%install
# virt-awaresched
install -d -m 0755 %{buildroot}%{_bindir}
install -d -m 0755 %{buildroot}%{_unitdir}
install -p -m 0500 virt-awaresched/build/bin/vas_daemon %{buildroot}%{_bindir}/vas_daemon
install -p -m 0500 virt-awaresched/build/bin/vasctl %{buildroot}%{_bindir}/vasctl
install -p -m 0644 virt-awaresched/vas-daemon.service %{buildroot}%{_unitdir}/vas-daemon.service
install -d -m 0750 %{buildroot}/var/log/vas
install -d -m 0700 %{buildroot}/var/run/vas

%pre awaresched
set -e
if systemctl cat vas-daemon.service >/dev/null 2>&1 ; then
    systemctl stop vas-daemon.service || true
    systemctl disable vas-daemon.service || true
fi

%post awaresched
set -e
systemctl daemon-reload
systemctl enable vas-daemon.service
if ! systemctl is-active --quiet vas-daemon.service; then
    systemctl start vas-daemon.service
fi

%preun awaresched
set -e
if [ "$1" -ne 0 ]; then
    exit 0
fi
if systemctl cat vas-daemon.service >/dev/null 2>&1 ; then
    systemctl stop vas-daemon.service || true
    systemctl disable vas-daemon.service || true
fi
if systemctl list-units --type=service | grep -q vas-daemon.service; then
    systemctl reset-failed vas-daemon.service || true
fi
systemctl daemon-reload

%postun awaresched
if [ "$1" -ne 0 ]; then
    exit 0
fi
rm -rf /var/log/vas /var/run/vas

%files
%license LICENSE
%doc README.md README_EN.md

%files awaresched
%attr(0500,root,root) %{_bindir}/vas_daemon
%attr(0500,root,root) %{_bindir}/vasctl
%attr(0644,root,root) %{_unitdir}/vas-daemon.service
%dir %attr(0750,root,root) /var/log/vas
%dir %attr(0700,root,root) /var/run/vas

%changelog
* Fri Sep 11 2026 ubs-virt <ubs-virt@openeuler.org> - 1.0.0-1
- Package init: package virt-awaresched as ubs-virt-awaresched subpackage
- Fix install paths to comply with openEuler packaging policy
