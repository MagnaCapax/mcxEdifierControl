# ADR 0002: C11 over BlueZ's D-Bus API, one command per run, no daemon

Date: 2026-11-18
In force since: this change

## Status
Proposed

## Context
The tool must be small, have few dependencies and build on any Linux system with BlueZ. Talking to a BLE device on Linux means either going through bluetoothd (D-Bus) or opening an L2CAP ATT socket directly. A direct socket needs the LE address up front and raw HCI access to scan, and it runs alongside bluetoothd's own GATT client on the same connection. BlueZ also ties a discovery session and a notification session to the D-Bus client that started it, so a short-lived helper such as `busctl` cannot keep notifications open.

## Options considered
- C11 with libdbus: libdbus is the reference D-Bus library and bluetoothd itself links it, so it is present wherever BlueZ runs. Marshalling is verbose but contained in `src/dbus.c`.
- C11 with sd-bus: shorter code, but needs libsystemd, which not every distribution has.
- C with GDBus (GLib): convenient, but pulls in GLib for a few calls.
- Raw L2CAP ATT socket: no D-Bus, but no scanning without privileges, and an untested interaction with bluetoothd on this speaker.
- A daemon that holds the link and serves a socket (the shape of the prototype this tool replaces): keeps the slow connect (10-26 s measured) out of each command, at the cost of a second program, a socket protocol and state.

## Decision
C11 and libdbus, with `AcquireNotify` and `AcquireWrite`. Each returns a socket file descriptor: reads deliver one notification each, and each write is sent as a write without response, as the official app sends its frames. Waits poll BlueZ's object tree every 250-500 ms instead of subscribing to signals: fewer moving parts, and every wait is seconds long anyway.

One command per run, no daemon. `monitor` keeps a connection open for as long as it runs. A volume change reads, sets and reads back within one connection, so the hardware check needs no persistent link.

## Consequences
- Runtime dependencies: libc, libdbus and a running bluetoothd whose GATT client offers `AcquireNotify`/`AcquireWrite`. They were added in BlueZ 5.46 as experimental and appear without that tag in the current BlueZ documentation.
- Each command pays the connect time. If volume keys need low latency later, a persistent mode is a new ADR.
- If another program already holds the RX notifications (a running `bluetoothctl` with notify on, for example), `AcquireNotify` fails and the tool says so.
- The tool disconnects only a connection it made, so the phone app can connect afterwards.

## References
`src/dbus.c`, `src/bluez.c`, BlueZ `doc/org.bluez.GattCharacteristic.rst` (AcquireWrite, AcquireNotify), BlueZ `src/gatt-client.c` (writes from the acquired socket go out with `bt_gatt_client_write_without_response`), BlueZ `ChangeLog` (5.46).
