# control4-speakerpoint-modern
Giving the 2006 Control4 Speakerpoint a new life

Custom Linux firmware for the Control4 SpeakerPoint (Cirrus EP9301 / EDB9301
reference design), replacing the stock 2.4.21 kernel + cramfs userland while
keeping the stock RedBoot bootloader untouched. See
[docs/hardware.md](docs/hardware.md) for hardware facts, the boot mechanism,
and the TFTP netboot workflow, and `/memories/session/plan.md` for the full
phased project plan.

## Status: Phase 1 (minimal SSH shell + app framework scaffolding)

## Layout

- `buildroot/` - Buildroot itself, vendored as a git submodule and pinned to
  a known-working commit, so the whole build is reproducible from this repo
  alone.
- `br-external/` - Buildroot external tree (`BR2_EXTERNAL`): board defconfig,
  kernel config fragment, device tree, rootfs overlay/post-build hook, and
  custom app packages (`br-external/package/`).
- `apps/` - source for custom SpeakerPoint applications, built by the
  cross-toolchain and wired into the image via `br-external/package/`. See
  [apps/README.md](apps/README.md) for how to add a new one.
- `build.sh` - single reproducible entry point used locally, in Docker, and
  in CI.
- `docker/` - container providing the Buildroot host build dependencies, for
  building on a Mac or anywhere else without hand-installing them. See
  [docker/README.md](docker/README.md).
- `.github/workflows/build.yml` - GitHub Actions CI: builds the image on
  every push/PR touching build-relevant paths and uploads the resulting
  images as an artifact.
- `tftp-serve.py` - read-only TFTP server for netbooting a
  freshly built image straight into RAM via RedBoot, with zero flash writes.
- `docs/hardware.md` - hardware facts, MTD layout, RedBoot boot script, TFTP
  netboot commands, and the recovery procedure.

## Building

```sh
git clone --recurse-submodules <this repo>
# or, if already cloned: git submodule update --init --recursive

./build.sh
```

Outputs land in `output/images/`: `zImage.ep93xx-speakerpoint` (kernel+DTB,
appended, for flashing to mtd2), `rootfs.squashfs` (mtd3), and
`rootfs.cpio.gz` (initramfs, for netboot without touching mtd3 at all).

No Buildroot host dependencies to install by hand? Build inside the
container instead - see [docker/README.md](docker/README.md).

Other useful targets: `./build.sh menuconfig`, `./build.sh linux-menuconfig`,
`./build.sh clean`.

## Testing a build (recommended: TFTP netboot, no flash writes)

```sh
python3 -m pip install -r requirements.txt
sudo ./tftp-serve.py output/images
```

Then, at the RedBoot prompt (`^C` during the 1 second boot delay), `load` +
`exec` the kernel (and optionally the initramfs) straight into RAM - see
"TFTP netboot" in [docs/hardware.md](docs/hardware.md) for the exact
commands. This is the fastest way to iterate: nothing is written to flash,
so there's no way to brick the board this way.

Once an image is verified good over netboot, RedBoot's `fis create` commands
persist it to flash permanently - also documented in
[docs/hardware.md](docs/hardware.md). If something ever fails to boot after
flashing, see "Recovery path" there.

## CI

Pushes/PRs touching `br-external/`, `apps/`, `buildroot`, or `build.sh`
trigger a GitHub Actions build (`.github/workflows/build.yml`) that caches
the download cache and toolchain output and uploads `output/images/*` as a
build artifact. The first cold-cache CI run is slow (full toolchain build);
subsequent runs reuse the cache.
