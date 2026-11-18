# Control4 driver (`openspeakerpoint.c4z`)

A Control4 DriverWorks driver that gives an openSpeakerPoint box a presence in a
Control4 project and controls it over the box's own MQTT API. The box carries the
audio itself (line-in from an EA3 ADC, or its on-board USB/AirPlay media); Control4
selects a source into it, sets volume, and drives transport.

## What it exposes

Presented as an **avswitch** audio endpoint (proxy `5001`):

| Control4 | → box MQTT |
|---|---|
| `SET_VOLUME_LEVEL`, `PULSE_VOL_UP/DOWN` | `cmd/audio/volume` 0-100 |
| `MUTE_ON/OFF/TOGGLE` | emulated via `cmd/audio/volume` 0 ↔ restore (box has no mute) |
| `SET_INPUT` (Line In / On-Board Media) | `cmd/audio/source` `linein` / `media` |
| Actions / commands: Play, Pause, Stop, Next, Previous | `cmd/transport` |
| Action: Rescan Library | `cmd/library/rescan` |
| Action: Return to Stock | `cmd/system/restore confirm` |
| Property `Output` (off/rca/amp/both) | `cmd/audio/output` |
| Properties `Bass`, `Treble`, `Loudness` | `cmd/audio/bass` / `treble` / `loudness` |

State flows back from the box's retained topics (`state/audio/volume|source|output|bass|
treble|loudness`, `state/now`, `status`) into the proxy (volume/mute) and the read-only
Properties (Status, Now Playing, Source, Volume).

DriverWorks has no MQTT client, so the driver frames **MQTT 3.1.1 (QoS 0)** itself over the
TCP binding to the box's mosquitto (`:1883`, anonymous on the LAN). It subscribes
`openspeakerpoint/+/#` and learns the box's base topic from the first retained message.

## Install

1. Add the driver to the project (My Drivers → the built `openspeakerpoint.c4z`).
   The box does not speak Control4 SDDP, so add it manually.
2. On the driver's **Network** tab, set the box's IP (binds the `:1883` connection).
3. Bind the room's speakers to **Speaker Output**, and the EA3 line-level source to
   **Line In** if you route line-in through Control4. Bind a room to **Room**.

## Build

CI (`.github/workflows/driver.yml`) runs the `lua5.1` self-check (`test.lua`) then the
Control4 driver packager, uploading `openspeakerpoint.c4z` as an artifact (and attaching
it to releases). Locally: `lua5.1 test.lua`.

## Native Control4 audio streaming to this box — not feasible

A native digital-audio client (so Control4 streams FLAC-in-RTP straight to the box, cf.
the sibling `control4-audio-client`) was assessed and ruled out on this hardware:

- **arm920t (ARMv4T)**, soft-float: Rust has no `std` target for ARMv4T-linux, and
  `arm-unknown-linux-musleabi` emits instructions this core lacks.
- **4.5 MiB rootfs, already near the ceiling**: GStreamer + glib + a Rust binary is
  10-20+ MB of libraries. It does not fit.
- **32 MB RAM** (rootfs is an initramfs) at ~200 MHz can't carry the GStreamer
  rtpbin/netclock stack alongside the existing MPD/shairport/control daemons.

If native streaming is ever wanted, the path is a tiny **C** daemon implementing just the
client wire protocol (TCP 6100 control + RTP→FLAC depay/decode via libFLAC → the existing
ALSA out), as a Buildroot package — not a Rust+GStreamer port.
