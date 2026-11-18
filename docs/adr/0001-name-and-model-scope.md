# ADR 0001: Name, and how far the code generalises across models

Date: 2026-11-18
In force since: this change

## Status
Proposed

## Context
The tool controls one speaker today, the Edifier R2750DB MKII. The frame format and the UUID base it uses are shared by other Edifier speakers and headphones (see [the prior-art list](../PROTOCOL.md#prior-art)), but this repository has command data for this one model only. The name has to describe the tool, fit the MagnaCapax repository names, and not claim more than the code does.

## Options considered
Repository names:
- `edifierControl`: descriptive and plain camelCase. In this organisation plain camelCase names are the platform products (nodeCore, nodeAgent, pulsedBox), while small open-source tools carry the `mcx` prefix (mcxSerp, mcxBMCView, mcxVirsilipas, mcxPikkumies). It also matches an unrelated GitHub repository of the same name (Edifier monitors, MIT) case-insensitively.
- `mcxEdifierControl`: the same descriptive name with the prefix of the small-tool repositories. No name collision found on GitHub.
- `mcxEdifier`: shortest, but it names the vendor rather than what the tool does, and a bare vendor name reads like an official product.
- Rejected early: a model-only name (`mcxR2750`, at odds with leaving room for more models), the generic `edifier-control` (four existing repositories use it), and a mythic name (not descriptive).

Model generalisation:
- G1: per-model data (UUIDs and command table) in one table in `src/model.c`, one entry, no model flag.
- G2: G1 plus a `--model` flag now. A flag that accepts one value only fixes a command-line contract before a second model exists.
- G3: choose the model from the advertisement. The advertised tail bytes are the protocol version and payload encoding, not a model identifier, so there is nothing to select on.

Binary name: `edifierctl` (the Unix `*ctl` form, as `bluetoothctl`) or the repository name.

## Decision
Repository `mcxEdifierControl`; binary `edifierctl`; G1.

The binary name differs from the repository name in the same way the mcxBMCView tools do. This README is the one place that maps the two, and no alias or symlink is shipped. A `--model` flag is added together with the second model's table, not before.

## Consequences
- Adding a model is one table in `src/model.c` plus a selector; the gate (ADR 0003) is protocol-wide and needs no change.
- The help text and README name the one model that is described, so the generic repository name does not suggest support for other devices.
- "Edifier" is used to say what the tool works with. The README states that the project is not affiliated with Edifier.

## References
`src/model.h`, `src/model.c`, `tests/test_model.c`, README "Name".
