# Competitor and reference CPU measurements

Measured with `NorthlineTool host-bench` (any installed VST3, same harness as our own strip) and
`NorthlineTool bench` (our strip, in-process). Stereo, 10 s per run, default state. Other builds were
running on the same machine during the runs, so the numbers are noisy: read them as an order of
magnitude, and compare rows within one table only.

RT CPU = processing time / audio duration on one core. inst/core = how many instances one core could run.

## Installed competitor: CHANNEV (AnalogObsession)

Read from the plug-in itself: `C:\Program Files\Common Files\VST3\CHANNEV.vst3`, reported as
"CHANNEV (AnalogObsession)". Default state.

| Rate | Block | Mean/block | p99/block | RT CPU | inst/core |
|---|---|---|---|---|---|
| 48000 Hz | 32 | 14.70 us | 16.60 us | 2.205 % | 45.4 |
| 48000 Hz | 64 | 28.80 us | 31.50 us | 2.160 % | 46.3 |
| 48000 Hz | 128 | 57.38 us | 62.90 us | 2.152 % | 46.5 |
| 48000 Hz | 512 | 228.23 us | 242.50 us | 2.140 % | 46.7 |
| 96000 Hz | 64 | 28.60 us | 31.30 us | 4.290 % | 23.3 |

host-bench reported 16 samples of latency for this plug-in (printed after the 48 kHz, 64-sample run).

## Not measured

- Omni Channel 2: not installed on this machine.
- SSL Native Channel Strip 2: not installed on this machine.

## Northline Channel (`NorthlineTool bench`, same harness)

| Configuration | Rate | Block | Mean/block | p99/block | RT CPU | inst/core |
|---|---|---|---|---|---|---|
| default (transparent) | 48000 Hz | 64 | 0.39 us | 0.40 us | 0.029 % | 3435.4 |
| default (transparent) | 96000 Hz | 64 | 0.59 us | 0.40 us | 0.089 % | 1123.8 |
| preset: Vocal - Upfront | 48000 Hz | 64 | 8.04 us | 19.30 us | 0.603 % | 165.7 |
| preset: Vocal - Upfront | 48000 Hz | 512 | 62.35 us | 248.90 us | 0.585 % | 171.1 |
| everything on (Eco, zero latency) | 48000 Hz | 64 | 12.65 us | 16.20 us | 0.949 % | 105.4 |
| everything on (Eco, zero latency) | 96000 Hz | 64 | 13.46 us | 44.50 us | 2.019 % | 49.5 |
| everything on (HQ sat + lookahead) | 48000 Hz | 64 | 14.81 us | 41.80 us | 1.110 % | 90.1 |
| everything on (HQ sat + lookahead) | 96000 Hz | 64 | 16.49 us | 69.10 us | 2.473 % | 40.4 |

Per-module cost (us per 64-sample block, everything on, Eco): PRE 2.16, GATE 0.77, DS 3.55, EQ 2.04,
COMP 2.31, MASTER 0.33. With HQ saturation and lookahead: PRE 3.48, GATE 0.76, DS 3.37, EQ 2.00,
COMP 2.28, MASTER 0.45.

The strip-stage table with latency, Level Match, determinism and CPU per stage is in
[engine.md](engine.md).
