# Control4 driver (`openspeakerpoint.c4z`)

A Control4 DriverWorks driver that presents an openSpeakerPoint box as a
network-controllable **amp**. The amp's audio comes in on **line-in (aux)** — the
installer routes a source into its Line In input (in the test rig, the openHC EA3's
analog output). Control4 selects the amp and sets volume/tone/output over the box's MQTT
API; the box plays whatever is on aux. It is **not** a media player or a digital-audio
streaming endpoint — no transport, no library, no streamed input.

## What it exposes

Presented as an **avswitch** audio endpoint (proxy `5001`) with one audio input (Line In)
and one output (Speaker):

| Control4 | → box MQTT |
|---|---|
| `SET_VOLUME_LEVEL`, `PULSE_VOL_UP/DOWN` | `cmd/audio/volume` 0-100 |
| `MUTE_ON/OFF/TOGGLE` | emulated via `cmd/audio/volume` 0 ↔ restore (box has no mute) |
| `SET_INPUT` (selecting the amp) | forces `cmd/audio/source linein` (aux is the source) |
| Property `Output` (off/rca/amp/both), command `SetOutput` | `cmd/audio/output` |
| Properties `Bass`, `Treble`, `Loudness` | `cmd/audio/bass` / `treble` / `loudness` |

On connect it sets `cmd/audio/source linein` so aux is the active source. State flows back
from the box's retained topics (`state/audio/volume|output|bass|treble|loudness`,
`status`) into the proxy (volume/mute) and the read-only Properties (Status, Volume).

The box's USB/AirPlay media player and its transport/library controls are real but are
*not* exposed here — in the amp-fed-by-line-in model they'd command a source that isn't
playing. Drive those directly over MQTT or the box's web UI if needed.

DriverWorks has no MQTT client. The **MQTT 3.1.1 (QoS 0)** framing is the shared
[`control4-lua-mqtt`](https://github.com/The1TrueJoe/control4-lua-mqtt) lib (a git
submodule at `control4-lua-mqtt/`), concatenated ahead of the driver body at package
time. This driver owns only the transport (the `:1883` TCP binding, anonymous on the
LAN) and the box's topic contract; it subscribes `openspeakerpoint/+/#` and learns the
box's base topic from the first retained message.

## Install

1. Add the driver to the project (My Drivers → the built `openspeakerpoint.c4z`).
   The box does not speak Control4 SDDP, so add it manually.
2. On the driver's **Network** tab, set the box's IP (binds the `:1883` connection).
3. Bind the line-level source (the EA3's analog output in the test rig) to the amp's
   **Line In (Aux)** input, the room's speakers to **Speaker Output**, and a room to
   **Room**.

## Build

`package.sh` assembles `build/driver.lua` (the shared `C4MQTT` codec + `driver.src.lua`)
and copies the XML/c4zproj into `build/`. CI (`.github/workflows/driver.yml`) checks out
submodules, runs `./package.sh && lua5.1 test.lua`, then the Control4 driver packager on
`build/`, uploading `openspeakerpoint.c4z` as an artifact (and attaching it to releases).

Locally (needs the submodule — `git submodule update --init --recursive`):

```sh
cd drivers/control4 && ./package.sh && lua5.1 test.lua
```

## Scope

Control only — the amp's audio always arrives on line-in. Native Control4 audio streaming
to the box (a DIGITAL_AUDIO_CLIENT that receives a stream) is intentionally out of scope.
