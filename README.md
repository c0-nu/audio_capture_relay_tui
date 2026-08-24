# AudioCaptureRelay TUI

> ⚠️ **This code was written by an AI (Claude, by Anthropic)** — the design decisions
> too, not just the typing. It has been tested by ear on real hardware, but it has not
> been reviewed line by line by a human. See [How this was built](#how-this-was-built).

[日本語版 README](README.ja.md)

A small relay that reads any PulseAudio / PipeWire-Pulse capture source and plays it
back as **its own playback stream**, with a single-screen `top`-style TUI that draws
the waveform in Unicode braille characters.

Why relay audio back to yourself? Screen sharing in Discord and similar apps picks up
*application playback*, not capture sources. Relaying a `.monitor` source through this
tool turns it into a normal playback stream, so the people you are sharing with can
hear it.

```
Latency: 60ms (ring 34 / out 24)   drift 0ms   underruns 0
⠀⠀⢀⣀⣤⣶⣿⣿⣷⣶⣤⣀⡀⠀⠀⠀⠀⢀⣠⣴⣾⣿⣿⣶⣄⡀⠀⠀
```

## Requirements

- A PulseAudio-compatible server (PulseAudio, or PipeWire with `pipewire-pulse`)
- C++20 compiler, CMake ≥ 3.16
- `libpulse`, `ncursesw`

Arch / CachyOS:

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf libpulse ncurses
```

Debian / Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build pkg-config libpulse-dev libncursesw5-dev
```

Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config pulseaudio-libs-devel ncurses-devel
```

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Install (optional):

```bash
sudo cmake --install build
```

Arch users can build a package instead:

```bash
cd packaging && makepkg -si
```

## Usage

```bash
./build/audio_capture_relay --list          # list capture sources and output sinks
./build/audio_capture_relay                 # use the default source
./build/audio_capture_relay --source 0      # by index
./build/audio_capture_relay --source monitor # by exact name or substring
./build/audio_capture_relay --select        # pick interactively
./build/audio_capture_relay --sink hdmi     # choose the output sink
./build/audio_capture_relay --no-relay      # capture and visualize only, no playback
./build/audio_capture_relay --no-tui        # one-line status instead of the TUI
./build/audio_capture_relay --no-waveform   # start with the waveform hidden
```

Waveform drawing style:

```bash
./build/audio_capture_relay --waveform-style line
```

| Style | Looks like | Trade-off |
|---|---|---|
| `envelope` (default) | fills each column from its min to its max | never misses a peak, even in a narrow terminal |
| `line` | picks one sample per column and joins them | thin oscilloscope-style trace |

Press `s` to switch at any time.

### Visualizing without relaying

```bash
./build/audio_capture_relay --no-relay
```

Reads the source and draws it, but **creates no playback stream at all** — nothing
appears in `pavucontrol`, nothing reaches a screen share, and no drift correction runs.
Use it when you only want to watch levels and the waveform.

This is not the same as `--volume 0`, which still opens a playback stream and keeps the
whole latency machinery going. With `--no-relay` the volume, mute, pause keys and the
`Latency:` line are all gone, because none of them mean anything.

### Bluetooth sources

Nothing special is needed. When a Bluetooth audio device is connected, the sound
server exposes it as an ordinary source (`bluez_input.XX_XX_XX_XX_XX_XX.a2dp-source`
for audio coming *from* a phone, or `bluez_output.XX_….monitor` for what is being
sent to a headset), so it shows up in `--list` and can be selected with `--source`.

## Keys

| Key | Action |
|---|---|
| `q` | quit |
| `+` / `-` | software volume |
| `m` | mute / unmute |
| `w` | toggle the waveform |
| `s` | waveform style (envelope / line) |
| `p` or Space | pause / resume |

## Latency

`--latency-ms` (default 120) targets the **total** delay from capture to audible
output — both this program's own ring buffer and the queue held by the sound server.
The TUI shows the split on the `Latency:` line (`ring` = ours, `out` = the server's).

### Start here

```bash
./build/audio_capture_relay --low-latency
```

Same as `--chunk-ms 5 --latency-ms 60`. An explicit `--chunk-ms` / `--latency-ms`
still wins.

### `--chunk-ms` matters more than `--latency-ms`

Shortening the chunk shrinks **both** our own headroom and what the server holds.
Measured on PipeWire-Pulse (runs of 14–90 s, graph quantum already down at 256):

| Settings | Measured | ring / out |
|---|---|---|
| default (`--chunk-ms 20 --latency-ms 120`) | ~121ms | 50 / 58 |
| `--chunk-ms 10 --latency-ms 50` | 71ms | 20 / 41 |
| `--low-latency` (chunk 5 / target 60) | 60ms, 0 underruns in 90 s | 34 / 24 |
| `--chunk-ms 5 --latency-ms 20` | 41ms (`floor` 37) | — |

Add another 5–8 ms for the capture side, which is not included in the display.

### Going lower — the quantum is already down

It is tempting to blame PipeWire's quantum (nominally 1024 = 21.3 ms), but lowering it
does nothing here: **PipeWire already reduces the quantum to match the latency a client
asks for**, so while `--low-latency` is running it is 256 (5.3 ms) without any tuning.

```bash
pw-top -b -n 3
```

The `QUANT` column for `AudioCaptureRelay` and for the output sink should both read 256,
with `clock.force-quantum` still 0. Forcing `clock.force-quantum 256` measurably changed
nothing (total stayed at 60 ms, `out` at 32–35 ms).

What is left in `out` is the **sink's ALSA buffer**:

```bash
pw-dump | grep -A2 api.alsa.period-size
```

The USB CODEC used for these measurements reported `period-size 512` + `headroom 512`
= 1024 frames ≒ 21.3 ms. That is separate from the graph quantum, so `force-quantum`
cannot touch it. To shrink it, override `api.alsa.headroom` per device in WirePlumber —
`~/.config/wireplumber/wireplumber.conf.d/51-alsa-headroom.conf`:

```
monitor.alsa.rules = [
  {
    matches = [ { node.name = "<node.name of the sink>" } ]
    actions = { update-props = { api.alsa.headroom = 128 } }
  }
]
```

Apply with `systemctl --user restart wireplumber` (**all audio cuts out briefly**).
Delete the file and run the same command to revert.

Measured on that USB CODEC over 90 s:

| | before | after |
|---|---|---|
| sink's ALSA side | period 512 + headroom 512 = 21.3ms | period 128 + headroom 256 = 8ms |
| `--low-latency` | 60ms (out 32–35) | 60ms (out 25–28) |
| `--latency-ms 20 --chunk-ms 5` | 44–47ms (`floor` 42) | **41ms** (`floor` 37) |

Zero underruns either way. Note that **writing `headroom = 128` actually settled at 256** —
PipeWire adjusts it to the period size, and it will not go below that.

This is a **persistent system setting**, and cutting too deep invites xruns. Some devices
distort at 128, so lower it in steps.

### What you give up

Thinner buffers starve more easily when the CPU stalls. A starved chunk fades to silence
over a few milliseconds (and bumps `underruns`), which is inaudible once but should be
answered by raising `--latency-ms` if it repeats.

`pads` counts chunks that were *not* starved but still needed a frame or two of filler
because of drift correction. A nonzero value is normal. If the requested target is
physically impossible, the tool settles at the shallowest level it can actually hold and
displays it as `floor`.

## Caveats

A `.monitor` source captures whatever is being played to that output device. Sending the
relay back to the *same* device can therefore feed the tool its own output. Use `--sink`
to pick a different one, or move the AudioCaptureRelay stream in `pavucontrol`.

Creating a dedicated null sink:

```bash
pactl load-module module-null-sink \
  sink_name=discord_share \
  sink_properties=device.description=DiscordShare
```

## Development

Tests cover `src/domain/` only — the pure half, which depends on nothing but the standard
library. They build without PulseAudio or ncurses:

```bash
cmake -S . -B build-tests -G Ninja -DACR_BUILD_APP=OFF -DACR_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Catch2 v3 is used from the system if present, and fetched otherwise.

Layering is one-directional: `main` → `app` / `adapters` → `domain`. `domain/` must never
include `<pulse/*>`, `<ncurses.h>`, or `<iostream>`. See [CLAUDE.md](CLAUDE.md) for the
full conventions (Japanese).

## How this was built

The C++ in this repository was written by Claude (Anthropic), across a series of
conversations. That includes the design decisions — the layering, the drift-correction
scheme, the pacing logic — not just the code that implements them.

What is actually mine, as the owner of this repository:

- **The conventions.** `CLAUDE.md` is based on a coding standard I wrote. But it was
  Claude that interpreted it and decided what it meant in practice, case by case.
- **Listening to the result.** Every latency figure in this README was measured on my
  own machine, and the calls about whether the audio was actually *right* — no clicks,
  no dropouts, picked up correctly by a screen share — are mine.

What is **not** mine is a careful reading of the code. I have not done one. The tests
under `tests/` cover `src/domain/` and pass, and the tool has been run for long stretches
on PipeWire-Pulse under Arch/CachyOS, but that is testing, not review.

So please **read the code yourself before running it on anything you care about.** It
runs a real-time audio thread and talks directly to your sound server. It is offered as
something that works for me, not as something a human has audited.

## License

MIT. See [LICENSE](LICENSE).
