# Northline Channel measurement: pre

PRE: saturation alias, staging, distortion, filters, Weight, switching, latency and CPU

Build: Oct 23 2026, AMD Ryzen 7 9800X3D 8-Core Processor

## Calibration

Building the gain-staging tables (Eco and HQ, all types, first use) took 183.691 ms.


## Aliasing (ROADMAP section 1)

Energy outside the true harmonics of a bin-exact sine, relative to total energy, in dB. Input: sine at -6 dBFS peak, 8192-point Blackman analysis after 0.25 s of warm-up. Targets: Eco <= -40 dB (Odd, Even, Heavy) and <= -30 dB (Crush); HQ <= -80 dB (Odd, Even) and <= -70 dB (Heavy, Crush).

| Type | Drive | Quality | 1k 44.1 | 1k 48 | 1k 96 | 5k 44.1 | 5k 48 | 5k 96 | 10k 44.1 | 10k 48 | 10k 96 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Odd | 0 % | Eco | -139.8 | -139.9 | -140.1 | -109.9 | -108.8 | -139.4 | -66.0 | -61.9 | -108.8 |
| Odd | 0 % | HQ | -105.8 | -107.4 | -112.2 | -95.5 | -93.0 | -94.5 | -111.5 | -110.0 | -92.9 |
| Odd | 50 % | Eco | -140.3 | -140.5 | -140.9 | -73.0 | -72.0 | -139.9 | -47.2 | -43.4 | -72.0 |
| Odd | 50 % | HQ | -110.6 | -111.2 | -121.1 | -121.1 | -119.3 | -109.2 | -118.1 | -121.2 | -119.3 |
| Odd | 100 % | Eco | -142.4 | -142.5 | -142.7 | -42.2 | -41.2 | -88.0 | -30.5 | -27.7 | -41.2 |
| Odd | 100 % | HQ | -129.5 | -130.2 | -134.2 | -131.8 | -130.8 | -133.9 | -114.6 | -120.1 | -130.8 |
| Even | 0 % | Eco | -139.6 | -140.0 | -140.2 | -109.9 | -108.8 | -139.4 | -66.0 | -61.9 | -108.8 |
| Even | 0 % | HQ | -105.8 | -107.4 | -112.2 | -95.5 | -93.0 | -94.5 | -111.5 | -110.0 | -92.9 |
| Even | 50 % | Eco | -140.4 | -140.6 | -140.9 | -68.6 | -67.5 | -136.7 | -44.7 | -41.0 | -67.5 |
| Even | 50 % | HQ | -108.0 | -113.0 | -123.4 | -124.9 | -123.3 | -106.4 | -116.8 | -120.0 | -123.2 |
| Even | 100 % | Eco | -133.3 | -133.6 | -134.4 | -42.5 | -41.0 | -67.2 | -29.6 | -27.0 | -41.0 |
| Even | 100 % | HQ | -130.2 | -128.3 | -127.7 | -129.1 | -127.5 | -127.5 | -113.5 | -120.0 | -129.3 |
| Heavy | 0 % | Eco | -138.0 | -138.4 | -138.2 | -137.9 | -137.8 | -138.7 | -76.6 | -72.4 | -137.9 |
| Heavy | 0 % | HQ | -104.5 | -107.2 | -112.2 | -95.5 | -92.9 | -94.4 | -111.6 | -109.4 | -92.9 |
| Heavy | 50 % | Eco | -138.5 | -138.9 | -138.6 | -138.4 | -138.5 | -139.2 | -53.3 | -49.1 | -138.4 |
| Heavy | 50 % | HQ | -106.0 | -112.8 | -121.2 | -125.4 | -123.6 | -106.8 | -122.8 | -125.6 | -123.6 |
| Heavy | 100 % | Eco | -81.3 | -85.0 | -99.3 | -44.8 | -43.6 | -69.9 | -25.2 | -22.7 | -43.6 |
| Heavy | 100 % | HQ | -140.8 | -140.7 | -139.7 | -125.9 | -134.0 | -140.3 | -103.5 | -103.3 | -133.8 |
| Crush | 0 % | Eco | -137.9 | -138.1 | -137.7 | -137.9 | -137.7 | -137.9 | -79.2 | -86.3 | -138.3 |
| Crush | 0 % | HQ | -108.1 | -114.7 | -115.6 | -105.4 | -103.5 | -102.9 | -120.7 | -119.5 | -103.9 |
| Crush | 50 % | Eco | -98.7 | -102.1 | -117.1 | -60.4 | -58.9 | -78.2 | -42.0 | -38.1 | -59.0 |
| Crush | 50 % | HQ | -112.9 | -112.4 | -125.6 | -129.8 | -129.8 | -115.2 | -114.9 | -116.9 | -130.1 |
| Crush | 100 % | Eco | -87.3 | -86.1 | -102.3 | -32.7 | -31.7 | -67.5 | -23.1 | -21.0 | -31.6 |
| Crush | 100 % | HQ | -134.0 | -132.4 | -133.5 | -122.4 | -121.4 | -135.5 | -101.4 | -104.8 | -121.3 |
| Tape | 0 % | Eco | -119.3 | -131.6 | -139.3 | -105.9 | -107.0 | -126.6 | -64.1 | -60.0 | -102.9 |
| Tape | 0 % | HQ | -107.8 | -110.0 | -115.2 | -99.6 | -97.3 | -101.9 | -115.6 | -115.8 | -100.4 |
| Tape | 50 % | Eco | -116.1 | -128.8 | -140.1 | -73.3 | -72.3 | -124.0 | -44.1 | -40.5 | -64.8 |
| Tape | 50 % | HQ | -113.2 | -111.3 | -126.8 | -125.7 | -124.0 | -108.7 | -115.1 | -119.3 | -126.8 |
| Tape | 100 % | Eco | -118.3 | -131.0 | -140.9 | -42.6 | -41.6 | -81.4 | -28.2 | -25.8 | -36.5 |
| Tape | 100 % | HQ | -132.8 | -131.9 | -136.5 | -131.0 | -130.0 | -136.7 | -115.0 | -124.1 | -126.7 |
| Tube | 0 % | Eco | -139.6 | -140.0 | -140.2 | -109.9 | -108.8 | -139.4 | -66.0 | -61.9 | -108.8 |
| Tube | 0 % | HQ | -105.8 | -107.4 | -112.2 | -95.5 | -93.0 | -94.5 | -111.5 | -110.0 | -92.9 |
| Tube | 50 % | Eco | -99.2 | -99.3 | -112.0 | -67.9 | -66.9 | -113.0 | -44.5 | -40.8 | -66.9 |
| Tube | 50 % | HQ | -98.9 | -99.9 | -114.0 | -112.9 | -112.8 | -105.7 | -114.9 | -116.6 | -117.9 |
| Tube | 100 % | Eco | -80.8 | -79.8 | -95.8 | -42.0 | -40.5 | -66.5 | -29.7 | -27.0 | -40.5 |
| Tube | 100 % | HQ | -81.1 | -83.2 | -95.3 | -94.8 | -94.9 | -95.3 | -100.9 | -100.7 | -101.2 |
| Transformer | 0 % | Eco | -138.5 | -138.5 | -138.4 | -109.9 | -108.8 | -138.9 | -66.0 | -61.9 | -108.8 |
| Transformer | 0 % | HQ | -104.9 | -107.2 | -112.1 | -95.5 | -92.9 | -94.6 | -111.6 | -109.9 | -92.9 |
| Transformer | 50 % | Eco | -139.0 | -138.9 | -138.8 | -73.0 | -72.0 | -139.6 | -47.2 | -43.4 | -72.0 |
| Transformer | 50 % | HQ | -111.5 | -110.4 | -120.9 | -121.1 | -119.3 | -109.1 | -118.2 | -121.1 | -119.1 |
| Transformer | 100 % | Eco | -140.8 | -141.1 | -140.7 | -42.2 | -41.2 | -88.0 | -30.5 | -27.7 | -41.2 |
| Transformer | 100 % | HQ | -129.7 | -130.2 | -133.8 | -131.9 | -130.8 | -133.7 | -115.0 | -119.6 | -130.8 |


## Gain staging at the Operating Level (-18 dBFS RMS, auto gain on)

Change in RMS level (output minus input, dB), worst case over sines at 100 Hz, 1 kHz, 5 kHz and pink noise. Target: within +-0.5 dB for every type and drive. Also the small-signal gain at 0 % drive (target 0 +- 0.1 dB).


### Eco (1x)

| Type | 0 % | 10 % | 20 % | 30 % | 40 % | 50 % | 60 % | 70 % | 80 % | 90 % | 100 % | Small signal, 0 % |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Odd | 0.06 | 0.07 | 0.07 | 0.08 | 0.08 | 0.09 | 0.11 | 0.14 | 0.17 | 0.22 | 0.28 | 0.07 |
| Even | 0.06 | 0.07 | 0.07 | 0.08 | 0.09 | 0.10 | 0.12 | 0.14 | 0.15 | 0.17 | 0.20 | 0.07 |
| Heavy | 0.06 | 0.06 | 0.06 | 0.07 | 0.07 | 0.08 | 0.09 | 0.12 | 0.16 | 0.23 | 0.36 | 0.06 |
| Crush | 0.06 | 0.06 | 0.06 | 0.06 | 0.07 | 0.07 | 0.10 | 0.14 | 0.20 | 0.29 | 0.39 | 0.06 |
| Tape | 0.04 | 0.04 | 0.06 | 0.07 | 0.09 | 0.11 | 0.14 | 0.18 | 0.23 | 0.29 | 0.38 | 0.04 |
| Tube | 0.06 | 0.07 | 0.07 | 0.08 | 0.09 | 0.11 | 0.13 | 0.16 | 0.19 | 0.21 | 0.23 | 0.07 |
| Transformer | 0.07 | 0.08 | 0.08 | 0.09 | 0.10 | 0.12 | 0.14 | 0.18 | 0.23 | 0.29 | 0.37 | 0.08 |


### HQ (4x)

| Type | 0 % | 10 % | 20 % | 30 % | 40 % | 50 % | 60 % | 70 % | 80 % | 90 % | 100 % | Small signal, 0 % |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Odd | 0.05 | 0.05 | 0.05 | 0.06 | 0.06 | 0.07 | 0.08 | 0.10 | 0.12 | 0.17 | 0.22 | 0.03 |
| Even | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.06 | 0.08 | 0.09 | 0.10 | 0.12 | 0.15 | 0.03 |
| Heavy | 0.05 | 0.05 | 0.05 | 0.05 | 0.05 | 0.06 | 0.07 | 0.08 | 0.11 | 0.18 | 0.30 | 0.02 |
| Crush | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.05 | 0.09 | 0.15 | 0.22 | 0.32 | 0.02 |
| Tape | 0.04 | 0.04 | 0.04 | 0.05 | 0.05 | 0.07 | 0.10 | 0.13 | 0.18 | 0.24 | 0.32 | 0.01 |
| Tube | 0.04 | 0.04 | 0.04 | 0.04 | 0.04 | 0.06 | 0.08 | 0.11 | 0.14 | 0.17 | 0.19 | 0.03 |
| Transformer | 0.05 | 0.05 | 0.05 | 0.05 | 0.06 | 0.07 | 0.09 | 0.13 | 0.17 | 0.23 | 0.31 | 0.03 |


## Harmonic distortion versus level (auto gain on, 1 kHz sine, 48 kHz, Eco)

| Type | Drive | -40 dBFS RMS | -30 dBFS RMS | -24 dBFS RMS | -18 dBFS RMS | -12 dBFS RMS | -6 dBFS RMS |
|---|---|---|---|---|---|---|---|
| Odd | 0 % | -116.1 | -96.1 | -84.1 | -72.1 | -60.1 | -48.2 |
| Odd | 100 % | -78.1 | -58.1 | -46.3 | -34.8 | -24.6 | -16.9 |
| Even | 0 % | -116.1 | -96.1 | -84.1 | -72.1 | -60.1 | -48.2 |
| Even | 100 % | -38.0 | -28.1 | -22.4 | -17.5 | -14.6 | -12.7 |
| Heavy | 0 % | -126.2 | -106.2 | -94.2 | -82.2 | -70.2 | -58.2 |
| Heavy | 100 % | -80.2 | -60.2 | -48.1 | -35.8 | -22.4 | -11.7 |
| Crush | 0 % | -159.5 | -156.7 | -159.6 | -158.9 | -158.6 | -157.9 |
| Crush | 100 % | -159.3 | -156.8 | -91.4 | -37.2 | -25.3 | -16.2 |
| Tape | 0 % | -118.6 | -99.4 | -87.6 | -75.7 | -63.8 | -51.9 |
| Tape | 100 % | -78.5 | -58.7 | -46.9 | -35.7 | -25.8 | -18.5 |
| Tube | 0 % | -116.1 | -96.1 | -84.1 | -72.1 | -60.1 | -48.2 |
| Tube | 100 % | -55.4 | -37.2 | -27.7 | -20.1 | -15.4 | -12.7 |
| Transformer | 0 % | -115.7 | -95.7 | -83.7 | -71.7 | -59.8 | -47.9 |
| Transformer | 100 % | -77.8 | -57.8 | -46.0 | -34.5 | -24.3 | -16.7 |

THD in dB (20 log of the harmonic amplitude ratio, harmonics 2 to 10).


## Filter response error (Butterworth prototype, cut-off 1 kHz, 48 kHz)

| Slope | Low-pass max |error| (dB), 20 Hz to 0.5 fc | High-pass max |error| (dB), 2 fc to 16 kHz |
|---|---|---|
| 6 dB/oct | 0.000 | 0.000 |
| 12 dB/oct | 0.000 | 0.000 |
| 18 dB/oct | 0.000 | 0.000 |
| 24 dB/oct | 0.000 | 0.000 |
| 36 dB/oct | 0.000 | 0.000 |
| 48 dB/oct | 0.000 | 0.000 |


## Weight (bump at the frequency, 48 kHz)

| Weight freq | Track HP | Amount | Measured gain at bump (dB) |
|---|---|---|---|
| 40 Hz | off | 2 dB | 2.00 |
| 40 Hz | off | 4 dB | 3.99 |
| 40 Hz | off | 6 dB | 5.98 |
| 72 Hz | off | 2 dB | 2.00 |
| 72 Hz | off | 4 dB | 3.99 |
| 72 Hz | off | 6 dB | 5.99 |
| 120 Hz | off | 2 dB | 2.00 |
| 120 Hz | off | 4 dB | 4.00 |
| 120 Hz | off | 6 dB | 6.00 |
| 114.3 Hz (HP 80, track) | on | 6 dB | 5.99 |


## Switching splatter (-60 dB target, sine carrier re 50 ms window, 48 kHz)

| Change | Quality | Splatter (dB) |
|---|---|---|
| Type Odd -> Tube, drive 0 % | Eco | -81.7 |
| Type Tape -> Transformer, drive 0 % | Eco | -84.8 |
| Auto gain off -> on, drive 100 %, -40 dBFS | Eco | -80.0 |
| HP slope 12 -> 24 dB/oct | Eco | -92.4 |
| HP slope 24 -> 48 dB/oct | Eco | -92.4 |
| Weight off -> on (+6 dB) | Eco | -92.4 |
| Weight +6 -> +2 dB | Eco | -92.4 |
| Type Odd -> Tube, drive 0 % | HQ | -78.7 |
| Type Tape -> Transformer, drive 0 % | HQ | -80.9 |
| Auto gain off -> on, drive 100 %, -40 dBFS | HQ | -78.8 |
| HP slope 12 -> 24 dB/oct | HQ | -92.4 |
| HP slope 24 -> 48 dB/oct | HQ | -92.4 |
| Weight off -> on (+6 dB) | HQ | -92.4 |
| Weight +6 -> +2 dB | HQ | -92.4 |


## Latency

| Quality | Latency (samples) | Latency (ms at 48 kHz) |
|---|---|---|
| Eco | 0 | 0.000 |
| HQ | 48 | 1.000 |


## PRE CPU per configuration (stereo, 48 kHz, 512-sample blocks, % of one core)

| Configuration | CPU (%) |
|---|---|
| Bypassed (PRE on, nothing enabled) | 0.001 |
| Eco, saturation Odd 50 % | 0.226 |
| Eco, saturation Crush 100 % | 0.334 |
| Eco, saturation Tube 100 % | 0.287 |
| Eco, everything on (48 dB HP and LP, Weight, tone) | 0.558 |
| HQ, saturation Odd 50 % | 1.554 |
| HQ, saturation Crush 100 % | 1.217 |
| HQ, saturation Tube 100 % | 1.681 |
| HQ, everything on (48 dB HP and LP, Weight, tone) | 2.474 |


## Harmonic profile (1 kHz sine at -18 dBFS RMS, auto gain on, HQ, 48 kHz)

| Type | Drive | H2 (dB) | H3 (dB) | H4 (dB) | H5 (dB) |
|---|---|---|---|---|---|
| Odd | 50 % | -145.9 | -53.1 | -146.4 | -99.8 |
| Odd | 100 % | -148.8 | -34.8 | -149.0 | -66.0 |
| Even | 50 % | -39.1 | -50.6 | -81.5 | -96.9 |
| Even | 100 % | -17.5 | -40.2 | -43.3 | -63.4 |
| Heavy | 50 % | -147.5 | -59.1 | -148.2 | -116.7 |
| Heavy | 100 % | -148.7 | -35.8 | -149.0 | -131.9 |
| Crush | 50 % | -165.4 | -124.7 | -155.8 | -116.3 |
| Crush | 100 % | -40.9 | -40.0 | -54.3 | -55.0 |
| Tape | 50 % | -146.5 | -55.3 | -147.0 | -103.7 |
| Tape | 100 % | -146.4 | -35.7 | -146.8 | -67.0 |
| Tube | 50 % | -48.8 | -50.2 | -91.0 | -95.9 |
| Tube | 100 % | -20.3 | -33.5 | -43.6 | -72.4 |
| Transformer | 50 % | -150.4 | -52.8 | -150.1 | -99.4 |
| Transformer | 100 % | -157.5 | -34.5 | -156.3 | -65.5 |


## ADAA order at 1x (Odd, Even; raw chain without calibration, 48 kHz)

Order 1 (with its droop correction) is used in Eco. Order 2 has the zero (1 + z^-1 + z^-2) at fs/3, so it droops to -9.5 dB at 12 kHz and to a null at 16 kHz; no FIR can correct that in the passband. Order 2 is therefore used only at 4x (HQ), where its null sits above the passband. Alias: 5 kHz at the drive gain of 100 %, relative to total. Passband: small signal, re 1 kHz. Order 2 is shown uncorrected.

| Type | Order | Alias 5 kHz, 100 % (dB) | 4 kHz re 1 kHz (dB) | 8 kHz re 1 kHz (dB) | 16 kHz re 1 kHz (dB) |
|---|---|---|---|---|---|
| Odd | 1 | -41.2 | -0.00 | 0.00 | -0.08 |
| Odd | 2 | -49.7 | -0.76 | -3.47 | -83.15 |
| Even | 1 | -41.0 | -0.00 | 0.00 | -0.08 |
| Even | 2 | -53.4 | -0.76 | -3.47 | -83.17 |


## Tape: level-dependent HF loss and head bump (Eco, 48 kHz)

| Input level (dBFS RMS) | Drive | 10 kHz re 1 kHz (dB) |
|---|---|---|
| -40 | 100 % | 1.22 |
| -30 | 100 % | 0.68 |
| -24 | 100 % | 0.12 |
| -18 | 100 % | -0.70 |
| -12 | 100 % | -1.86 |
| -40 (small signal) | 0 % | 0.25 |
| head bump 60 Hz re 1 kHz, -40 | 0 % | 1.13 |

