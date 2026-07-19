# apps/

Source for the custom applications this firmware ships, built by the
cross-toolchain as part of the normal `./build.sh` (or `make`) run - not
hand-copied into the rootfs overlay.

Each subdirectory here is one app, paired with a Buildroot package under
`br-external/package/<name>/` that knows how to build+install it. See
`apps/example-daemon/` + `br-external/package/example-daemon/` for a minimal
working template (plain C + Makefile, installed to `/usr/bin`, started by a
BusyBox init script).

## Adding a new app

1. `apps/<name>/` - your source + a `Makefile` (or CMake/whatever, see below).
2. `br-external/package/<name>/Config.in` - one `BR2_PACKAGE_<NAME>` bool
   option (copy `example-daemon`'s).
3. `br-external/package/<name>/<name>.mk` - build/install rules (copy
   `example-daemon`'s `.mk` and adjust the build command + installed files;
   for CMake-based apps use `$(eval $(cmake-package))` instead of
   `$(eval $(generic-package))` with `HOST_MAKE_CMDS`/etc - see Buildroot's
   own `docs/manual/adding-packages-*.txt` for the CMake/Python/Go package
   infra if a given app isn't a plain Makefile).
4. `source "$BR2_EXTERNAL_SPEAKERPOINT_PATH/package/<name>/Config.in"` in
   `br-external/Config.in`.
5. `BR2_PACKAGE_<NAME>=y` in `br-external/configs/speakerpoint_defconfig`.
6. If it needs to start at boot, drop a BusyBox-init script under
   `br-external/package/<name>/S##<name>` and install it to
   `/etc/init.d/S##<name>` from the `.mk`'s `INSTALL_TARGET_CMDS`.

Rebuild (`./build.sh`) - Buildroot only rebuilds what changed.
