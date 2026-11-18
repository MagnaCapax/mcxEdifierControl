# mcxEdifierControl: notes for contributors and coding agents

## What this is

`edifierctl`, a small C11 program that controls an Edifier R2750DB MKII speaker over Bluetooth LE through BlueZ's D-Bus API. [README.md](README.md) explains use; [docs/PROTOCOL.md](docs/PROTOCOL.md) the wire protocol; [docs/HARDWARE.md](docs/HARDWARE.md) the device.

## Principles

KISS, DRY, YAGNI. Delete first: question the requirement, delete the part, simplify, then speed up. Pit of success: safe defaults, risky paths explicit and loud. Tests that can fail. Files around 150 lines (a target, not a hard limit). About one line of comment per ten lines of code, saying why rather than what.

## Rules

- **License:** Apache-2.0. No per-file headers; LICENSE and NOTICE cover the project.
- **Dependencies:** libc and libdbus only. No GLib, no libsystemd, no libbluetooth. POSIX 2008 interfaces only, so the build works on glibc and musl.
- **Warnings are errors** with the flags in the Makefile, on gcc and clang. Do not weaken them; fix the code.
- **Pure and impure stay apart.** `frame.c`, `model.c` and `decode.c` do no I/O and are what the tests link. D-Bus and sockets live in `dbus.c` and `bluez.c`; `commands.c` ties them together.
- **Tests are hermetic:** no Bluetooth, no D-Bus, no network, no files written. Every behaviour change comes with a test, including a negative control where one makes sense (a frame with a bad checksum is rejected; a destructive opcode is refused without `-f`; a firmware opcode is refused even with it).
- **The send gate is not negotiable** ([ADR 0003](docs/adr/0003-send-gate-volume-rule-and-quiet-logging.md)): firmware-update and bulk-transfer opcodes are never sent; destructive opcodes only with `-f`; anything else only if it is in the model's table. No firmware-update code, no opcode sweeps, no fuzzing against a device.
- **Volume:** every change starts from a fresh read and is clamped to the reported maximum.
- **Privacy:** payloads of frames the speaker sends unasked are printed only with `-v`; they can carry what the user is playing. Never commit captures, frame logs or device addresses. Example addresses use `AA:BB:CC:DD:EE:xx`.
- **Facts, not claims.** A command is "verified" only after it worked on the speaker; until then the docs tag it as derived from the app. Hardware facts carry a public source. Behaviour of other Edifier models is not evidence for this one.
- **Never in this repository:** Edifier app code (decompiled or otherwise), keys, encrypted or decrypted catalog data, or anything about unannounced products. A commit that adds one of these cannot be fixed by a later commit; the history would have to be rebuilt.
- **Machine channel:** results on standard output, progress and errors on standard error. Exit codes 0 / 1 / 2 as documented in the README.

## Governing law and ADRs

This file and the ADRs in `docs/adr/` govern the project. A significant change of behaviour, interface, safety or dependencies adds or amends an ADR in the same commit (template: `docs/adr/0000-template.md`). When code and documents disagree, fix whichever is wrong in the same commit.

| ADR | Decision |
|---|---|
| [0001](docs/adr/0001-name-and-model-scope.md) | Name `mcxEdifierControl` / `edifierctl`; one model table, no `--model` flag yet |
| [0002](docs/adr/0002-c-over-bluez-dbus-one-shot-cli.md) | C11 over BlueZ D-Bus with AcquireNotify/AcquireWrite; one command per run, no daemon |
| [0003](docs/adr/0003-send-gate-volume-rule-and-quiet-logging.md) | Send gate, volume rule, payload-free logging |

## Before committing

`make && make test` with gcc and clang, or let CI do it. Commit to `main`; one coherent change per commit, with a message that says what changed and why.
