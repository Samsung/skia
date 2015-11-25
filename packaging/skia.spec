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
#BuildRequires: pkgconfig(wayland-egl)
BuildRequires: pkgconfig(x11)
BuildRequires: libjpeg-devel
BuildRequires: giflib-devel
BuildRequires: libpng-devel
BuildRequires: python
BuildRequires: pkgconfig(expat)

BuildRoot:     %{_tmppath}/%{name}-%{version}-%{release}-build

%description
Skia drawing library.

%prep
%setup -q -n %{name}-%{version}

%build
GYP_GENERATORS=make \
./gyp_skia -Dskia_egl=1            -Dskia_shared_lib=1           -Dskia_arch_width=32            -Dskia_warnings_as_errors=0            -Dskia_arch_type=arm

make -j32 -C out BUILDTYPE=Release

%install
echo "Installing to ${RPM_BUILD_ROOT}"
mkdir -p ${RPM_BUILD_ROOT}/%{_includedir}/%{name}
cp -r include/* ${RPM_BUILD_ROOT}/%{_includedir}/%{name}/
#cp -r * ${RPM_BUILD_ROOT}/%{_includedir}/%{name}/
mkdir -p ${RPM_BUILD_ROOT}/%{_includedir}/%{name}/ports
cp -r src/ports/*.h ${RPM_BUILD_ROOT}/%{_includedir}/%{name}/ports/
cp -r src/gpu/*.h ${RPM_BUILD_ROOT}/%{_includedir}/%{name}/gpu/
cp -r src/gpu/effects/ ${RPM_BUILD_ROOT}/%{_includedir}/%{name}/gpu/
cp -r include/private/*.h ${RPM_BUILD_ROOT}/%{_includedir}/%{name}/

mkdir -p ${RPM_BUILD_ROOT}/%{_libdir}/
mkdir -p ${RPM_BUILD_ROOT}/%{_libdir}/lib.target/
cp out/Release/lib.target/libskia.so ${RPM_BUILD_ROOT}/%{_libdir}
#cp out/Release/gm ${RPM_BUILD_ROOT}/%{_libdir}
#cp out/Release/dm ${RPM_BUILD_ROOT}/%{_libdir}

mkdir -p ${RPM_BUILD_ROOT}/%{_datadir}/license
cat LICENSE > ${RPM_BUILD_ROOT}/%{_datadir}/license/%{name}

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%doc AUTHORS LICENSE
%manifest packaging/%{name}.manifest
%{_libdir}/libskia.so*
#%{_libdir}/gm
#%{_libdir}/dm
%{_datadir}/license/%{name}
%{_includedir}/*
