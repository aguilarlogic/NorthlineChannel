# Northline Channel measurement: CPU

Build: Oct 24 2026, AMD Ryzen 7 9800X3D 8-Core Processor, Windows 11, MSVC Release. Generated with
`NorthlineTool bench` on a quiet machine (nothing else building or running). The CPU figures are
thread cycle time, so other processes barely affect them; the wall p99 column shows scheduling spikes.

## Against the budgets (roadmap section 15)

Stereo, 64-sample blocks, editor closed, median of 5 runs.

| Configuration | Before wave 3 | Now | Budget | Met |
|---|---|---|---|---|
| Default (transparent), 48 kHz | 0.163 % | 0.126 % | ≤ 0.02 % | no |
| Vocal preset (Vocal - Upfront), 48 kHz | 0.819 % | 0.719 % | ≤ 0.40 % | no |
| Everything on, Eco, 48 kHz | 1.179 % | 1.081 % | ≤ 0.60 % | no |
| Everything on, HQ (4×), 48 kHz | 2.822 % | 1.891 % | ≤ 1.5 % | no |
| Everything on, Eco, 96 kHz | 2.272 % | 2.118 % | ≤ 1.2 % | no |
| p99 / median block time (wall) | — | 1.05× to 1.13× | ≤ 2.0× | yes |

Where the time goes (editor open, µs per 64-sample block at 48 kHz, wall time inside the plug-in):

| Configuration | PRE | GATE | DE-ESS | EQ | COMP | MASTER |
|---|---|---|---|---|---|---|
| Default | 0.26 | 0 | 0 | 0.26 | 0 | 1.02 |
| Vocal - Upfront | 2.66 | 0 | 2.79 | 0.83 | 2.46 | 1.05 |
| Everything on, Eco | 3.70 | 1.41 | 4.12 | 1.01 | 3.07 | 1.39 |
| Everything on, HQ | 14.35 | 1.40 | 4.12 | 1.01 | 3.06 | 1.44 |

Notes:

- **Default.** The default state has PRE and EQ switched on with neutral settings, and MASTER holds the limiter and
  the processor-side loudness meter (integrated loudness, LRA and true peak must keep running while audio plays).
  MASTER alone is about 1 µs, four times the 0.02 % budget (0.27 µs). The budget predates processor-side metering.
- **HQ.** PRE at 4× with second-order ADAA is the largest single cost. Wave 4 cut it from 24 µs to 14 µs without
  changing the output (null at −112 dB or lower). The rest needs either single-precision ADAA (an output change at
  about −75 to −90 dB) or work in DE-ESS, COMP, GATE and EQ.
- **Not measured here:** the UI thread at 60 fps, editor open time and memory, and the comparison with Omni Channel 2
  in the same host (see [competitors.md](competitors.md)).

## Raw output

```
Northline Channel CPU benchmark (in-process, stereo, 5 runs of 2.0 s per row)
CPU = thread cycle time (QueryThreadCycleTime) at 4.695 GHz, so other processes do not count. wall = wall clock.
CPU % = CPU time / audio time on one core. min and median are over the runs. inst/core = instances one core could run (median).

Editor closed (nothing reads the meters or scopes):
  configuration                           rate block    CPU us med   CPU us min    CPU min    CPU med   wall med     wall p99  inst/core
  default (transparent)               48000 Hz    32       1.09 us      1.08 us    0.162 %    0.164 %    0.161 %      1.20 us    610.0
  default (transparent)               48000 Hz    64       1.69 us      1.68 us    0.126 %    0.127 %    0.126 %      1.80 us    787.3
    reported latency: 0 samples
  default (transparent)               48000 Hz   128       2.91 us      2.90 us    0.109 %    0.109 %    0.109 %      3.10 us    914.8
  default (transparent)               48000 Hz   512      10.29 us     10.24 us    0.096 %    0.096 %    0.096 %     10.60 us   1036.8
  default (transparent)               96000 Hz    64       1.60 us      1.58 us    0.237 %    0.240 %    0.237 %      1.80 us    417.1
  preset: Vocal - Upfront             48000 Hz    32       5.26 us      5.22 us    0.783 %    0.789 %    0.786 %      5.70 us    126.8
  preset: Vocal - Upfront             48000 Hz    64       9.60 us      9.59 us    0.719 %    0.720 %    0.718 %     10.50 us    138.9
    reported latency: 0 samples
  preset: Vocal - Upfront             48000 Hz   128      18.25 us     18.20 us    0.683 %    0.684 %    0.683 %     19.60 us    146.1
  preset: Vocal - Upfront             48000 Hz   512      70.12 us     70.02 us    0.656 %    0.657 %    0.657 %     74.80 us    152.1
  preset: Vocal - Upfront             96000 Hz    64       9.45 us      9.44 us    1.416 %    1.417 %    1.415 %     10.00 us     70.6
  everything on (Eco, zero latency)   48000 Hz    32       7.87 us      7.80 us    1.170 %    1.181 %    1.186 %      9.10 us     84.7
  everything on (Eco, zero latency)   48000 Hz    64      14.45 us     14.42 us    1.081 %    1.084 %    1.083 %     16.20 us     92.3
    reported latency: 0 samples
  everything on (Eco, zero latency)   48000 Hz   128      27.65 us     27.63 us    1.036 %    1.037 %    1.036 %     29.80 us     96.4
  everything on (Eco, zero latency)   48000 Hz   512     107.41 us    106.75 us    1.001 %    1.007 %    1.009 %    120.80 us     99.3
  everything on (Eco, zero latency)   96000 Hz    64      14.15 us     14.12 us    2.118 %    2.123 %    2.121 %     15.70 us     47.1
  everything on (HQ sat + lookahead)  48000 Hz    32      13.04 us     13.02 us    1.954 %    1.957 %    1.953 %     14.10 us     51.1
  everything on (HQ sat + lookahead)  48000 Hz    64      25.26 us     25.22 us    1.891 %    1.894 %    1.892 %     27.50 us     52.8
    reported latency: 120 samples
  everything on (HQ sat + lookahead)  48000 Hz   128      49.11 us     49.04 us    1.839 %    1.842 %    1.841 %     51.80 us     54.3
  everything on (HQ sat + lookahead)  48000 Hz   512     194.34 us    193.77 us    1.817 %    1.822 %    1.823 %    207.70 us     54.9
  everything on (HQ sat + lookahead)  96000 Hz    64      24.98 us     24.80 us    3.720 %    3.746 %    3.747 %     29.90 us     26.7

Editor open (meters and scopes read at 30 Hz), 48 kHz, 64 samples:
  configuration                           rate block    CPU us med   CPU us min    CPU min    CPU med   wall med     wall p99  inst/core
  default (transparent)               48000 Hz    64       2.19 us      2.14 us    0.160 %    0.165 %    0.159 %      2.30 us    607.8
    per-module (us/block, wall inside the plug-in): PRE 0.26  GATE 0.00  DS 0.00  EQ 0.26  COMP 0.00  MASTER 1.02
  preset: Vocal - Upfront             48000 Hz    64      10.23 us     10.13 us    0.760 %    0.767 %    0.759 %     10.70 us    130.4
    per-module (us/block, wall inside the plug-in): PRE 2.66  GATE 0.00  DS 2.79  EQ 0.83  COMP 2.46  MASTER 1.05
  everything on (Eco, zero latency)   48000 Hz    64      15.13 us     15.10 us    1.133 %    1.134 %    1.128 %     16.80 us     88.2
    per-module (us/block, wall inside the plug-in): PRE 3.70  GATE 1.41  DS 4.12  EQ 1.01  COMP 3.07  MASTER 1.39
  everything on (HQ sat + lookahead)  48000 Hz    64      25.84 us     25.75 us    1.931 %    1.938 %    1.932 %     28.20 us     51.6
    per-module (us/block, wall inside the plug-in): PRE 14.35  GATE 1.40  DS 4.12  EQ 1.01  COMP 3.06  MASTER 1.44
```
