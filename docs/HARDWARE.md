# Edifier R2750DB MKII: hardware sheet

Model number EDF100102. Each row names its source. "Unit" means observed on one speaker with BlueZ tools; the other sources are public documents, linked below.

## Specifications

| Item | Value | Source |
|---|---|---|
| Output power (RMS) | 12 W + 12 W treble, 20 W + 20 W mid, 40 W + 40 W bass | [Quick Start Guide][qsg] §11 |
| Frequency response | 45 Hz – 40 kHz | [Quick Start Guide][qsg] §11 |
| Radio | 2.402 – 2.480 GHz, RF output ≤ 13 dBm EIRP | [Quick Start Guide][qsg] §11 |
| Power input | 100–240 V~ 50/60 Hz, 600 mA | [Quick Start Guide][qsg] |
| Inputs | Bluetooth, HDMI eARC (CEC), Line In, Optical, Coaxial | [Quick Start Guide][qsg] |
| Subwoofer output | rear SUB OUT port | [Edifier product page][product] |
| Bluetooth version | 6.0 | [Edifier product page][product] |
| Codecs | LDAC, SBC | [Edifier product page][product]; on the unit: SBC (bitpool 2–53) and LDAC (vendor `0x012D`, codec `0x00AA`) |
| Sample rates over Bluetooth | 44.1 and 48 kHz; no 96 kHz | unit |
| Multipoint | two devices; pause the first before the second streams | [Quick Start Guide][qsg] |
| Controls on the speaker | VOLUME/SOURCE knob (press: change source and Bluetooth actions; double-press: disconnect or pairing), BASS and TREBLE controls, rear power switch, display | [Quick Start Guide][qsg] |
| Other control paths | remote control (that it is infrared is an inference; the guide says "remote control"), HDMI CEC from a TV remote (standby and wake, mute, volume), the EDIFIER ConneX phone app | [Quick Start Guide][qsg] |
| Power saving (ECO), on by default | Bluetooth or HDMI with a source connected: power saving after 15 min without playback or operation, back in about 1 s when playback resumes. Bluetooth or HDMI with no source: off after 15 min. Line In, Optical, Coaxial: off after 15 min without playback and operation. Toggle: volume to 0, then hold ▶II on the remote for 2 s ("ECO OFF" / "ECO ON" on the display), or in the app | [Quick Start Guide][qsg] (the button is drawn as a glyph; ▶II read from the rendered page) |
| Bluetooth hidden mode | after 10 min with no device connected | [Quick Start Guide][qsg] |
| Absolute volume over Bluetooth | none: "Volume sync is not supported"; the AVRCP record offers no category 2 | [Quick Start Guide][qsg] §9 + unit |
| Firmware update | over the air through the phone app; no PC update path found | [Quick Start Guide][qsg]; PC path not found |
| Installed firmware version | unknown | — |

## Bluetooth chip

| Item | Value | Source |
|---|---|---|
| SoC vendor | JieLi (杰理): the chip carries the JieLi logo | [Giteki internal photos][photos] |
| Chip marking | `JL7034A6` (second line `BP2911GTLC`) | [Giteki internal photos][photos] |
| Module | Bluetooth board silkscreened `A-JL7034A-01 V0.2`, dated `20250620`, with a printed PCB antenna | [Giteki internal photos][photos] |
| Classic Bluetooth service name | `JL_A2DP` in the SDP record, consistent with a JieLi SDK | unit |
| Family, core, memory, Bluetooth feature set | not stated: no public JieLi datasheet for a part named JL7034 was found (search on 2026-11-18). Nothing is inferred from sibling JieLi parts. | — |

## Classic Bluetooth and BLE (unit)

- Classic SDP: exactly two records, `JL_A2DP` Audio Sink (A2DP 1.4) and AVRCP **controller** 1.6 with SupportedFeatures `0x0001` (category 1 only). No SPP or Device ID record.
- BLE: advertises as `EDIFIER BLE` with service UUID `0x2C03` and a separate public address. The control service and its characteristics are in [PROTOCOL.md](PROTOCOL.md).

## Why a host-side tool

With no absolute volume, the computer's volume slider only attenuates the digital signal before encoding (32 % on PipeWire is about −29.7 dB). The speaker's own master volume is reachable only by the knob, the remote, HDMI CEC or the phone app, and the ECO setting only by the remote or the app. `edifierctl` is meant to reach both from Linux over BLE, once the commands are verified on hardware.

## Regulatory

- Japan MIC certificate [220-J10573][giteki]: model EDF100102, applicant Edifier International Limited, certified 2025-11-20 (construction design certification under the mutual recognition agreement), 2402–2480 MHz at 10 mW. Its exhibits include [internal photos][photos] and external photos.
- EU: [declaration of conformity][doc] names model EDF100102, sales name R2750DB MKⅡ, under the Radio Equipment Directive (EN 300 328).
- US FCC: no filing found under EDF100102.

[qsg]: https://new-edifier-us-oss.edifier.com/files/20260204/5291685b1fdb24cb71197cbb736719ab.pdf
[product]: https://www.edifier.com/global/p/bookshelf-speakers/r2750db-mkii
[giteki]: https://fccid.io/giteki/220-J10573
[photos]: https://fccid.io/giteki/220-J10573/220-J10573_01_002
[doc]: https://new-edifier-us-oss.edifier.com/files/20260204/def4601cfe9bbb745b4d1c51990dd313.pdf
