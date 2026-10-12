Name:     ubs-optimizer
Version:  0.1.0
Release:  k5.1%{?dist}
Summary:  Package containing binaries and object files
License:  Proprietary
Source0: %{package_name}.tar.gz
BuildRoot: %{_buildirootdir}/%{name}_%{version}-build

BuildRequires:  clang cmake make libbpf-devel bpftool rapidjson-devel

%global debug_package %{nil}
%global __os_install_post %{nil}

%description
昇腾虚拟化场景瓶颈识别、调优下发

%pre

if [ ! -f /proc/config.gz ]; then
    echo "[Error] Config file: '/proc/config.gz' not exists, please confirm the kernel access configuration enabled."
    exit 1
fi

check_bpf_config() {
    local config_name=$1
    local status=$(zcat /proc/config.gz | grep "^${config_name}=")

    if [ -z "$status" ]; then
        echo "${config_name} not exists."
        return 1
    elif [[ "$status" == *"=y" || "$status" == *"=m" ]]; then
        echo "${config_name} enabled (${status})"
        return 0
    else
        echo "${config_name} disabled (${status})"
        return 1
    fi
}

echo "Checking BPF configs..."
check_bpf_config "CONFIG_BPF"
bpf_status=$?

check_bpf_config "CONFIG_BPF_SYSCALL"
bpf_syscall_status=$?

check_bpf_config "CONFIG_BPF_EVENTS"
bpf_events_status=$?

check_bpf_config "CONFIG_BPF_JIT"
bpf_jit_status=$?

# 汇总结果
if [ $bpf_status -eq 0 ] && [ $bpf_syscall_status -eq 0 ] && [ $bpf_events_status -eq 0 ] && [ $bpf_jit_status -eq 0 ]; then
    echo "Your kernel is ready."
else
    echo "[Error] CONFIG_BPF should be enabled. Please make sure configs [CONFIG_BPF, CONFIG_BPF_SYSCALL, CONFIG_BPF_EVENTS, CONFIG_BPF_JIT] enabled and recompile kernel."
    exit 1
fi

echo "Checking vsock config..."
if lsmod | grep -q vsock; then
    echo "OK"
else
    echo "[Error] vsock not ready. Please refer to the user guide Chapter 4.1 to enable the vsock module."
    exit 1
fi

%prep
%setup -c -n %{name}_%{version}

%install
# 创建目标目录
mkdir -p %{buildroot}/usr/local/sbin/ubs-optimizer

# 将解压后的文件移动到目标目录
cp -f %{_builddir}/%{name}_%{version}/rpm/usr/local/sbin/ubs-opt %{buildroot}/usr/local/sbin/
cp -f %{_builddir}/%{name}_%{version}/rpm/usr/local/sbin/ubs-opt-guard %{buildroot}/usr/local/sbin/
cp -f %{_builddir}/%{name}_%{version}/rpm/usr/local/sbin/ubs-opt-tuner %{buildroot}/usr/local/sbin/
cp -f %{_builddir}/%{name}_%{version}/rpm/usr/local/sbin/ubs-optimizer/config.json %{buildroot}/usr/local/sbin/ubs-optimizer/

%files
%dir %attr(700,root,root) /usr/local/sbin/ubs-optimizer
%attr(500,root,root) /usr/local/sbin/ubs-opt
%attr(500,root,root) /usr/local/sbin/ubs-opt-guard
%attr(500,root,root) /usr/local/sbin/ubs-opt-tuner
%attr(600,root,root) /usr/local/sbin/ubs-optimizer/config.json

%preun
if [ $1 -eq 0 ]; then
    if [ -f /usr/local/sbin/ubs-optimizer/ubs-opt-guard.pid ]; then
        PID=$(cat /usr/local/sbin/ubs-optimizer/ubs-opt-guard.pid)
        kill $PID 2>/dev/null || true
        sleep 2
        kill -9 $PID 2>/dev/null || true
        rm -f /usr/local/sbin/ubs-optimizer/ubs-opt-guard.pid
    fi

    if [ -f /usr/local/sbin/ubs-optimizer/ubs-opt.pid ]; then
        PID=$(cat /usr/local/sbin/ubs-optimizer/ubs-opt.pid)
        kill $PID 2>/dev/null || true
        sleep 2
        kill -9 $PID 2>/dev/null || true
        rm -f /usr/local/sbin/ubs-optimizer/ubs-opt.pid
    fi

fi
