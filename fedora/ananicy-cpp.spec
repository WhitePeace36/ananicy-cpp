Name:           ananicy-cpp
Version:        1.0.2
Release:        %autorelease
Summary:        Rewrite of ananicy in c++ for lower cpu and memory usage
License:        GPLv3
URL:            https://gitlab.com/ananicy-cpp/ananicy-cpp
Source0:        %{url}/-/archive/v%{version}/ananicy-cpp-v%{version}.tar.gz
#Source1:        https://gitlab.com/vnepogodin/std-format/-/archive/dfa4fdc87c7cb9bb1683125009baa7278bb85901/std-format-dfa4fdc87c7cb9bb1683125009baa7278bb85901.tar.gz

ExcludeArch:    s390x i686 ppc64le

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  systemd-devel
BuildRequires:  zlib-devel
BuildRequires:  git
BuildRequires:  bpftool
BuildRequires:  libbpf
BuildRequires:  elfutils-libelf
BuildRequires:  llvm
BuildRequires:  clang

%description
Rewrite of ananicy in c++ for lower cpu and memory usage

%prep
%autosetup -n ananicy-cpp-v%{version}
#%autosetup -n std-format-dfa4fdc87c7cb9bb1683125009baa7278bb85901
#mv std-format-dfa4fdc87c7cb9bb1683125009baa7278bb85901 ananicy-cpp-v%{version}

%build
%cmake \
    -GNinja \
    -DENABLE_SYSTEMD=ON \
    -DUSE_BPF_PROC_IMPL=ON \
    -DBPF_BUILD_LIBBPF=OFF \
    -DENABLE_ANANICY_TESTS=ON \
    -DVERSION=%{version}
%ninja_build -C %{_vpath_builddir}

%install
%ninja_install -C %{_vpath_builddir}

%check
./%{_vpath_builddir}/src/tests/test-core
./%{_vpath_builddir}/src/tests/test-utility --test-case-exclude="Process Info"

%files
%license LICENSE
%doc README.md
%{_bindir}/ananicy-cpp
%{_libdir}/systemd/system/ananicy-cpp.service

%changelog
%autochangelog
