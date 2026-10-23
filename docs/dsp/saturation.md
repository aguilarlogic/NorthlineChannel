# PRE saturation

The saturation section of PRE (`Source/dsp/SatEngine.h`, `Source/dsp/SatSection.h`, `Source/dsp/Saturation.h`).
Every number here comes from the `pre` measurement suite (`docs/measurements/pre.md`) or from the tests in
`tools/tests/TestPre.cpp`. ROADMAP section 7.2 has the targets.

## Signal chain

```
input -> tone pre-tilt -> type pre-emphasis (LF bell, HF shelf) -> ADAA droop pre-correction (FIR)
      -> x drive gain -> shaping curve (ADAA) -> x output gain
      -> type post-emphasis, level trims, Tape extras -> tone post-tilt -> DC blocker (asymmetric types)
```

- Every linear stage before the shaper is inverted after it, so only the harmonics are shaped. The clean signal
  is flat: the small-signal gain at 0 % drive is within 0.1 dB, and the tone control leaves a clean signal flat
  (`pre_toneCleanIsFlat`).
- Eco (1x) uses first-order ADAA with its droop corrected before the shaper, so the saturation path is flat to
  16 kHz (`pre_adaaPassbandFlat`, worst 0.15 dB for every type).
- HQ (4x) runs the same chain on a 4x path around a Kaiser FIR 2x + 2x oversampler (more than 90 dB of attenuation
  above 24.9 kHz, integer latency 45 base samples). It uses second-order ADAA and a zero-phase droop correction, so the HQ
  latency is 48 samples (`Oversampler4` 45 + droop 2 + ADAA 1 sample at 4x).
- Mix (`sat_mix`) is a parallel path against a linear twin of the wet path (the same droop and ADAA average,
  without the nonlinearity), so the mix is phase-coherent: at 0 % drive, 50 % and 100 % mix give the input
  delayed by the latency with 0.01 deg of phase error at 5 kHz (`pre_mixLatencyAligned`).

## Types and harmonic profiles

Harmonic levels in dB re the fundamental, 1 kHz sine at -18 dBFS RMS (the Operating Level), auto gain on, HQ,
48 kHz (`docs/measurements/pre.md`, "Harmonic profile"):

| Type | Drive | H2 | H3 | H4 | H5 | Character |
|---|---|---|---|---|---|---|
| Odd | 50 % | -145.9 | -53.1 | -146.4 | -99.8 | Odd harmonics only, `u / sqrt(1 + u^2)` |
| Odd | 100 % | -148.8 | -34.8 | -149.0 | -66.0 | |
| Even | 50 % | -39.1 | -50.6 | -81.5 | -96.9 | Biased sigmoid: even harmonics from the bias |
| Even | 100 % | -17.5 | -40.2 | -43.3 | -63.4 | |
| Heavy | 50 % | -147.5 | -59.1 | -148.2 | -116.7 | Cubic soft clip with a hard knee at 1.5 |
| Heavy | 100 % | -148.7 | -35.8 | -149.0 | -131.9 | |
| Crush | 50 % | -165.4 | -124.7 | -155.8 | -116.3 | Asymmetric knee clip (+1 / -0.72) |
| Crush | 100 % | -40.9 | -40.0 | -54.3 | -55.0 | Even and odd about equal at full drive |
| Tape | 50 % | -146.5 | -55.3 | -147.0 | -103.7 | Atan soft knee, dynamic HF loss, head bump |
| Tape | 100 % | -146.4 | -35.7 | -146.8 | -67.0 | |
| Tube | 50 % | -48.8 | -50.2 | -91.0 | -95.9 | Envelope-driven bias: even harmonics rise with level |
| Tube | 100 % | -20.3 | -33.5 | -43.6 | -72.4 | |
| Transformer | 50 % | -150.4 | -52.8 | -150.1 | -99.4 | Odd harmonics, LF-weighted by 90 Hz pre-emphasis |
| Transformer | 100 % | -157.5 | -34.5 | -156.3 | -65.5 | |

### Odd (`Sig`)
`u / sqrt(1 + u^2)`. Odd harmonics only. Drive cap 7.0 dB, set so that the Eco alias meets the target.

### Even (`BiasedSig`)
Sigmoid with a static bias of 0.45, normalised to zero at zero and to unit slope. The bias is faded in with the
shaper level U = drive + Operating Level (`biasWeight`, from -22 dB to -8 dB), so an asymmetric curve does not
add even harmonics at a clean setting. A static bias would give THD of about -38 dB at -18 dBFS and 0 % drive,
and no gain change can fix that, because asymmetry at low level produces even harmonics in proportion to its
size.

### Heavy (`Cubic`)
A cubic soft clip with a hard knee at |x| = 1.5. Drive cap 11 dB.

### Crush (`Clip`)
Asymmetric quadratic-knee clip: positive threshold 1.0 (knee width 0.7), negative threshold 0.72 (knee width 0.5).
Drive cap 7.5 dB. Its pre-emphasis is a 4 dB HF shelf at 1.5 kHz; the post stage restores it.

### Tape (`Atan`, dynamic HF loss, head bump)
- Atan soft knee, odd harmonics, 2.5 dB LF pre-emphasis at 80 Hz and 3 dB HF pre-emphasis at 5.5 kHz (both
  inverted after the shaper). Drive cap 8.5 dB.
- Level-dependent HF loss: after the post stage, a 3 dB high shelf at 5.5 kHz is faded in by e = env / (env + 0.4),
  where env follows the shaper input level. 10 kHz re 1 kHz is +1.22 dB at -40 dBFS and -1.86 dB at -12 dBFS
  (100 % drive), so the loss is about 3 dB across that range (`pre_characterTapeHighFrequencyLossAndHeadBump`).
- Head bump: a bell at 60 Hz, Q 1.5, +1.5 dB in the post stage. Measured +1.13 dB (small signal, 0 % drive), because
  the calibrated trims absorb part of it.
- Exception to the flatness rule: the HF loss is level-dependent by design, and the calibrated HF trim sets the
  level at the Operating Level. Quieter signals are therefore up to 0.25 dB above flat at 10 kHz at 0 % drive,
  which is within the 0.5 dB limit of `pre_characterTapeHighFrequencyLossAndHeadBump`. Tape is excluded from the
  strict passband test.

### Tube (`BiasedSig` with envelope-driven bias)
Bias b = 0.55 · env / (env + 0.4) · biasWeight(U), where env follows the shaper input (attack 5 ms, release 80 ms).
Even harmonics rise with level: the second harmonic is at least 6 dB higher at -12 dBFS than at -30 dBFS
(`pre_characterTubeEvenRisesWithLevel`). Drive cap 10 dB.

### Transformer (`Sig` with LF pre-emphasis)
Odd harmonics. A 5 dB bell at 90 Hz (Q 0.6) before the shaper, inverted after it. The low-frequency THD is at least
3 dB above the 5 kHz THD (HQ, full drive, `pre_characterTransformerLowWeighted`). Drive cap 7.0 dB.

## Gain staging (D5)

- Each engine has a calibration table indexed by U = drive + Operating Level (36 points, 2 dB step, from -40 to +30
  dB). For each point, the chain runs with sines at 100 Hz, 1 kHz and 5 kHz and with a pink multisine, all at the
  -18 dBFS reference. The output gain C and the low and high shelf trims are the least-squares solution that returns
  every stimulus to its input RMS. Tables are built once per process, on the first `prepare` (about 180 ms for all
  types, both chains; measured in the `pre` suite).
- There are separate tables for Eco and HQ, because the HQ chain has second-order ADAA and its own droop.
- The calibration is applied when auto gain is on (`sat_autogain`), and the output is blended with a plain
  drive (+12 dB make-up, unity at 0 %) when it is off. With auto gain off, drive is louder (+20 dB from 0 to 100 %
  for Tape, `pre_driveLouderWithoutAutoGain`).

Level change at the Operating Level, worst case over sines at 100 Hz, 1 kHz and 5 kHz and pink noise, 0 to 100 %
drive in 10 % steps (`pre_stagingWithinHalfDb`; full table in `docs/measurements/pre.md`):

| Quality | Worst over all types and drives | Small-signal gain at 0 % drive, worst |
|---|---|---|
| Eco | 0.39 dB (Crush, 100 %) | 0.08 dB |
| HQ | 0.32 dB (Crush, 100 %) | 0.03 dB |

## Aliasing (ROADMAP section 1)

5 kHz sine at -6 dBFS peak, full drive, energy outside the true harmonics relative to total, dB (48 kHz):

| Type | Eco (targets: -40 dB; Crush -30 dB) | HQ (targets: -80 dB Odd/Even; -70 dB Heavy/Crush/new) |
|---|---|---|
| Odd | -41.2 | -130.8 |
| Even | -41.0 | -127.5 |
| Heavy | -43.6 | -134.0 |
| Crush | -31.7 | -121.4 |
| Tape | -41.6 | -130.0 |
| Tube | -40.5 | -94.9 |
| Transformer | -41.2 | -130.8 |

Every target is met. The 1x targets are met by capping the shaper drive (about 7 to 11 dB, see the types above).
Eco is also reported at 44.1 and 96 kHz in the measurement report.

## ADAA order (D5 and §7.2)

Raw chain without calibration, 48 kHz (`docs/measurements/pre.md`, "ADAA order at 1x"):

| Type | Order | Alias 5 kHz, 100 % | 4 kHz re 1 kHz | 8 kHz | 16 kHz |
|---|---|---|---|---|---|
| Odd | 1 (corrected) | -41.2 dB | 0.00 dB | 0.00 dB | -0.08 dB |
| Odd | 2 (uncorrected) | -49.7 dB | -0.76 dB | -3.47 dB | -83 dB |
| Even | 1 (corrected) | -41.0 dB | 0.00 dB | 0.00 dB | -0.08 dB |
| Even | 2 (uncorrected) | -53.4 dB | -0.76 dB | -3.47 dB | -83 dB |

Second-order ADAA has about 8 to 12 dB less alias, but its average has a zero at fs/3 (16 kHz at 48 kHz), so the
passband droops to -3.5 dB at 8 kHz and to a null at 16 kHz. No FIR can correct that. Eco therefore keeps first-order
ADAA. HQ uses second-order ADAA at 4x, where the null sits above 24 kHz (base) and the zero-phase droop correction
covers the passband.

The ADAA droop is corrected before the shaper (`designDroopFirTaps`, minimum phase, 32 taps at 1x for Eco).
The old post-stage de-emphasis (rho = 0.7) is removed.

## HQ oversampling

- `Oversampling.h`: Kaiser-windowed linear-phase FIR, 2x then 2x. Latency 45 base samples, integer at every sample rate. Tight stage 81 taps, wide stage 21 taps.
- The droop correction on the 4x path is a zero-phase FIR (23 taps, centre at 11 samples, Hann window,
  normalised to unity DC). It corrects the ADAA-2 droop up to 24 kHz and tapers to 0 dB by 30 kHz, so the 16 kHz
  edge is not smoothed by the window. Its delay plus the one-sample ADAA average is 12 samples at 4x, which is
  3 base samples, so the wet path has an integer delay. A minimum-phase correction gives a fractional delay of about
  0.25 base samples, which made the mix comb (about -17 dB error) and was rejected.
- Latency: 48 samples (`PreModule::latency()`, `SatSection::kHqLatency`).

## Parameters

| Parameter | Behaviour |
|---|---|
| `sat_drive` | Drive 0 to 100 %, smoothed on 32-sample control ticks, glide 50 ms. Drive maps to pre-gain -12 dB at 0 % to the type's cap at 100 %. |
| `sat_mix` | Parallel mix against the linear twin (phase-coherent, see above). |
| `sat_tone` | -100 to +100 %. Positive values move harmonics up: the shaper sees the lows raised and the highs cut, and the post stage restores the balance. 6 dB tilt at 600 Hz, power-normalised. |
| `sat_autogain` | Calibrated staging on or off. |
| `sat_type` | 0 Odd, 1 Even, 2 Heavy, 3 Crush, 4 Tape, 5 Tube, 6 Transformer. Type changes cross-fade over 20 ms between two engines. |
| `quality` | Eco or HQ. Non-automatable. Latency 0 or 48. |

## Click-free switching (D9, rule 5)

Splatter in a 50 ms window around each switch, relative to a 1 kHz carrier, Blackman-Harris analysis
(`pre_clickFreeSwitching`; numbers from `docs/measurements/pre.md`):

| Change | Eco | HQ |
|---|---|---|
| Type Odd -> Tube, 0 % drive | -81.7 dB | -78.7 dB |
| Type Tape -> Transformer, 0 % drive | -84.8 dB | -80.9 dB |
| Auto gain off -> on, 100 % drive | -80.0 dB | -78.8 dB |
| HP / LP slope changes, Weight on/off, amount and frequency | -92.4 dB | -92.4 dB |

Quality (Eco <-> HQ) is not click-free: it changes the latency by 48 samples, and the delay lines reset. See
"Limitations".

## Determinism (D3, D8, rule 2)

- Control-rate state (smoothers, filter coefficients, the Tube and Even bias, Weight) changes on absolute
  multiples of 32 samples, from a persistent `uint32_t` `ControlClock`. Nothing uses a block-local counter or a
  signed sample counter.
- `pre_determinismBlockSizes`: every type, Eco and HQ, with the filters and Weight on and drive, tone, mix and the
  frequencies automated every 2048 samples. Block sizes 1, 7, 32, 37, 64, 511, 512 and 2048 give a maximum
  difference of 1e-6 or less against block size 2048.

## CPU (PRE alone, one core, 48 kHz, stereo, 512-sample blocks)

| Configuration | CPU |
|---|---|
| Eco, saturation Odd 50 % | 0.23 % |
| Eco, everything on (48 dB HP and LP, Weight, tone) | 0.56 % |
| HQ, saturation Tube 100 % | 1.68 % |
| HQ, everything on | 2.47 % |

The roadmap budget (section 15) is 0.6 % for everything on in Eco and 1.5 % for HQ, for the whole strip. PRE alone
is at the Eco budget and over the HQ budget. See "Limitations".

## Limitations

- HQ CPU is over budget (2.47 % with everything on, against 1.5 % for the whole strip). The HQ path runs a second
  decimator for the linear twin; that is the first lever.
- Quality switching is not click-free and changes latency (48 samples). The strip must report the new latency
  and should crossfade if the quality switch needs to be inaudible.
- Eco reports latency 0 as the roadmap requires, but by design the first-order ADAA average (1 + z^-1) / 2 delays the
  Eco path by about half a sample at low frequencies. This is not measured; the mix twin keeps the parallel path coherent.
- Tape's level-dependent HF loss makes the small-signal response level-dependent, so it is excluded from the strict
  passband test (see "Tape").
- Tape's head bump is +1.13 dB measured, against +1.5 dB designed, because the calibrated trims absorb part of it.
