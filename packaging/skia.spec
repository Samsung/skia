#
%define prefix   /usr
%define checkout 20140319git

Name:           skia
Summary:        Skia rendering library
Version:        0.0.0.%{checkout}
Release:        1.0
Group:          System/Libraries
License:        BSD
URL:            https://code.google.com/p/skia/
ExcludeArch:    i586
Source0:        %{name}-%{version}.tar.gz
Source1001:     packaging/%{name}.manifest

BuildRequires: pkgconfig(freetype2)
BuildRequires: pkgconfig(fontconfig)
BuildRequires: pkgconfig(gles20)
BuildRequires: pkgconfig(x11)
BuildRequires: libjpeg-devel
BuildRequires: giflib-devel
BuildRequires: libpng-devel
BuildRequires: python

BuildRoot:     %{_tmppath}/%{name}-%{version}-%{release}-build

%description
Skia drawing library.

%prep
%setup -q -n %{name}-%{version}

%build
GYP_GENERATORS=make \
./gyp_skia -Dskia_build_for_tizen=1 \
           -Duse_system_libjpeg=1 \
           -Dskia_shared_lib=1 \
           -Dskia_arch_width=32 \
           -Dskia_warnings_as_errors=0 \
           -Dskia_arch_type=arm \
           -Darm_thumb=1
make -j12 -C out BUILDTYPE=Release

%install
echo "Installing to ${RPM_BUILD_ROOT}"
#mkdir -p ${RPM_BUILD_ROOT}/%{_includedir}/%{name}
#cp -r include/* ${RPM_BUILD_ROOT}/%{_includedir}/%{name}/

mkdir -p ${RPM_BUILD_ROOT}/%{_libdir}/
cp out/Release/lib.target/libskia.so ${RPM_BUILD_ROOT}/%{_libdir}

mkdir -p %{RPM_BUILD_ROOT}/%{_datadir}/license
cat LICENSE > %{RPM_BUILD_ROOT}/%{_datadir}/license/%{name};

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%doc AUTHORS LICENSE
%manifest packaging/%{name}.manifest
%{_libdir}/libskia.so*
#%{_datadir}/license/%{name}
#/usr/share/license/%{name}
#%{_includedir}/*


