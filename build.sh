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
#   BR2_JLEVEL      - jobs per package (default: auto-tuned by CPU+RAM)
#   TOPLEVEL_JOBS   - top-level make -j value (experimental in Buildroot)
#   BR2_VERBOSE     - 1 prints every recipe command as it runs (make V=1)
#                     for debugging; 0 (default) is Buildroot's normal quiet
#                     output
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILDROOT_DIR="${BUILDROOT_DIR:-$ROOT/buildroot}"
EXTERNAL_DIR="$ROOT/br-external"
OUTPUT_DIR="${BR2_OUTPUT_DIR:-$ROOT/output}"
DL_DIR="${BR2_DL_DIR:-$ROOT/dl}"

is_pos_int() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
        *) [ "$1" -gt 0 ] ;;
    esac
}

detect_cpu_jobs() {
    local cpu_jobs=1
    if command -v nproc >/dev/null 2>&1; then
        cpu_jobs="$(nproc)"
    else
        cpu_jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
    fi

    # Keep one core for the host/UI while still driving parallelism hard.
    cpu_jobs=$((cpu_jobs + 1))
    if [ "$cpu_jobs" -lt 1 ]; then
        cpu_jobs=1
    fi
    echo "$cpu_jobs"
}

detect_mem_jobs() {
    local mem_kib mem_jobs

    if [ ! -r /proc/meminfo ]; then
        echo 1
        return
    fi

    mem_kib="$(awk '/^MemAvailable:/ { print $2; exit } /^MemTotal:/ { print $2; exit }' /proc/meminfo)"
    if ! is_pos_int "$mem_kib"; then
        echo 1
        return
    fi

    # C++ host builds (notably host-cmake) can exceed 1GiB per job.
    mem_jobs=$((mem_kib / 1800000))
    if [ "$mem_jobs" -lt 1 ]; then
        mem_jobs=1
    fi
    echo "$mem_jobs"
}

CPU_JOB_LIMIT="$(detect_cpu_jobs)"
MEM_JOB_LIMIT="$(detect_mem_jobs)"

if [ -n "${BR2_JLEVEL:-}" ] && ! is_pos_int "$BR2_JLEVEL"; then
    echo "error: BR2_JLEVEL must be a positive integer, got: '$BR2_JLEVEL'" >&2
    exit 1
fi

if [ -n "${TOPLEVEL_JOBS:-}" ] && ! is_pos_int "$TOPLEVEL_JOBS"; then
    echo "error: TOPLEVEL_JOBS must be a positive integer, got: '$TOPLEVEL_JOBS'" >&2
    exit 1
fi

if [ -z "${BR2_JLEVEL:-}" ]; then
    BR2_JLEVEL="$CPU_JOB_LIMIT"
    if [ "$MEM_JOB_LIMIT" -lt "$BR2_JLEVEL" ]; then
        BR2_JLEVEL="$MEM_JOB_LIMIT"
    fi
    echo "info: auto BR2_JLEVEL=$BR2_JLEVEL (cpu_limit=$CPU_JOB_LIMIT, mem_limit=$MEM_JOB_LIMIT)"
elif [ "$BR2_JLEVEL" -gt "$MEM_JOB_LIMIT" ]; then
    echo "warn: BR2_JLEVEL=$BR2_JLEVEL exceeds memory-safe estimate ($MEM_JOB_LIMIT); OOM risk is high" >&2
fi

if [ -n "${TOPLEVEL_JOBS:-}" ] && [ "$TOPLEVEL_JOBS" -gt "$BR2_JLEVEL" ]; then
    echo "warn: capping TOPLEVEL_JOBS=$TOPLEVEL_JOBS to BR2_JLEVEL=$BR2_JLEVEL to reduce OOM risk" >&2
    TOPLEVEL_JOBS="$BR2_JLEVEL"
fi

MAKE_ARGS=(
    -C "$BUILDROOT_DIR"
    "O=$OUTPUT_DIR"
    "BR2_EXTERNAL=$EXTERNAL_DIR"
    "BR2_DL_DIR=$DL_DIR"
    "BR2_JLEVEL=$BR2_JLEVEL"
    "V=${BR2_VERBOSE:-0}"
)

TOPLEVEL_ARGS=()
if [ -n "${TOPLEVEL_JOBS:-}" ]; then
    TOPLEVEL_ARGS+=("-j$TOPLEVEL_JOBS")
fi

if [ ! -f "$BUILDROOT_DIR/Makefile" ]; then
    echo "error: Buildroot source tree not found at: $BUILDROOT_DIR" >&2
    echo "       set BUILDROOT_DIR or build via the Docker image." >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR" "$DL_DIR"

make "${MAKE_ARGS[@]}" speakerpoint_defconfig

if [ "$#" -eq 0 ]; then
    set -- all
fi

set +e
make "${MAKE_ARGS[@]}" "${TOPLEVEL_ARGS[@]}" "$@"
BUILD_STATUS=$?
set -e

# host-tar's configure script can leave a self-referential "confdir3"
# symlink loop under output/build (a known autoconf/gnulib test artifact).
# Any later recursive walk of output/ (e.g. Docker BuildKit exporting a
# cache mount, or a plain cp/rsync) can spin until it hits ENAMETOOLONG.
# Safe to always prune: rm -rf never follows symlinks, and -prune stops
# find from ever descending into the loop.
find "$OUTPUT_DIR/build" -type d -name 'confdir3' -prune -exec rm -rf {} + 2>/dev/null || true

exit "$BUILD_STATUS"
