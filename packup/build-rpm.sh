#!/usr/bin/env bash
#
# Build a RHEL/Fedora/openEuler .rpm package for vlink via CPack RPM generator.
#
# CPM-fetched third-party libs are static + marked EXCLUDE_FROM_ALL in
# cmake/cpm_thirdparty.cmake, so their install rules do not run during
# cpack's internal `cmake --install`. The resulting .rpm contains only
# vlink's own files.
#
# Usage:
#   ./packup/build-rpm.sh {project dir}
#
# Output:
#   {project}/build-rpm/packup/linux/vlink-<ver>-linux-<arch>.rpm
#
# Required system build deps (CPM fetches DDS/iceoryx/etc itself):
#   gcc-c++ cmake git
#   openssl-devel sqlite-devel libzstd-devel
#   protobuf-devel protobuf-compiler
#   flatbuffers-devel flatbuffers-compiler
#
# Host support:
#   - Fedora / RHEL / openEuler / Anolis: native; rpmbuild from rpm-build
#       sudo dnf install rpm-build
#   - Manjaro / Arch : sudo pacman -S rpm-tools
#   - Debian / Ubuntu: sudo apt install rpm

shopt -s extglob
set -e

WORK_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PLATFORM_ARCH=$(uname -m)

if [ -z "$1" ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    echo -e "Usage:\n  build-rpm.sh {project dir}"
    exit 0
fi

command -v cmake >/dev/null || { echo "Error: cmake not found in PATH"; exit 2; }
command -v rpmbuild >/dev/null || {
    echo "Error: rpmbuild not found."
    echo "  Fedora/RHEL  : sudo dnf install rpm-build"
    echo "  Manjaro/Arch : sudo pacman -S rpm-tools"
    echo "  Debian/Ubuntu: sudo apt install rpm"
    exit 2
}

SRC_DIR=$(cd "$1" && pwd)
BUILD_DIR=$SRC_DIR/build-rpm
OUTPUT_DIR=$BUILD_DIR/packup/linux
VERSION=$(tr -d '[:space:]' < "$SRC_DIR/version.txt")

case "$PLATFORM_ARCH" in
    x86_64|aarch64|ppc64le|s390x) LIBDIR=lib64 ;;
    *) LIBDIR=lib ;;
esac

RPM_REQUIRES="openssl-libs, sqlite-libs, libzstd, protobuf, flatbuffers"

[ -d "$BUILD_DIR" ] && rm -rf "$BUILD_DIR"

echo ""
echo "********************************************************************"
echo "*** [build-rpm] cmake configure..."
echo "*** SRC_DIR    = $SRC_DIR"
echo "*** BUILD_DIR  = $BUILD_DIR"
echo "*** VERSION    = $VERSION"
echo "*** ARCH       = $PLATFORM_ARCH"
echo "*** LIBDIR     = $LIBDIR"
echo "********************************************************************"
echo ""

cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_LIBDIR="$LIBDIR" \
    -DINSTALL_CONFIG_DIR=share/vlink \
    -DENABLE_SYMLINKS=OFF \
    -DENABLE_COMPLETIONS=ON \
    -DENABLE_CPM=ON \
    -DENABLE_CPM_PROTOBUF=ON \
    -DENABLE_CPM_FLATBUFFERS=ON \
    -DENABLE_WEBVIZ=ON \
    -DCPACK_GENERATOR=RPM \
    -DCPACK_RPM_PACKAGE_REQUIRES="$RPM_REQUIRES"

echo ""
echo "********************************************************************"
echo "*** [build-rpm] build..."
echo "********************************************************************"
echo ""

cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ""
echo "********************************************************************"
echo "*** [build-rpm] cpack -G RPM..."
echo "********************************************************************"
echo ""

(cd "$BUILD_DIR" && cpack -G RPM)

mkdir -p "$OUTPUT_DIR"
mv -f "$BUILD_DIR"/vlink*.rpm "$OUTPUT_DIR"/ 2>/dev/null || true

echo ""
echo "********************************************************************"
echo "*** Generated:"
echo "********************************************************************"
ls -la "$OUTPUT_DIR"/vlink*.rpm 2>/dev/null || echo "(none)"

echo ""
echo "Install:    sudo dnf install $OUTPUT_DIR/vlink*.rpm"
echo "Inspect:    rpm -qpi  $OUTPUT_DIR/vlink*.rpm"
echo "List files: rpm -qpl  $OUTPUT_DIR/vlink*.rpm"
echo "Requires:   rpm -qpR  $OUTPUT_DIR/vlink*.rpm"
echo "Done."
exit 0
