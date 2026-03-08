Name:           os-transport
Version:        %{version}
Release:        %{release}
Summary:        OS transport layer shared library
License:        MIT
BuildArch:      x86_64
Requires:       glibc >= 2.17

%description
OS transport layer library (libos_transport.so) for data send/recv.

%package devel
Summary:        Development files for os-transport
Requires:       %{name} = %{version}-%{release}

%description devel
Header files and development libraries for os-transport.

%prep
# 空（build.sh已处理源码路径）

%build
# 空（build.sh已完成编译）

%install
# 空（build.sh已完成临时安装）

%files
%defattr(-,root,root)
/usr/lib64/libos_transport.so.1*
/usr/lib64/libos_transport.so

%files devel
%defattr(-,root,root)
/usr/include/os-transport/os_transport.h

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%changelog
* Sun Mar 08 2026 OS Dev <dev@example.com> - 1.0.0-1
- Initial RPM package for os-transport