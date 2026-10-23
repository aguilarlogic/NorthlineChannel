# Northline Channel measurement: eq

EQ: static band accuracy against the analog prototype (all types, fc, gain, Q, fs), old vs new design, dynamic bands, display vs DSP, CPU

Build: Oct 23 2026, AMD Ryzen 7 9800X3D 8-Core Processor

## 1. Static bands against the analog prototype

Magnitude error of the digital band against the analog RBJ prototype (same gain, Q and fc), over probes from 20 Hz to 0.9 Nyquist. Targets: shelves at all fc, and bells with fc <= 0.4 fs: 0.5 dB; bells with fc > 0.4 fs: 1.0 dB. Gains 36 values (1 dB steps), Q 6 values for the parametric bell, 25 centre frequencies, 20 Hz to 20 kHz.

| Type | fs (kHz) | Cases | Max, fc <= 0.4 fs (dB) | Max, fc > 0.4 fs (dB) | RMS, all (dB) | Worst case | Dense check of worst case (dB) | Target (dB) | Verdict |
|---|---|---|---|---|---|---|---|---|---|
| Low shelf (Q 0.71) | 44.1 | 424548 | 0.235 | 0.295 | 0.028 | 20000 Hz / -18 dB / Q 0.71 | 0.295 | 0.5 (all fc) | pass |
| Low resonant shelf (Q 1.5) | 44.1 | 427932 | 0.269 | 0.299 | 0.052 | 20000 Hz / -18 dB / Q 1.50 | 0.299 | 0.5 (all fc) | pass |
| High shelf (Q 0.71) | 44.1 | 424548 | 0.235 | 0.295 | 0.028 | 20000 Hz / 18 dB / Q 0.71 | 0.295 | 0.5 (all fc) | pass |
| High resonant shelf (Q 1.5) | 44.1 | 427932 | 0.269 | 0.299 | 0.052 | 20000 Hz / 18 dB / Q 1.50 | 0.299 | 0.5 (all fc) | pass |
| Bell wide (Mid, Q 0.55) | 44.1 | 423036 | 0.337 | 0.130 | 0.066 | 11246.8 Hz / 18 dB / Q 0.55 | 0.337 | 0.5 / 1.0 | pass |
| Bell narrow (Mid, Q 2.5) | 44.1 | 429120 | 0.424 | 0.327 | 0.052 | 14997.9 Hz / -18 dB / Q 2.50 | 0.424 | 0.5 / 1.0 | pass |
| Bell parametric (Q 0.2 to 10) | 44.1 | 2551572 | 0.448 | 0.397 | 0.055 | 14997.9 Hz / 18 dB / Q 2.00 | 0.448 | 0.5 / 1.0 | pass |
| Bell parametric, Proportional | 44.1 | 2562608 | 0.499 | 0.397 | 0.050 | 14997.9 Hz / -18 dB / Q 1.25 | 0.499 | 0.5 / 1.0 | pass |
| Low shelf (Q 0.71) | 48.0 | 425052 | 0.217 | 0.252 | 0.024 | 20000 Hz / -18 dB / Q 0.71 | 0.252 | 0.5 (all fc) | pass |
| Low resonant shelf (Q 1.5) | 48.0 | 428472 | 0.242 | 0.308 | 0.049 | 20000 Hz / 18 dB / Q 1.50 | 0.308 | 0.5 (all fc) | pass |
| High shelf (Q 0.71) | 48.0 | 425052 | 0.217 | 0.252 | 0.024 | 20000 Hz / 18 dB / Q 0.71 | 0.252 | 0.5 (all fc) | pass |
| High resonant shelf (Q 1.5) | 48.0 | 428472 | 0.242 | 0.308 | 0.049 | 20000 Hz / 18 dB / Q 1.50 | 0.308 | 0.5 (all fc) | pass |
| Bell wide (Mid, Q 0.55) | 48.0 | 423540 | 0.327 | 0.240 | 0.067 | 8433.93 Hz / 18 dB / Q 0.55 | 0.327 | 0.5 / 1.0 | pass |
| Bell narrow (Mid, Q 2.5) | 48.0 | 429696 | 0.350 | 0.513 | 0.057 | 20000 Hz / -18 dB / Q 2.50 | 0.516 | 0.5 / 1.0 | pass |
| Bell parametric (Q 0.2 to 10) | 48.0 | 2555388 | 0.406 | 0.437 | 0.056 | 20000 Hz / -18 dB / Q 5.00 | 0.437 | 0.5 / 1.0 | pass |
| Bell parametric, Proportional | 48.0 | 2567338 | 0.414 | 0.513 | 0.050 | 20000 Hz / -18 dB / Q 2.50 | 0.516 | 0.5 / 1.0 | pass |
| Low shelf (Q 0.71) | 96.0 | 427860 | 0.188 | 0.000 | 0.010 | 20000 Hz / 18 dB / Q 0.71 | 0.188 | 0.5 (all fc) | pass |
| Low resonant shelf (Q 1.5) | 96.0 | 430452 | 0.244 | 0.000 | 0.036 | 14997.9 Hz / 18 dB / Q 1.50 | 0.244 | 0.5 (all fc) | pass |
| High shelf (Q 0.71) | 96.0 | 427860 | 0.188 | 0.000 | 0.010 | 20000 Hz / -18 dB / Q 0.71 | 0.188 | 0.5 (all fc) | pass |
| High resonant shelf (Q 1.5) | 96.0 | 430452 | 0.244 | 0.000 | 0.036 | 14997.9 Hz / -18 dB / Q 1.50 | 0.244 | 0.5 (all fc) | pass |
| Bell wide (Mid, Q 0.55) | 96.0 | 426420 | 0.336 | 0.000 | 0.057 | 20000 Hz / 18 dB / Q 0.55 | 0.336 | 0.5 / 1.0 | pass |
| Bell narrow (Mid, Q 2.5) | 96.0 | 431028 | 0.222 | 0.000 | 0.020 | 14997.9 Hz / -18 dB / Q 2.50 | 0.222 | 0.5 / 1.0 | pass |
| Bell parametric (Q 0.2 to 10) | 96.0 | 2566980 | 0.348 | 0.000 | 0.041 | 20000 Hz / 18 dB / Q 1.00 | 0.349 | 0.5 / 1.0 | pass |
| Bell parametric, Proportional | 96.0 | 2576436 | 0.330 | 0.000 | 0.032 | 20000 Hz / -18 dB / Q 1.25 | 0.330 | 0.5 / 1.0 | pass |


### 1a. Maximum error by centre frequency (48 kHz, all gains and Q)

| fc (Hz) | Low shelf | Low res. shelf | High shelf | High res. shelf | Bell wide | Bell narrow | Bell param | Bell prop. |
|---|---|---|---|---|---|---|---|---|
| 20 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 |
| 26.6704 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.001 | 0.000 |
| 35.5656 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.001 | 0.000 |
| 47.4275 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.002 | 0.000 |
| 63.2456 | 0.000 | 0.000 | 0.000 | 0.000 | 0.001 | 0.000 | 0.004 | 0.001 |
| 84.3393 | 0.000 | 0.001 | 0.000 | 0.001 | 0.001 | 0.000 | 0.007 | 0.001 |
| 112.468 | 0.000 | 0.001 | 0.000 | 0.001 | 0.002 | 0.000 | 0.012 | 0.002 |
| 149.979 | 0.000 | 0.002 | 0.000 | 0.002 | 0.003 | 0.001 | 0.021 | 0.004 |
| 200 | 0.000 | 0.003 | 0.000 | 0.003 | 0.006 | 0.001 | 0.037 | 0.007 |
| 266.704 | 0.000 | 0.006 | 0.000 | 0.006 | 0.010 | 0.002 | 0.065 | 0.012 |
| 355.656 | 0.000 | 0.010 | 0.000 | 0.010 | 0.018 | 0.003 | 0.113 | 0.021 |
| 474.275 | 0.000 | 0.018 | 0.000 | 0.018 | 0.032 | 0.005 | 0.120 | 0.037 |
| 632.456 | 0.000 | 0.031 | 0.000 | 0.031 | 0.057 | 0.009 | 0.175 | 0.065 |
| 843.393 | 0.000 | 0.055 | 0.000 | 0.055 | 0.100 | 0.016 | 0.211 | 0.115 |
| 1124.68 | 0.000 | 0.098 | 0.000 | 0.098 | 0.114 | 0.029 | 0.222 | 0.117 |
| 1499.79 | 0.000 | 0.106 | 0.000 | 0.106 | 0.146 | 0.052 | 0.229 | 0.166 |
| 2000 | 0.001 | 0.108 | 0.001 | 0.108 | 0.213 | 0.092 | 0.268 | 0.217 |
| 2667.04 | 0.005 | 0.109 | 0.005 | 0.109 | 0.212 | 0.115 | 0.292 | 0.217 |
| 3556.56 | 0.015 | 0.164 | 0.015 | 0.164 | 0.221 | 0.102 | 0.289 | 0.237 |
| 4742.75 | 0.049 | 0.210 | 0.049 | 0.210 | 0.265 | 0.116 | 0.279 | 0.279 |
| 6324.56 | 0.108 | 0.208 | 0.108 | 0.208 | 0.304 | 0.165 | 0.313 | 0.313 |
| 8433.93 | 0.127 | 0.242 | 0.127 | 0.242 | 0.327 | 0.212 | 0.321 | 0.321 |
| 11246.8 | 0.210 | 0.207 | 0.210 | 0.207 | 0.322 | 0.235 | 0.356 | 0.357 |
| 14997.9 | 0.217 | 0.216 | 0.217 | 0.216 | 0.286 | 0.350 | 0.406 | 0.414 |
| 20000 | 0.252 | 0.308 | 0.252 | 0.308 | 0.240 | 0.513 | 0.437 | 0.513 |


### 1b. Previous bilinear design against the new one (Clean, same grid)

| Type | fs (kHz) | Old max, fc <= 0.4 fs | New max, fc <= 0.4 fs | Old max, fc > 0.4 fs | New max, fc > 0.4 fs | Old RMS | New RMS |
|---|---|---|---|---|---|---|---|
| Low shelf (Q 0.71) | 44.1 | 5.18 | 0.235 | 6.14 | 0.295 | 0.26 | 0.028 |
| Low resonant shelf (Q 1.5) | 44.1 | 7.56 | 0.269 | 10.01 | 0.299 | 0.39 | 0.052 |
| High shelf (Q 0.71) | 44.1 | 5.18 | 0.235 | 6.14 | 0.295 | 0.26 | 0.028 |
| High resonant shelf (Q 1.5) | 44.1 | 7.56 | 0.269 | 10.01 | 0.299 | 0.39 | 0.052 |
| Bell wide (Mid, Q 0.55) | 44.1 | 10.18 | 0.337 | 10.64 | 0.130 | 0.68 | 0.066 |
| Bell narrow (Mid, Q 2.5) | 44.1 | 7.34 | 0.424 | 12.01 | 0.327 | 0.55 | 0.052 |
| Bell parametric (Q 0.2 to 10) | 44.1 | 10.20 | 0.448 | 12.68 | 0.397 | 0.61 | 0.055 |
| Low shelf (Q 0.71) | 48.0 | 4.52 | 0.217 | 5.09 | 0.252 | 0.23 | 0.024 |
| Low resonant shelf (Q 1.5) | 48.0 | 6.63 | 0.242 | 9.70 | 0.308 | 0.37 | 0.049 |
| High shelf (Q 0.71) | 48.0 | 4.52 | 0.217 | 5.09 | 0.252 | 0.23 | 0.024 |
| High resonant shelf (Q 1.5) | 48.0 | 6.63 | 0.242 | 9.70 | 0.308 | 0.37 | 0.049 |
| Bell wide (Mid, Q 0.55) | 48.0 | 10.28 | 0.327 | 7.72 | 0.240 | 0.58 | 0.067 |
| Bell narrow (Mid, Q 2.5) | 48.0 | 6.09 | 0.350 | 11.65 | 0.513 | 0.49 | 0.057 |
| Bell parametric (Q 0.2 to 10) | 48.0 | 10.20 | 0.406 | 11.51 | 0.437 | 0.54 | 0.056 |


### 1c. Design cost (pure function, per call at control rate)

| Type | Designs | Mean (us) | Max (us) | Closed form (%) | 3-value refinement (%) | 5-value refinement (%) | Fallback (%) |
|---|---|---|---|---|---|---|---|
| Low shelf (Q 0.71) | 2700 | 4.3 | 153.5 | 90.7 | 6.2 | 3.1 | 0.00 |
| Low resonant shelf (Q 1.5) | 2700 | 10.3 | 187.7 | 77.4 | 13.1 | 9.5 | 0.00 |
| High shelf (Q 0.71) | 2700 | 4.4 | 154.3 | 90.7 | 6.2 | 3.1 | 0.00 |
| High resonant shelf (Q 1.5) | 2700 | 10.3 | 326.7 | 77.4 | 13.1 | 9.5 | 0.00 |
| Bell wide (Mid, Q 0.55) | 2700 | 15.6 | 272.9 | 71.1 | 14.9 | 14.0 | 0.00 |
| Bell narrow (Mid, Q 2.5) | 2700 | 8.0 | 149 | 84.7 | 9.3 | 6.0 | 0.00 |
| Bell parametric (Q 0.2 to 10) | 16200 | 10.7 | 530 | 79.1 | 11.8 | 9.1 | 0.00 |
| Bell parametric, Proportional | 16200 | 8.4 | 282 | 83.6 | 9.9 | 6.5 | 0.00 |

Stage 1 is the closed form; a stage above it means the closed form missed the tolerance on the solver grid and the refinement ran (only at parameter changes, never per tick when nothing moved).


## 2. Dynamic bands


### 2a. Gain law and steady state (48 kHz, 1 kHz tone, detection on the input, threshold -30 dBFS)

| Tone (dBFS) | Band level (dB) | Cut -12 dB: expected (dB) | Cut: measured (dB) | Boost +9 dB: expected (dB) | Boost: measured (dB) |
|---|---|---|---|---|---|
| -36 | -39.0 | -0.00 | -0.00 | 0.00 | -0.00 |
| -30 | -33.0 | -0.00 | -0.00 | 0.00 | 0.00 |
| -27 | -30.0 | -0.74 | -0.81 | 0.56 | 0.60 |
| -24 | -27.0 | -2.99 | -3.11 | 2.24 | 2.33 |
| -21 | -24.0 | -5.99 | -6.11 | 4.49 | 4.58 |
| -18 | -21.0 | -8.99 | -9.11 | 6.74 | 6.83 |
| -12 | -15.0 | -12.00 | -12.00 | 9.00 | 9.00 |
| -6 | -9.0 | -12.00 | -12.00 | 9.00 | 9.00 |
| 0 | -3.0 | -12.00 | -12.00 | 9.00 | 9.00 |

Worst |measured - expected| over these levels: 0.119 dB. Expected uses the gain law in docs/dsp/eq.md and the band level of a sine (mean square of the band-passed tone, -3.01 dB at 0 dBFS).

Same detector sampled at control ticks after 0.6 s: mean -27.0119 dB, range -27.170 to -26.871 dB (2 kHz ripple of the mean-square follower). Mean target gain -2.9889 dB.

Detector level of a steady 1 kHz sine at -24 dBFS: -26.9945 dB (expected -27.0103 dB).

Detector band-pass gain at 1 kHz (48 kHz): Q 0.5: -0.0000 dB; Q 1.0: -0.0000 dB; Q 2.5: -0.0000 dB.


### 2b. Speed (48 kHz, 0 dBFS tone burst, range -12 dB, threshold -30 dBFS)

| Speed | Attack, nominal tau (ms) | Attack to 63 % (ms) | Release, nominal tau (ms) | Release to 1/e (ms) | Steady gain, 1 kHz at -24 dBFS (dB) |
|---|---|---|---|---|---|
| Fast | 2.0 | 2.0 | 50.0 | 60.0 | -3.11 |
| Medium | 8.0 | 8.0 | 150.0 | 160.0 | -3.11 |
| Slow | 30.0 | 30.0 | 500.0 | 510.0 | -3.11 |

Expected steady gain at -24 dBFS: -2.99 dB (range -12 dB, threshold -30 dBFS). The attack is within one control tick (0.67 ms) of the nominal tau: a 0 dBFS tone crosses the knee in well under a tick. The release includes the time for the detector to fall below the knee (about 10 ms for a 2 ms mean-square follower). The steady gain at a tone at the band frequency is biased by the detector's 2 kHz ripple, see docs/dsp/eq.md.


## 3. Display against the DSP with a dynamic gain (D13)

The Mid band is a dynamic bell at 1 kHz (static gain 0, range -12 dB, threshold -30 dBFS). An external 1 kHz key tone holds it at a steady dynamic gain, which is read back from the module. Probe tones at -20 dBFS are measured through the module and compared with `EqModule::magnitudeDb` given that dynamic gain. The key levels sample the dynamic gain between the 1 dB nodes of the gain-axis table, so interpolation between nodes is included.

| Key tone (dBFS) | Dynamic gain, 48 kHz (dB) | Worst display error, 48 kHz (dB) | Dynamic gain, 96 kHz (dB) | Worst display error, 96 kHz (dB) |
|---|---|---|---|---|
| -27 | -0.81 | 0.000 | -0.81 | 0.000 |
| -24 | -3.11 | 0.001 | -3.11 | 0.000 |
| -21 | -6.11 | 0.001 | -6.11 | 0.001 |
| -18 | -9.11 | 0.001 | -9.11 | 0.001 |
| -15 | -12.00 | 0.000 | -11.99 | 0.000 |
| -12 | -12.00 | 0.000 | -12.00 | 0.000 |

Probe detail at key -21 dBFS (dynamic gain about -6 dB):

| Probe (Hz) | DSP 48 kHz (dB) | Display 48 kHz (dB) | Error | DSP 96 kHz (dB) | Display 96 kHz (dB) | Error |
|---|---|---|---|---|---|---|
| 50 | -0.017 | -0.017 | 0.000 | -0.017 | -0.017 | 0.000 |
| 200 | -0.273 | -0.273 | 0.000 | -0.273 | -0.273 | 0.000 |
| 500 | -1.922 | -1.922 | 0.000 | -1.922 | -1.921 | 0.001 |
| 800 | -5.038 | -5.038 | 0.000 | -5.039 | -5.039 | 0.000 |
| 1000 | -6.109 | -6.109 | 0.001 | -6.113 | -6.113 | 0.000 |
| 1200 | -5.340 | -5.340 | 0.000 | -5.348 | -5.347 | 0.000 |
| 2000 | -1.912 | -1.911 | 0.000 | -1.919 | -1.919 | 0.000 |
| 5000 | -0.265 | -0.265 | 0.000 | -0.271 | -0.271 | 0.000 |
| 10000 | -0.060 | -0.060 | 0.000 | -0.065 | -0.065 | 0.000 |
| 16000 | -0.021 | -0.021 | 0.000 | -0.024 | -0.024 | 0.000 |
| 20000 | -0.014 | -0.014 | 0.000 | -0.015 | -0.015 | 0.000 |

Worst display error over all key levels, probes and both rates: 0.001 dB (target 0.1 dB, roadmap D13).


## 4. CPU

One EQ instance, stereo, 10 s of white noise at -20 dBFS RMS, 64-sample blocks with setParams every block, best of three runs, percent of one core. Lead's budget: four dynamic bands at 48 kHz <= 0.15 %.

| Configuration | fs | CPU (% of one core) | Budget |
|---|---|---|---|
| All four bands off | 48 kHz | 0.0050 | - |
| 4 static bands, steady | 48 kHz | 0.0606 | - |
| 4 static bands, gains gliding (6 dB every 1024 samples) | 48 kHz | 0.7253 | within 1 % (glide target) |
| 4 dynamic bands, steady detection | 48 kHz | 0.1359 | within 0.15 % |
| 4 dynamic bands, gains gliding | 48 kHz | 0.8037 | within 1 % (glide target) |
| All four bands off | 96 kHz | 0.0099 | - |
| 4 static bands, steady | 96 kHz | 0.1209 | - |
| 4 static bands, gains gliding (6 dB every 1024 samples) | 96 kHz | 1.4318 | over 1 % (glide target) |
| 4 dynamic bands, steady detection | 96 kHz | 0.2696 | over 0.15 % |
| 4 dynamic bands, gains gliding | 96 kHz | 1.5953 | over 1 % (glide target) |


### 4b. Worst tick: all four bands switch to refinement-needing settings in the same tick (48 kHz)

| Quantity | Value |
|---|---|
| Refinement designs per tick, maximum | 1 (budget 1) |
| Refinement designs in 2.5 s | 488 |
| Processing time per tick, maximum (us) | 156.6 |
| Processing time per tick, mean (us) | 3.43 |
| Tick budget at 48 kHz (32 samples, us) | 666.7 |

Time is the wall clock of one 32-sample process call including the design work of that tick. The maximum includes the first tick's start-up and the timer resolution.

Thresholds for the same numbers are asserted in tools/tests/TestEq.cpp. Method, the gain law, the speeds and the exclusion rule for high-frequency bells are in docs/dsp/eq.md.

