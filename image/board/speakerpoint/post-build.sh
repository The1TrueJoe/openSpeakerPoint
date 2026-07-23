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

# dropbear's init script special-cases /etc/dropbear being a symlink to
# /var/run/dropbear: it creates that directory instead of trying (and
# failing, since / is read-only) to mkdir /etc/dropbear itself. Host
# keys will be regenerated on every boot, which is fine for this test
# image - it is not meant to be a production/security-hardened build.
force_symlink /var/run/dropbear "$TARGET_DIR/etc/dropbear"

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

if [ -f "$TARGET_DIR/etc/init.d/K99z-ep93xx-reset" ]; then
        chmod 0755 "$TARGET_DIR/etc/init.d/K99z-ep93xx-reset" || true
fi
# so boot reflects the current defconfig package set.
rm -f "$TARGET_DIR/usr/bin/example-daemon" || true
rm -f "$TARGET_DIR/etc/init.d/S60example-daemon" || true
