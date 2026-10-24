# Changelog

All notable changes to Northline Channel are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Audit defects are numbered D1 to D15, as listed in
[docs/ROADMAP.md](docs/ROADMAP.md), section 6. The entries come from `git log v0.9.0-audit..dev` (66 commits,
including the merge commits).

Status tags on the defects: **fixed** means the change and a named test were both found. **Fixed in code**
means the change was found but no named acceptance test was. **Partial** means part of the defect is still open.

## 1.0.0-beta (unreleased)

### Added

**Processing**

- PRE: seven saturation types (Odd, Even, Heavy, Crush, Tape, Tube, Transformer), with auto gain calibrated
  at the Operating Level. Eco runs at the base rate with first-order ADAA. HQ runs at 4× with second-order
  ADAA and a linear-phase oversampler, at 48 samples of latency.
- PRE: high-pass and low-pass filters with slopes from 6 to 48 dB per octave, and Weight, a low-frequency bump.
- GATE: gate, expander and duck, with a sidechain high-pass and low-pass, an external key and a listen.
- DE-ESS: two dynamic bands (low shelf, bell, narrow bell or high shelf), relative detection, an external key,
  and listen buttons for each band and for the difference signal.
- EQ: four bands, each a shelf or a bell, with Clean and Proportional Q. The display draws Mid and Side curves
  in M/S mode, and an analyser runs before and after the EQ. Dynamic band settings and band solo are host-only.
- COMP: four types (VCA, FET, OPT, SOFT), auto make-up gain, auto release, a sidechain high-pass, low-pass and
  bell, an external key, and KEY and DELTA listens. The stereo link is host-only.
- Master: a three-mode limiter (Safety Clip, Brickwall with 1.5 ms of lookahead, and True Peak with a 4×
  detector), with a ceiling and a release.
- Master: EBU R128 metering (BS.1770 K-weighting, gated integrated loudness, true peak at 4×, LRA) and a
  vectorscope with correlation. The meter runs in the processor, so it does not need the editor open.
- Master: loudness targets (Streaming, Apple Music, Podcast, EBU R128, ATSC A/85 and Custom).
- Master: Level Match in Off, Learn and Live modes, with a trim of ±12 dB.
- Monitor modes: Stereo, Mono, Left, Right, Side and Swap.
- Compare: A and B slots in the top bar, with A>B and B>A copy. The engine also has whole-strip and per-module
  Compare, and Delta for PRE, GATE and EQ. The editor has no controls for those last two (see Known issues).
- Smart Start: a starting point from a 12-second listen to one of nine sources (Vocal, Kick, Snare, Toms, Bass,
  Guitar, Keys, Bus and Dialogue). It is one undo step, and the controls it changes are written into Spotlight.
- Instance Hub: a table of the Northline instances in the same plug-in process, with loudness, gain reduction,
  latency and CPU. Groups link parameters across instances.

**Presets and state**

- 160 factory presets: 20 categories of eight, in two families (Starting points and Character), plus Init.
- A preset browser with search, categories, families, folders, favourites and sort orders. User presets can be
  moved to the trash.
- User presets (`.nlpreset`), module presets (`.nlmodule`), and copy and paste of a module between instances.
- Spotlight: the controls a preset highlights, with an edit mode.
- Labelled undo history of 32 steps, with a list of the steps.
- State schema v2, with migration from v1 sessions and presets.

**Interface**

- Chain strip with a detail area, a console layout for all five modules, and a master column.
- Module views: PRE (saturation and filter curves), EQ with an analyser and handles, and level scopes. The
  compressor threshold can be dragged on the scope.
- Shared control behaviour: drag, Shift for fine steps, wheel steps, Alt-click or Command-click to reset, and a
  right-click menu with Reset, Enter value, Copy, Paste and Spotlight.
- Settings: dark and high-contrast themes, interface size from 75 % to 200 %, a default size for new instances,
  a CPU overlay, and a four-step first-run tour.
- Accessibility roles on knobs, chain cards, segmented controls and the gain-reduction bar.

**Release and development**

- Continuous integration: a Windows build with the test suite, and a macOS build of VST3 and AU that does not
  block the run.
- A release workflow, installer scripts for Windows (Inno Setup) and a macOS package script (pkgbuild), and
  signing scripts. The macOS build is universal.
- A CLAP build, enabled when the `external/clap-juce-extensions` checkout is present.
- 185 tests and five measurement suites (`pre`, `dynamics`, `eq`, `master` and `engine`), plus a benchmark and a
  snapshot command.

### Changed

- The parameter table is version 2. Parameter IDs are frozen from this beta.
- Level Match v2 replaces the unweighted 1.5 s automatic gain control that was printed into the render (D1).
- Latency is the sum of the module latencies, which depend on the quality setting and the gate, de-esser and
  compressor lookahead, plus the limiter latency. Switching the limiter or a module on or off does not change it (D2).
- Discrete switches cross-fade instead of stepping: the compressor, gate and de-esser types and modes, and the
  EQ band type and target, over 15 ms. The PRE saturation type cross-fades over 20 ms (D10).
- Monitor replaces the duplicate Mono and Mid options. Monitor and every listen are transient: they are not
  saved in presets, A/B slots or undo steps, and they reset when a state loads (D11).
- Compressor: the hidden FET attack scaling and the OPT attack floor are removed, and the OPT memory runs on a
  time constant (D9). The auto make-up gain is re-calibrated.
- Gate and de-esser: the expander law is continuous across the threshold (D14).
- The strip runs its smoothers on absolute 32-sample control ticks, so the output does not depend on the host
  block size (D3).
- Source layout: DSP in `Source/dsp`, the editor in `Source/gui`, Smart Start in `Source/smart`, and the Instance
  Hub in `Source/hub`.

### Fixed

Audit defects from `docs/ROADMAP.md` section 6:

- **D1 Level Match (fixed).** Level Match v2 measures with K-weighting and gating, and it writes a static trim in
  Learn mode, so the output is not modulated. The tests allow a re-entry peak error of 0.3 dB and a noise-floor
  change of 0.5 dB.
- **D2 Limiter (fixed in code).** The limiter crossfades on and off over 10 ms, and the LIMIT switch no longer
  changes the latency. The re-enable test from the roadmap was not checked.
- **D3 Smoothing (fixed).** Smoothers and gain ramps tick on absolute 32-sample boundaries. The dynamics tests
  check identical output with automation at block sizes 1, 7, 32, 37, 64, 511, 512 and 2048. This also fixes D12.
- **D4 Parallel compression (fixed).** The make-up gain no longer scales the dry signal. The test
  `comp_mixZeroNullsAndDryIsNeverScaledByMakeup` checks that a 0 % mix nulls against the input at −120 dB or lower.
- **D5 Saturation gain (fixed in code).** Per-type calibration keeps the output level at the Operating Level
  within the tolerance. The PRE report measures 0.39 dB for Eco and 0.32 dB for HQ.
- **D6 Metering (fixed).** The loudness meter runs in the processor (`MeterHub`). The test
  `master_meterOfflineRealtimeEquivalence` checks that integrated loudness, LRA, true peak and correlation match
  across several block sizes and a whole-file offline pass. The ThreadSanitizer check in the roadmap is not in the repository.
- **D7 State races (fixed in code).** Non-parameter state is guarded by a `stateLock` that the audio thread never
  takes. The metering rings are allocated once. The ThreadSanitizer run in the roadmap is not in the repository.
- **D8 Counters (fixed in code).** Signed sample counters are replaced by a `uint32` control phase. No test seeds
  the counter near 2³¹.
- **D9 Compressor timing (fixed).** The hidden FET and OPT scaling is removed. The dynamics report measures attack
  against the knob to the ±10 % spec.
- **D10 Discrete switches (fixed in code).** Type and target changes cross-fade through a shadow instance over 15 ms.
- **D11 Monitor and listens (partial).** Monitor and listens are transient, and the duplicate Mid mode is removed.
  The monitor's mid and side gain is still 0.5, and the engine uses 0.707 (see Known issues).
- **D12 De-esser output smoothing (fixed).** Covered by the D3 change.
- **D13 EQ display (fixed).** The display uses the session sample rate, and it draws Mid and Side curves separately
  in M/S mode.
- **D14 De-esser and gate (partial).** The gate expander law is continuous in level. A de-esser band is not
  confirmed to reset cleanly when it is re-enabled.
- **D15 Tests (partial).** The dynamics block-size tests, the D4 null test, and the D6 equivalence test are added or
  tightened, and the real-time safety test counts only rendering-thread allocations. Not every D1 to D14 guard was
  checked against `v0.9.0-audit`.

Other fixes:

- The HQ Safety Clip ceiling is guarded after the decimator.
- The compressor RMS detector ripple is removed, and the auto make-up gain is re-calibrated.
- The EQ display uses the correct parameter stride for the version 2 table.
- Smart Start capture can start again over a stale ready state.
- The settings overlay buttons no longer overlap.

### Known issues

These are open in this beta. Several are also listed in the manual's troubleshooting chapter.

- Some parameters have no editor control, so the host must set them: input swap, global mix, Weight Track HP,
  the gate, de-esser and compressor lookaheads, the de-esser attack, the de-esser band mode (Split or Wide), EQ band
  solo, the EQ dynamic band settings, and the compressor stereo link.
- The Q knob has no effect on shelf, Wide and Narrow bands. It works on Bell and Param bands.
- Whole-strip Compare does not loudness-match its two states. Module Compare does. Merge commit `67ac4d4` describes
  Compare as loudness-matched, so the commit message and the code disagree.
- Whole-strip Compare and Delta (PRE, GATE and EQ) run in the engine, but the editor has no controls for them.
- The listen buttons light up in the PRE accent colour (pink). Their tooltips say amber.
- Clicking a row in the Instance Hub brings that instance's editor window forward. It does not edit the instance in place.
- Safety Clip and Brickwall do not hold a true-peak ceiling. With a −1 dB ceiling on band-limited drums, the master
  report measures 1.89 dBTP for Safety Clip HQ and 1.75 dBTP for Brickwall. Use True Peak when the true-peak
  ceiling matters.
- Switching between Eco and HQ is not click-free, and it changes the reported latency.
- PRE at HQ with every control on uses about 2.5 % CPU in the PRE report.
- The monitor's mid and side gain (0.5) differs from the engine's (0.707) (D11).
- No test seeds the sample counter near 2³¹ (D8).
- Screen reader behaviour has not been recorded.
- `docs/measurements/engine.md` predates the PRE HQ latency and the determinism fix. Regenerate it before you use its figures.
- The project version in `CMakeLists.txt` is 0.9.1. This entry is 1.0.0-beta.
