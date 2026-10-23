# EQ: design, dynamic bands and measurements

Roadmap 7.3 (decramped bands, Clean and Proportional character, dynamic bands, band solo), D3, D8, D10 and D13.
Code: `Source/dsp/EqDesign.h` (band design), `Source/dsp/EqDynamics.h` (detectors, gain law),
`Source/dsp/EqModule.h` (module, display API, `readEqParams`). Tests: `tools/tests/TestEq.cpp`.
Measurements: `tools/measure/MeasureEq.cpp`, report in `docs/measurements/eq.md` (`measure eq --out docs/measurements`).

## 1. Static bands

**Analog prototypes** (RBJ, the same shapes the bilinear design approximated): bell with gain A = 10^(G/40) and
Q; low and high shelves with the RBJ shelf Q. Shelves have a fixed Q per type (0.71 and 1.5); bells take the
user Q (0.2 to 10) or the fixed bell Q of the Wide (0.55, Tone 0.8) and Narrow (2.5) types.

**Digital design.** Second-order section fitted so its magnitude follows the analog one up to 0.9 Nyquist,
then mapped onto the TPT SVF so the automation-safe structure is kept (`toCoefs`: g, k, a1 = 1/(1+g(g+k)),
a2 = g·a1, a3 = g·a2, mixing m0, m1, m2).

1. **Closed form** (Vicanek, "Matched Second Order Digital Filters", 2016): the matched z-poles from impulse
   invariance of the analog denominator (`matchedPoles`), with the numerator fitted at DC, Nyquist and one anchor
   frequency. The bilinear pole placement (the previous design) is also tried, and the better one kept.
2. **Refinement**, only if the closed form misses the tolerance on the solver grid (`Options`: 0.12 dB to accept the
   closed form; 0.22 dB after the three-value stage). Levenberg-Marquardt on the dB error, Lawson re-weighting toward
   minimax. Stage 2 holds DC and Nyquist exact (three free values). Stage 3 frees all five SVF values from the best
   and from the other closed form. A polish round runs only above 0.25 dB.
3. **Fit grid**: 12 log points from 25 Hz to 3 kHz, 22 from 3 kHz to 0.9 Nyquist, three light points at
   0.93 to 0.99 Nyquist, and 13 points around fc scaled by the half-gain bandwidth. The grid is per sample rate.

The design is a pure function of (band, type, frequency, total gain, Q, character, sample rate); it holds no state
and does not allocate. Refinement runs only when static parameters move (a control tick on which a smoothed
parameter changed), never when nothing moved.

**Accuracy** (`measure eq`, full run: every 1 dB from -18 to +18, 25 centre frequencies 20 Hz to 20 kHz, Q 0.2 to 10,
probes from 20 Hz to 0.9 Nyquist, 2.5 million bell cases per rate):

| Target | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Shelves, all fc: max error <= 0.5 dB | 0.30 dB | 0.31 dB | 0.24 dB |
| Bells, fc <= 0.4 fs: max error <= 0.5 dB | 0.45 dB | 0.41 dB | 0.35 dB |
| Bells, fc > 0.4 fs: max error <= 1.0 dB | 0.40 dB | 0.51 dB | 0.00 dB (no such fc) |
| Proportional bells, fc <= 0.4 fs | 0.499 dB | 0.414 dB | 0.330 dB |

The worst case over the whole grid is the Proportional bell at 44.1 kHz (fc 15 kHz, -18 dB, effective Q 1.25) at
0.499 dB; a dense re-check of that case (24 000 probes) gives the same value, so the probe set is adequate. RMS
error over all cases is 0.01 to 0.07 dB. The previous bilinear design was 5 to 13 dB off above 0.4 fs.

**Bells centred above 0.4 fs.** Above 0.4 fs the closed form gets its anchor at 0.2 fs (the response is fitted
where the digital band is still well defined), and the fit grid clamps its points to 0.9 Nyquist. No cases are
excluded: the analog response is evaluated only up to 0.9 Nyquist, where it is well defined for every centre
frequency. These bells meet the 1 dB target (worst 0.52 dB, 48 kHz, fc 20 kHz, Q 2.5, -18 dB). The design clamps the
frequency to 0.49 fs.

**Design cost** (per call, measured): mean 4.5 to 15 us; most calls (70% to 90%) are the closed form. About 6% to 15%
run the three-value refinement and 3% to 14% the five-value refinement. The worst call takes about 165 to 375 us.
Refinement therefore costs one tick's worth of time when a band moves through a hard region (section 6).

## 2. Character: Clean and Proportional

- **Clean**: Q is the set Q for all gains.
- **Proportional** (console style): effective Q rises with the total band gain |G| (dB):
  bells `Q_eff = Q · (1 + |G| / 12)` (x2.5 at +-18 dB); shelves `Q_eff = Q · (1 + |G| / 72)` (x1.25 at +-18 dB, so
  shelf resonance stays tame).
- The blend is smoothed at control rate, so switching Clean to Proportional glides and does not step.

## 3. Dynamic bands

Each band can be dynamic. Per band: Dyn on/off, Threshold (-60 to 0 dBFS), Range (-18 to +18 dB, negative
= dynamic cut, positive = dynamic boost), Speed (Fast, Medium, Slow), and Sidechain (`dynExt`: detect from the
external key instead of the input). The total band gain is static gain + dynamic gain.

**Detector.** A region filter of the detection signal (bells: band-pass at the band frequency and Q, clamped to
0.3 to 12; low shelf: low-pass; high shelf: high-pass). Its output is squared and followed by a one-pole
mean-square follower with time constant `clamp(1500 / f_Hz, 2, 30)` ms (about 1.5 periods of the band, kept between
2 and 30 ms). The band level is `10 log10(mean square)` read at each control tick. A full-scale sine reads -3.01 dB.
Linked L/R (target both) uses the larger channel level. On a channel-specific target only that channel is used.

**Gain law.** With over = level - threshold:

- over <= -3 dB: fraction 0;
- -3 < over < +3 dB (soft knee, 6 dB wide): fraction = (over + 3)^2 / 12 / 12 (quadratic, meeting the linear part);
- +3 dB <= over <= +12 dB: fraction = over / 12 (linear);
- over > +12 dB: fraction 1 (full range "reached 12 dB over threshold").

Dynamic gain = Range x fraction. Half the range is applied at +6 dB over threshold.

**Speeds.** Attack and release are one-pole time constants (1/e) of the dynamic gain at control rate:

| Speed | Attack (ms) | Release (ms) | Measured attack to 63 % (ms) | Measured release to 1/e (ms) |
|---|---|---|---|---|
| Fast | 2 | 50 | 2.0 | 60 |
| Medium | 8 | 150 | 8.0 | 160 |
| Slow | 30 | 500 | 30.0 | 510 |

Attack applies while the target gain is more extreme than the current one, release otherwise. The attack is measured
within one control tick of the nominal value. The release is about 10 ms longer: the detector must first fall below
the knee, which takes about 10 ms for a 2 ms follower.

**Gain axis table.** Static gain plus dynamic gain is designed at 1 dB steps of dynamic gain (up to |Range|, with a
last node at |Range|), 19 nodes at most. The band's g, k and m values are interpolated linearly between nodes at
control rate. Node 0 is the static design itself, so a dynamic band at rest is bit-identical to the same band
without dynamics. The table is rebuilt only when the static parameters or the range change.

**At rest.** Below threshold (dynamic gain 0) and with static gain 0, the band is inactive: no processing, so it
nulls exactly (-300 dB measured). Below threshold with a non-zero static gain it is bit-identical to the static band.

**Measured steady state** (`measure eq` 2a, 48 kHz, range -12 dB, threshold -30 dBFS, tone at 1 kHz): the measured
gain follows the law to within 0.12 dB at all levels from -36 to 0 dBFS (see the report).

**Known bias.** On a steady pure tone at the band frequency, the 2 kHz ripple of the detector (about +-0.15 dB at a
2 ms follower) is sampled at control ticks. The attack/release follower then settles toward the more extreme
samples, so the steady dynamic gain reads about 0.12 dB more reduction than the law gives (-3.11 dB against -2.99 dB
for a -24 dBFS tone). The mean of the detector level over ticks is correct (-27.01 dB), and the bias is the same at
all three speeds, which points to the follower. Program material with broadband detection content does not show
this as a steady offset. The fix would be a longer detector follower at low frequencies, which slows the release
start; this is not made.

## 4. Discrete changes: click-free (D10)

- **Band type and target**: the old configuration keeps running in a preallocated shadow state (`sh`) for 15 ms and
  is crossfaded out with a smootherstep weight. Splatter for a switch during a 100 Hz or 1 kHz tone is -76 to -133 dB
  (test `eq_discreteSwitchesAreClickFree`, bound -60 dB). A 0 to +18 dB gain jump glides through the smoothers
  (splatter -54 dB), which is a continuous change, not a switch, and is bounded at -45 dB.
- **Frequency, gain, Q, character**: 40 ms one-pole smoothers, evaluated at control rate on absolute ticks.

## 5. Band solo (transient)

The soloed band's region (band-pass for bells, low-pass below a low shelf, high-pass above a high shelf, at the
band frequency and Q) is faded in over 15 ms while the rest of the module is faded down. A 25 ms pre-roll lets the
region filter settle on the running signal before the fade. Splatter on/off: -66 to -97 dB at 200 Hz and 1 kHz.
`eq_solo` is transient (not saved, reset on load), as the roadmap requires; the parameter table must mark it so.

## 6. Determinism, latency and real-time

- **Determinism (D3, D8)**: every smoother and every design moves on absolute multiples of `kControlBlock` (32) using
  `ControlClock`. The per-tick work reads the detector at the tick, and detectors run in place before the band
  processing of each segment. No signed counters. Output is identical for block sizes 1, 7, 32, 37, 64, 511, 512
  and 2048, with automation of frequency, gain, Q, types, targets, character, dynamic settings, external key and
  solo (max difference 0.0, tests `eq_blockSizeIndependentWithAutomation` and `eq_stripBlockSizeIndependentWithAutomation`).
- **Latency**: 0 samples, independent of any setting.
- **Allocation**: none in `process()` (test `eq_noAllocationOnTheAudioThread`: 0 allocations).
- **Per-tick refinement cost**: a design that needs the refinement can take 165 to 375 us (one 32-sample tick is
  about 670 us at 48 kHz). Several bands refining in the same tick would overrun it. Open item (section 8).

## 7. Display (D13)

- `EqModule::magnitudeDb (params, fs, freqHz, channel)` returns the exact response of the static design, honouring
  targets. Channel 0 is Mid or L, channel 1 is Side or R. `magnitudeDb (params, fs, freqHz, channel, dynGainDb[4])`
  adds the current dynamic gains (`getDynGainDb (band)`). `magnitudeCurveDb` does a whole curve for the UI.
- Measured against the DSP: static bands 0.000 dB (44.1 and 96 kHz); with a steady dynamic gain of -3 to -12 dB
  (key tones -27 to -15 dBFS, 48 and 96 kHz, nine probe frequencies), worst error 0.001 dB (tests
  `eq_displayMatchesMeasuredResponse`, `eq_displayIncludesDynamicGain`).
- The UI keeps the legacy `EqModule::bandCoefs` for compilation only. The DSP does not use it.

## 8. CPU (measured, `measure eq`, stereo, 10 s of noise at -20 dBFS, 64-sample blocks)

| Configuration | 48 kHz (% of one core) | 96 kHz |
|---|---|---|
| All four bands off | 0.005 | 0.010 |
| Four static bands, steady | 0.060 | 0.120 |
| Four dynamic bands, steady detection | 0.134 (budget 0.15 %) | 0.265 |
| Four static bands, gains gliding every 1024 samples | 0.71 | 1.41 |
| Four dynamic bands, gains gliding | 4.9 | 9.8 |

The four dynamic bands meet the 0.15 % budget at 48 kHz with steady parameters. Gliding is the worst case: every
tick rebuilds the design (static) or the gain-axis table (dynamic), so the cost is per moving band, not per module.
Costs were brought down by interleaving the two channels in the band and detector loops, keeping recursion state
and coefficients in locals (the compiler otherwise reloads them every sample), and recomputing Hz and Q only when
the smoothed logs change. Before this the four dynamic bands cost 0.34 %.

## 9. Open items

1. **Per-tick refinement budget.** A refining design can take up to about 375 us. Four bands gliding through hard
   regions in one tick could overrun a 64-sample block. Proposed fix: at most one refinement per control tick across
   bands (absolute tick order, so still deterministic), the others use the closed form and retry on the next tick.
2. **Glide cost of dynamic bands** (4.9 % for four gliding bands): the gain-axis table is rebuilt every tick while the
   band moves. Options: fewer nodes, or rebuilding every other tick while moving.
3. **Detector bias on pure tones** (section 3, 0.12 dB).
4. **Proportional shelves** are not in the grid; they use the same design at Q up to 1.9, inside the grid's range.
5. Linear-phase mode (P6) is not part of this work.
