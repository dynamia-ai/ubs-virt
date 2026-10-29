##########################################################################################################
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# ub-device-manager is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
##########################################################################################################
%global debug_package %{nil}
%global __os_install_post %{nil}

%if %{defined python_v}
    %define python_version %{python_v}
%else
    %define python_version 3.11
%endif
%define python_path python%{python_version}
%define site_packages_dir /usr/lib/%{python_path}/site-packages
%define service_user ub-device-manager

Name:           ub-device-manager
Version:        1.0.0
Release:        1%{?dist}
Summary:        Ub Device Manager
License:        Mulan PSL v2
Source0:        %{name}-%{version}.tar.gz
BuildArch:      aarch64
Requires:       python3
Requires:       sudo
Requires:       dmidecode

%description
UB Device Manager: creates/deletes UB (NPU passthrough) VMs on the local node, and
enables/disables/queries UB devices.
It provides three entry points: HTTP, the Python SDK (ub_device_manager.ub_dm_sdk),
and the ubdmctl CLI.
Python dependencies (fastapi/uvicorn/pydantic/loguru/httpx/typer/rich/PyYAML, etc.)
must be installed separately; see requirements.txt inside the package.

%prep
%setup -c

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}/etc/ub_device_manager
mkdir -p %{buildroot}/var/log/ub_device_manager
mkdir -p %{buildroot}/etc/systemd/system
mkdir -p %{buildroot}/etc/sudoers.d
mkdir -p %{buildroot}%{site_packages_dir}/ub_device_manager

# Remove cache files on the build machine
find . -name "__pycache__" -type d -prune -exec rm -rf {} +
find . -name "*.pyc" -type f -delete

# Server main package and bundled Python SDK
cp -r ub_device_manager/* %{buildroot}%{site_packages_dir}/ub_device_manager/
cp requirements.txt %{buildroot}%{site_packages_dir}/ub_device_manager/requirements.txt

install -m 0440 ub_device_manager/ub-device-manager-smbios.sudoers \
    %{buildroot}/etc/sudoers.d/ub-device-manager-smbios

# Config file: the code prefers /etc/ub_device_manager/ub_device_manager.toml and falls back
# to the copy bundled in site-packages, so operators only need to edit the /etc one.
cp ub_device_manager/ub_device_manager.toml %{buildroot}/etc/ub_device_manager/ub_device_manager.toml

# The service template/spec are not installed into site-packages
rm -f %{buildroot}%{site_packages_dir}/ub_device_manager/ub-device-manager.service
rm -f %{buildroot}%{site_packages_dir}/ub_device_manager/ub-device-manager-smbios.sudoers
rm -f %{buildroot}%{site_packages_dir}/ub_device_manager/ub_device_manager.spec

# Render the Python and install path placeholders in the systemd service file
sed -e "s|@PYTHON3@|%{_bindir}/python%{python_version}|g" \
    -e "s|@SITE_PACKAGES@|%{site_packages_dir}|g" \
    ub_device_manager/ub-device-manager.service > %{buildroot}/etc/systemd/system/ub-device-manager.service

# Generate the global ubdmctl command (points to the CLI controller)
cat << 'EOF' > %{buildroot}%{_bindir}/ubdmctl
#!/bin/bash
exec %{_bindir}/python%{python_version} %{site_packages_dir}/ub_device_manager/ub_device_manager_cli.py "$@"
EOF

chmod +x %{buildroot}%{_bindir}/ubdmctl

%pre
getent group %{service_user} >/dev/null || groupadd -r %{service_user}
getent passwd %{service_user} >/dev/null || \
    useradd -r -g %{service_user} -d / -s /sbin/nologin \
    -c "Ub Device Manager Service User" %{service_user}

# The service user needs access to libvirt and UBSE; join the related groups when they exist
for grp in libvirt qemu ubse; do
    if getent group "$grp" >/dev/null; then
        usermod -aG "$grp" %{service_user} || true
    fi
done
exit 0

%post
systemctl enable ub-device-manager
systemctl daemon-reload >/dev/null 2>&1 || true
chown -R %{service_user}:%{service_user} /var/log/ub_device_manager >/dev/null 2>&1 || true

%preun
if [ "$1" -eq 0 ]; then
    systemctl stop ub-device-manager >/dev/null 2>&1 || true
    systemctl disable ub-device-manager >/dev/null 2>&1 || true
fi

%postun
systemctl daemon-reload >/dev/null 2>&1 || true

%clean
rm -rf %{buildroot}

%files
%attr(0750, %{service_user}, %{service_user}) %{site_packages_dir}/ub_device_manager
%attr(0644, root, root) /etc/systemd/system/ub-device-manager.service
%config(noreplace) %attr(0440, root, root) /etc/sudoers.d/ub-device-manager-smbios
%dir %attr(0750, %{service_user}, %{service_user}) /etc/ub_device_manager
%config(noreplace) %attr(0640, %{service_user}, %{service_user}) /etc/ub_device_manager/ub_device_manager.toml
%dir %attr(0750, %{service_user}, %{service_user}) /var/log/ub_device_manager
%attr(0755, root, root) %{_bindir}/ubdmctl

%changelog
* Tue Jun 30 2026 Developer - 1.0.0
- Refactor the install paths, support passing the Python version dynamically, and generate the global ubdmctl control command
