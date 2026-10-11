# Northline Channel: Fix, Polish and Upgrade Roadmap

Status: plan of record from 2026-10-11. It supersedes the execution sections of [PLAN.md](PLAN.md); PLAN.md stays as the research rationale.
Inputs: the 2026-10-11 audit (five audible defects reproduced, see Phase 1), [research/](research/), and the current code at commit `45a58d9`.

---

## 0. Summary

Northline today is a sound, real-time-safe engine wrapped around a product that is not finished. It has five reproduced audible defects, gain staging that makes processed audio sound "better" by making it louder, a dense UI with no hierarchy, and none of the release engineering a commercial plugin needs. This roadmap takes it to a 1.0 that can credibly claim to be the best channel strip in its class. It runs in six phases over roughly **24 calendar weeks with two engineers**:

| Phase | Weeks | Outcome |
|---|---|---|
| P0 Baseline | 1–2 | Measurement harness, competitor CPU numbers, red tests for every known defect |
| P1 Stabilize | 2–5 | Every audit defect fixed and guarded by a test; thread-safe state; consistent latency rules |
| P2 DSP excellence | 5–12 | Better anti-aliasing, calibrated gain staging, true-peak limiter, real expander, dynamic EQ, decramped EQ |
| P3 UX redesign | 5–13 (parallel) | Hierarchy, value entry, collapsible modules, drag-reorder, scopes, accessibility |
| P4 Differentiators | 12–18 | Smart Start, Instance Hub, a library of 150+ presets, Δ-listen, level-honest Compare |
| P5 Ship | 16–24 | macOS, ARM64, CLAP, signing, installers, licensing, docs, beta, 1.0 |
| P6 Post-launch | 24+ | AAX, insert slot, linear-phase EQ, more characters, controller support |

The three bets that make it "the best", not just "complete":
1. **Level-honest everything.** Every drive, switch, preset and A/B comparison is loudness-calibrated, so the engineer judges tone instead of volume. No competitor makes this a guarantee.
2. **Zero latency by default without the usual quality tax.** Eco mode uses properly built anti-aliasing (ADAA). An optional HQ mode reports its latency honestly. Latency only changes when you flip a quality switch.
3. **Faster to a good result.** Smart Start analyses the signal and sets a starting point, Spotlight guides the next move, scopes show what each module is doing, and the Instance Hub shows the whole mix.

---

## 1. What "best" means: measurable bars

Nothing ships as a claim until it is measured. Each bar below has an owner phase and a test.

| Dimension | Bar | Measured by | Phase |
|---|---|---|---|
| Sound preference | Preferred over Scheps Omni Channel 2 and SSL Native CS2 in a blind, loudness-matched (±0.2 LU) MUSHRA test on ≥3 of 4 source categories, ≥12 engineers | Listening protocol §14.10 | P5 |
| Aliasing (Eco, zero latency) | Alias energy ≤ −40 dB (Odd/Even/Heavy) and ≤ −30 dB (Crush) relative to total, 5 kHz sine, full drive, 48 kHz | `measure alias` | P2 |
| Aliasing (HQ) | ≤ −80 dB (Odd/Even) and ≤ −70 dB (Heavy/Crush) | `measure alias` | P2 |
| Gain staging | Every saturation type and drive: RMS change ≤ ±0.5 dB at the operating level with auto-gain on | `measure staging` | P1/P2 |
| Limiter | True Peak mode output ≤ ceiling + 0.1 dBTP on the test corpus | `measure limiter` | P2 |
| Metering | EBU Tech 3341 and 3342 conformance; within ±0.1 LU of libebur128 on 50 files; offline render equals realtime | `measure loudness` | P1/P2 |
| Determinism | Bit-identical output (or ≤ 1e-6) across block sizes 1–4096 under automation, at 44.1–192 kHz | test suite | P1 |
| CPU | ≤ 50% of Omni Channel 2 per instance in the same host test, or the CPU claim is dropped from marketing; absolute budgets in §15 | `bench`, `host-bench` | P0/P2 |
| Latency | 0 samples by default; latency changes only on non-automatable quality switches | test suite + host matrix | P1 |
| Reliability | pluginval strictness 10, VST3 validator, auval and clap-validator all green; 0 crashes in a 72 h soak; host matrix passes | CI + QA | P5 |
| Usability | SUS ≥ 80; ≥ 90% completion on the core task set; time to an acceptable vocal chain ≤ 3 min | Usability study §8.1 | P3 |

---

## 2. Product principles (the tie-breakers)

1. **Level-honest.** Any control whose purpose is not loudness must not change loudness at the operating level. Loudness changes are explicit (Input, Output, Trim).
2. **Never surprise the bounce.** Monitoring tools (Mono, Side, Listen, Solo) are transient, not saved, and shown with a persistent banner. Nothing adaptive runs in the render unless the user chose it.
3. **Zero latency by default; constant latency always.** Module on/off never changes latency. Only global quality switches (HQ, Lookahead, Limiter mode) do, and those are not automatable.
4. **Deterministic.** The same input and the same automation give the same output at any host block size.
5. **Every control earns its pixels.** Primary controls are big, secondary small, advanced ones in a drawer. Controls that do nothing in the current mode are hidden.
6. **Measured, not claimed.** Marketing numbers come from the published measurement suite, run on a stated machine.

---

## 3. Positioning against Omni Channel 2

| Area | Omni Channel 2 | Northline today | Northline 1.0 |
|---|---|---|---|
| Modules | PRE, GATE, DS², EQ, COMP, INSERT, MASTER | Same, minus INSERT | Same, plus dynamic EQ bands, global Mix; INSERT in P6 |
| Reorder | Drag | Tiny `<` `>` buttons | Drag with animation, undoable |
| Views | Collapsed and expanded | Expanded only | Chain view plus detail view; console layout optional |
| Presets | 250+ artist presets, FOCUS | 15, FOCUS | 150+ curated, Spotlight (user-authorable), module presets, tags and search |
| Metering | Master meters | LUFS/TP/corr (broken lifecycle) | Conformant LUFS/LRA/TP, delivery targets, scopes on every module |
| Level matching | Not documented | Broken AGC | Learn (static) and gated Live; level-honest Compare |
| Anti-aliasing | Undocumented | Weak | Eco ADAA plus 4× HQ, published numbers |
| Limiter | Brickwall | Clipper-like, sample peak | Safety Clip, Brickwall and True Peak modes |
| Formats | VST3/AU/AAX (AAX unverified) | VST3, Standalone (Windows only) | VST3, AU, CLAP, Standalone on Win x64/ARM64 and macOS universal; AAX in P6 |
| Guidance | FOCUS | FOCUS | Smart Start, Spotlight, Δ-listen, help mode |
| Mix-wide | — | — | Instance Hub with track names, loudness overview, groups |

Naming (legal hygiene, §11.6): DS2 → **DE-ESS**, Thump → **Weight**, FOCUS → **Spotlight**, DUO → **DUAL**. No Scheps or Waves names appear in metadata, presets or marketing.

---

## 4. Phase map and gates

```
W1   W2   W3   W4   W5   W6   W7   W8   W9   W10  W11  W12  W13  W14  W15  W16  W17  W18  W19  W20  W21  W22  W23  W24
[P0 ][====P1 stabilize====]
                         [==========P2 DSP excellence==========]
                         [design research][=====P3 UX build=====]
                                                             [=======P4 differentiators=======]
                                                                            [==========P5 ship (beta W18–22)==========]
```

| Gate | Exit criteria |
|---|---|
| P0 → P1 | Baseline report committed (`docs/measurements/baseline.md`); competitor CPU numbers recorded; red tests exist for D1–D5 |
| P1 → P2 | All §6 acceptance tests green; pluginval L8 green on Windows; no known audible defect; state is thread-safe under the stress test |
| P2 → beta | All §1 DSP bars met or waived in writing; golden files regenerated with listening sign-off; CPU budget §15 met |
| P3 → beta | Usability round 2: SUS ≥ 80, task completion ≥ 90%; accessibility checklist passed |
| P4 → beta | Smart Start rated an acceptable start on ≥ 80% of the corpus; Instance Hub works in ≥ 6 hosts |
| Beta → 1.0 | Validators green on every format and OS; host matrix passed; 0 open crash bugs; ≤ 5 open P2 bugs; listening-test criterion met, or the "preferred" claim is removed |

---

## 5. Phase 0: Baseline and instrumentation (weeks 1–2)

The goal is to make quality measurable before changing anything.

| ID | Task | Detail | Output |
|---|---|---|---|
| B1 | Tag current state | `git tag v0.9.0-audit`; versions follow semver; `VERSION` in CMake becomes 0.9.x until release | Tag |
| B2 | Red tests | Port the five audit probes into permanent tests that currently fail (D1–D5 in §6) | `tools/tests/RegressionAudit.cpp` |
| B3 | Measurement harness | New `NorthlineTool measure <suite>` with these suites. `alias`: per type and drive, 100 Hz–10 kHz, 44.1/48/96 kHz. `thd`: THD+N vs level and frequency. `staging`: RMS and K-weighted level change per module setting. `curves`: EQ and filter vs analog prototype. `dynamics`: static curves and measured attack/release. `limiter`: overs, THD. `loudness`: EBU 3341/3342 vectors. `click`: discontinuity energy on switches. Results go to Markdown and CSV. | `tools/Measure.cpp`, `docs/measurements/baseline.md` |
| B4 | Competitor CPU | Run `host-bench` on Omni Channel 2 (if licensed), SSL Native CS2 and one free strip, all default and "everything on", 48/96 kHz at 32/64/128/512 samples, on a recorded machine | `docs/measurements/competitors.md` |
| B5 | Golden-file stimuli | 20 s stimuli (pink noise, log sweep, drum loop, vocal, bass DI), license-clean (self-recorded or CC0) | `tools/stimuli/` |
| B6 | CI skeleton | GitHub Actions, Windows: configure (Ninja), build, `NorthlineTool test`, upload artefacts | `.github/workflows/ci.yml` |
| B7 | Profiling | Visual Studio profiler / Superluminal on the "everything on" config; find p99 outliers (currently up to 5.6× the mean) | `docs/measurements/profile.md` |

---

## 6. Phase 1: Stabilize (weeks 2–5)

Every row is a defect with a fix and an acceptance test. D1–D5 were reproduced in the audit; the measured numbers are in the "today" column.

### 6.1 Reproduced audible defects

| ID | Defect (today) | Fix | Files | Acceptance |
|---|---|---|---|---|
| **D1** | Level Match is a 1.5 s unweighted AGC printed into the render. After a 15 s gated pause, a −20 dBFS tone re-enters at **+3.9 dBFS**; gated noise is raised 24 dB | Replace with **Level Match v2**. Modes: *Off*, *Learn* (default when engaged) and *Live*. Measure with K-weighting and BS.1770-style gating: a 400 ms block counts only if input loudness is above max(−60 LUFS, running − 20 LU). *Learn* measures 3–10 s of active signal, then writes a static `match_trim`, so nothing modulates. *Live* updates only on active blocks, slews at ≤ 1 dB/s, holds during silence and clamps to ±12 dB. Applied post-processing, pre-limiter | `ChannelStrip.cpp`, new `dsp/LevelMatch.h`, `Params.cpp` | Re-entry peak error ≤ 0.3 dB; noise floor change ≤ 0.5 dB; Learn within ±0.3 LU on pink noise for ±6 dB tilt and 6 dB of compression; Learn output nulls against a static gain |
| **D2** | The lookahead limiter replays stale audio when re-enabled: a **−6 dBFS burst** into silence. Toggling LIMIT also clicks and changes the reported latency | `reset()` on every off→on transition. Latency follows `lim_mode` only, not `lim_on`. When LIMIT is off in a lookahead mode, the delay line still runs (delayed dry), so timing never shifts. 10 ms crossfade. Ceiling smoothed over 20 ms. `lim_mode` is non-automatable | `Limiter.h`, `ChannelStrip.cpp`, `PluginProcessor.cpp` | Re-enable on silence ≤ −144 dBFS; impulse alignment unchanged by LIMIT toggles; no discontinuity above −60 dBFS on a 100 Hz sine below the ceiling; latency unchanged by LIMIT |
| **D3** | Smoothing advances once per host block of up to 32 samples. At block size 1 a 20 ms ramp becomes ~0.6 ms (−19.9 dB after 2 ms vs −0.7 dB at 512) | Add `ControlClock` (a `uint32_t` phase per strip). Every smoother ticks exactly once per 32 absolute samples, with gain ramps interpolated per 32-sample segment. Applies to in/out gain, match gain, drive, DS output, limiter ceiling and every new smoother | `dsp/Common.h`, `ChannelStrip.cpp`, `PreModule.h`, `DeEsserModule.h` | Block sizes {1, 7, 32, 37, 64, 511, 512, 2048}, random automation of every continuous parameter every 50 ms (events on multiples of 2048): max diff ≤ 1e-6. Step time constant within ±2% of spec at every block size |
| **D4** | Auto gain boosts the dry signal in parallel compression: **+11.25 dB at mix 0%** | Flow becomes `wet = x·g_gr·g_makeup → y = dry + (wet − dry)·mix → y·g_out`. The auto-gain reference moves to the operating level (§7.1) | `CompModule.h` | Mix 0% nulls with input (≤ −120 dB); dry contribution unscaled at every mix |
| **D5** | Saturation compensation only holds at −12 dBFS peaks, so Drive is a loudness knob: Odd 100% is +6.7 dB at −18 dBFS; Crush is **+30 dB** at −40 dBFS. At 0% drive, Odd still compresses 0 dBFS peaks by ~2.7 dB | Calibrate numerically. At prepare, build a per-type table `comp[driveDb]` (0.25 dB steps) from the output RMS of a sine at the operating level (−18 dBFS RMS by default), with K-weighted calibration for Crush because of its pre-emphasis. 0% drive maps to −12 dB pre-gain with matching post-gain, so THD at the operating level is ≤ −70 dB. Auto-gain switch, default on | `Saturation.h`, `PreModule.h` | ΔRMS ≤ ±0.5 dB at the operating level for every type and drive (100 Hz/1 kHz/5 kHz sines and pink noise); 0% drive small-signal gain 0 ± 0.1 dB |

### 6.2 Other correctness and robustness fixes

| ID | Issue | Fix | Acceptance |
|---|---|---|---|
| D6 | The loudness meter lives in the editor: integrated LUFS resets on window close, nothing is measured while closed, and offline renders overflow the 0.75 s ring | A `MeterHub` owned by the processor. K-weighting (2 biquads/ch) and 4× true-peak (12 taps/phase) run on the audio thread. Hop energies (100 ms) feed a fixed-size histogram on the audio thread (O(1000) per hop, no allocation). Snapshots are published by seqlock. Options: pause integration while the transport is stopped; reset on play. The sample stream remains only for UI analysers (lossy is fine) | EBU 3341 cases pass; integrated LUFS and TP identical between a 100× offline render and realtime; closing the editor does not reset |
| D7 | State races: get/setStateInformation touch the undo stack, A/B slots, focus and preset name while the UI reads them; `prepare` reallocates the SPSC rings under a live consumer | `stateLock` (`juce::CriticalSection`) for all non-parameter state, never taken on the audio thread. UI reads copies. Rings allocated once in the constructor for the worst case (384 kHz); `prepare` resets through a generation counter the consumer checks | Clang ThreadSanitizer build (macOS/Linux): 10 min of editor timer plus random get/setState from another thread plus `processBlock`: zero reports |
| D8 | Signed `int` sample counters overflow after 2³¹ samples (6.2 h at 96 kHz): undefined behaviour | Keep only the phase (`uint32_t phase % kControlBlock`) | Counters seeded near 2³¹ and 2³² render identically to fresh ones |
| D9 | The compressor hides its real behaviour: FET attack is ×0.5, OPT attack is floored at 5 ms, opto memory decays per sample (sample-rate and channel-count dependent) | Remove the hidden scaling. Where a type needs a different range, the displayed value is the effective value, with a tooltip. Opto memory runs on a time constant, once per frame, and is reset in `reset()` | Measured attack (10→90% GR on a 10 dB step) within ±10% of the displayed value for every type at 44.1/48/96/192 kHz |
| D10 | Discrete switches click: EQ band type and target, ST/MS mode on every module, comp type, gate type, DS band type | `ShadowCrossfade`: each module has one preallocated shadow instance. On a discrete change the old configuration keeps running in the shadow for 15 ms and is crossfaded out. CPU rises only during the fade | Any discrete switch during a 100 Hz or 1 kHz sine: splatter ≤ −60 dB re signal in a 50 ms window; no derivative step > 2× the signal's own |
| D11 | Monitor and listen are saved into the render and the session; "Mono" and "Mid" are duplicates; monitor M/S uses 0.5 while the engine uses 0.707 | `monitor` and every `*_listen` become non-automatable and transient: excluded from presets, A/B and undo, reset on load. An amber banner names the active monitor mode. Options: Stereo, Mono, Left, Right, Side, Swap | A session saved while soloed reloads in Stereo with listens off |
| D12 | DS output smoothing is block-local (`i % kControlBlock`) | Covered by D3 | D3 test |
| D13 | EQ display assumes 48 kHz and shows one summed curve in M/S | Use the session rate; draw Mid and Side curves separately, plus the combined L/R result | Display error ≤ 0.1 dB vs the DSP at 44.1/96 kHz |
| D14 | A de-esser band re-enabled with stale filter state; the expander jumps from 0 to −hyst·(ratio−1) at close | Reset the band on enable; continuous expander law (§7.4) | Click test (D10) |
| D15 | Tests too loose to catch any of the above (stress bound "< 1000" passes at +54 dBFS; determinism test changes no parameters; alias check relative only) | Bounds derived from the gain structure; absolute alias thresholds; impulse at exact sample index; automation in the determinism test | All D1–D14 guarded by tests that fail against `v0.9.0-audit` |

---

## 7. Phase 2: DSP excellence (weeks 5–12)

### 7.1 Global engine features

| Feature | Design | Notes |
|---|---|---|
| **Operating Level** | `vu_ref` becomes "Operating Level" (−24…−10 dBFS RMS, default −18). It is the calibration point for saturation auto-gain, compressor auto-gain, VU 0, Level Match Learn and Smart Start targets | One concept the user sets once |
| **Quality switch** | Global, non-automatable. *Eco*: 1×, ADAA, 0 latency. *HQ*: 4× polyphase FIR half-band oversampling (linear phase) around each nonlinear stage (saturation, FET colour, Safety Clip); latency reported as an integer | Rejected: switching automatically to HQ in offline render, because latency would differ from playback |
| **Lookahead switch** | Global maximum lookahead: Off, 2, 5 or 10 ms, non-automatable. Gate, compressor and de-esser lookahead knobs work within it. Reported latency = the selected maximum, constant whether modules are on or off | The same pattern Pro-C 3 uses (a global maximum lookahead in the bottom bar) |
| **LatencyManager** | Sums module latencies, delays dry and bypass paths internally so module toggles never shift timing, and calls `setLatencySamples` on the message thread when a quality switch changes | Also used by the global Mix |
| **Global Mix** | Whole-strip dry/wet, latency-aligned | Parallel processing without a send |
| **Input section** | Polarity L, R or both; channel swap; mono-in/stereo-out bus layout; a dedicated mono version | Omni has a mono component |
| **Parameter dirty flags** | APVTS listeners set an atomic bitmask; the audio thread recomputes coefficients only for dirty modules | Removes per-block `tan`/`pow`/`exp` for every module (§15) |

### 7.2 PRE: saturation, filters, Weight

| Item | Design | Target |
|---|---|---|
| ADAA fix | Remove the post de-emphasis (ρ = 0.7 boosts aliases by up to +15 dB near Nyquist). Put the droop correction *before* the shaper as a gentle high-shelf pre-emphasis, so ADAA's own (1+z⁻¹)/2 response acts as the matching de-emphasis. Evaluate ADAA-2 for the smooth shapers (Odd, Even) and keep it where measurements justify it | Eco ≤ −40/−30 dB, HQ ≤ −80/−70 dB (§1); passband flat ±0.3 dB to 16 kHz |
| Calibrated auto-gain | D5 tables, referenced to the Operating Level | ±0.5 dB |
| New controls | Sat **Mix** (parallel), **Tone** (pre/post tilt: steer harmonics toward lows or highs), **Auto-gain** switch | — |
| New characters (P2b) | *Tape*: soft knee, level-dependent high-frequency loss (dynamic shelf), mild head bump. *Tube*: asymmetric curve whose bias drifts with signal level (an envelope-driven bias). *Transformer*: low-frequency-weighted saturation through pre/post emphasis. Generic names, with published harmonic profiles | Each documented in `docs/dsp/saturation.md` with THD spectra |
| Filters | Add 36 and 48 dB/oct (cascaded SVF Butterworth sections), resonance on every slope, the existing slope crossfade kept | Response within ±0.1 dB of the Butterworth prototype in the passband |
| Weight (was Thump) | Adjustable frequency (40–120 Hz), amount (+2/+4/+6 dB), and a "Track HP" option that places the bump half an octave above the HP cutoff | — |

### 7.3 EQ

| Item | Design | Target |
|---|---|---|
| Decramping | Static bands use a magnitude-matched second-order design (Vicanek, *Matched Second Order Digital Filters*), mapped onto the SVF's g/k/m coefficients so automation safety is kept | ≤ 0.5 dB from the analog prototype up to 0.9·Nyquist at 44.1/48 kHz |
| Curve character | *Clean* (constant Q) and *Proportional* (Q narrows with gain, a console feel) per band | — |
| **Dynamic bands** | Per band: Dyn on/off, Threshold, Range (±, so it can cut or boost), Speed (attack/release pairs), sidechain from the band or the external key. Reuses the DE-ESS dynamic-EQ engine | At rest the band nulls (≤ −120 dB); the main differentiator over Omni |
| Band tools | Band solo (band-pass audition), M/S target with per-channel display, Q on the mouse wheel and Alt-drag, right-click menu | — |
| Later (P6) | Linear-phase mode with reported latency | — |

### 7.4 Gate and expander (rewrite)

| Item | Design | Target |
|---|---|---|
| Detector | Sidechain-filtered peak with a hold of at least one period of the sidechain HP frequency (stops LF chatter), plus hysteresis | ≤ 1 open/close cycle on a 40 Hz sine decaying through the threshold, hold 0 |
| Modes | *Gate*; *Expander* (a true downward law: ratio 1.2–8, knee 0–12 dB, range; continuous across the threshold); *Duck* (inverted, for keyed ducking) | Continuous gain law, no jump at close |
| Lookahead | 0 to the global maximum | Kick test: transient energy loss in the first 5 ms ≤ 0.5 dB with 2 ms lookahead |
| Attack shape | Raised-cosine opening in the dB domain, minimum 0.02 ms | No click above −60 dB re signal at opening |
| Display | Scrolling level vs threshold and hysteresis lines, open/closed shading, attenuation meter (−6…−60 dB) | — |

### 7.5 DE-ESS (was DS2)

| Item | Design |
|---|---|
| Modes per band | *Split* (dynamic EQ, as now) and *Wideband* |
| Detection | Absolute and Relative (as now), a **Speed** control (attack/release pairs), optional lookahead within the global maximum |
| Listen | Detector band solo and **Δ** (only what is being removed) |
| Display | Band level vs threshold scope with GR trace |
| Target | At matched GR on test vocals, spectral change outside the band ≤ 0.3 dB in Split mode (no dulling) |

### 7.6 Compressor

| Item | Design | Target |
|---|---|---|
| Knee | 0–24 dB, defaults per type | Static curve within ±0.2 dB of spec |
| Lookahead | 0 to the global maximum | — |
| Auto release | Program-dependent, two time constants (fast for transients, slow for sustained gain reduction) | — |
| Stereo link | 0–100% in ST mode | — |
| Range | Maximum GR limit | — |
| Sidechain | Add an LP filter alongside HP and bell; Listen and Δ | — |
| FET colour | Moved into the ADAA shaper path, oversampled in HQ | Alias target as for Odd |
| Auto-gain | Static makeup at the Operating Level, applied on the wet path only (D4) | Output loudness within ±1 LU for pink noise at the operating level, ratio 1–10, threshold −40…−10 dB |
| Display | Scrolling input, output and GR scope with a draggable threshold line | — |

### 7.7 Limiter and master

| Item | Design | Target |
|---|---|---|
| Modes | *Safety Clip* (0 latency, ADAA soft clipper, oversampled in HQ); *Brickwall* (1.5 ms lookahead, as now); *True Peak* (lookahead plus 4× oversampled peak detection, ceiling in dBTP) | TP mode ≤ ceiling + 0.1 dBTP over the test corpus driven 10 dB into it; 1 kHz sine 3 dB over the ceiling: THD ≤ −40 dB |
| Release | Program-dependent, two stages, with the Release knob as the base time | — |
| Overs | Over counter and a 10 s GR history | — |
| Loudness | Momentary, short-term and integrated LUFS; LRA (EBU Tech 3342); PLR; max momentary and short-term; TP; delivery targets (Streaming −14, Apple −16, EBU R128 −23, ATSC A/85 −24) with pass/fail colouring | Conformance §1 |
| VU | Real ballistics: rectified average with a second-order critically damped response (300 ms rise, 1–1.5% overshoot), computed at control rate on the audio side | Step response within IEC 60268-17 tolerances |
| Scopes | Correlation plus a vectorscope in the master detail view | — |

---

## 8. Phase 3: UX and UI redesign (weeks 5–13)

### 8.1 Design process
1. **Research (W5–6):** interview 8–10 working engineers (mix, podcast, live). Task analysis of the five most common jobs: vocal chain, drum close-mics, bass, bus glue, dialogue.
2. **Information architecture and wireframes (W6–7)**, then a clickable Figma prototype (W7–8).
3. **Usability rounds (W8 and W11):** 5 users each, think-aloud. Tasks: tame sibilance, gate a tom without clicks, set up parallel compression, match loudness for an honest A/B, reorder the chain, save a preset with Spotlight. Metrics: SUS, completion, time on task, error count.
4. **Design tokens** (colour, type, spacing, radii, motion) in `Source/ui/theme/Tokens.h`, so code and design stay in sync.

### 8.2 Layout

```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ NORTHLINE  ◂ Vocal – Upfront ▸  Save ▾ │ A B A→B │ ↶ ↷ │ ✦ Smart Start │ ◉ Spotlight │ Eco|HQ  LA:Off  0 smp │ ⚙ │
├──────────────────────────────────────────────────────────────────────────┬───────────────────┤
│ CHAIN  [≡ PRE ▣ ▂▂] → [≡ GATE ▢] → [≡ DE-ESS ▣ ▂] → [≡ EQ ▣] → [≡ COMP ▣ ▃▃]   (drag ≡)       │  MASTER           │
├──────────────────────────────────────────────────────────────────────────┤  In/Out meters    │
│ DETAIL: COMP   ST|DUAL|MS   Type: VCA FET OPT SOFT   ● SC ok  [Listen] [Δ] │  with scales      │
│ ┌──────────── scrolling in/out/GR scope, draggable threshold ───────────┐ │  Limiter mode     │
│ └────────────────────────────────────────────────────────────────────────┘ │  Ceiling, GR      │
│   ( THRESH )   ( RATIO )   ( ATTACK )   ( RELEASE )      big primary knobs │  LUFS M/S/I, LRA  │
│   knee  link  range  mix  out  makeup     small secondary knobs            │  TP, targets      │
│   ▸ Sidechain (HP, LP, bell, key)          collapsible drawer              │  Match: Learn ●   │
└──────────────────────────────────────────────────────────────────────────┴───────────────────┘
```

- **Chain view plus detail (default):** collapsed module cards, each with power, name, mode, 2–3 macro values and a GR meter. Clicking a card opens it in a large detail area with its display. One module at a time gets full attention, with large primary controls and an analyser or scope.
- **Console layout (option):** every module expanded side by side, as now but with hierarchy, for wide screens.
- **Drag-and-drop reorder** of cards, with an insertion marker and a 150 ms animation; undoable.
- **Master column** always visible.

### 8.3 Interaction standard (every control)

| Gesture | Action |
|---|---|
| Drag (vertical or horizontal) | Adjust |
| Shift + drag / Shift + wheel | Fine (×0.1) |
| Double-click | Inline text entry with units (`3k`, `-18`, `inf`) |
| Alt-click / Ctrl- or Cmd-click | Reset to default |
| Wheel | Step |
| Right-click | Menu: reset, enter value, copy and paste value, add to Spotlight, link to group (P4) |
| Hover | Value bubble and a descriptive tooltip (what it does, range, shortcut) |
| Tab / arrows / PgUp / PgDn / Space | Keyboard focus, adjust (coarse and fine), toggle; unhandled keys pass through to the DAW |

### 8.4 Visual system
- **Typography:** embed Inter (OFL) with tabular numerals; labels ≥ 11 px and values ≥ 12 px at 100%.
- **Contrast:** text ≥ 4.5:1, UI elements ≥ 3:1. Verify the module accents under deuteranopia and protanopia simulation.
- **Colour semantics:** red is reserved for clip, overs and errors; amber for transient states (monitoring, listen, learning); module accents chosen for distinctness (COMP no longer alarm red).
- **Hierarchy:** primary knobs 56–64 px, secondary 36–40 px, advanced controls in drawers. Contextual hiding: M/S targets only in MS mode, expander controls only in Expander mode.
- **Meters:** every meter has a dB scale. GR meters show ticks at 1/3/6/10/20 dB with the number at the zero end. Peak hold everywhere.
- **Sidechain status:** with EXT on, a green dot when a key signal is present, amber with "no key signal" after 2 s of silence.
- **Debug readouts:** per-module CPU moves to an Info overlay (Settings → Show CPU).
- **Themes:** dark (default) and high contrast.

### 8.5 Visualisations

| Module | Display |
|---|---|
| PRE | Transfer curve at the current drive, live harmonic bars, filter curve |
| GATE | Scrolling level vs threshold and hysteresis, open/closed shading |
| DE-ESS | Band level vs threshold, GR trace, Δ listen |
| EQ | Large curve; pre/post analyser (FFT 4096, 75% overlap, 4.5 dB/oct tilt, peak hold, freeze) on a background thread; Mid/Side curves; Q rings on handles |
| COMP | Scrolling input, output and GR scope; draggable threshold; static curve inset |
| MASTER | Meters, GR history, loudness panel, vectorscope and correlation |

Scope data comes from per-block meter frames, decimated to a fixed 5 ms grid, so it looks the same at any host block size.

### 8.6 Presets, Spotlight, undo
- **Preset browser overlay:** categories, tags (source, style, character), search, favourites, author notes; inline save (name, category, tags, notes); user folder management; the prev/next buttons cover factory and user presets and track the loaded preset's position.
- **Module presets:** save or load one module; copy and paste a module between instances through the clipboard (XML).
- **Spotlight (was FOCUS):** user-authorable. "Edit Spotlight" mode toggles highlighting per control; saved with presets.
- **Undo history menu** with labelled entries ("Comp Threshold −18 → −22 dB"); Ctrl/Cmd+Z when the editor has focus.

### 8.7 Window, accessibility, performance
- Free resize from the corner (75–200%, fixed aspect) plus menu presets. The default size is stored globally (`juce::PropertiesFile` in the user app-data folder), with an "Apply to all instances" option.
- Accessibility: titles, descriptions and values on every control through JUCE `AccessibilityHandler`; tested with NVDA (Windows) and VoiceOver (macOS).
- Onboarding: a four-step first-run coach, plus a "?" help mode with inline explanations.
- Rendering: Direct2D on Windows (present in JUCE 9.0.3); static layers cached with `setBufferedToImage`; meters driven by `VBlankAttachment`; repaint only dirty regions.
- Budgets: editor opens in ≤ 150 ms; UI thread ≤ 4% of one core at 60 fps with every scope visible; ≤ 80 MB per instance with the editor open.

---

## 9. Phase 4: Differentiators (weeks 12–18)

### 9.1 Smart Start (signal-aware starting point)
The user picks a source (Vocal, Kick, Snare, Toms, Bass, Guitar, Keys, Bus, Dialogue) and presses **Listen**. The processor captures 8–15 s into a preallocated buffer; the analysis runs on a background thread. Everything is deterministic and explainable; no ML in v1.

| Step | Method | Sets |
|---|---|---|
| Level | Short-term RMS histogram: program level = 90th percentile, noise floor = 10th | Input trim so the program sits at the Operating Level |
| Low end | YIN pitch tracking on voiced frames → lowest stable f0 | HP at 0.7 × f0_low (clamped 20–150 Hz) |
| Gate | Histogram valley between the noise and program modes | Gate on only if separation > 15 dB; threshold at the valley; hold from note duration |
| Sibilance | 4–10 kHz band energy relative to full band, top 5% of events | DE-ESS threshold for 3–6 dB GR on those events |
| Tone | Long-term spectrum vs the source's target curve, smoothed to 1/3 octave | EQ moves limited to ±3 dB, at most two bands |
| Dynamics | Crest factor and level distribution | Compressor threshold for a median of ~3–4 dB GR on active segments, with the type default per source |

Afterwards Spotlight highlights exactly what Smart Start changed, a summary lists every change, and the whole action is one undo step.
**Acceptance:** on a 30-multitrack evaluation corpus (licensed for internal testing), engineers rate the result an "acceptable starting point" in ≥ 80% of cases.

### 9.2 Instance Hub (mix-wide view)
- An in-process registry (a static singleton with weak references, touched only on the message thread) of every Northline instance. Track name and colour come from `AudioProcessor::updateTrackProperties` where the host provides them.
- **Hub view:** a table of instances with loudness, GR per module, latency and CPU. Click to edit that instance in place. Search and filter.
- **Groups:** link selected parameters across instances with relative offsets (for example, every drum compressor's threshold).
- **Limitation:** hosts that sandbox plugins in separate processes only show same-process instances. This is detected and documented, and the feature is flagged.
- **Acceptance:** works in Live, Reaper, Cubase, Studio One, Logic and FL Studio.

### 9.3 Level-honest Compare and Δ-listen
- **Compare:** momentary bypass of the whole strip, or one module, through the Learn trim, so A/B is loudness-matched.
- **Δ (delta):** per module, hear only what the module adds or removes.

### 9.4 Preset library
- 150+ presets authored with 3–5 working engineers on work-for-hire contracts. Credited in notes only, never in the product or preset names.
- Two families: *Starting points* (gentle, source-specific) and *Character* (obvious colour).
- Every preset has Spotlight controls and a one-line note on what to adjust first.
- **Acceptance:** each preset reviewed at the Operating Level and checked to hit no limiter at typical source levels.

### 9.5 Controller support (stretch)
MIDI learn on every parameter, saved globally. A knob-per-function map for generic controllers.

---

## 10. Architecture refactor (cross-cutting, P1–P2)

| Change | Why | Design |
|---|---|---|
| `ParamSnapshot` + dirty bitmask | `readParams()` recomputes every coefficient every block | The audio thread reads a POD snapshot once per block and recomputes only modules whose dirty bits are set (`std::atomic<uint64_t>[2]`, set by APVTS listeners) |
| Module interface | Uniform latency, reset, shadow and crossfade support | `prepare(Spec)`, `setParams(const Snapshot&)`, `process(Block, Key)`, `latency()`, `reset()`, `cloneInto(Module&)`; static dispatch over the fixed module set |
| `ControlClock` | Determinism (D3) | Strip-level phase; modules query `isTick(i)` |
| `ShadowCrossfade` | Click-free discrete switches (D10) | One preallocated shadow per module |
| `LatencyManager` | Constant latency policy | Sums latencies, delays bypass paths, reports on the message thread |
| `MeterHub` | Metering lifecycle (D6) | Processor-owned; audio-thread accumulators; seqlock snapshots |
| State schema v2 | New parameters, transient monitor, Spotlight, module presets | `stateVersion = 2` with a migration function. Parameter IDs are frozen at beta. After 1.0, any change to the sound gets an algorithm-version parameter so old sessions keep their sound |
| Source layout | Navigability | `Source/dsp/{core,modules,meters}`, `Source/state`, `Source/ui/{components,views,theme}`, `tools/{tests,measure,bench}` |
| Inner loops | CPU | Per-type templated loops instead of per-sample `switch`; 2-lane stereo SIMD (`juce::dsp::SIMDRegister` or xsimd) only where the profiler shows hot spots (SVF banks, detectors) |

---

## 11. Phase 5: Ship (weeks 16–24)

### 11.1 Formats and platforms
| Format | Plan |
|---|---|
| VST3 | Windows x64 and **ARM64** (native), macOS universal (arm64 + x86_64) |
| AU v2 | macOS universal; auval clean |
| CLAP | The JUCE 9.0.3 checkout has no CLAP client. Use `free-audio/clap-juce-extensions` (verify JUCE 9 compatibility in P0) and switch to native CLAP if JUCE ships it |
| Standalone | Keep, for demos and auditioning |
| AAX | P6. Needs an Avid developer account, the AAX SDK and PACE signing |
| LV2/Linux | P6, optional |

Supported sample rates are 44.1–384 kHz, all tested.

### 11.2 CI/CD (GitHub Actions)
| Trigger | Jobs |
|---|---|
| Pull request | Windows and macOS builds; unit, module and integration tests (≤ 5 min); pluginval L5 on VST3 |
| Nightly | Golden files at 44.1/48/96 kHz; conformance suites; 30 min fuzz; pluginval L10 on every format; VST3 validator; auval; clap-validator; 1 h soak; ThreadSanitizer job |
| Release tag | Sign → notarize → package → checksums → draft GitHub release with changelog |

### 11.3 Signing and installers
- **macOS:** Developer ID Application and Installer certificates; `notarytool submit --wait`; `stapler staple`; `spctl` verification in CI.
- **Windows:** Azure Artifact Signing if eligible (individuals: US and Canada only), otherwise an OV certificate on a hardware token or HSM; `signtool` with a timestamp server.
- **Installers:** Inno Setup on Windows (format selection, uninstaller); `pkgbuild` and `productbuild` on macOS with component choices.
- User presets stay in `Documents/Northline Audio/Northline Channel/Presets` so they survive reinstalls.

### 11.4 Licensing and privacy
- Moonbase (JUCE SDK, permanent offline activation), or self-hosted Ed25519-signed license files.
- 21-day full trial; 3 machines; self-service deactivation.
- **Never sabotage audio:** an expired trial passes audio unprocessed and shows a banner. No noise injection, no session corruption.
- No telemetry by default. Opt-in crash reporting with symbol upload.

### 11.5 Documentation
- **User manual** (web and PDF): signal-flow diagram, every control, the latency policy, workflows. Tooltip text is generated from the manual source, so the two stay in sync.
- Six short tutorial videos, each with loudness-matched before/after.
- **Developer docs:** `ARCHITECTURE.md`, `docs/dsp/*.md` (one per module, with measurements), `TESTING.md`, `RELEASE.md`.

### 11.6 Legal and brand
- Apply the renames in §3.
- Trademark search for "Northline" (USPTO and EUIPO) and secure the domain; replace `COMPANY_WEBSITE "https://example.com"`.
- EULA and privacy policy; VST trademark usage rules if the VST name or logo is used.
- JUCE licence: Starter is capped at $20k revenue, so budget the Indie licence before crossing it.
- Comparative marketing only with the published, reproducible measurement method. No competitor or person names in metadata.

### 11.7 Beta and launch
- **Closed beta (W18–22):** 30–50 engineers across the host matrix; weekly builds; in-plugin "Report issue" that attaches the state XML and system info; structured feedback form.
- **Launch:** product page with measurement data, videos, KVR/Gearspace/Plugin Boutique listings, an intro price, free updates within the major version.

---

## 12. Phase 6: Post-launch (v1.1+)

| Version | Items |
|---|---|
| 1.1 | AAX; linear-phase EQ mode; more saturation and compressor characters; MIDI learn and controller maps |
| 1.2 | INSERT slot (VST3/AU hosting): plugins scanned in a child process so a crash during scanning can't take Northline down; latency summed and reported; crash blocklist; state stored as a blob |
| 1.3 | Smart Start v2 (an on-device source classifier, only if v1 data shows value); unmasking in the Instance Hub; LV2/Linux |

---

## 13. Parameter and state changes

Parameter IDs may change freely until beta (there are no users yet). They are frozen at beta.

| Area | Change |
|---|---|
| Master | `vu_ref` → display "Operating Level" (−24…−10). `level_match` → `match_mode` (Off/Learn/Live) + `match_trim`. New: `quality` (Eco/HQ, non-automatable), `lookahead_max` (Off/2/5/10 ms, non-automatable), `global_mix`, `in_polarity` (Off/L/R/Both), `in_swap`. `monitor` becomes transient. `lim_lookahead` → `lim_mode` (Clip/Brickwall/True Peak, non-automatable) |
| PRE | `sat_mix`, `sat_autogain`, `sat_tone`; `sat_type` gains Tape, Tube, Transformer (P2b); slopes gain 36 and 48; `thump_*` → `weight_on`, `weight_freq`, `weight_amt`, `weight_track` |
| GATE | `gate_type` gains Duck; `gate_knee`, `gate_lookahead`; `gate_listen` becomes transient |
| DE-ESS | `ds{n}_mode` (Split/Wide), `ds_speed`, `ds_lookahead`; `ds_listen` gains Δ and becomes transient |
| EQ | Per band: `dyn_on`, `dyn_thresh`, `dyn_range`, `dyn_speed`, `dyn_key`, `character` |
| COMP | `comp_knee`, `comp_lookahead`, `comp_link`, `comp_auto_rel`, `comp_range`, `comp_sc_lp`; `comp_listen` gains Δ and becomes transient |
| State | `stateVersion` 2; Spotlight list per preset; module order; instance group membership; UI layout (chain or console) |

---

## 14. Test and QA strategy

| # | Layer | Contents |
|---|---|---|
| 14.1 | Unit | Filters vs analytic responses, shaper transfer and antiderivatives, detectors, rings, fast math |
| 14.2 | Module conformance | Static curves, measured time constants, null at rest, latency, alias and THD tables (from the `measure` suites, with thresholds) |
| 14.3 | Strip integration | Determinism under automation × block sizes × sample rates; state round-trip and migration; preset loads; click tests on every discrete switch; counter overflow; denormals (CPU flat after loud → silence); NaN/Inf input sanitized |
| 14.4 | Golden files | Every factory preset × stimuli × {44.1, 48, 96} kHz; threshold ≤ −100 dB. An intentional sound change needs regenerated goldens plus a listening sign-off in the PR |
| 14.5 | Metering conformance | EBU Tech 3341 and 3342 vectors; libebur128 comparison on 50 files (±0.1 LU) |
| 14.6 | Fuzz | Parameter fuzz including extremes; state fuzz (corrupt or random XML must not crash); buffer fuzz |
| 14.7 | Concurrency | ThreadSanitizer build plus the stress harness (D7) |
| 14.8 | Validators | pluginval L10, VST3 validator, auval, clap-validator |
| 14.9 | Host matrix | Live, Logic, Cubase/Nuendo, Studio One, Reaper, FL Studio, Bitwig (Pro Tools after AAX). Script per host: insert; automation write and read; sidechain routing; quality and lookahead toggles; offline bounce nulls with realtime; save and reload; preset/A-B/undo; UI scaling; 100 instances; sample-rate change while playing |
| 14.10 | Listening | Blind MUSHRA with hidden reference and anchor, loudness-matched to ±0.2 LU, against Omni Channel 2 and SSL Native CS2. ≥ 12 engineers; stimuli per source; success criteria pre-registered before testing |
| 14.11 | Usability | SUS, task completion and time (§8.1) |

---

## 15. Performance budgets

Measured on the reference machine recorded in P0, stereo, 48 kHz, 64-sample blocks unless noted.

| Configuration | Today | Target |
|---|---|---|
| Default (transparent) | 0.036% | ≤ 0.02% |
| Vocal preset | 0.61% | ≤ 0.40% |
| Everything on, Eco (now including processor-side metering) | 1.03% | ≤ 0.60% |
| Everything on, HQ (4×) | 1.17% (2× IIR, no reported latency) | ≤ 1.5% |
| Everything on, Eco, 96 kHz | 2.10% | ≤ 1.2% |
| p99 / mean block time | up to 5.6× | ≤ 2.0× |
| vs Omni Channel 2 (same host test) | unmeasured | ≤ 50%, or drop the CPU claim |
| UI thread at 60 fps, all scopes | — | ≤ 4% of one core |
| Editor open time / memory | — | ≤ 150 ms / ≤ 80 MB |

The levers, in order: dirty-flag coefficient updates; per-type templated loops; skipping the DE-ESS main-path band filter when not listening; float instead of double ADAA where conditioning allows; SIMD for SVF banks and detectors; metering at hop rate rather than per sample where the standard allows.

---

## 16. Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Scope creep stalls 1.0 | High | High | Gates in §4; cut list in this order: Tape/Tube/Transformer, Instance Hub groups, vectorscope, controller maps |
| DSP rewrites change a sound people liked | Medium | Medium | No users before beta; golden diffs plus listening sign-off |
| Hosts mishandle latency changes | Medium | Medium | Latency only on non-automatable switches; documented; host-matrix test |
| Instance Hub fails in sandboxed hosts | Medium | Low | Graceful degradation, feature flag, documented |
| Smart Start gives bad suggestions | Medium | Medium | Conservative bounds, a change summary, one-step undo, corpus acceptance gate |
| Trademark or trade dress conflict | Medium | High | Renames, legal review, no competitor names |
| CPU targets missed with HQ and metering | Medium | Medium | Profiling in P0, levers in §15, meter cost toggle |
| Windows signing outside the US and Canada | Medium | Low | OV certificate path |
| AAX cost and lead time | High | Medium | Deferred to 1.1 |
| Small-team bandwidth | High | High | Two engineers minimum; contract designer, preset authors and testers; estimates ×1.7 for one developer |
| A competitor ships a new version | Low | Medium | Differentiate on honesty, workflow and measurements, not feature count |

---

## 17. Team and effort

| Role | Load | Phases |
|---|---|---|
| Lead DSP/C++ engineer | 1.0 | P0–P2, P4 (Smart Start), P5 |
| UI/C++ engineer | 1.0 | P1 (D11, D13), P3, P4 (Hub, browser), P5 |
| UX/visual designer (contract) | 0.5 | P3, P4 |
| QA/release engineer | 0.5 | P0 CI, P1, P5 |
| Preset authors (contract) | 3–5 people | P4 |
| Beta testers | 30–50 | P5 |

Effort (person-weeks): P0 3, P1 7, P2 14, P3 12, P4 10, P5 10, for about 56 in total. The critical path is P1 → P2 (saturation, limiter, metering) → beta.

---

## 18. First ten working days

1. Tag `v0.9.0-audit`; create the `dev` branch; turn on CI (B6).
2. Land the red tests for D1–D5 (B2).
3. Quick fixes, about two days: D2 limiter, D4 compressor makeup, D3 `ControlClock`, D8 counters.
4. Build the `measure alias|staging|dynamics|limiter|loudness` suites and commit the baseline report (B3).
5. Run `host-bench` against Omni Channel 2 and SSL Native CS2 if licences are available (B4).
6. Start D1 (Level Match v2) and D6 (MeterHub); they share the K-weighting code.
7. Draft the D7 locking plan and the state schema v2.
8. Schedule the engineer interviews for UX research (P3).
9. Verify that `clap-juce-extensions` builds against JUCE 9.0.3 in a spike branch.
10. Hold the P0 gate review; adjust estimates against the measured baseline.
