# Edifier R2750DB MKII: BLE control protocol

What this tool knows about the speaker's Bluetooth LE control channel, and how sure it is of each part.

Independently written. Contains no Edifier app code, keys or catalog data; the protocol description comes from interoperability analysis and observation of the operator's own unit.

## How sure each part is

| Tag | Meaning |
|---|---|
| **unit** | Observed on one R2750DB MKII with BlueZ tools and a frame log. |
| **app** | Derived from static analysis of the official EDIFIER ConneX Android app, version 1.0.32. **Not verified on hardware.** |
| **inference** | Reasoned, not proven. |
| **unknown** | Not known. |

A command loses its **app** tag only when that command has been verified on the speaker. As of this writing, **no command has been verified**: the speaker has not yet replied to any query (see [observed behaviour](#observed-behaviour)).

## Advertising and GATT layout

| Item | Value | Tag |
|---|---|---|
| Advertised name | `EDIFIER BLE` | unit |
| Address | public; on the unit observed it differs from the classic Bluetooth address in one byte | unit (whether that holds for every unit: unknown) |
| Advertised 16-bit service UUID | `0x2C03` (`00002c03-0000-1000-8000-00805f9b34fb`) | unit |
| Manufacturer data | company ID `0x07E0`: 6 bytes of the classic Bluetooth address, then `02 00` | unit |
| The two tail bytes | protocol version (`02`) and payload encoding (`00` = plain); other encodings are out of scope | app |
| GAP | service `0x1800`, Device Name `0x2A00` readable and writable | unit |
| Control service | `48092c03-1a48-11e9-ab14-d663bd873d93` | unit |
| RX, speaker to host | `48090001-1a48-11e9-ab14-d663bd873d93`, read + notify, CCCD present | unit |
| TX, host to speaker | `48090002-1a48-11e9-ab14-d663bd873d93`, write + write without response | unit |

`edifierctl` finds the speaker by the advertised UUID or the control service UUID, or by the address given with `-a`.

## Frame format (protocol version 2)

```
host -> speaker   AA <app> <cmd> <len_hi> <len_lo> <payload...> <sum8>
speaker -> host   BB <app> <cmd> <len_hi> <len_lo> <payload...> <sum8>
sum8 = (sum of all preceding bytes) & 0xFF
app  = EC (default) or ED (the sub-out commands)
```

- **unit:** the format and checksum hold for every frame the speaker sent. Example, the A2DP-state push `BB EC C3 00 01 0D 78`: `BB+EC+C3+00+01+0D = 0x278`, low byte `0x78`. A 61-byte frame (length field `0x0037`) checks out the same way.
- **app:** `CC` is also accepted as a reply header.
- **app:** the app's first frame after connecting is `AA EC D8 00 00 6E` (capability query). There is no handshake or authentication before it. The app writes with write without response, requests an MTU of 300, the 2M PHY and high connection priority, and never sends `BB` frames (it does not acknowledge pushes).

A parser must accept `BB` and `CC`, check `sum8`, reassemble frames split across notifications (and split notifications carrying several frames), and log every frame it did not ask for, including unknown commands. `src/frame.c` does this. It rejects a frame with a wrong checksum and keeps searching from the next byte.

## Commands

Which of these the R2750DB MKII implements is **unknown**: the app learns it at runtime from the `D8` reply, whose layout is not described here. Every row is **app**-tagged and unverified. `edifierctl` prints the `D8` reply as raw hex.

| Name in `edifierctl` | Query → reply payload | Set → payload | Function |
|---|---|---|---|
| `volume` | `66` → `[max, current]` | `67` `[value]` | master volume |
| `input` | `61` → `[group, source]` | `62` `[group, source]` | input source (source values come from the `D8` reply) |
| `eco` | `B1` → `[0\|1]` | `B2` `[0\|1]` | ECO / power-saving mode |
| `autooff` | `D7` → `[1 = on]` | `D6` `[value]` | auto power-off without audio |
| `timer`, `timer-cancel` | `D3` → 16-bit big-endian | `D1` 16-bit, `D2` cancel | shutdown timer (unit unknown) |
| `ldac` | `48` → `[v]` | `49` `[v]` | LDAC setting |
| `multipoint` | `7C` → `[a, b]` or `[b]` | `7D` | multipoint |
| `eq` | `D5` → `[eqIndex, 5-6 more bytes]` | `C4`: write back only bytes read with `D5`, changing eqIndex | EQ preset |
| `sound` | `BB` → layout unknown | `BC` `[index, config]` | sound setting |
| `subout` | app `ED`: `13` → `[index, value]` | app `ED`: `14` `[index, value]` | sub-out |
| `firmware` | `C6` → bytes, shown joined with `.` | — | firmware version |
| `mac` | `C8` → 6 bytes | — | address |
| `name` | `C9` → UTF-8 | — | device name |
| `a2dp` | `C3` → `[state]`, `0D` = playing | — | A2DP state; also pushed (unit) |
| `caps` | `D8` → capability bits | — | supported features (raw hex) |
| — | `01`, `02` pushed | — | now-playing title and artist (pushed, unit; meaning: inference) |

### Destructive and forbidden opcodes

**app:** `07` factory reset, `CE` power off, `CD` A2DP disconnect, `CF` re-pair, `C7` clear pairing records, `AE`/`AF` connect/disconnect by address, `B0` delete log, `CA` rename. `edifierctl raw` sends these only with `-f`.

**Never sent, not even with `-f`:** `10 11 12` (firmware update) and `79-7B`, `88-8A` (bulk transfers). The gate applies under every app code ([ADR 0003](adr/0003-send-gate-volume-rule-and-quiet-logging.md)). Opcodes are never swept or fuzzed.

## Observed behaviour

On the unit, up to 2026-11-18 (all **unit**):

- Writes to TX are accepted at the ATT level, both write and write without response.
- Speaker frames have appeared only shortly after notifications were enabled, and only sometimes:
  - `BB EC C3 00 01 0D 78` (A2DP state, playing) after six of the subscriptions (four of them during a test tone or real playback; the setting of the other two was not recorded);
  - `02` then `01`, ASCII payloads carrying the artist and title of the track playing, once, during real playback. That these are forwarded AVRCP metadata is an inference.
  - no frame after the other subscriptions, including 15 of 19 probe rounds triggered by real playback and every session without audio. What decides whether a frame comes is unknown.
- **No reply to any host query so far:** about ten manual sessions (idle, silence, playing, after a power cycle), `C9` and `C6` sent right after a `C3` push, and 19 automatic rounds of `D8 66 61 C6 B1` and `ED 13` sent right after subscribing during playback.
- The earlier idea that the speaker answers only while it plays real audio does not hold: it pushes frames during playback and still ignores queries. The cause is unknown.

## BlueZ notes

(unit)

- A notification session and a discovery session last as long as the D-Bus client that started them, so the client must stay alive while listening.
- An LE connect took 10-26 s. Wait for `ServicesResolved` before writing.
- After a disconnect BlueZ drops the LE device object; scan again (filtering on `0x2C03`) before reconnecting.
- KDE's Add Device wizard does not list BLE devices. Use `edifierctl scan`.
- Other Edifier speakers are reported to accept one BLE connection at a time (the kralonur and j1kwen READMEs below). For this model that is not tested; `edifierctl` disconnects a link it made when it exits.

## Prior art

None of these covers the R2750DB MKII, and no code from them is used here. They share the UUID base `xxxxxxxx-1a48-11e9-ab14-d663bd873d93` or the frame family. Licences as GitHub reports them on 2026-11-18.

| Repository | Device | Licence |
|---|---|---|
| [tomcoolpxl/edifier-mr-volume](https://github.com/tomcoolpxl/edifier-mr-volume) | MR3, MR4.5, MR5 monitors | MIT |
| [pauljones0/qd35-mcp](https://github.com/pauljones0/qd35-mcp) | QD35 | MIT |
| [kralonur/edifier-speakers-ble](https://github.com/kralonur/edifier-speakers-ble) | M60, M90 (Home Assistant) | MIT |
| [terezos/d32-remote](https://github.com/terezos/d32-remote) | D32 | MIT |
| [MikeBirdTech/liberated-edifier-s880db-mkii](https://github.com/MikeBirdTech/liberated-edifier-s880db-mkii) | S880DB MKII | MIT |
| [j1kwen/hass-edifier-ble-component](https://github.com/j1kwen/hass-edifier-ble-component) | N300 (Home Assistant) | MIT |
| [Devis99/omarchy-edifier-mr5](https://github.com/Devis99/omarchy-edifier-mr5) | MR5 | MIT |
| [mqrious/edifier-mp85-battery](https://github.com/mqrious/edifier-mp85-battery) | MP85 | none stated |
| [wh201906/mEDIFIER](https://github.com/wh201906/mEDIFIER) | W820NB, W200BT Plus headphones (older framing) | MIT |
| [azazo1/EdifierCtrl](https://github.com/azazo1/EdifierCtrl) | headphones | MIT |
| [estevao-tarifa/edifier-macos](https://github.com/estevao-tarifa/edifier-macos) | W820NB (macOS) | MIT |
| [HappyAny/edifier-manager](https://github.com/HappyAny/edifier-manager) | W820NB family, W830NB, Lolli ANC | GPL-3.0 |
| [Basil-AS/DeviceDeck](https://github.com/Basil-AS/DeviceDeck), [Edifier app analysis](https://github.com/Basil-AS/DeviceDeck/blob/main/research/edifier/EDIFIER_ANDROID_REVERSE_ENGINEERING.md) | W820NB Plus, multi-vendor | not recognised by GitHub |
| [jahn2007/EdifierXCorolos](https://github.com/jahn2007/EdifierXCorolos), [official app analysis](https://github.com/jahn2007/EdifierXCorolos/blob/main/docs/OFFICIAL_APK_ANALYSIS.md) | Evo Pro earbuds | GPL-3.0 |
