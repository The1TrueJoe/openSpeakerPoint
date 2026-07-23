#!/usr/bin/env bash
# Reproducible entry point for building the SpeakerPoint firmware image.
#
# Usage:
#   ./build.sh                  # full build -> output/images/{zImage.*,rootfs.squashfs,rootfs.cpio.gz}
#   ./build.sh menuconfig       # any other buildroot/kernel make target
#   ./build.sh linux-menuconfig
#   ./build.sh clean
#   ./build.sh dev-binary       # fast local build of apps/speakerpoint-control only
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
EXTERNAL_DIR="$ROOT/image"
OUTPUT_DIR="${BR2_OUTPUT_DIR:-$ROOT/output}"
DL_DIR="${BR2_DL_DIR:-$ROOT/dl}"

build_dev_binary() {
    local app_dir="$ROOT/apps/speakerpoint-control"

    if [ ! -f "$app_dir/Makefile" ]; then
        echo "error: missing app Makefile at: $app_dir/Makefile" >&2
        exit 1
    fi

    echo "info: building development binary in apps/speakerpoint-control"
    make -C "$app_dir" clean speakerpoint-control
    echo "info: built $app_dir/speakerpoint-control"
}

build_dashboard_assets() {
    local dashboard_dir="$ROOT/apps/speakerpoint-dashboard"
    local dashboard_dist="$dashboard_dir/dist"
    local overlay_www="$ROOT/image/board/speakerpoint/rootfs-overlay/var/www/data"

    if [ ! -f "$dashboard_dir/package.json" ]; then
        echo "error: missing dashboard package.json at: $dashboard_dir/package.json" >&2
        exit 1
    fi

    if ! command -v node >/dev/null 2>&1 || ! command -v npm >/dev/null 2>&1; then
        echo "error: node and npm are required to build dashboard assets" >&2
        exit 1
    fi

    echo "info: building dashboard (Vite/React/TypeScript) with node"
    if [ -f "$dashboard_dir/package-lock.json" ]; then
        npm --prefix "$dashboard_dir" ci --no-audit --no-fund
    else
        npm --prefix "$dashboard_dir" install --no-audit --no-fund
    fi

    npm --prefix "$dashboard_dir" run build

    # The Vite build emits a complete static site (index.html + hashed
    # assets) into dist/. Sync the whole tree into the rootfs overlay www
    # dir, which is a build-output location (gitignored) fed solely from the
    # dashboard source - there is no hand-written index.html any more.
    rm -rf "$overlay_www"
    mkdir -p "$overlay_www"
    cp -a "$dashboard_dist/." "$overlay_www/"
    echo "info: installed dashboard site into rootfs overlay ($overlay_www)"
}

should_build_dashboard_assets() {
    local target
    if [ "$#" -eq 0 ]; then
        return 0
    fi

    for target in "$@"; do
        case "$target" in
            clean|distclean|mrproper|menuconfig|linux-menuconfig|busybox-menuconfig|help)
                ;;
            *)
                return 0
                ;;
        esac
    done

    return 1
}

# Buildroot's linux package doesn't track image/board/speakerpoint/dts-overlay
# or the apps/ep93xx-ac97 driver sources (injected via LINUX_POST_PATCH_HOOKS)
# as build dependencies, so editing them alone does not invalidate the
# package's stamps.
should_force_linux_rebuild() {
    local target
    for target in "$@"; do
        case "$target" in
            clean|distclean|mrproper|menuconfig|linux-menuconfig|busybox-menuconfig|help|linux-dirclean|linux-rebuild|linux-reconfigure)
                return 1
                ;;
        esac
    done

    return 0
}

if [ "${1:-}" = "dev-binary" ]; then
    shift
    if [ "$#" -gt 0 ]; then
        echo "error: dev-binary does not accept additional args" >&2
        exit 1
    fi
    build_dev_binary
    exit 0
fi

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

# Toolchain-wide config changes (BR2_STATIC_LIBS <-> BR2_SHARED_LIBS, a
# different BR2_TOOLCHAIN_BUILDROOT_MUSL/glibc choice, etc.) aren't picked
# up by anything else in this script: the toolchain packages (musl,
# host-gcc-final, ...) are foundational enough that packages built against
# the old config (e.g. a static-only musl with no real libc.so) get reused
# as-is, silently producing broken links in anything built afterward. Opt
# into this explicitly (FORCE_CLEAN=1 ./build.sh, or
# --build-arg FORCE_CLEAN=1 for the Docker build) only when such a change
# was actually made - it's a real `make clean`, wiping build/host/target/
# images (downloads in $DL_DIR are kept), so it costs a full rebuild.
if [ -n "${FORCE_CLEAN:-}" ]; then
    echo "info: FORCE_CLEAN set - running a full 'make clean' before the real build (toolchain-wide config change)"
    make "${MAKE_ARGS[@]}" clean
fi

if [ "$#" -eq 0 ]; then
    set -- all
fi

if should_build_dashboard_assets "$@"; then
    build_dashboard_assets
fi

if should_force_linux_rebuild "$@"; then
    echo "info: forcing a clean linux package rebuild (DTS overlay / injected driver sources aren't tracked as build deps, and only dirclean re-runs the patch step that copies them in)"
    make "${MAKE_ARGS[@]}" "${TOPLEVEL_ARGS[@]}" linux-dirclean
fi

# Same staleness class as linux, for any other package we patch via
# BR2_GLOBAL_PATCH_DIR (image/package/<name>/*.patch): Buildroot's stamps
# don't know our patch changed, so a package already extracted+patched from
# an earlier build (e.g. one that failed later, at build/link) keeps its
# stale .stamp_patched and pre-patch object files forever without this.
# Add future patched packages here the same way if this bites again.
if should_force_linux_rebuild "$@"; then
    echo "info: forcing a clean i2c-tools rebuild (image/package/i2c-tools/*.patch isn't tracked as a build dep either)"
    make "${MAKE_ARGS[@]}" "${TOPLEVEL_ARGS[@]}" i2c-tools-dirclean
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
