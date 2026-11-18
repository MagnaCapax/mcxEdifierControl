# mcxEdifierControl

`edifierctl` is a small command-line tool that talks to an **Edifier R2750DB MKII** powered speaker over Bluetooth LE from Linux. It is meant for the speaker's own master volume, input source and ECO mode, and for watching what the speaker reports. One C binary that depends directly only on libc and libdbus, with BlueZ doing the radio work.

> **Status: unverified on hardware.** The frame format and the BLE layout are confirmed on a real unit, but the speaker has not yet answered a query. Every command layout comes from analysis of the official app and is untested. The [verification table](#verification-status) shows the state per command.

## Why this exists

The R2750DB MKII has no absolute volume over Bluetooth: its manual says "Volume sync is not supported", and its AVRCP record offers no volume category. A computer's volume slider therefore only attenuates the digital signal before it is encoded, and the speaker's own volume is reachable only by its knob, its remote, HDMI CEC or the phone app. The ECO power-saving setting is reachable only by the remote or the app. The speaker has a BLE control channel that the phone app uses; this tool uses the same channel. [HARDWARE.md](docs/HARDWARE.md) has the full sheet, including the Bluetooth chip.

## Compatibility

- Linux with BlueZ running (`bluetoothd`), on any distribution: CI builds and tests on Ubuntu (gcc and clang), Debian 12 and Alpine (musl).
- BlueZ's GATT client must offer `AcquireNotify` and `AcquireWrite`. They were added in BlueZ 5.46, first as experimental; the current BlueZ documentation lists them without that tag.
- A Bluetooth adapter with LE support, powered on.
- Only the R2750DB MKII is described. Other Edifier devices use the same frame family, but their commands are not in this tool and it does not claim to work with them.

## Install

Build dependencies: a C11 compiler, `make`, `pkg-config` and the libdbus headers.

```sh
sudo apt install build-essential pkg-config libdbus-1-dev    # Debian, Ubuntu
sudo apk add build-base pkgconf dbus-dev                     # Alpine

make
make test                       # hermetic unit tests, no Bluetooth needed
sudo make install               # /usr/local/bin/edifierctl (PREFIX=... to change)
```

The binary links directly only libc and libdbus. It writes no files.

## Usage

```
edifierctl [-a ADDRESS] [-t SECONDS] [-v] [-f] COMMAND [ARGS]
```

| Command | What it does |
|---|---|
| `scan` | Lists speakers that advertise the control service, for 30 s. |
| `info` | Runs every read-only query in one connection and prints one line each. |
| `get NAME` | Runs one query: `caps volume input eco autooff timer ldac multipoint eq sound subout firmware mac name a2dp`. |
| `volume` | Prints the volume as `current (max N)`. |
| `volume +N`, `volume -N`, `volume N` | Reads the volume, computes the target, clamps it to the reported maximum, sets it, reads it back and prints `before -> after`. |
| `input SOURCE` | Sets the input source (a number 0–255) and keeps the group the speaker reports. Source numbers are not documented yet; read them with `get input`. |
| `eco on`, `eco off` | Sets ECO / power-saving mode and reads it back. |
| `raw APP CMD [HEX]` | Sends one frame, e.g. `raw ec 66`. See [Safety](#safety). |
| `monitor` | Stays connected and prints every frame the speaker sends, until Ctrl-C. |

| Option | Meaning |
|---|---|
| `-a ADDRESS` | The speaker's BLE address. Without it, the speaker is found by its advertised UUID; if more than one answers, the tool lists them and asks for `-a`. |
| `-t SECONDS` | How long to wait for each reply (default 3). |
| `-v` | Also print the payload of frames the speaker sends unasked. |
| `-f` | Allow destructive opcodes in `raw`. |

Exit status: 0 success, 1 no reply or link failure, 2 usage error or refused by the gate. Results go to standard output; progress and errors go to standard error.

Ctrl-C or SIGTERM ends a wait at its next check and the tool then disconnects cleanly. A D-Bus call already under way finishes first: a connect can take up to 60 s before it gives up.

The speaker's BLE address differs from its classic Bluetooth address (on the unit observed, in one byte). `scan` shows the BLE one. A connect takes 10–26 s, so each command takes about that long. On exit the tool disconnects a link it opened and leaves alone a link that something else opened.

## Safety

- Every frame passes a gate ([ADR 0003](docs/adr/0003-send-gate-volume-rule-and-quiet-logging.md)). Firmware update (`10 11 12`) and bulk transfer (`79–7B`, `88–8A`) opcodes are never sent, not even with `-f`. Factory reset, power off, disconnect, re-pair, clear pairing records, connect or disconnect by address, delete log and rename (`07 CE CD CF C7 AE AF B0 CA`) need `-f`. Any other opcode must be in the model's command table. The gate ignores the app code.
- There is no firmware-update code and no opcode sweep or fuzz mode.
- Volume changes always start from a fresh read and stay within the maximum the speaker reports. When trying the tool for the first time, step by one: `volume +1`, then `volume -1`.
- `eq` writes are raw: write back only bytes you read with `get eq`, changing the first.

## Privacy

Speaker frames `01` and `02` carry the title and artist of what is playing. Frames the speaker sends unasked are printed as app, opcode and length only; their payload appears only with `-v`. Nothing is written to disk.

## Verification status

What is confirmed on a real R2750DB MKII:

| Item | Status |
|---|---|
| Advertisement (`EDIFIER BLE`, UUID `0x2C03`, manufacturer data) | verified on the unit |
| GATT layout (control service, RX notify, TX write) | verified on the unit |
| Frame format and checksum | verified on the unit, from frames the speaker sent |
| `a2dp` push (`C3`) | verified on the unit (pushed; not yet as a reply) |
| `01` / `02` pushes | seen on the unit (pushed) |

Every command, `caps volume input eco autooff timer timer-cancel ldac multipoint eq sound subout firmware mac name a2dp` as a query or a set: **derived from the app, not yet verified on hardware.** A command moves to the table above only after it is verified on the speaker.

[PROTOCOL.md](docs/PROTOCOL.md) describes the frames, the command table with its sources, what has been observed and the prior art.

## How it works

`bluetoothd` does scanning, connecting and GATT. `edifierctl` asks it over D-Bus to find the speaker (filtered on the advertised UUID), connect and resolve services. It then takes two socket descriptors from BlueZ: `AcquireNotify` on the RX characteristic delivers each notification as one read, and `AcquireWrite` on TX sends each write as a write without response, as the official app does. The frame code reassembles notifications into frames and checks each checksum.

```
src/frame.c     frame build, checksum, reassembly (pure)
src/model.c     the R2750DB MKII table, the send gate, the volume rule (pure)
src/decode.c    reply decoding and hex parsing (pure)
src/dbus.c      libdbus helpers
src/bluez.c     find, connect, acquire, read, write, disconnect
src/session.c   one link per run: the gate, draining, request and reply
src/commands.c  the commands
src/main.c      options and dispatch
tests/          hermetic tests of the pure modules
```

Adding a model means adding a table in `src/model.c` ([ADR 0001](docs/adr/0001-name-and-model-scope.md)).

## Name

The repository is `mcxEdifierControl`; the program it builds is `edifierctl`. [ADR 0001](docs/adr/0001-name-and-model-scope.md) records the names considered and why these were chosen.

## Contributing

Read [AGENTS.md](AGENTS.md) and the [ADRs](docs/adr/). The build uses `-Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion -Wsign-conversion -Werror`; CI also runs the tests under AddressSanitizer and UndefinedBehaviorSanitizer and builds on Debian 12 and Alpine. A change to behaviour comes with its test and, when significant, an ADR.

## Attribution

Author: Aleksi Ursin / MCX (with Väinämöinen) — [Pulsed Media](https://pulsedmedia.com)

Independently written. Contains no Edifier app code, keys or catalog data; the protocol description comes from interoperability analysis and observation of the operator's own unit.

Edifier and EDIFIER ConneX are names of their owner. This project is independent and is not affiliated with, endorsed by or supported by Edifier.

## License

Apache License 2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
