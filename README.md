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
- `br-external/` - Buildroot external tree (`BR2_EXTERNAL`): board defconfig,
  kernel config fragment, device tree, rootfs overlay/post-build hook, and
  custom app packages (`br-external/package/`).
- `apps/` - source for custom SpeakerPoint applications, built by the
  cross-toolchain and wired into the image via `br-external/package/`. See
  [apps/README.md](apps/README.md) for how to add a new one.
- `build.sh` - single reproducible entry point used locally, in Docker, and
  in CI.
- `Dockerfile` - containerized builder that clones Buildroot into the image,
  so firmware builds can run entirely in Docker.
- `.github/workflows/build.yml` - GitHub Actions CI: builds the image on
  every push/PR touching build-relevant paths and uploads the resulting
  images as an artifact.
- `tftp-serve.py` - read-only TFTP server for netbooting a
  freshly built image straight into RAM via RedBoot, with zero flash writes.
- `docs/hardware.md` - hardware facts, MTD layout, RedBoot boot script, TFTP
  netboot commands, and the recovery procedure.

## Building

```sh
git clone <this repo>
cd control4-speakerpoint-modern
docker build --target artifacts --output type=local,dest=./output .
```

This builds everything inside Docker and exports final images to `output/images`
with no runtime bind-mounted build flow.

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

Pushes/PRs touching `br-external/`, `apps/`, `build.sh`, or `Dockerfile`
trigger a GitHub Actions build (`.github/workflows/build.yml`) that builds in
Docker and exports `output/images/*` as a build artifact. The first build is
still slow because it includes a full Buildroot toolchain build.
