#!/bin/bash
# Copyright (C) 2022 Vladislav Nepogodin
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

set -e

cd "`dirname "$0"`"

# if SYSDEP_ASSUME_YES=ON is set, then system package managers are attempted
# to install packages automatically, i.e. without confirmation.
if [ x$SYSDEP_ASSUME_YES = xON ]
then
    SYSDEP_ASSUME_YES='-y'
else
    unset SYSDEP_ASSUME_YES
fi

# Special environment variable to be used to install needed dependencies,
# for BPF implementation in order to compile and work.
#
# set this as environment variable to ON to activate this mode.
if [ x$USE_BPF = x ]
then
    USE_BPF=OFF
fi

install_deps_ubuntu()
{
    local packages="
        build-essential
        cmake
        git
        g++
        libc6-dev
        make
        libsystemd-dev
    "

    RELEASE=`grep VERSION_ID /etc/os-release | cut -d= -f2 | tr -d '"'`

    local NAME=`grep ^NAME /etc/os-release | cut -d= -f2 | cut -f1 | tr -d '"'`

    if [ x$USE_BPF = xON ]; then
        packages="$packages libelf-dev llvm clang"
        if [ "${NAME}" = "Debian GNU/Linux" ]; then
            packages="$packages bpftool"
        else
            # On Ubuntu bpftool is packaged as part of the `linux-tools-common` package. 
            packages="$packages linux-tools-common"
        fi
    fi

    sudo apt install $SYSDEP_ASSUME_YES $packages
}

install_deps_arch()
{
    local _packages=(
        base-devel
        cmake
        git
        ninja
    )
    if [ x$USE_BPF = xON ]; then
        _packages+=('libelf' 'zlib' 'bpf' 'libbpf' 'llvm' 'clang')
    fi
    sudo pacman -Sy "${_packages[@]}"
}

install_deps_suse()
{
    local packages="
        cmake
        git
        gcc-c++
        ninja
        libsystemd0
    "

    if [ x$USE_BPF = xON ]; then
        packages="$packages libelf1 zlib bpftool libbpf-dev llvm clang"
    fi

    sudo zypper install $SYSDEP_ASSUME_YES $packages
}

install_deps_fedora()
{
    local packages="
        cmake
        git
        gcc-c++
        ninja-build
        systemd-devel
    "

    if [ x$USE_BPF = xON ]; then
        packages="$packages elfutils-libelf zlib-devel bpftool libbpf llvm clang"
    fi
   
    sudo dnf install $SYSDEP_ASSUME_YES $packages
}

main()
{
    if test x$OS_OVERRIDE != x; then
        # In CI, we need to be able to fetch embedd-setups for different OSes.
        ID=$OS_OVERRIDE
    elif test -f /etc/os-release; then
        ID=`grep ^ID= /etc/os-release | cut -d= -f2`
    else
        ID=`uname -s`
    fi

    # Strip double-quotes, as used by opensuse for interesting reason.
    ID=`echo $ID | tr -d '"'`

    case "$ID" in
        arch|manjaro|cachyos)
            install_deps_arch
            ;;
        opensuse*)
            install_deps_suse
            ;;
        fedora)
            install_deps_fedora
            ;;
        ubuntu|neon|debian)
            install_deps_ubuntu
            ;;
        *)
            echo "OS '$ID' not supported."
            echo "Please install the remaining dependencies manually."
            ;;
    esac
}

main $*
