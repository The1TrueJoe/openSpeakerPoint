#!/usr/bin/env bash
# Reproducible entry point for building the SpeakerPoint firmware image.
#
# Usage:
#   ./build.sh                  # full build -> output/images/{zImage.*,rootfs.squashfs,rootfs.cpio.gz}
#   ./build.sh menuconfig       # any other buildroot/kernel make target
#   ./build.sh linux-menuconfig
#   ./build.sh clean
#
# This script is the single thing local dev, Docker, and CI all call, so
# there's exactly one build path to keep working.
#
# Env overrides:
#   BR2_OUTPUT_DIR  - build output dir (default: <repo>/output)
#   BR2_DL_DIR      - shared download cache dir (default: <repo>/dl)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILDROOT_DIR="$ROOT/buildroot"
EXTERNAL_DIR="$ROOT/br-external"
OUTPUT_DIR="${BR2_OUTPUT_DIR:-$ROOT/output}"
DL_DIR="${BR2_DL_DIR:-$ROOT/dl}"

if [ ! -f "$BUILDROOT_DIR/Makefile" ]; then
    echo "error: buildroot/ submodule is not checked out." >&2
    echo "       run: git submodule update --init --recursive" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR" "$DL_DIR"

make -C "$BUILDROOT_DIR" O="$OUTPUT_DIR" BR2_EXTERNAL="$EXTERNAL_DIR" BR2_DL_DIR="$DL_DIR" \
    speakerpoint_defconfig

if [ "$#" -eq 0 ]; then
    set -- all
fi

exec make -C "$BUILDROOT_DIR" O="$OUTPUT_DIR" BR2_DL_DIR="$DL_DIR" "$@"
