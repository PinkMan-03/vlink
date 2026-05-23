#!/usr/bin/env bash
#
# Build an Arch/Manjaro pacman package (.pkg.tar.zst) for vlink.
#
# CPack has no native pacman generator, so this script:
#   1) Builds with cmake + DESTDIR install to INSTALL_DIR
#   2) Hard-links INSTALL_DIR under a /usr-wrapper + emits .PKGINFO
#   3) Packs the wrapper into a .pkg.tar.zst with bsdtar
#
# CPM-fetched third-party libs are static + marked EXCLUDE_FROM_ALL in
# cmake/cpm_thirdparty.cmake, so INSTALL_DIR contains only vlink's
# own files.
#
# Usage:
#   ./packup/build-arch.sh {project dir}
#
# Output:
#   {project}/build-arch/packup/linux/vlink-<ver>-1-<arch>.pkg.tar.zst
#
# Required system build deps (CPM fetches DDS/iceoryx/etc itself):
#   base-devel cmake git
#   openssl sqlite zstd
#
# Host support:
#   - Manjaro / Arch: native; bsdtar is part of base (libarchive).
#   - Other distros: install `libarchive-tools` (Debian) / `bsdtar` (Fedora).
#                    Output is a valid .pkg.tar.zst regardless of host.

shopt -s extglob
set -e

WORK_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PLATFORM_ARCH=$(uname -m)

if [ -z "$1" ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    echo -e "Usage:\n  build-arch.sh {project dir}"
    exit 0
fi

command -v cmake >/dev/null || { echo "Error: cmake not found in PATH"; exit 2; }
command -v bsdtar >/dev/null || {
    echo "Error: bsdtar (libarchive) not found."
    echo "  Manjaro/Arch : pre-installed (libarchive)"
    echo "  Debian/Ubuntu: sudo apt install libarchive-tools"
    echo "  Fedora/RHEL  : sudo dnf install bsdtar"
    exit 2
}

SRC_DIR=$(cd "$1" && pwd)
BUILD_DIR=$SRC_DIR/build-arch
INSTALL_DIR=$BUILD_DIR/install
OUTPUT_DIR=$BUILD_DIR/packup/linux
VERSION=$(tr -d '[:space:]' < "$SRC_DIR/version.txt")
PKGREL=1

[ -d "$BUILD_DIR" ] && rm -rf "$BUILD_DIR"

echo ""
echo "********************************************************************"
echo "*** [build-arch] cmake configure..."
echo "*** SRC_DIR     = $SRC_DIR"
echo "*** BUILD_DIR   = $BUILD_DIR"
echo "*** INSTALL_DIR = $INSTALL_DIR"
echo "*** VERSION     = $VERSION"
echo "*** ARCH        = $PLATFORM_ARCH"
echo "********************************************************************"
echo ""

cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DINSTALL_CONFIG_DIR=share/vlink \
    -DENABLE_SYMLINKS=OFF \
    -DENABLE_COMPLETIONS=ON \
    -DENABLE_CPM=ON \
    -DENABLE_CPM_PROTOBUF=ON \
    -DENABLE_CPM_FLATBUFFERS=ON \
    -DENABLE_IOX_ROUDI=OFF \
    -DENABLE_WEBVIZ=ON

echo ""
echo "********************************************************************"
echo "*** [build-arch] build..."
echo "********************************************************************"
echo ""

cmake --build "$BUILD_DIR" -j"$(nproc)"

echo ""
echo "********************************************************************"
echo "*** [build-arch] installing to INSTALL_DIR..."
echo "********************************************************************"
echo ""

cmake --install "$BUILD_DIR" --strip

echo ""
echo "********************************************************************"
echo "*** [build-arch] wrapping into /usr layout + .PKGINFO..."
echo "********************************************************************"
echo ""

WRAP_DIR=$BUILD_DIR/packup/linux/arch-wrap
rm -rf "$WRAP_DIR"
mkdir -p "$WRAP_DIR/usr"
cp -al "$INSTALL_DIR"/. "$WRAP_DIR/usr/"

PKG_SIZE=$(du -sb "$WRAP_DIR" | cut -f1)
BUILDDATE=$(date +%s)
PKGINFO_TEMPLATE="$WORK_DIR/PKGINFO.in"

if [ ! -f "$PKGINFO_TEMPLATE" ]; then
    echo "Error: PKGINFO template not found: $PKGINFO_TEMPLATE"
    exit 2
fi

sed -e "s|@VERSION@|${VERSION}|g" \
    -e "s|@PKGREL@|${PKGREL}|g" \
    -e "s|@BUILDDATE@|${BUILDDATE}|g" \
    -e "s|@PKG_SIZE@|${PKG_SIZE}|g" \
    -e "s|@PLATFORM_ARCH@|${PLATFORM_ARCH}|g" \
    "$PKGINFO_TEMPLATE" > "$WRAP_DIR/.PKGINFO"

echo ""
echo "********************************************************************"
echo "*** [build-arch] packing .pkg.tar.zst..."
echo "********************************************************************"
echo ""

PKG_NAME="vlink-${VERSION}-${PKGREL}-${PLATFORM_ARCH}.pkg.tar.zst"
mkdir -p "$OUTPUT_DIR"

(cd "$WRAP_DIR" && bsdtar --zstd -cf "$OUTPUT_DIR/$PKG_NAME" .PKGINFO usr)

echo ""
echo "********************************************************************"
echo "*** Generated:"
echo "********************************************************************"
ls -la "$OUTPUT_DIR/$PKG_NAME"

echo ""
echo "Install:    sudo pacman -U $OUTPUT_DIR/$PKG_NAME"
echo "Inspect:    pacman -Qpi  $OUTPUT_DIR/$PKG_NAME"
echo "List files: bsdtar -tf   $OUTPUT_DIR/$PKG_NAME"
echo "Done."
exit 0
