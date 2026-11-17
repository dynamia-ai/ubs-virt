#!/bin/bash

#
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
#

###
### build.sh --- build project
###
### Usage:
###     build.sh <target> [-D] [-C] [-t <target>] [-- <pass-through-args>]
###
### Options:
###     <target>        Build target used by Make
###     -h | --help     Show help message
###     -D | --debug    Build debug version
###     -C | --coverage Generate coverage report files
###     -t | --target   Specifying build target, default is `all`
###                     Supported targets:
###                         `all`       build all target in source code
###                         `test`      build all tests in test/ directory

trans_params=()
trans_flag=false

# exit when background cmd fail
set -o errtrace
# exit when foreground cmd fail
set -o errexit

build_target='all'
build_type='Release'
enable_coverage='OFF'
enable_test='OFF'
enable_source_compiling='OFF'

# Get the project root directory (currently the directory where the build script is located)
PROJECT_ROOT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

OUTPUT_DIR=${PROJECT_ROOT_DIR}/output

# Capture errors when the script encounters them, and execute the trap_error function to handle the errors.
trap 'trap_error $LINENO ${FUNCNAME} $BASH_LINENO' ERR
# When the script exits, return to the script's execution directory.
trap 'cd $PROJECT_ROOT_DIR' EXIT

FAILURE='[\033[1;31mFAILED\033[0;39m]'

function log_info() {
    if [ $# -lt 1 ]; then
        return
    fi
    echo "$(date +"%F %T") [INFO] $*" >>"$LOG_FILE"
    echo "$(date +"%F %T") [INFO] $*"
}

# Script Error Handling
function trap_error() {
    local err=$?
    local line=$1 # LINENO
    [ "$2" != "" ] && local func_stack=$2 # func name
    [ "$3" != "" ] && local line_call_func=$3 # line where func was called
    echo "<---"
    echo "ERROR: line $line - command exited with status: $err"
    if [ "$func_stack" != "" ]; then
        echo -n "   ... Error at function ${func_stack[0]}() "
        if [ "$line_call_func" != "" ]; then
            echo -n "called at line $3"
        fi
        echo
    fi
    echo "--->"
}

function echo_failure() {
    echo -e "VIRT project : $FAILURE"
}

# Executing a CMake Build
function build_cmake() {

    if [[ "$build_target" == 'package' || "$enable_clean" == 'ON' ]]; then
        clean "$build_target"
    fi

    log_info "***** start build_cmake *****"

    pushd build
    log_info "building target build_${build_target}."
    if [[ $build_target == 'test' ]]; then
        enable_test='ON'
    fi

    if [ ${#trans_params[@]} -ne 0 ]; then
        export TRANS_PARAMS="${trans_params[*]}"
        log_info "Pass-through parameters detected: ${trans_params[*]}"
    else
        unset TRANS_PARAMS
    fi

    cmake .. -DCMAKE_BUILD_TYPE=${build_type} -DBUILD_TESTS=${enable_test} -DENABLE_COVERAGE=${enable_coverage} -DSOURCE_COMPILING=${enable_source_compiling} -DCMAKE_INSTALL_PREFIX=../output

    N_CPUS=$(grep processor /proc/cpuinfo | wc -l)
    make build_"${build_target}" -j $((N_CPUS-2))
    if [[ $enable_coverage == 'ON' ]]; then
        make coverage
    fi
    local ret=$?
    popd

    if [ $ret -ne 0 ]; then
        log_info "build_cmake failed"
        echo_failure
        exit 1
    fi
}

# Parse and print the help information, which is placed at the beginning of the file.
function help() {
    sed -rn 's/^### ?//;T;p;' "$0"
}

function parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
        -h | --help)
            help
            exit
            ;;
        -D | --debug)
            build_type='Debug'
            shift
            ;;
        -t | --target)
            if [[ $# -gt 1 && "$2" != "-"* ]]; then
                build_target="$2"
                shift 2
            else
                log_info "Error: Argument required after -t|--target."
                exit 1
            fi
            ;;
        -C | --coverage)
            enable_coverage='ON'
            shift
            ;;
        -S | --source-compiling)
            enable_source_compiling='ON'
            shift
            ;;
        -c | --clean)
            clean
            shift
            ;;
        --)
            trans_flag=true
            shift
            ;;
        *)
            if $trans_flag; then
                trans_params+=("$1")
            else
                [ "$1" != "" ] &&build_target="$1"
            fi
            shift
            ;;
        esac
    done
}

function clean() {
    local target_dirs=()
    case $1 in
        "package")
            target_dirs+=("${PROJECT_ROOT_DIR}/cmake-build-release")
            ;;
        *)
            target_dirs+=("${PROJECT_ROOT_DIR}/cmake-build-release")
            ;;
    esac

    for target_dir in "${target_dirs[@]}"; do
        if [ ! -d "$target_dir" ]; then
            echo "Warning: Directory '$target_dir' does not exist."
        else
            rm -rf "$target_dir"
            echo "Directory '$target_dir' has been cleaned."
        fi
    done

    if [[ ! -d ${PROJECT_ROOT_DIR}/build ]]; then
        mkdir -p "${PROJECT_ROOT_DIR}/build"
        echo "Directory 'build' has been recreated."
    fi
}

function build_package() {
    local pkg_name="virt-awaresched"
    local pkg_version="1.0.0"
    mkdir -p "${PROJECT_ROOT_DIR}"/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
    # 打源码压缩包（命名须与 spec 中 Source0 一致：virt-awaresched-<version>.tar.gz）
    tar -czf "${PROJECT_ROOT_DIR}/rpmbuild/SOURCES/${pkg_name}-${pkg_version}.tar.gz" \
        --exclude './build' \
        --exclude './output' \
        --exclude './rpmbuild' \
        --exclude './cmake-build-*' \
        --transform "s,^\.,${pkg_name}-${pkg_version}," \
        -C "${PROJECT_ROOT_DIR}" .
    rpmbuild -D "_topdir ${PROJECT_ROOT_DIR}/rpmbuild" -bb --clean "${PROJECT_ROOT_DIR}"/virt-awaresched.spec
    mkdir -p "${PROJECT_ROOT_DIR}"/output
    rm -rf "${PROJECT_ROOT_DIR}"/output/*
    cp -p "${PROJECT_ROOT_DIR}"/rpmbuild/RPMS/*/*.rpm "${PROJECT_ROOT_DIR}"/output
}

echo $(date +"[%Y-%m-%d %H:%M]"): "$0" "$@"
START_TIME=$(date +%s.%N)

PROJECT_BUILD_DIR=$PROJECT_ROOT_DIR/build
LOG_FILE=$PROJECT_BUILD_DIR/build.log
cd "${PROJECT_ROOT_DIR}"

[ ! -d "${PROJECT_ROOT_DIR}/build" ] && mkdir -p ${PROJECT_ROOT_DIR}/build

parse_args "$@"
build_cmake

if [[ "$build_target" == 'package' ]]; then
    build_package
fi

END_TIME=$(date +%s.%N)
EXEC_TIME=$(echo "scale=3; ($END_TIME - $START_TIME) / 1" | bc)
echo -e $(date +"[%Y-%m-%d %H:%M]"): "\033[32m" Build complated in "$EXEC_TIME"s '\033[0m'
