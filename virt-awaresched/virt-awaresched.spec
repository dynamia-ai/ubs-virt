Name:           virt-awaresched
Version:        1.0.0
Release:        2%{?dist}
Summary:        Virtual machine aware scheduling and tuning service
License:        Mulan PSL v2
URL:            https://gitcode.com/openeuler/ubs-virt
Source0:        %{name}-%{version}.tar.gz

ExclusiveArch:  aarch64
# Keep the package arch marked as aarch64 even when building on a cross (x86_64) host
BuildArch:      aarch64

# Binaries are stripped (-s) at link stage; no debug info to extract
%global debug_package %{nil}

%global service_name vas-daemon.service

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  make
BuildRequires:  patch
BuildRequires:  systemd
BuildRequires:  libvirt-devel
BuildRequires:  libboundscheck
Requires:       libvirt
Requires:       libboundscheck
Requires:       systemd

%description
virt-awaresched (VSched) is a virtual machine aware scheduling and tuning
service based on the Kunpeng CPU topology (numa->cluster->core->cpu).
It contains the vas_daemon daemon and the vasctl CLI tool, which provide
vCPU static/dynamic binding and CPU fragment consolidation to reduce
meaningless vCPU migration and improve VM linearity. Only the aarch64
architecture is supported for both build and runtime.

%prep
%autosetup -p1

%build
bash build.sh

%install
install -d -m 0755 %{buildroot}%{_bindir}
install -d -m 0755 %{buildroot}%{_unitdir}
install -p -m 0500 build/bin/vas_daemon %{buildroot}%{_bindir}/vas_daemon
install -p -m 0500 build/bin/vasctl %{buildroot}%{_bindir}/vasctl
install -p -m 0644 vas-daemon.service %{buildroot}%{_unitdir}/%{service_name}
install -d -m 0750 %{buildroot}/var/log/vas
install -d -m 0700 %{buildroot}/var/run/vas

%pre
set -e
if systemctl cat %{service_name} >/dev/null 2>&1 ; then
    systemctl stop %{service_name} || true
    systemctl disable %{service_name} || true
fi

%post
set -e
systemctl daemon-reload
systemctl enable %{service_name}
if ! systemctl is-active --quiet %{service_name}; then
    systemctl start %{service_name}
fi

%preun
set -e
if [ "$1" -ne 0 ]; then
    exit 0
fi
if systemctl cat %{service_name} >/dev/null 2>&1 ; then
    systemctl stop %{service_name} || true
    systemctl disable %{service_name} || true
fi
if systemctl list-units --type=service | grep -q %{service_name}; then
    systemctl reset-failed %{service_name} || true
fi
systemctl daemon-reload

%postun
if [ "$1" -ne 0 ]; then
    exit 0
fi
rm -rf /var/log/vas /var/run/vas

%files
%attr(0500,root,root) %{_bindir}/vas_daemon
%attr(0500,root,root) %{_bindir}/vasctl
%attr(0644,root,root) %{_unitdir}/%{service_name}
%dir %attr(0750,root,root) /var/log/vas
%dir %attr(0700,root,root) /var/run/vas

%changelog
* Fri Apr 24 2026 Zeren Lu <luzeren@h-partners.com> - 1.0.0-2
- Package init

* Fri Apr 24 2026 Zeren Lu <luzeren@h-partners.com> - 1.0.0-1
-
