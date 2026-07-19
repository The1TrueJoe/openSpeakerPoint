#!/bin/sh
# Runs against $TARGET_DIR after the rootfs overlay has been copied in,
# before the squashfs image is generated. Root is read-only (squashfs),
# so anything that needs to write at runtime (SSH host keys, pidfiles)
# has to live under a tmpfs. /run and /tmp are already tmpfs via the
# default /etc/fstab; point the bits that need to write there.
set -e

TARGET_DIR="$1"

# /var/run -> /run (tmpfs). Leaves the rest of /var (e.g. /var/www/data,
# our static site content) as real, read-only files from the image.
rm -rf "$TARGET_DIR/var/run"
ln -sf /run "$TARGET_DIR/var/run"

# dropbear's init script special-cases /etc/dropbear being a symlink to
# /var/run/dropbear: it creates that directory instead of trying (and
# failing, since / is read-only) to mkdir /etc/dropbear itself. Host
# keys will be regenerated on every boot, which is fine for this test
# image - it is not meant to be a production/security-hardened build.
rm -rf "$TARGET_DIR/etc/dropbear"
ln -sf /var/run/dropbear "$TARGET_DIR/etc/dropbear"
