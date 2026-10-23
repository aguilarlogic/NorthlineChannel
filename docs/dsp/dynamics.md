# Dynamics: gate, de-esser, compressor

Source: `Source/dsp/Dynamics.h` (shared layer), `GateModule.h`, `DeEsserModule.h`, `CompModule.h`.
Tests: `tools/tests/TestDynamics.cpp` (`dynamics_`, `comp_`, `gate_`, `ds_` cases).
Measurements: `docs/measurements/dynamics.md` (`NorthlineTool measure dynamics --out docs/measurements`).

## Shared rules

- **Timing knobs are 10-90 % rise times** of the gain-reduction step response. `riseCoef` turns a
  rise time into a one-pole coefficient (ln 9 time constants), so the displayed value is the
  measured value.
- **Control rate.** Parameter changes take effect on absolute multiples of `kControlBlock` (32
  samples). `setParams` derives coefficients and stores targets only; the audio sees them at the
  next tick. The position inside a tick is the persistent `ControlClock` phase, so the output does
  not depend on the host block size. Checked at block sizes 1, 7, 32, 37, 64, 511, 512, 2048, with
  and without automation (`renderWithAutomation`, events on multiples of 2048).
- **Latency** is the `*_lookahead` choice in samples (`kLookaheadMs`). It is the same whether the
  module or its lookahead is in use. The main path is delayed by it, the detector sees the undelayed
  signal, and internal dry paths (mix, listen, delta) are delayed to match.
- **Discrete switches** (COMP type, GATE type, DE-ESS band type or mode, band on/off) cross-fade over
  15 ms with a raised cosine between the live core and a preallocated shadow core that keeps running
  the old configuration. The new core inherits the gain state and its detector is primed from the
  old reading. Nothing allocates.

## COMP

Signal flow: `wet = x * g_gr * g_makeup`, `y = dry + (wet - dry) * mix`, `out = y * g_out`. The dry
path is never scaled by make-up, so mix 0 % nulls with the aligned input (below -120 dB).

- Types: VCA and FET use a peak detector with a 5 ms hold; OPT and SOFT use an RMS detector.
  FET adds odd-harmonic colour from a first-order ADAA shaper.
- **RMS window.** Two cascaded one-poles, each `tau = 0.0748 * fastest` (fastest = the shorter of
  attack and, for OPT, a quarter of release; otherwise release). The 10-90 % rise of two poles is
  `3.35 tau`; the attack smoother is set to `sqrt(A^2 - rise^2)` so the measured attack matches the
  knob. A single pole left a 7 % ripple at twice the signal frequency, which the asymmetric
  attack/release smoother turned into about 0.2 dB of extra gain reduction. Two poles reduce this to
  about 0.03 dB on a 1 kHz sine.
- **Auto make-up.** `makeup = -10 log10( E[G^2] )`, where the expectation runs over a Gaussian of
  detector levels (offset and spread per type) around the Operating Level. The offsets are
  VCA/FET 9 dB / 2 dB, OPT 5.25 dB / 4 dB, SOFT 5 dB / 2.5 dB. Fitted on pink noise at the default
  timing (10 ms attack, 150 ms release): worst error 0.31 LU over the spec grid, 0.65 LU on a held-out
  grid and at two other timings, the worst case being 0.65 LU (SOFT at 3/80 ms). See the measurement report.
- **Stereo link** interpolates the two detector levels in dB: a link of 50 % between -6 and -50 dB
  gives a detector level of -28 dB.
- **OPT memory** is a control-rate quantity. Its charge and discharge time constants (2 s and 4 s)
  are evaluated per tick, not per sample or per channel, and it is cleared in `reset()`. The release
  scales with the memory: 1 + 2 x memory.
- **Auto release** (VCA, FET, SOFT) moves between the knob and 6 x the knob with the same memory.
- **Knee** is the quadratic soft knee of Giannoulis, Massberg & Reiss (JAES 2012), width 0-24 dB.
  Range caps the gain reduction; 40 dB means unlimited.

## GATE

- Detector: sidechain high-pass (or band-pass) and peak follower, with a hold of at least one period
  of the high-pass frequency plus hysteresis. A 40 Hz sine decaying through the threshold gives one
  open/close cycle.
- Gate: opens at the threshold, closes below threshold minus hysteresis once the hold has run out.
  Opening is a raised cosine in the dB domain over the attack time (floor 0.02 ms).
- Expander: a true downward law with ratio 1.2-8 (`gate_ratio`) and a knee of 0-12 dB (`gate_knee`), continuous across the
  threshold (no jump at close). Floor and range set the lowest gain.
- Duck: attenuates by the floor while the key is above the threshold.
- Lookahead covers detector and filter delay. The default 0.5 ms opening already reacts at the
  onset, so the lookahead gains little on a click (0.25 dB without, 0 dB with 1 ms or more).

## DE-ESS

- Two bands. Split mode is dynamic EQ: each band's gain acts only on its band, so the spectrum
  outside it is unchanged (0.00 dB at matched gain reduction on the synthetic vocal). Wide mode
  applies the band detector's gain to the whole signal (-12 dB outside at the same reduction).
- Attack and release are 10-90 % rise times. The output gain ticks on absolute control ticks.
- A band that is re-enabled starts from a clean state (no stale filter or detector memory).

## Known limits

- **Opening at -60 dB above 8 kHz needs about 0.3 ms.** A 0.1 ms dB-domain ramp measures -43 dB,
  because a dB-domain ramp puts most of its linear-gain change in the last part of the ramp. The knob floor (0.02 ms) is therefore not
  click-free by that criterion. Option for the lead: raise the minimum `gate_attack` in `Params.cpp`.
- **Auto make-up is a fit** at the default timing. It holds within 0.65 LU for the timings tested
  (3/80 ms and 30/400 ms), but has not been checked for other ratios or for program material.
- **OPT release is program dependent.** After a 30 dB step it measured 18-20 % short of the knob.
- **RMS windows are short** compared with the period of bass content, so the window ripples on
  bass (0.16 dB peak-to-peak at 100 Hz for OPT in the measurement report).
