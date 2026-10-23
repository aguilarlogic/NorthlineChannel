# Northline Channel measurement: master

Limiter modes (latency, true peak, THD, release), EBU 3341/3342 conformance table, MeterHub CPU

Build: Oct 23 2026, AMD Ryzen 7 9800X3D 8-Core Processor

## Limiter latency

Reported latency in samples. It depends on the mode and the quality switch only, never on LIMIT on/off (verified in `master_latencyDependsOnModeAndQualityOnly`).

| Mode | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| Safety Clip Eco | 0 | 0 | 0 | 0 |
| Safety Clip HQ | 60 | 60 | 60 | 60 |
| Brickwall | 66 | 72 | 144 | 288 |
| True Peak | 66 | 72 | 144 | 288 |


## Output peak with the input driven 10 dB over the ceiling (-1 dB)

Three levels of peak measurement on the whole output: sample peak, the BS.1770-4 4x meter (the definition of dBTP, what `MeterHub` reads) and an independent 16x reference with 64 taps per phase (stricter). True Peak mode keeps 0.1 dB of margin, so the 4x meter reads at most ceiling - 0.1 dB.

| Mode | Signal | Sample peak (dBFS) | 4x meter (dBTP) | 16x reference (dBTP) | Deepest GR (dB) | Overs |
|---|---|---|---|---|---|---|
| Safety Clip Eco @ 48.0 kHz | pink | -1.00 | 0.08 | 0.60 | -10.0 | 0 |
| Safety Clip Eco @ 48.0 kHz | drums (band-limited) | -1.00 | 0.40 | 0.98 | -45.1 | 0 |
| Safety Clip Eco @ 48.0 kHz | sine sweep 20 Hz - 0.45 fs | -1.00 | 1.26 | 1.52 | -107.0 | 0 |
| Safety Clip HQ @ 48.0 kHz | pink | -1.03 | -0.07 | 0.04 | -10.2 | 0 |
| Safety Clip HQ @ 48.0 kHz | drums (band-limited) | -1.00 | 1.89 | 1.81 | -10.0 | 0 |
| Safety Clip HQ @ 48.0 kHz | sine sweep 20 Hz - 0.45 fs | -1.00 | 1.04 | 1.07 | -11.0 | 0 |
| Brickwall @ 48.0 kHz | pink | -1.00 | -0.21 | -0.07 | -10.0 | 0 |
| Brickwall @ 48.0 kHz | drums (band-limited) | -1.00 | 1.75 | 1.88 | -10.0 | 0 |
| Brickwall @ 48.0 kHz | sine sweep 20 Hz - 0.45 fs | -1.00 | -0.99 | -0.99 | -10.0 | 0 |
| True Peak @ 48.0 kHz | pink | -1.10 | -1.10 | -0.93 | -10.3 | 0 |
| True Peak @ 48.0 kHz | drums (band-limited) | -1.10 | -1.10 | -1.03 | -10.3 | 0 |
| True Peak @ 48.0 kHz | sine sweep 20 Hz - 0.45 fs | -1.10 | -1.10 | -1.10 | -10.1 | 0 |
| Safety Clip Eco @ 96.0 kHz | pink | -1.00 | 0.08 | 0.61 | -10.0 | 0 |
| Safety Clip Eco @ 96.0 kHz | drums (band-limited) | -1.00 | 0.42 | 0.80 | -61.9 | 0 |
| Safety Clip Eco @ 96.0 kHz | sine sweep 20 Hz - 0.45 fs | -1.00 | 1.26 | 1.52 | -91.4 | 0 |
| Safety Clip HQ @ 96.0 kHz | pink | -1.02 | -0.05 | 0.04 | -10.2 | 0 |
| Safety Clip HQ @ 96.0 kHz | drums (band-limited) | -1.00 | 1.67 | 1.57 | -10.3 | 0 |
| Safety Clip HQ @ 96.0 kHz | sine sweep 20 Hz - 0.45 fs | -1.00 | 1.01 | 1.06 | -11.0 | 0 |
| Brickwall @ 96.0 kHz | pink | -1.00 | -0.21 | -0.07 | -10.0 | 0 |
| Brickwall @ 96.0 kHz | drums (band-limited) | -1.00 | 1.32 | 1.45 | -10.0 | 0 |
| Brickwall @ 96.0 kHz | sine sweep 20 Hz - 0.45 fs | -1.00 | -1.00 | -1.00 | -10.0 | 0 |
| True Peak @ 96.0 kHz | pink | -1.10 | -1.10 | -0.91 | -10.3 | 0 |
| True Peak @ 96.0 kHz | drums (band-limited) | -1.10 | -1.10 | -1.02 | -10.2 | 0 |
| True Peak @ 96.0 kHz | sine sweep 20 Hz - 0.45 fs | -1.10 | -1.10 | -1.10 | -10.1 | 0 |


## Inter-sample peaks: fs/4 sine at 45 degrees, true peak 2 dB over a -3 dB ceiling

The samples of this signal sit 1 dB under the ceiling while the waveform exceeds it by 2 dB.

| Mode | Output true peak, 16x reference (dBTP) | Verdict |
|---|---|---|
| Safety Clip Eco | -3.76 | within ceiling + 0.1 dB |
| Safety Clip HQ | -2.21 | over the ceiling (mode does not detect inter-sample peaks) |
| Brickwall | -1.00 | over the ceiling (mode does not detect inter-sample peaks) |
| True Peak | -3.11 | within ceiling + 0.1 dB |


## Distortion with the input 3 dB over the ceiling (-6 dB), steady sine

THD of harmonics 2 to 10 relative to the fundamental, measured over 100 ms after 0.9 s of settling at 48 kHz. Brickwall and True Peak hold a steady gain, so a sine is essentially undistorted; the Safety Clip is a waveshaper and distorts by design.

| Mode | 100 Hz | 1 kHz | 5 kHz |
|---|---|---|---|
| Safety Clip Eco | -18.0 dB | -17.8 dB | -15.6 dB |
| Safety Clip HQ | -17.4 dB | -17.4 dB | -17.5 dB |
| Brickwall | -68.2 dB | -152.1 dB | -167.0 dB |
| True Peak | -66.6 dB | -150.7 dB | -160.9 dB |


## Gain reduction and program-dependent release

Brickwall, ceiling -6 dB, release knob = base time. A burst 12 dB over the ceiling is followed by a quiet 1 kHz tone; the time for the tone to recover 63% of its gain is read from the output envelope. Short bursts use the fast stage (base/4), sustained limiting the slow stage (4 x base), blended by how long the reduction has lasted.

| Release (ms) | Burst 4 ms | Burst 50 ms | Burst 300 ms | Burst 1.5 s |
|---|---|---|---|---|
| 30 | 14 ms | 95 ms | 122 ms | 122 ms |
| 100 | 30 ms | 110 ms | 396 ms | 402 ms |
| 300 | 80 ms | 111 ms | 662 ms | 1194 ms |

| Ceiling | Sine at the ceiling, GR (dB) | Sine 6 dB over the ceiling, GR (dB) |
|---|---|---|
| -1 dB | 0.00 | -6.00 |
| -6 dB | 0.00 | -6.00 |
| -12 dB | 0.00 | -6.00 |


## Loudness conformance (MeterHub)

EBU Tech 3341 minimum-requirement signals (stereo cases generated here; cases 6 to 8 need 5.0-channel or authentic programme material and are not covered) and Tech 3342 LRA cases 1 to 4. Tolerance +-0.1 LU for loudness, +-1 LU for LRA (the measured value is also within 0.01 LU in these synthetic cases), true peak +0.2/-0.4 dB.

| Case | Expected | Measured | Tolerance | Result |
|---|---|---|---|---|
| 3341 #1 M @ 44.1 kHz | -23.0 LUFS | -22.99 LUFS | +0.1 / -0.1 | pass |
| 3341 #1 S @ 44.1 kHz | -23.0 LUFS | -22.99 LUFS | +0.1 / -0.1 | pass |
| 3341 #1 I @ 44.1 kHz | -23.0 LUFS | -22.99 LUFS | +0.1 / -0.1 | pass |
| 3341 #2 I @ 44.1 kHz | -33.0 LUFS | -32.99 LUFS | +0.1 / -0.1 | pass |
| 3341 #1 M @ 48.0 kHz | -23.0 LUFS | -22.99 LUFS | +0.1 / -0.1 | pass |
| 3341 #1 S @ 48.0 kHz | -23.0 LUFS | -22.99 LUFS | +0.1 / -0.1 | pass |
| 3341 #1 I @ 48.0 kHz | -23.0 LUFS | -22.99 LUFS | +0.1 / -0.1 | pass |
| 3341 #2 I @ 48.0 kHz | -33.0 LUFS | -32.99 LUFS | +0.1 / -0.1 | pass |
| 3341 #1 M @ 96.0 kHz | -23.0 LUFS | -23.01 LUFS | +0.1 / -0.1 | pass |
| 3341 #1 S @ 96.0 kHz | -23.0 LUFS | -23.01 LUFS | +0.1 / -0.1 | pass |
| 3341 #1 I @ 96.0 kHz | -23.0 LUFS | -23.01 LUFS | +0.1 / -0.1 | pass |
| 3341 #2 I @ 96.0 kHz | -33.0 LUFS | -33.01 LUFS | +0.1 / -0.1 | pass |
| 3341 #3 I (-36 / -23 / -36) | -23.0 LUFS | -23.01 LUFS | +0.1 / -0.1 | pass |
| 3341 #4 I (-72 / -36 / -23 / -36 / -72) | -23.0 LUFS | -23.01 LUFS | +0.1 / -0.1 | pass |
| 3341 #5 I (-26 / -20 / -26) | -23.0 LUFS | -22.98 LUFS | +0.1 / -0.1 | pass |
| 3341 #9 S min after 3 s | -23.0 LUFS | -22.99 LUFS | +0.1 / -0.1 | pass |
| 3341 #9 S max after 3 s | -23.0 LUFS | -22.99 LUFS | +0.1 / -0.1 | pass |
| 3341 #12 M min after 1 s | -23.0 LUFS | -22.96 LUFS | +0.1 / -0.1 | pass |
| 3341 #12 M max after 1 s | -23.0 LUFS | -22.96 LUFS | +0.1 / -0.1 | pass |
| 3341 #10 max S, worst of 20 segments | -23.0 LUFS (worst deviation shown) | -22.99 LUFS (worst deviation shown) | +0.1 / -0.1 | pass |
| 3341 #13 max M, worst of 20 segments | -23.0 LUFS (worst deviation shown) | -22.99 LUFS (worst deviation shown) | +0.1 / -0.1 | pass |
| 3341 #11 live max S, worst of 20 values | 0.0 LU (deviation) | 0.01 LU (deviation) | +0.1 / -0.1 | pass |
| 3341 #14 live max M, worst of 20 values | 0.0 LU (deviation) | 0.01 LU (deviation) | +0.1 / -0.1 | pass |
| 3341 #15 true peak @ 44.1 kHz | -6.0 dBTP | -6.02 dBTP | +0.2 / -0.4 | pass |
| 3341 #16 true peak @ 44.1 kHz | -6.0 dBTP | -6.02 dBTP | +0.2 / -0.4 | pass |
| 3341 #17 true peak @ 44.1 kHz | -6.0 dBTP | -6.02 dBTP | +0.2 / -0.4 | pass |
| 3341 #18 true peak @ 44.1 kHz | -6.0 dBTP | -6.02 dBTP | +0.2 / -0.4 | pass |
| 3341 #19 true peak @ 44.1 kHz | 3.0 dBTP | 2.99 dBTP | +0.2 / -0.4 | pass |
| 3341 #20 true peak @ 44.1 kHz | 0.0 dBTP | -0.26 dBTP | +0.2 / -0.4 | pass |
| 3341 #21 true peak @ 44.1 kHz | 0.0 dBTP | -0.10 dBTP | +0.2 / -0.4 | pass |
| 3341 #22 true peak @ 44.1 kHz | 0.0 dBTP | 0.00 dBTP | +0.2 / -0.4 | pass |
| 3341 #23 true peak @ 44.1 kHz | 0.0 dBTP | -0.15 dBTP | +0.2 / -0.4 | pass |
| 3341 #15 true peak @ 48.0 kHz | -6.0 dBTP | -6.02 dBTP | +0.2 / -0.4 | pass |
| 3341 #16 true peak @ 48.0 kHz | -6.0 dBTP | -6.02 dBTP | +0.2 / -0.4 | pass |
| 3341 #17 true peak @ 48.0 kHz | -6.0 dBTP | -6.02 dBTP | +0.2 / -0.4 | pass |
| 3341 #18 true peak @ 48.0 kHz | -6.0 dBTP | -6.02 dBTP | +0.2 / -0.4 | pass |
| 3341 #19 true peak @ 48.0 kHz | 3.0 dBTP | 2.99 dBTP | +0.2 / -0.4 | pass |
| 3341 #20 true peak @ 48.0 kHz | 0.0 dBTP | -0.31 dBTP | +0.2 / -0.4 | pass |
| 3341 #21 true peak @ 48.0 kHz | 0.0 dBTP | -0.18 dBTP | +0.2 / -0.4 | pass |
| 3341 #22 true peak @ 48.0 kHz | 0.0 dBTP | -0.04 dBTP | +0.2 / -0.4 | pass |
| 3341 #23 true peak @ 48.0 kHz | 0.0 dBTP | -0.16 dBTP | +0.2 / -0.4 | pass |
| 3342 #1 LRA (-20 / -30) | 10.0 LU | 10.00 LU | +1.0 / -1.0 | pass |
| 3342 #2 LRA (-20 / -15) | 5.0 LU | 5.00 LU | +1.0 / -1.0 | pass |
| 3342 #3 LRA (-40 / -20) | 20.0 LU | 20.00 LU | +1.0 / -1.0 | pass |
| 3342 #4 LRA (-50 / -35 / -20 / -35 / -50) | 15.0 LU | 15.00 LU | +1.0 / -1.0 | pass |


## Offline / realtime equivalence

| Block size | Integrated (LUFS) | LRA (LU) | True peak (dBTP) | Max momentary | Max short-term |
|---|---|---|---|---|---|
| 1 | -22.5172 | 10.000 | -0.870 | -17.5276 | -19.9933 |
| 64 | -22.5172 | 10.000 | -0.870 | -17.5276 | -19.9933 |
| 512 | -22.5172 | 10.000 | -0.870 | -17.5276 | -19.9933 |
| 4096 | -22.5172 | 10.000 | -0.870 | -17.5276 | -19.9933 |
| whole file | -22.5172 | 10.000 | -0.870 | -17.5276 | -19.9933 |


## CPU cost

Percentage of one core (best of several runs), stereo, 64-sample blocks, loud pink noise, flush-to-zero on. The MeterHub target is 0.05% at 48 kHz stereo.

| Configuration | 48 kHz | 96 kHz |
|---|---|---|
| MeterHub, stereo, peaks at -1 dBFS | 0.077 % | 0.140 % |
| MeterHub, stereo, peaks at -20 dBFS | 0.078 % | 0.141 % |
| MeterHub, mono, peaks at -1 dBFS | 0.049 % | 0.085 % |
| MasterSection (limiter + MeterHub), Safety Clip Eco, LIMIT on, +10 dB drive | 0.242 % | 0.459 % |
| MasterSection, Safety Clip Eco, LIMIT off | 0.090 % | 0.164 % |
| MasterSection (limiter + MeterHub), Safety Clip HQ, LIMIT on, +10 dB drive | 1.173 % | 2.261 % |
| MasterSection, Safety Clip HQ, LIMIT off | 0.089 % | 0.164 % |
| MasterSection (limiter + MeterHub), Brickwall, LIMIT on, +10 dB drive | 0.159 % | 0.286 % |
| MasterSection, Brickwall, LIMIT off | 0.100 % | 0.176 % |
| MasterSection (limiter + MeterHub), True Peak, LIMIT on, +10 dB drive | 0.454 % | 0.701 % |
| MasterSection, True Peak, LIMIT off | 0.091 % | 0.167 % |

