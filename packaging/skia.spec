#
Name:           skia
Version:        1.0.0
Release:        4
License:        BSD
Summary:        Skia rendering library
Group:          System/Libraries
BuildRoot:      %(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)
ExcludeArch:    i586
Source0:        %{name}-%{version}.tar.gz
Source1001:     packaging/skia.manifest

BuildRequires: pkgconfig(freetype2)
BuildRequires: pkgconfig(fontconfig)
BuildRequires: pkgconfig(gles20)
BuildRequires: pkgconfig(x11)
BuildRequires: libjpeg-devel
BuildRequires: giflib-devel
BuildRequires: libpng-devel
BuildRequires: python


%description
Skia drawing library.

%prep
%setup -q

%build
./gyp_skia -Dskia_build_for_tizen=1 \
           -Duse_system_libjpeg=1 \
           -Dskia_shared_lib=1 \
           -Dskia_arch_width=32 \
           -Dskia_warnings_as_errors=0 \
           -Dskia_arch_type=arm \
           -Darm_thumb=1
make -j12 BUILDTYPE=Release

%install
echo "Installing to ${RPM_BUILD_ROOT}"
mkdir -p ${RPM_BUILD_ROOT}/%{_includedir}/%{name}
cp -r include/* ${RPM_BUILD_ROOT}/%{_includedir}/%{name}/

mkdir -p ${RPM_BUILD_ROOT}/%{_libdir}/
cp out/Release/lib.target/libskia.so ${RPM_BUILD_ROOT}/%{_libdir}

mkdir -p %{RPM_BUILD_ROOT}/%{_datadir}/license
cp LICENSE %{RPM_BUILD_ROOT}/%{_datadir}/license/%{name}

%files
#%manifest skia.manifest
%{_includedir}/*
%{_libdir}/libskia.so*
#%{_datadir}/license/%{name}

%files

