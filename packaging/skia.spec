#
Name:           skia
Version:        1.0.0
Release:        4
License:        BSD
Summary:        Skia rendering library
Group:          System/Libraries
ExcludeArch:    i586
Source0:        %{name}-%{version}.tar.gz

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
make -j12

%install
echo "This is where install would take place."

%files

