# Dynamics, Metering, and Filter Building Blocks for a Channel-Strip Plugin

Research date: 2026-10-10. Scope excludes topics already covered (RBJ biquads, Cytomic SVF existence, Pultec/Neve/SSL folklore, LA-2A/1176 qualitative behaviour, neural compressor papers, ADAA and oversampling basics).

Source-type tags used below: [peer-reviewed], [standard], [vendor doc], [OSS repo], [encyclopedia], [web/trade press], [forum/anecdotal].

Several sources were only available as OCR-garbled excerpts. Where an equation was read from such an excerpt, this is stated, and the transcription was checked for internal consistency.

## 1. Compressor gain computer and soft knee

### Takeaway
The Giannoulis/Massberg/Reiss static curve is a piecewise function of input level in dB with threshold T, ratio R, and knee width W, joined by a quadratic transition. The formula is legible in an OCR-garbled excerpt of the same authors' 2013 JAES paper, and I verified that the three branches meet continuously at both knee edges.

### Cited Findings
- The compressor model is feed-forward. A peak detector feeds a log-domain gain computer. The paper says the design "is intended to be applicable to live performance and broadcast," and that its peak detector "ensures a quick response to changes in signal characteristics, hence lessening the need for look-ahead." — [Giannoulis, Massberg, Reiss, JAES 2013 (automation paper)](https://joshreiss.github.io/documents/2013/Giannoulis%20Massberg%20Reiss%20-%20dynamic%20range%20compression%20automation%20-%20JAES%202013.pdf) [peer-reviewed; OCR excerpt]
- Gain computer, Eq. (3), with W the knee width in dB:
  - below knee, 2(x_G − T) < −W: y_G = x_G
  - knee, 2|x_G − T| ≤ W: y_G = x_G + (1/R − 1)(x_G − T + W/2)² / (2W)
  - above knee, 2(x_G − T) > W: y_G = T + (x_G − T)/R
  — same source (Eq. 3; the middle-branch layout is partly garbled in the excerpt).
- The detector output is converted to dB as x_G = 20·log10|g[n]|, and the static compression amount is x_L = x_G − y_G. — same source (Eq. 2 and the static-compression definition).
- The full 2012 tutorial is "Digital Dynamic Range Compressor Design—A Tutorial and Analysis," JAES vol. 60, pp. 399–408 (June 2012). The 2013 paper cites it as "A Tutorial on Digital Dynamic Range Compressor Design." — [ResearchGate record](https://www.researchgate.net/publication/277772168_Digital_Dynamic_Range_Compressor_Design-A_Tutorial_and_Analysis) [peer-reviewed; abstract only]

### Inferences
- Boundary check on Eq. (3), done by hand: at x = T − W/2 the knee branch returns x, matching the linear branch. At x = T + W/2 it returns T + W/(2R), matching the upper branch. The curve is therefore continuous, and the transcription is consistent.
- The slope in the knee is 1 + (1/R − 1)(x − T + W/2)/W, which goes from 1 at the lower knee edge to 1/R at the upper edge. This is my derivation. The excerpt's derivative line is garbled, so it should not be quoted directly.
- W = 0 reduces to the hard-knee piecewise curve. A practical implementation computes y_G per sample in dB, subtracts it from x_G, then converts the gain to linear. The cost is one log and one exp per sample, or one shared exp per block with the envelope in the log domain (see Section 14).

### Gaps
- The 2012 tutorial's own text was not retrieved. The QMUL PDF URL returned the department homepage, and the firecrawl scrape was rate-limited. Confirm the equations against the 2012 paper before quoting it as the primary source.
- Feedback-topology gain computer behaviour is not sourced in this pass.

## 2. Level detection: peak, RMS, branching and decoupled detectors, feedforward vs feedback

### Takeaway
The 2013 paper gives explicit one-pole peak and RMS detectors. The peak detector has instantaneous attack and smoothed release, and when both detectors share a time constant the peak output never falls below the RMS output. Branching and decoupled peak detectors are named in a thesis abstract, but no equations or definitions were retrieved.

### Cited Findings
- Peak detector: y²_Peak[n] = max(x²[n], α·y²_Peak[n−1] + (1−α)·|x²[n]|). RMS detector: y²_RMS[n] = α·y²_RMS[n−1] + (1−α)·x²[n]. Both are one-pole filters. — [Giannoulis et al., JAES 2013, Eq. (7)](https://joshreiss.github.io/documents/2013/Giannoulis%20Massberg%20Reiss%20-%20dynamic%20range%20compression%20automation%20-%20JAES%202013.pdf) [peer-reviewed; OCR excerpt]
- Setting the peak and RMS time constants to be identical "guarantee[s] that the release envelopes of both detectors are the same, and that the peak detector's output cannot be less than the detected RMS output." — same source.
- The peak detector "has instantaneous attack and a smooth release trajectory." — same source.
- The 2013 paper uses a peak detector rather than RMS for the main sidechain because it "ensures a quick response to changes in signal characteristics." — same source.
- A thesis abstract states that "the discrete branching and decoupled peak detectors are compared, as well as the different options for detector placement." — [Manasidis, Aristotle University of Thessaloniki thesis](https://ikee.lib.auth.gr/record/345494/files/Manasidis.pdf) [academic thesis; abstract only]
- The reference list of the 2013 paper includes "RMS-Based Feedback Compressors," J. Audio Eng. Soc. 47, pp. 788–804 (1999 Oct.). Its content was not retrieved. — same source (reference list).

### Inferences
- In the 2013 design, the instantaneous-attack peak detector comes from the max() operator, so it is not a branching detector. Branching in the usual sense means separate per-channel detectors combined later, and that combination rule is not defined in any source retrieved here. Treat branching versus decoupled definitions as unverified.
- The feed-forward choice matters for the crest-factor auto-release in Section 4, because the crest factor is computed from the same sidechain.

### Gaps
- Equations and stereo-combination rules for branching and decoupled detectors (the thesis full text was not retrieved).
- Feedback-topology RMS behaviour and its dependence on detector placement (the 1999 feedback-compressor paper was not accessed).

## 3. Attack and release coefficient definitions

### Takeaway
The Giannoulis convention is α = exp(−1/(τ·f_s)), so τ is the 1/e (63.2%) time constant. The earlier unverified form α = exp(−ln 9 /(t·f_s)) corresponds to a 10–90% time t. Both are valid, but the plugin must state which one it uses.

### Cited Findings
- α_A = e^(−1/(τ_A·f_s)) and α_R = e^(−1/(τ_R·f_s)), where τ_A and τ_R are attack and release time constants and f_s is the sample rate. — [Giannoulis et al., JAES 2013, Eq. (6)](https://joshreiss.github.io/documents/2013/Giannoulis%20Massberg%20Reiss%20-%20dynamic%20range%20compression%20automation%20-%20JAES%202013.pdf) [peer-reviewed; OCR excerpt]
- Gain smoothing ("ballistics"), Eq. (5): y1[n] = max(x_L[n], α_R·y1[n−1] + (1−α_R)·x_L[n]); y_L[n] = α_A·y_L[n−1] + (1−α_A)·y1[n]. — same source.
- The paper warns that "very short attack and release times should be avoided" because they can reshape note decays and modify the sound of instruments. — same source (partly legible).

### Inferences
- In Eq. (5), the first stage gives an instant attack and a release set by α_R. The second stage then smooths both edges with α_A. The effective attack is therefore slower than the first stage alone. This is my reading of the equations.
- For a 10–90% time t, the one-pole coefficient is α = exp(−ln9/(t·f_s)). This follows because a one-pole 10–90% rise takes τ·ln 9 seconds. The derivation is mine. The ln 9 form therefore uses a different definition of the same knob, not a different filter. The earlier notes' version was written as "T_s·t" and should be read as t·f_s.

### Gaps
- No source compares the two time definitions as used in shipping plugins, and no manual was checked.
- No source for the coefficient update rate when attack or release is modulated in real time.

## 4. Program-dependent release, auto attack and release, auto knee, auto make-up

### Takeaway
The 2013 paper automates attack and release from a crest factor (peak over RMS), with an optional spectral-flux variant. It automates the knee from average gain reduction, and it compares two make-up methods. Average gain reduction works well, while loudness-based make-up slightly underestimates for percussive material. The evaluation was a listening test, so it is subjective, and its statistics are only partly legible.

### Cited Findings
- Crest factor: y_C = y_Peak / y_RMS, with both detectors at a 200 ms time constant, which the paper says was "set at 200 ms based on informal testing." — [Giannoulis et al., JAES 2013](https://joshreiss.github.io/documents/2013/Giannoulis%20Massberg%20Reiss%20-%20dynamic%20range%20compression%20automation%20-%20JAES%202013.pdf) [peer-reviewed; OCR excerpt]
- Maximum times τ_A,max = 80 ms and τ_R,max = 1 s. Attack and release are τ_A[n] = 2·τ_A,max / y_C²[n] and τ_R[n] = 2·τ_R,max / y_C²[n] − τ_A[n]. The paper says the release is reduced by the attack time. — same source, Eq. (8).
- Spectral-flux variant: STFT with N = 1024 Hamming window and hop 512, half-wave rectified flux, then τ_A = 2·τ_A,max / SF_smooth and τ_R = 2·τ_R,max / SF_smooth^γ − τ_A with γ = 0.8. — same source, Eqs. (9)–(13).
- Auto knee: W[n] is set from a smoothed average of gain reduction, with an exponent k that depends on a smoothed spectral-flux minimum. The paper uses k = 0.6 when that value exceeds 0.1 and k = 0.05 otherwise. — same source, Section 2.3 (partly garbled).
- Auto make-up: the paper compares average-control-voltage make-up with loudness-based make-up (BS.1770-2 and EBU R128 style). Its conclusion is that using average gain reduction as make-up "worked well and was described as helpful by some testers," while the loudness method shows "a slight underestimation of required make-up gain with the more percussive samples." — same source, Sections 3.3 and 4.
- The evaluation used listening tests on guitar, bass, drums, and slap bass with a professional group and a group of seven amateurs "who had experience with dynamic range compression" (Amateur group). The professional group size is not legible in the excerpt. — same source, Section 3.

### Inferences
- The crest-factor auto release needs one peak and one RMS one-pole per channel plus one division, so it is cheap. The spectral-flux variant needs an STFT and is not a cheap per-sample operation. This is my estimate, not a benchmark.
- Average-gain-reduction make-up is causal and needs no loudness measurement. Loudness-based make-up needs a 400 ms gated measurement (Section 12), which adds latency or look-ahead in practice.
- The paper's "program-dependent" release is crest-factor based. It is not a memory model of the kind used in opto cells (Section 5).

### Gaps
- No peer-reviewed comparison against commercial auto-release algorithms.
- Listening-test sample sizes and statistics are not legible in the excerpt.
- No CPU cost measurement for these methods in a real-time plugin.

## 5. FET, opto, and VCA character approximations

### Takeaway
No source retrieved in this pass gives a citable model of FET, opto, or VCA dynamic behaviour. The LA-2A and 1176 qualitative behaviour is out of scope, so this section is a gap.

### Cited Findings
- None retrieved in this pass.

### Inferences
- None drawn. Any value stated for these hardware cells would have to come from measurement or from a primary modelling paper, and none was reviewed.

### Gaps
- Digital models of FET nonlinear gain-control, opto cell time constants (fast and slow stages), and VCA exponential gain law. No primary sources were fetched.
- The earlier notes listed "two-stage opto release (LA-2A T4 cell)" as a concept. It remains unverified.

## 6. Gate and expander: hysteresis, hold, range, look-ahead, sidechain key filter, chatter and click avoidance

### Takeaway
The retrieved sources are encyclopedia entries, practitioner articles, forum posts, and vendor product copy. They agree on the parameter set: attack, release, hold, look-ahead, hysteresis, and sidechain filtering. No peer-reviewed or code-level source for gate design was retrieved.

### Cited Findings
- "Gates typically feature attack, release, and hold settings and may feature a look-ahead function. The hold control is used to define the length of time the..." — [Wikipedia, Noise gate](https://en.wikipedia.org/wiki/Noise_gate) [encyclopedia; excerpt truncated]
- "Hysteresis lets you pick a different volume level for the gate to close at. Almost like setting a second threshold. Instead of closing when the..." — [Mastering.com, Noise Gate: 12 Tips](https://mastering.com/noise-gate/) [web/trade press; excerpt truncated]
- A Logic Pro user reports that with self-gating the gate "seems to stay open 25ms after the signal falls below the threshold/hysteresis," and that sidechain operation differs. — [Logic Pro forum thread](https://www.logicprohelp.com/forums/topic/31513-what-exactly-should-lookahead-be-doing-for-a-noise-gate/) [forum/anecdotal]
- A forum answer says look-ahead "allows the gate to respond to events that haven't happened yet." — [Reddit r/audioengineering](https://www.reddit.com/r/audioengineering/comments/1j2pf3l/how_does_lookahaed_work_on_gate/) [forum/anecdotal]
- A commercial gate describes itself as having "hysteresis, lookahead, hold, and frequency-focused sidechain detection." — [ARGATE product page](https://www.musehub.com/plugin/argate) [vendor copy; a product claim, not a measurement]

### Inferences
- Hysteresis is implemented as a pair of thresholds: open at T_open, close at T_close < T_open. A hold timer starts when the detector falls below T_close. This is a standard construction and is my derivation.
- Look-ahead of L samples needs an L-sample delay on the audio path and a detector that reads L samples ahead. The plugin must report this latency to the host. This is standard practice, not sourced here.
- Chatter is reduced by the hysteresis plus hold combination. Click is reduced by ramping gain changes. Neither is quantified in any source retrieved.
- An expander can reuse the Section 1 knee curve with the ratio below 1:1 in the downward direction. This is my inference.

### Gaps
- Hysteresis and hold formulas as implemented in published designs.
- Expander range and knee specifics, and a citable equation for the gain law below threshold.
- Sidechain key-filter design (HPF or bandpass Q and order) and its effect on gate behaviour.
- Quantified chatter and click avoidance. No peer-reviewed source retrieved.
- Code references (LSP, Faust, x42, DPF) were not reviewed in this pass.

## 7. Transient shaper (fast/slow envelope difference, SPL-style, level independence, artifacts)

### Takeaway
Retrieved sources agree on the structure: a fast and a slow envelope follower run on the same signal, their difference drives attack gain, and the slower envelope drives sustain gain. The best-documented open implementation is sevagh's MIT-licensed repository, whose defaults are fast attack 1 ms, slow attack 15 ms, and release 20 ms.

### Cited Findings
- "Compute fast and slow attack-release envelopes from the derivative of the power." Defaults: "fast attack = 1ms, slow attack = 15ms, release = 20ms," configurable per band. — [sevagh/multiband-transient-shaper README](https://github.com/sevagh/multiband-transient-shaper) [OSS repo]
- "The attack gain is the difference between the attack and release envelopers, normalized to [0, 1]." The sustain gain is "the inverse of the attack gain (1 - attack gain)." The repository uses 24 Bark bands from 20 Hz to 20 kHz. — same source. The normalization formula is not given in the README excerpt.
- License: the repository page shows an "MIT license" link in the sidebar. The LICENSE text was not extracted. — same source.
- "Basically, once you have two envelopes (same release times but different attack times), you can substract them and then use the difference ..." (search-result description, truncated; spelling as in the source) — [Cabbage Audio forum](https://forum.cabbageaudio.com/t/transient-shaper-algorithm/3631) [forum/anecdotal]
- "The basics are simple: use fast attack and slow attack envelope followers in parallel and estimate difference between the two." — [KVR Audio forum](https://www.kvraudio.com/forum/viewtopic.php?t=466276) [forum/anecdotal; excerpt]
- "A transient shaper's detector is essentially 2 envelope followers - one with a fast response and one with a slow response." — [Reddit r/audioengineering](https://www.reddit.com/r/audioengineering/comments/t3ek56/transient_shaper_vs_compressor/) [forum/anecdotal]

### Inferences
- A level-independent gain is obtained by normalizing the envelope difference by the slow envelope, or by working in the log domain. The sevagh normalization to [0, 1] is the only one named, and its formula is not in the excerpt. This is my inference.
- Cost is two one-pole followers per band. The 24-band design multiplies this by 24. A full-band version is much cheaper. This is my estimate.

### Gaps
- No peer-reviewed analysis of transient shapers was retrieved.
- The exact normalization and gain-mapping curve in the sevagh code were not confirmed.
- Commercial SPL-style internal algorithm: unknown. Do not assert its structure without a primary source.
- Artifacts (pre-echo, sibilance, release-time coupling) are not sourced.
- Other open implementations (LSP, Airwindows) were not reviewed.

## 8. Dynamic EQ and split-band de-esser (sidechain bandpass, gain-controlled bell/shelf, relative vs absolute threshold, phase-preserving split)

### Takeaway
Available sources are practitioner forums and vendor blogs. The consistent pattern is a bandpass sidechain feeding a compressor, applied either to a gain-controlled band or as a split-band gain above a frequency. No source for relative versus absolute threshold, or for phase behaviour of the split, was retrieved.

### Cited Findings
- "Use a Band-Pass Filter to create the key signal and output that into a separate compressor." — [Reddit r/QSYS](https://www.reddit.com/r/QSYS/comments/1aqpwg3/making_a_deesser_using_side_chain_on_compressor/) [forum/anecdotal]
- "Many de-essers have split-band modes, which reduce sibilance above a selected frequency, leaving the rest of the signal intact." — [iZotope blog, The dos and don'ts of de-essing](https://www.izotope.com/community/blog/the-dos-and-donts-of-de-essing) [vendor blog]
- "A set of filters that splits a signal into separate frequency bands for independent processing." — [Beat Kitchen, Multiband, De-Esser, Dynamic EQ, and Sidechain](https://beatkitchen.io/guides/mix-primer/20-multiband-deesser-sidechain/) [educational web]
- Gain-controlled bell via a trapezoidal SVF: A = 10^(dB/40), k = 1/(Q·A), m1 = k(A² − 1), m2 = 0 for a bell. The paper states that morphing between responses with interpolation stays stable under audio-rate modulation, with aliasing if sidebands exceed Nyquist. — [Cytomic, Solving the continuous SVF equations (Simper)](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf) [primary DSP paper; not peer-reviewed]

### Inferences
- A dynamic EQ band can be built as a sidechain detector (bandpass or peak-filter SVF), then a compressor or expander, with the resulting gain applied through the Cytomic bell coefficients (m1 changes with gain). The architecture is my synthesis from the sources above.
- A split-band de-esser needs a complementary crossover pair. Whether the summed bands are phase-linear depends on the filter pair, and this is not sourced here.
- Relative threshold (tracking the band's own level, or the full-band level) versus absolute threshold (fixed dBFS) is a design choice. No source distinguishes them.

### Gaps
- Definitions and implementations of relative and absolute thresholds.
- Phase-preserving split design (all-pass compensated or matched-phase crossover). No source retrieved.
- Sidechain bandpass Q and order choices for de-essing.
- Code-level references (FabFilter, TDR, LSP, x42, Airwindows DeEss) were not reviewed.
- No peer-reviewed source for dynamic EQ or split-band de-essing.

## 9. HPF/LPF design with resonance (Butterworth 12/18/24 dB/oct cascades, resonant SVF Q mapping, one-pole TPT, click-free order switching)

### Takeaway
A Butterworth cascade has closed-form stage Q values: Q_k = 1/(2·cos θ_k), where θ_k is the pole angle measured from the negative real axis. The source gives this formula and the order 2 and 4 values. The order 6 and 8 values below are my calculations from the same formula. The trapezoidal SVF uses k = 1/Q.

### Cited Findings
- "This calculator gives the Q values for each filter to achieve Butterworth response for lowpass and highpass filters." The formula is "For a given pole angle, θ, Q is 1 / (2cos(θ))," with poles spaced π/n. Stated values: order 2 Q = 0.707107; order 4 Q = 0.541196 and 1.306563. — [EarLevel Engineering, Cascading filters](https://www.earlevel.com/main/2016/09/29/cascading-filters/) [web/practitioner]
- "The exact Q you want for a 'maximally flat' 2-pole filter (known as Butterworth) is sqrt(.5) which is about 0.707." — [KVR Audio forum, Calculating Q factor for cascading biquads](https://www.kvraudio.com/forum/viewtopic.php?t=470183) [forum/anecdotal]
- Trapezoidal SVF: k = 1/Q (or k = 2 − 2·res in the paper's resonance form). LP = v2, BP = v1, HP = v0 − k·v1 − v2. — [Cytomic / Simper](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf) [primary DSP paper]

### Inferences
- Derived with the EarLevel formula and checked against its stated order 4 values, which match:
  - Order 6 (36 dB/oct): Q = 0.517638, 0.707107, 1.931852.
  - Order 8 (48 dB/oct): Q = 0.509796, 0.601345, 0.899976, 2.562915.
  - Order 3 (18 dB/oct): one first-order section plus one biquad with Q = 1.0, from θ = 60° in the same formula. This is derived, not extracted from a source.
- Mapping to the requested slopes: 12 dB/oct is order 2 (one biquad, Q = 0.707); 18 dB/oct is order 3; 24 dB/oct is order 4 (two biquads, Q = 0.541 and 1.307).
- For a resonant knob on a 2nd-order slope, the Cytomic k = 1/Q mapping applies directly. For cascades, each biquad takes its own Q from the table above.
- Click-free order switching is not sourced. A plausible approach is to run both structures and crossfade over a short ramp. This is my suggestion, not verified.

### Gaps
- One-pole TPT (6 dB/oct) coefficients and derivation: not retrieved.
- Click-free order switching: no source.
- Order 3 Q value was derived, not extracted from a source.
- The user-facing resonance-to-Q curve (perceptual mapping) has no source.
- No peer-reviewed filter-design source retrieved in this pass.

## 10. Parameter and coefficient smoothing (one-pole vs linear, JUCE SmoothedValue, per-sample vs per-N-sample updates, SVF vs DF1/DF2 under modulation)

### Takeaway
JUCE SmoothedValue uses linear ramps by default, with the ramp length set by reset(). skip(n) is equivalent to calling getNextValue() n times. Cytomic states that the trapezoidal SVF's interpolated morphing is stable under audio-rate modulation, with aliasing above Nyquist. Per-N-sample update costs and DF1 vs DF2 behaviour under modulation were not sourced.

### Cited Findings
- "A ValueSmoothingTypes::Linear template parameter selects linear smoothing, which increments the SmoothedValue linearly towards its target value." Linear is the default. — [JUCE docs, SmoothedValue](https://docs.juce.com/master/classjuce_1_1SmoothedValue.html) [vendor doc]
- reset(sampleRate, rampLengthInSeconds) and reset(numSteps) set the ramp length. setTargetValue() sets the target. getNextValue() advances one sample. skip(numSamples) is "identical to calling getNextValue numSamples times." setCurrentAndTargetValue() jumps without a ramp. — same source.
- The docs do not state a default ramp time, and do not say what happens before reset() is called. — same source.
- Morphing between SVF responses with interpolation stays stable under audio-rate modulation. Aliasing appears if modulation sidebands exceed Nyquist. — [Cytomic / Simper](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf) [primary DSP paper]

### Inferences
- Smooth the user-facing parameters (frequency, Q, gain) in a suitable domain, then recompute the SVF constants. Because g = tan(π·fc/fs) is the expensive term, recompute g per block or per N samples, not per sample. This is my inference from the Cytomic constants.
- Linear ramping on frequency in Hz is not perceptually even. Ramping in log frequency or in g is a reasonable choice. This is my inference and is not sourced.
- The Cytomic claim supports using the trapezoidal SVF for modulated filters. A DF1 vs DF2 comparison under modulation was not retrieved.

### Gaps
- Per-N-sample update error bounds and zipper-noise thresholds.
- DF1 vs DF2 under modulation (no source).
- Exponential versus linear smoothing comparison (no source).
- JUCE SmoothedValue behaviour before reset().

## 11. VU and PPM metering (IEC 60268-17, IEC 60268-10)

### Takeaway
VU is an averaged meter with 300 ms ballistics under IEC 60268-17. PPM is a quasi-peak meter with 5 ms integration (Type I) or 10 ms (Type II), and the Type II return is 24 dB in 2.8 s. The exact tolerance and overshoot specifications were not retrieved from the standards.

### Cited Findings
- "Standard VU (Volume Unit) IEC 60268-17. Ballistics (Attack): 300 ms." — [like.audio, Reference of Audio Metering](https://like.audio/20260203/reference-of-audio-metering/) [web compilation]
- A Type I PPM has an integration time of 5 ms and a Type II PPM has 10 ms. Type II PPMs "fall back 24dB in 2.8 seconds." — [Wikipedia, Peak programme meter](https://en.wikipedia.org/wiki/Peak_programme_meter) [encyclopedia; cites IEC 60268-10]
- PPM instruments have "typical return times ... 1,5 to 3,0 second/24dB decay" for Type I. — [proAV, Level Meters](http://www.proav.de/audio/meters.html) [web; excerpt garbled]
- "A standard PPM has a 5ms integration time ... These requirements are satisfied by an attack time constant of 1.7ms." — [Elliott Sound Products, VU and PPM](https://sound-au.com/project55.htm) [technical web]
- "The VU meter shows an averaged signal level and gives an impression of perceived loudness, while a PPM indicates something closer to the peak." — [Sound On Sound, PPM vs VU](https://www.soundonsound.com/sound-advice/q-whats-difference-between-ppm-and-vu-meters) [trade press]

### Inferences
- A 24 dB decay in 2.8 s corresponds to a one-pole release with τ ≈ 1.0 s, since e^(−2.8/τ) = 10^(−24/20) gives τ = 2.8/ln(15.85) ≈ 1.01 s. This is my calculation. A real PPM is not a one-pole filter, so this is an approximation only.
- A one-pole filter with τ = 300 ms is not the IEC 60268-17 VU response. The standard defines overshoot and rise characteristics that were not retrieved. This is my inference.

### Gaps
- The text of IEC 60268-17 and IEC 60268-10 was not retrieved. Overshoot, rise, tolerance, and scale specifications are unsourced.
- Digital reference alignment for VU (dBFS equivalent) is not sourced in this pass.

## 12. LUFS and true-peak metering (K-weighting, gating, EBU R128 momentary/short-term/integrated, BS.1770-5 true-peak with 4x oversampling)

### Takeaway
Primary sources were verified for this section: ITU-R BS.1770-5 for K-weighting, gating, and the true-peak annex, and EBU Tech 3341 for EBU Mode window lengths. The earlier 192 kHz claim is confirmed, but it is narrower than the earlier notes stated.

### Cited Findings
- Loudness: L_K = −0.691 + 10·log10(Σ_i G_i·z_i), summed over channels L, R, C, Ls, Rs. The LFE channel is excluded from the channel-weighted sum. — [ITU-R BS.1770-5 (11/2023)](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf) [standard, Eq. (2)]
- Gating: blocks of 400 ms with 75% overlap. Two thresholds: an absolute gate at −70 LKFS, then a relative gate 10 dB below the level after the first gate. — same source.
- K-weighting stage 1 (head-related pre-filter), at 48 kHz: b0 = 1.53512485958697, b1 = −2.69169618940638, b2 = 1.19839281085285, a1 = −1.69065929318241, a2 = 0.73248077421585. Stage 2 (RLB high-pass), at 48 kHz: b0 = 1.0, b1 = −2.0, b2 = 1.0, a1 = −1.99004745483398, a2 = 0.99007225036621. — same source (Tables 1 and 2).
- For other sample rates, coefficients "should be chosen to provide the same frequency response that the specified filter provides at 48 kHz." The standard says the algorithm "is not sensitive to small variations in these coefficients." — same source.
- True-peak annex: 12.04 dB attenuation (2-bit shift), then 4× oversampling from 48 kHz to 192 kHz using a 4-phase FIR interpolator of order 48 (tabulated coefficients in the annex), then absolute value, then 20·log10 plus 12.04 dB. — [ITU-R BS.1770-5, Annex 2](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf) [standard]
- "Meters that follow these guidelines, and that use an oversampled sampling rate of at least 192 kHz, should indicate the result in the units of dB TP." For a 96 kHz input, "a 2 over-sampling would be sufficient." "Higher sampling rates and over-sampling ratios are preferred." — same source.
- EBU Mode: momentary is a 0.4 s sliding rectangular window (ungated, no extra attack or release smoothing). Short-term is a 3 s sliding rectangular window, updated at least every 100 ms for live meters. Integrated uses BS.1770 gating. Calibration: a 1 kHz stereo in-phase sine at −18 dBFS peak should read −18.0 LUFS. — [EBU Tech 3341](https://tech.ebu.ch/docs/tech/tech3341.pdf) [standard]
- EBU Tech 3341 true-peak tests 15–23 include an fs/4 test at 0.5 full-scale, which should read −6 dBTP within ±0.2 or −0.4. — same source.
- libebur128 (C library): MIT license, "A library implementing the EBU R128 loudness standard." Features: true-peak scanning; momentary, short-term, and integrated modes; loudness range per EBU TECH 3342; coefficients recalculated for any sample rate. Its page does not document the true-peak oversampling factor. — [libebur128 README](https://github.com/jiixyj/libebur128) [OSS repo]
- Secondary: K-weighting is described as "a low-frequency cut-off at 80 Hz, a shelf between 100 Hz and 1000 Hz." — [AES Loudness Project, Learn more: peak metering](https://aes.org/resources/audio-topics/loudness-project/learn-more/) [secondary]

### Inferences
- Per channel, the 4× true-peak path is a 48-tap polyphase FIR. Each input sample produces four outputs, each from 12 taps, so about 48 multiply-accumulates per input sample per channel. This is my derivation from the annex.
- At a 96 kHz host, a 2× stage is enough per the annex. At 192 kHz or higher, the annex's implied minimum rate is already met, so the true-peak stage may be bypassed or set to 1×. This is my inference and is not stated explicitly in the standard.
- The K-weighting coefficients above are for 48 kHz. A 44.1 kHz plugin must recompute them from the analog prototype. The standard only says the response must match, not how to compute it.
- EBU momentary (0.4 s) and BS.1770 gating blocks (400 ms, 75% overlap, so a 100 ms hop) share the same window length but have different update schedules. This is derived from the cited parameters.
- Earlier notes said BS.1770-5 "requires 192 kHz for oversampling." The standard instead requires at least 192 kHz for meters that indicate dB TP, and uses 4× from 48 kHz as the reference case.

### Gaps
- The integrated-loudness memory and real-time gating implementation details beyond the standard's definitions.
- Multichannel surround weighting (not needed for a stereo channel strip).
- True-peak FIR coefficient verification against an independent implementation. The annex coefficients were read directly, but not tested.

## 13. Gain-reduction meter conventions, lock-free FIFO metering off the audio thread, correlation meter

### Takeaway
No source retrieved for these three items. The only sourced fact is the static gain-reduction quantity defined by the compressor curve.

### Cited Findings
- Static amount of compression: x_L[n] = x_G[n] − y_G[n], expressed in dB. — [Giannoulis et al., JAES 2013](https://joshreiss.github.io/documents/2013/Giannoulis%20Massberg%20Reiss%20-%20dynamic%20range%20compression%20automation%20-%20JAES%202013.pdf) [peer-reviewed; OCR excerpt]

### Inferences
- A GR meter can display the smoothed x_L in dB after the same ballistics as the gain path, or with separate display ballistics. This is my suggestion. Under stereo linking, the displayed value should be the linked value.

### Gaps
- Standard GR meter ballistics and display conventions (no source).
- Lock-free SPSC FIFO design for audio-to-UI metering (no source retrieved).
- Correlation meter definition and normalization (no source retrieved).

## 14. Cheap math approximations (fast exp2/log2/dB, fast tanh Pade/rational with error and speed, log-domain gain per sample, SIMD)

### Takeaway
Schraudolph's bit-trick exp is documented at relative error below 3%. Rational and Pade tanh approximations are listed with formulas, but no error or speed figures were found in the sources read. JUCE ships a Pade-based tanh in its FastMathApproximations. Forum posts warn that reciprocal instructions raise the noise floor.

### Cited Findings
- "These functions return an approximation of exp(x) with a relative error <3%. The implementation uses a method by Nicole Schraudolph." — [Fast approximate exponential function gist](https://gist.github.com/jrade/293a73f89dfef51da6522428c857802d) [OSS code]
- The Schraudolph method "manipulat[es] the components of a standard (IEEE-754) computer memory." — [Schraudolph, A Fast, Compact Approximation of the Exponential Function (PubMed record)](https://pubmed.ncbi.nlm.nih.gov/10226185/) [peer-reviewed; abstract only]
- A rational tanh approximation tanh(x) ≈ x(27 + x²)/(27 + 9x²) with "tweaked coefficients." — [Mathr, Approximating hyperbolic tangent](https://mathr.co.uk/blog/2017-09-06_approximating_hyperbolic_tangent.html) [blog; quotes the musicdsp archive]
- The mathr page lists Pade approximants [1/2] through [7/8] with explicit formulas, and gives no error or speed figures. The author says they will "probably end up using the [5/6]." — same source.
- The musicdsp archive page carries a comment calling it "the fastest tanh() approximation in the archive so far," and another saying the reciprocal instruction "increases the noise floor somewhat, giving a quantized sound." — [musicdsp, Rational tanh approximation](https://www.musicdsp.org/en/latest/Other/238-rational-tanh-approximation.html) [archive with forum comments; anecdotal]
- JUCE's FastMathApproximations provides a tanh "using a Pade approximant continued fraction, calculated sample by sample," described as an approximation. — [JUCE docs, FastMathApproximations](https://docs.juce.com/master/structjuce_1_1dsp_1_1FastMathApproximations.html) [vendor doc]
- A KVR forum thread notes that an approximate tanh "will definitely produce some" aliasing, even though the true tanh produces close to none for small inputs. — [KVR Audio forum, Fast TANH approximation](https://www.kvraudio.com/forum/viewtopic.php?t=388650&start=15) [forum/anecdotal]
- A Math Stack Exchange answer claims an approximation that is "x4 times faster than built in tanh(), with only 0.0001% error on all range." — [Math Stack Exchange, Rapid approximation of tanh(x)](https://math.stackexchange.com/questions/107292/rapid-approximation-of-tanhx) [Q&A; anecdotal, unverified]

### Inferences
- The Pade forms [5/6] and [7/6] are candidates for a channel-strip saturator. Their maximum error on the working range and their cost must be measured on target hardware. Not done in this pass.
- Per-sample exp is not needed for the Section 3 envelopes. The alpha coefficients change only when a time constant changes. The per-sample work is multiply-accumulate. This follows from Eqs. (5) and (6).
- Log-domain gain: compute x_G = 20·log10|g| per sample. A fast log2 approximation would replace this, but none was sourced.

### Gaps
- Error and speed tables for fast log2 and dB conversion.
- Measured maximum error for the Pade or rational tanh forms over a stated range.
- SIMD throughput figures for any approximation.
- The Math Stack Exchange "0.0001% error" claim is unverified and should not be used without measurement.

## 15. Open-source references and licences (chowdsp_utils, Faust compressors.lib, LSP, IEM, x42, Airwindows, JUCE dsp, libebur128)

### Takeaway
Two licences were verified in this pass: libebur128 (MIT) and the sevagh transient-shaper repository (MIT, per its sidebar). The remaining libraries were not checked and are listed as gaps.

### Cited Findings
- libebur128: "All source code is licensed under the MIT license. See COPYING file for details." — [libebur128 README](https://github.com/jiixyj/libebur128) [OSS repo; COPYING file not read]
- sevagh/multiband-transient-shaper: MIT license link in repository sidebar. — [sevagh/multiband-transient-shaper](https://github.com/sevagh/multiband-transient-shaper) [OSS repo; LICENSE text not read]
- The 2013 Giannoulis paper says its compressor designs and analysis are "available as VST plug-ins" from c4dm.eecs.qmul.ac.uk/audioengineering/compressors/. Licence not checked. — [Giannoulis et al., JAES 2013](https://joshreiss.github.io/documents/2013/Giannoulis%20Massberg%20Reiss%20-%20dynamic%20range%20compression%20automation%20-%20JAES%202013.pdf) [peer-reviewed; footnote]
- JUCE provides dsp::FastMathApproximations (see Section 14). Licence not checked in this pass.

### Inferences
- Any code reference for this plugin should be checked at the file level. Licences are not uniform across modules within one library, and the excerpt above does not establish any of the remaining licences.

### Gaps
- chowdsp_utils (per-module licence), Faust compressors.lib, LSP DSP, IEM plug-in suite, x42, Airwindows (the earlier notes claim MIT; not checked here), JUCE dual licence terms, and the COPYING text for libebur128.
- Giannoulis analysis VST source code: licence unknown.

#### Status of earlier unverified claims
- Soft-knee formula: now supported by the 2013 excerpt (Section 1). Boundary check passed. Transcription is OCR-based.
- α = exp(−ln9/(t·f_s)): not in the sources. The Giannoulis convention is exp(−1/(τ·f_s)). The ln 9 form is a 10–90% definition (Section 3).
- Branching and decoupled peak detectors: named in a thesis abstract only. Not defined (Section 2).
- Schraudolph exp: exists. Relative error below 3% per the gist (Section 14).
- JUCE dsp::Compressor and dsp::Limiter API: not checked. Gap.
- ADAA tanh antiderivative log(cosh x): not checked in this pass. Gap.
- 192 kHz claim: restated. The standard requires at least 192 kHz for meters that indicate dB TP, and uses 4× from 48 kHz as reference (Section 12).
- Compressor sourcing: upgraded from secondary to primary for the 2013 paper. The 2012 tutorial text is still unretrieved.
- Cytomic SVF PDF URL: fetched live on 2026-10-10 (HTTP 200, 32 pages, Simper, 2013, corrected 2016). The bell, low-pass, band-pass and high-pass equations quoted in Sections 8 and 9 match the code in that PDF.
- Order-8 Butterworth Q1 corrected to 0.509796 (= 1/(2·cos(π/16))) in Section 9, both places.
- Look-ahead sentence (Section 1) and the Cabbage transient-shaper quote (Section 7) now quote the source text, including the source spelling "substract".
- Section 4 listening test: the seven-amateur count is legible in the Giannoulis 2013 figure caption. The professional group size is not legible in the excerpt.
