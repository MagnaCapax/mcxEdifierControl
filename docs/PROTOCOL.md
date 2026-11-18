# Edifier R2750DB MKII: BLE control protocol

What this tool knows about the speaker's Bluetooth LE control channel, and how sure it is of each part.

Independently written. Contains no Edifier app code, keys or catalog data; the protocol description comes from interoperability analysis and observation of the operator's own unit.

## How sure each part is

| Tag | Meaning |
|---|---|
| **unit** | Observed on one R2750DB MKII (firmware 2.5.1) with BlueZ tools, a frame log or `edifierctl`. |
| **app** | Derived from static analysis of the official EDIFIER ConneX Android app, version 1.0.32. **Not verified on hardware.** |
| **inference** | Reasoned, not proven. |
| **unknown** | Not known. |

A command loses its **app** tag only when that command has been verified on the speaker. On 2026-11-18 the speaker answered 12 of the 15 queries and the volume set; every other set is still **app** only (see [observed behaviour](#observed-behaviour)).

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

The layouts come from the app (**app**). The last column is what the unit did with `edifierctl` on 2026-11-18, firmware 2.5.1, audio playing: **answered** means the speaker replied with that opcode and the reply had the size shown; **no reply** means it did not answer within the wait; **not tested** means the command was not sent. A set marked not tested is unverified. The app learns which features a model has from the `D8` reply, whose layout is not described here; `edifierctl` prints it as raw hex.

| Name in `edifierctl` | Query → reply payload | Set → payload | Function | On the unit |
|---|---|---|---|---|
| `volume` | `66` → `[max, current]` | `67` `[value]` | master volume | query answered (2 bytes, max 50); set **verified**: one step down and back, each read back |
| `input` | `61` → `[group, source]` | `62` `[group, source]` | input source (source values come from the `D8` reply) | query answered (2 bytes); set not tested |
| `eco` | `B1` → `[0\|1]` | `B2` `[0\|1]` | ECO / power-saving mode | query answered (1 byte); set not tested |
| `autooff` | `D7` → `[1 = on]` | `D6` `[value]` | auto power-off without audio | no reply; set not tested |
| `timer`, `timer-cancel` | `D3` → 16-bit big-endian | `D1` 16-bit, `D2` cancel | shutdown timer (unit unknown) | query answered (2 bytes); sets not tested |
| `ldac` | `48` → `[v]` | `49` `[v]` | LDAC setting | no reply; set not tested |
| `multipoint` | `7C` → `[a, b]` or `[b]` | `7D` | multipoint | query answered (2 bytes); set not tested |
| `eq` | `D5` → `[eqIndex, 5-6 more bytes]` | `C4`: write back only bytes read with `D5`, changing eqIndex | EQ preset | query answered with **1 byte**, not the length the app layout gives; set not tested |
| `sound` | `BB` → layout unknown | `BC` `[index, config]` | sound setting | query answered (2 bytes); set not tested |
| `subout` | app `ED`: `13` → `[index, value]` | app `ED`: `14` `[index, value]` | sub-out | no reply; set not tested |
| `firmware` | `C6` → bytes, shown joined with `.` | — | firmware version | answered (3 bytes: 2.5.1) |
| `mac` | `C8` → 6 bytes | — | address | answered (6 bytes, the classic Bluetooth address) |
| `name` | `C9` → UTF-8 | — | device name | answered (`EDIFIER R2750DB MKⅡ`) |
| `a2dp` | `C3` → `[state]`, `0D` = playing | — | A2DP state; also pushed | answered (1 byte, `0D` while playing); also pushed |
| `caps` | `D8` → capability bits | — | supported features (raw hex) | answered (24 bytes) |
| — | `01`, `02` pushed | — | now-playing title and artist | pushed (meaning: inference) |

### Destructive and forbidden opcodes

**app:** `07` factory reset, `CE` power off, `CD` A2DP disconnect, `CF` re-pair, `C7` clear pairing records, `AE`/`AF` connect/disconnect by address, `B0` delete log, `CA` rename. `edifierctl raw` sends these only with `-f`.

**Never sent, not even with `-f`:** `10 11 12` (firmware update) and `79-7B`, `88-8A` (bulk transfers). The gate applies under every app code ([ADR 0003](adr/0003-send-gate-volume-rule-and-quiet-logging.md)). Opcodes are never swept or fuzzed.

## Observed behaviour

On the unit (all **unit**).

**With `edifierctl` on 2026-11-18** (firmware 2.5.1, BlueZ 5.85, audio playing in every run):

- The speaker answered queries: the 12 marked answered above, within one connection for `info`. `D7`, `48` and `ED 13` got no reply.
- `volume -1` and then `volume 47` each read the volume, set it and read it back (47 → 46 → 47, max 50).
- After each volume set the speaker also pushed a `66` frame with the new volume, and a `C3` frame.
- During the `info` run it pushed `C3` many times and `02` / `01` (now playing) several times.
- Sending `D8` first did not stop later queries from being answered.
- Whether the speaker answers with audio idle is not tested.
- The `info` run and the volume set runs each ended with 5 bytes the parser dropped, and the `info` run also with one frame rejected for a bad checksum. Where these bytes come from is not known yet.

**Before that, with other tools** (BlueZ `bluetoothctl`-based, notifications through `StartNotify`, writes through `WriteValue`):

- Writes to TX were accepted at the ATT level, both write and write without response.
- Speaker frames appeared only shortly after notifications were enabled, and only sometimes:
  - `BB EC C3 00 01 0D 78` (A2DP state, playing) after six of the subscriptions (four of them during a test tone or real playback; the setting of the other two was not recorded);
  - `02` then `01`, ASCII payloads carrying the artist and title of the track playing, once, during real playback. That these are forwarded AVRCP metadata is an inference.
  - no frame after the other subscriptions, including 15 of 19 probe rounds triggered by real playback and every session without audio.
- No reply to any host query: about ten manual sessions (idle, silence, playing, after a power cycle), `C9` and `C6` sent right after a `C3` push, and 19 automatic rounds of `D8 66 61 C6 B1` and `ED 13` sent right after subscribing during playback.
- Why those sessions got no replies while `edifierctl` does is not known.

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
