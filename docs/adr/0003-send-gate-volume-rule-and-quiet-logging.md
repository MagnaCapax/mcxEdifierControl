# ADR 0003: Send gate, volume rule and payload-free logging

Date: 2026-11-18
In force since: this change

## Status
Proposed

## Context
The official app's protocol includes opcodes that reset, power off, unpair or rename the speaker, and opcodes that start a firmware update or bulk data transfer. None of the command layouts is verified on this hardware yet. Some unsolicited speaker frames carry the title and artist of whatever the user is playing.

## Options considered
- Send whatever the user types (the prototype's raw mode): maximum flexibility, but one typo can reset the speaker.
- Gate by a list of known commands, with a force flag for destructive opcodes and a hard refusal for firmware and bulk opcodes.
- Leave out raw sending entirely: safest, but every set command without its own subcommand becomes unreachable.

## Decision
Every outgoing frame, including the ones built by subcommands, passes `ed_gate_check()`:
- firmware update `10 11 12` and bulk transfer `79-7B`, `88-8A` are never sent, with or without `-f`;
- `07 CE CD CF C7 AE AF B0 CA` are sent only with `-f`;
- any other opcode must be a query or set opcode in the model's table for that app code;
- the two lists ignore the app code, so they also hold under app `ED` or any other.

Volume changes, relative or absolute, start with a fresh `66` read and clamp the target to the maximum the speaker reports; a reported maximum of 0 refuses the change. The tool then reads `66` again and reports whether the speaker took the value.

Frames the speaker sends unasked are logged as app, opcode and length only. Their payload is printed only with `-v`.

There is no firmware-update code and no opcode sweep or fuzz mode.

## Consequences
- `raw` stays useful for set commands without a subcommand (EQ, sound, sub-out, timer) and cannot reach a gated opcode by accident.
- Adding a command to the table makes it sendable: a table entry on a gated opcode would be shadowed by the gate, and a test checks that no table entry sits on one.
- Pushed-only opcodes (`01`, `02`) are in the table for naming in logs but are not sendable.

## References
`src/model.c` (`never_ops`, `destructive_ops`, `ed_gate_check`, `ed_volume_target`), `src/commands.c` (`exchange`, `print_frame`), `tests/test_model.c`.
