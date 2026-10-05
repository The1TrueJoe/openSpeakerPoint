#!/bin/sh
# Runs against $TARGET_DIR after the rootfs overlay has been copied in,
# before the squashfs image is generated. Root is read-only (squashfs),
# so anything that needs to write at runtime (SSH host keys, pidfiles)
# has to live under a tmpfs. /run and /tmp are already tmpfs via the
# default /etc/fstab; point the bits that need to write there.
#
# Everything here is a best-effort convenience tweak, not required for a
# valid/bootable image: this script must never fail the overall Buildroot
# build. `trap ... EXIT` guarantees we always exit 0, and every operation
# below only warns (to stderr) on failure instead of aborting.
trap 'exit 0' EXIT

warn() {
	echo "warn: $*" >&2
}

TARGET_DIR="$1"

if [ -z "$TARGET_DIR" ] || [ ! -d "$TARGET_DIR" ]; then
	warn "invalid TARGET_DIR passed to post-build hook: '$TARGET_DIR' - skipping symlink tweaks"
	exit 0
fi

force_symlink() {
	link_target="$1"
	link_path="$2"
	parent_dir="$(dirname "$link_path")"

	# Ensure parent exists, then replace any pre-existing file/dir/link atomically.
	if [ ! -d "$parent_dir" ]; then
		if ! mkdir -p "$parent_dir"; then
			warn "failed to create parent dir for symlink: $parent_dir"
			return 0
		fi
	fi

	# Skip work if link is already exactly right.
	if [ -L "$link_path" ] && [ "$(readlink "$link_path")" = "$link_target" ]; then
		return 0
	fi

	if ! rm -rf "$link_path"; then
		warn "failed to remove path before symlink: $link_path"
		return 0
	fi

	if ! ln -s "$link_target" "$link_path"; then
		warn "failed to create symlink '$link_path' -> '$link_target'"
		return 0
	fi
}

# /var/run -> /run (tmpfs). Leaves the rest of /var (e.g. /var/www/data,
# our static site content) as real, read-only files from the image.
force_symlink /run "$TARGET_DIR/var/run"

# /data is the persistent settings partition on an installed box (osp-data,
# mounted by S03osp-data; tmpfs on a netboot). The mountpoint has to exist in
# the image: the squashfs root is read-only, so it cannot be made at boot.
mkdir -p "$TARGET_DIR/data" || warn "failed to create /data"

# SSH host keys live on /data, so they survive a reboot: regenerating them on
# every boot took minutes on this 200 MHz ARM9 and changed the host key each
# time. S03osp-data creates /data/dropbear before dropbear starts (S50).
force_symlink /data/dropbear "$TARGET_DIR/etc/dropbear"

# Make the MOTD refresh script executable so it can run during boot and
# write the current LAN address into /etc/motd.
if [ -f "$TARGET_DIR/etc/init.d/S41motd" ]; then
	chmod 0755 "$TARGET_DIR/etc/init.d/S41motd" || true
fi

if [ -f "$TARGET_DIR/sbin/reboot" ]; then
	chmod 0755 "$TARGET_DIR/sbin/reboot" || true
fi

if [ -f "$TARGET_DIR/etc/init.d/S10led-animation" ]; then
        chmod 0755 "$TARGET_DIR/etc/init.d/S10led-animation" || true
fi

# so boot reflects the current defconfig package set.
rm -f "$TARGET_DIR/usr/bin/example-daemon" || true
rm -f "$TARGET_DIR/etc/init.d/S60example-daemon" || true

# Man pages/docs are dead weight here: nothing on this headless device (no
# `man` reader in our BusyBox config) ever reads them.
rm -rf "$TARGET_DIR/usr/share/man" "$TARGET_DIR/usr/share/doc" || true

# i2c-tools installs these unconditionally (no Kconfig option to skip them);
# they're x86 DIMM-SPD/VAIO/monitor-DDC diagnostic tools, irrelevant on this
# hardware. We only ever use i2ctransfer (speakerpoint-audio-apply) and keep
# i2cdetect/i2cget/i2cset/i2cdump for manual debugging.
for f in eeprog decode-dimms decode-vaio ddcmon decode-edid; do
	rm -f "$TARGET_DIR/usr/bin/$f" "$TARGET_DIR/usr/sbin/$f" || true
done
rm -f "$TARGET_DIR/usr/sbin/i2c-stub-from-dump" || true

# shairport-sync (AirPlay) needs a C++ toolchain to build, but with our
# options (no convolution, no AirPlay 2, no Apple ALAC) it compiles no C++
# and no longer links libstdc++ itself (see the shairport-sync --as-needed
# patch). Buildroot's gcc-final.mk still unconditionally ships libstdc++.so
# whenever C++ is toolchain-enabled at all, and libconfig's build similarly
# ships libconfig++.so alongside the plain-C libconfig.so that shairport-sync
# actually links. Prune each, in dependency order, only if nothing on the
# rootfs actually references it - verified via real NEEDED entries, not
# assumed.
READELF="$(ls "$HOST_DIR"/bin/*-readelf 2>/dev/null | head -n1)"
if [ -n "$READELF" ] && [ -x "$READELF" ]; then
	prune_if_unused() {
		lib_glob="$1"
		soname="${lib_glob%.so\*}.so"
		lib_path="$(ls "$TARGET_DIR"/usr/lib/$lib_glob 2>/dev/null | head -n1)"
		[ -n "$lib_path" ] || return 0

		needed_by=""
		for f in $(find "$TARGET_DIR/usr/bin" "$TARGET_DIR/usr/sbin" \
		                 "$TARGET_DIR/bin" "$TARGET_DIR/sbin" \
		                 "$TARGET_DIR/usr/lib" "$TARGET_DIR/lib" \
		                 -type f 2>/dev/null); do
			[ "$f" = "$lib_path" ] && continue
			if "$READELF" -d "$f" 2>/dev/null | grep -F "$soname" | grep -q NEEDED; then
				needed_by="$needed_by $f"
			fi
		done

		if [ -z "$needed_by" ]; then
			rm -f "$TARGET_DIR"/usr/lib/$lib_glob
			echo "info: removed unused $lib_glob from target"
		else
			warn "$lib_glob is dynamically needed by:$needed_by - keeping it"
		fi
	}

	prune_if_unused "libconfig++.so*"
	prune_if_unused "libstdc++.so*"
else
	warn "no cross readelf found under \$HOST_DIR/bin - leaving libstdc++/libconfig++ in place (unverified)"
fi
