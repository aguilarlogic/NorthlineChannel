# Northline Channel measurement: engine

Latency per configuration, Level Match accuracy, block-size determinism, CPU of the strip stages

Build: Oct 11 2026, AMD Ryzen 7 9800X3D 8-Core Processor
Strip-level behaviour of the engine (roadmap D1, D3, 7.1). Latency, Level Match accuracy and determinism numbers are checked against thresholds by `NorthlineTool test engine`.


## Latency per configuration (48 kHz)

Reported = what the plug-in tells the host after `updateLatency()`. Measured = position of the peak of a 0.25 impulse through the whole processor. They must be equal, and the module on/off state must not change them. Rows with all modules on use settings that are transparent (no filtering, no gain change), so the peak position is a pure delay. A row that applies real filtering would show the filters' own group delay (for example the minimum-phase IIR half-band of the HQ oversampler, which PRE does not report yet).

| Configuration | Reported (samples) | Measured (samples) | Equal |
|---|---|---|---|
| default | 0 | 0 | yes |
| all modules on (transparent settings), limiter off | 0 | 0 | yes |
| all modules on (transparent settings), limiter on (Safety Clip) | 0 | 0 | yes |
| limiter off, Brickwall | 0 | 0 | yes |
| limiter on, Brickwall | 72 | 72 | yes |
| limiter on, True Peak | 72 | 72 | yes |
| quality HQ, all modules on (transparent settings) | 0 | 0 | yes |
| gate lookahead 2 ms, gate off | 0 | 0 | yes |
| gate lookahead 2 ms, gate on | 0 | 0 | yes |
| comp lookahead 5 ms, comp off | 0 | 0 | yes |
| comp lookahead 5 ms, comp on | 0 | 0 | yes |
| de-esser lookahead 2 ms, de-esser on | 0 | 0 | yes |
| test latency 11/37/64/0/100 on the five modules (alignment check) | 212 | 212 | yes |


## Level Match accuracy

Learn: pink noise at -20 dBFS RMS, 16 s. Truth = difference of BS.1770 integrated loudness (input minus chain output) over the blocks Learn used. Error must stay within 0.3 LU.

| Scenario | Learn time (s) | Learned trim (dB) | Measured (LU) | Error (LU) |
|---|---|---|---|---|
| low +6 / high -6 dB tilt | 3.3 | 0.53 | 0.53 | -0.00 |
| low -6 / high +6 dB tilt | 3.3 | -3.97 | -3.97 | 0.00 |
| compression (VCA, -24 dB, 3:1) | 3.7 | 5.72 | 5.78 | -0.06 |
| everything on | 3.3 | 7.06 | 7.09 | -0.04 |

Live: 1 kHz tone at -20 dBFS for 12 s, 15 s of gated noise, tone again for 3 s. EQ bell at 1 kHz as the chain. Re-entry error = peak of the first second after re-entry (without the first 50 ms, which are the EQ's own onset) minus the peak before the gap. Gap drift = noise level at 11-13 s into the gap minus 2-4 s into the gap. Old AGC: re-entry +3.9 dBFS, gated noise +24 dB.

| Chain | Gap noise (dBFS RMS) | Gain before gap (dB) | Re-entry error (dB) | Gap drift (dB) |
|---|---|---|---|---|
| transparent | -50 | 0.00 | 0.000 | 0.013 |
| EQ bell -6 dB | -50 | 6.00 | -0.003 | 0.017 |
| EQ bell -6 dB | -75 | 6.00 | -0.000 | 0.017 |
| EQ bell 6 dB | -50 | -6.00 | 0.002 | 0.008 |


## Determinism: output difference against block size 2048

3 s of noise, random automation of the continuous parameters of the listed part every 2048 samples (sample-exact, `renderWithAutomationExact`), gain-like parameters limited to +/-4 dB. Maximum absolute sample difference per host block size; the bound is 1e-6. Rows other than the strip gains show what the module internals contribute (the strip does not own them).

| Part | block 1 | block 7 | block 32 | block 37 | block 64 | block 511 | block 512 |
|---|---|---|---|---|---|---|---|
| strip gains + Level Match Live (modules off) | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| PRE | 3.18e-01 | 2.18e-01 | 1.86e-01 | 1.63e-01 | 1.86e-01 | 1.82e-01 | 1.82e-01 |
| GATE | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| DE-ESS | 1.24e-01 | 8.09e-02 | 0 | 2.87e-02 | 0 | 2.01e-03 | 0 |
| EQ | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| COMP | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| limiter (Safety Clip) | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| everything on (all continuous parameters) | 9.31e-03 | 7.37e-03 | 2.76e-03 | 4.55e-03 | 2.76e-03 | 2.67e-03 | 2.67e-03 |


## CPU cost of the strip stages (48 kHz, 64-sample blocks, stereo)

Processing time of `processBlock` with every module off unless stated, mean and 99th percentile per block. Single run on a shared machine: noisy, use differences between rows only as an order of magnitude.

| Configuration | Mean (us/block) | p99 (us/block) | Real-time CPU (%) |
|---|---|---|---|
| modules off, nothing engaged (unity path) | 0.27 | 0.30 | 0.020 |
| polarity + swap + input/output gain | 0.35 | 0.40 | 0.026 |
| global mix 50 % | 0.34 | 0.40 | 0.025 |
| Level Match Live (K-weighted measurement) | 1.09 | 1.20 | 0.082 |
| Level Match Learn, learned (measurement stopped) | 1.25 | 0.40 | 0.093 |
| monitor Side | 0.34 | 0.40 | 0.026 |
| latency alignment (five modules with 20-100 samples, all off) | 0.59 | 1.60 | 0.044 |
| everything on (Eco, zero latency) | 12.91 | 47.50 | 0.968 |
| everything on + Live match + mix 80 % | 13.98 | 38.10 | 1.049 |

