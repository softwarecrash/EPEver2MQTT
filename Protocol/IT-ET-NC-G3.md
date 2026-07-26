# EPEVER IT/ET-NC G3 register map

This map is based on the public device profiles shipped with EPEVER Solar
Guardian PC V2.3.18. It is cross-checked against EPEVER's IT-NC G3 and ET-NC
G3 product documentation and the public third-generation controller protocol.
The older `ControllerProtocolV2.3.pdf` map must not be used for these devices.

Sources:

- [EPEVER Solar Guardian PC](https://www.epever.com/download/solar-guardian-pc-software-windows/)
- [IT-NC G3 datasheet](https://www.epever.com/wp-content/uploads/2025/03/EPEVER-DataSheet-IT-NC-G3-Series.pdf)
- [ET-NC G3 datasheet](https://www.epever.com/wp-content/uploads/2025/03/EPEVER-DataSheet-ET-NC-G3-Series.pdf)
- [Third Generation Series Communication Protocol V1.00](https://cdck-file-uploads-europe1.s3.dualstack.eu-west-1.amazonaws.com/arduino/original/4X/f/8/9/f89535b09569a1306594bf817ebd679ee483ffe0.pdf)

Addresses are hexadecimal Modbus addresses, not `3xxxx`/`4xxxx` display
notation. Input registers use function `0x04`, holding registers use
`0x03`/`0x06`, and coils use `0x01`/`0x05`. A 32-bit `CDAB` value is sent low
word first. Unless noted otherwise, electrical values use scale `/100`.

## Identification and ratings

| Address | Words | Value |
| --- | ---: | --- |
| `3000` | 1 | Model id; `0..11` IT-NC G3, `12..23` ET-NC G3 |
| `3002` | 1 | Maximum PV open-circuit voltage |
| `3004` | 2 | Rated charging power, unsigned 32-bit CDAB |
| `3006` | 1 | Rated battery voltage |
| `3007` | 1 | Rated charging current |
| `300B` | 1 | Rated load current (IT only) |
| `300E` | 1 | DSP firmware version |
| `300F` | 1 | Number of PV inputs, 1 or 2 |
| `3011` | 1 | ARM firmware version |

The model ids list IT and then ET variants of `5215`, `6215`, `7215`, `10215`,
`5420`, `6415`, `6420`, `7415`, `7420`, `8420`, `10415`, and `10420`.

## Live data

| Address | Words | Value |
| --- | ---: | --- |
| `3100`, `3101`, `3102` | 1, 1, 2 | PV1 voltage, current, power |
| `3108`, `3109`, `310A` | 1, 1, 2 | PV2 voltage, current, power |
| `3110`, `3111`, `3112` | 1, 1, 2 | Load voltage, current, power (IT only) |
| `3114` | 1 | Battery voltage |
| `3117` | 1 | Battery current, signed |
| `3118` | 1 | Remote battery temperature, signed |
| `3119` | 1 | Battery SOC, raw integer percent (no `/100`) |
| `311A` | 1 | Device temperature, signed |
| `311D` | 1 | Current system/rated battery voltage |
| `311E` | 1 | Highest PV voltage |
| `311F` | 1 | Total PV charging current |
| `3120` | 2 | Total PV power, unsigned 32-bit CDAB |

## Status registers

| Address | Bits | Value |
| --- | --- | --- |
| `3200` | `0..3`, `4..7`, `15` | Battery voltage state, temperature state, lithium voltage-id error |
| `3201` | `0`, `11`, `12..13` | Load on, short circuit, overload (IT only) |
| `3202` | `1`, `2..3`, `5`, `7`, `14..15` | Day/night, charge state, device/charge over-temperature, PV1 input state |
| `3203` | `5`, `8`, `9`, `10..11`, `14..15` | MPPT active, remote charge enabled, low power, PV-mode alarm, PV2 input state |
| `3205` | `0` | BMS online |
| `3205` | `1` | Low SOC |
| `3205` | `2`, `4` | Discharge and over-discharge protection |
| `3205` | `3`, `10` | Charge and over-charge protection |
| `3205` | `5..9` | Sensor, temperature and cell-voltage faults |
| `3205` | `13`, `14` | Full SOC, DSP communication fault |

Charging states in `3202` are off, float, boost, and equalization.

## Statistics and BMS

| Address | Words | Value |
| --- | ---: | --- |
| `3301`, `3302` | 1 each | Maximum/minimum battery voltage today |
| `3303/05/07/09` | 2 each | Daily/monthly/annual/total consumption (IT only) |
| `330B/0D/0F/11` | 2 each | Daily/monthly/annual/total generation |
| `3400` | 1 | BMS cell count (IT profile) |
| `3401`, `3402` | 1 each | BMS pack voltage and signed current |
| `3405`, `3406`, `3407` | 1 each | Full capacity, remaining percent, remaining minutes |
| `3408`, `3409` | 1 each | Maximum/minimum cell temperature, signed |

Energy counters are unsigned 32-bit CDAB values in kWh with scale `/100`.

## Holding registers

| Address | Value |
| --- | --- |
| `9000` | Battery type: User, SLA, GEL, Flooded, LFP4S, LFP8S, LFP15S, LFP16S, LNCM3S, LNCM6S, LNCM7S, LNCM13S, LNCM14S |
| `9001`, `9002` | Battery capacity; temperature compensation (`/-100`) |
| `9006` | Rated voltage: auto, 12, 24, 36, 48 V |
| `9007..9012` | OVD, charge limit, OVR, equalize, boost, float, boost recovery, low-voltage recovery, undervoltage recovery/warning, LVD, discharge limit |
| `9013` | Charging-current limit, A, raw `/100` |
| `9014`, `9015` | Equalization and boost time, minutes |
| `9016` | Lithium protection: `3` enabled, `4` disabled |
| `9017`, `9018` | Low-temperature charge/discharge limits, signed |
| `9019..901B` | RTC: packed seconds/minutes, hours/day, month/year |
| `901C..901F` | Battery/device maximum, minimum, and recovery temperatures |
| `9022` | IT load mode |
| `9038` | Charging regulation mode: voltage or SOC |
| `9039..903E` | Full/recovery/discharge/low-power SOC thresholds |
| `903F` | Recording period, minutes |
| `9040`, `9041` | BMS protocol and enable |
| `9042` | PV input mode: independent or centralized |
| `9045`, `9046` | Modbus address and baud-rate code (`24`, `96`, `1152`) |
| `9047` | Parallel maximum charging current |
| `9049`, `904B`, `904C` | PV restart time, manual equalize, remote switch enable |

Do not copy the legacy 15-register settings block starting at `9000`: gaps and
new fields shift its interpretation.

## Coils

| Address | Value |
| --- | --- |
| `0003` | Load switch (IT only) |
| `0005`, `0006` | Enter load test / load test (IT only) |
| `0007` | Factory reset |
| `0008` | Clear accumulated energy |
| `0009` | Fault reset |
| `000A`, `000B` | Reset learned capacity / reset SOC |

## EPEver2MQTT behavior

The firmware probes `3000` and keeps Legacy, IT-NC G3, and ET-NC G3 paths
separate. This is safety relevant: the legacy RTC is at `9013`, while the NC G3
RTC is at `9019`; on NC G3, `9013` changes the charging-current limit.

Classic MQTT current-limit command:

```text
<base-topic>/EP_<device>/DeviceControl/CHARGING_CURRENT_LIMIT
```

The payload is amperes with up to two decimals. Zero, negative, malformed, and
above-rated values are rejected, and the write is verified by reading `9013`
back. In JSON mode use
`EP_<device>.DeviceControl.CHARGING_CURRENT_LIMIT`. Home Assistant discovery
creates a bounded `number` entity.

The web page **Settings → MPPT Settings** reads, edits, writes, and verifies
the 15 main battery parameters. On NC G3 it maps the logical values to
`9000..9002` plus `9007..9012`; it never writes the reserved/intervening
registers as a legacy contiguous block. Cloning is permitted between IT and ET
NC G3 devices, but blocked between Legacy and NC G3 layouts.
