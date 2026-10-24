# Architecture

This document describes how Northline Channel is built. It is for developers. The user's view is in the
[manual](manual/index.md), and the rules for changing code are in [DEVELOPMENT.md](DEVELOPMENT.md).

## 1. Overview

Each plug-in instance holds one `NorthlineProcessor` (`Source/PluginProcessor.*`). The processor owns one
`ChannelStrip` (`Source/dsp/ChannelStrip.*`), which runs the five modules and the master section, and the
processor's parameter tree and state. The editor (`Source/gui/`) reads copies of that state and sends
parameter changes back. The formats are VST3, Standalone, Audio Unit (macOS) and CLAP (optional, when the
`external/clap-juce-extensions` checkout is present). The build uses JUCE 9.0.3 and C++20. CMake globs
`Source/*.cpp` and `tools/**/*.cpp`.

## 2. Threads

| Thread | Runs | Work | Must not |
|---|---|---|---|
| Audio | `processBlock` and `ChannelStrip::process` | DSP, master section, meter and scope publish | allocate, lock, wait, log, or touch files |
| Message | editor, processor timer (10 Hz), `AsyncUpdater` for undo labels, presets, state, Instance Hub poll timer (30 ms) | UI, parameter writes, state, presets, undo, A/B, hub values | block the audio thread |
| Analyser | `EqAnalyser` and `HarmonicAnalyser` (`juce::Thread`, `Source/gui/Analyser.*`) | FFT and harmonic analysis of the output sample stream | write UI state directly; publish through `Seqlock` |
| Smart Start | `juce::Thread` named "Northline Smart Start" (`Source/smart/SmartStart.cpp`) | Analyse the 12 s capture (`SmartAnalysis.cpp`) | apply parameters; the message thread applies the result as one undo step |

The audio thread never waits on another thread. Data crosses threads in three ways only: atomics, the SPSC
rings (`SpscRing` in `Source/dsp/Meters.h`), and seqlocks (`MeterHub` snapshots, `Seqlock` in
`Source/gui/Analyser.h`). Non-parameter state is guarded by `stateLock`, a `juce::CriticalSection` that the audio
thread never takes.

## 3. Signal flow

```
 host input
     |
 [input stage]  channel swap, polarity, gain (ramped)
     |  \__ dry copy: input delayed to the chain latency (used by module learn and Delta)
     |  \__ compare copy: input delayed to the total latency (used by whole-strip Compare)
     v
 [PRE] -> [GATE] -> [DE-ESS] -> [EQ] -> [COMP]     in the user's order; each module is
     |                                             faded, bypass-delayed, held off on enable,
     |                                             and optionally learned or shown as Delta
     v
 [level match] -> [global mix] -> [output gain]
     v
 [master: limiter, MeterHub]
     v
 [Compare]  module Compare (learned level) then whole-strip Compare (crossfade to the compare copy)
     v
 [monitor]  Stereo, Mono, Left, Right, Side, Swap (transient)
     v
 host output
     |
     +--> meter frames, scope taps, hub gain-reduction values --> editor (rings and atomics)
```

`ChannelStrip::process` runs these steps in this order for each host block:

1. `updateParams`: compares each module's parameter range with the previous snapshot (`memcmp`) and calls the
   module's `setParams` only when its range changed.
2. Level-match update and monitor set.
3. Input stage: swap, polarity and gain ramps.
4. Dry copy and compare copy, each delayed to its latency.
5. Module chain in the user's order. Each module is faded, held off, learned, and shown as Delta when asked.
6. Level match, global mix and output gain.
7. Master section: limiter, then `MeterHub`.
8. Compare crossfades: module Compare, then whole-strip Compare.
9. Monitor.
10. Meter frames, scope taps and hub values are published.
11. `clock.advance(n)`, once per block after every user of the clock.

## 4. Parameters and the module contract

`Source/Params.cpp` holds the parameter table. Each entry has an ID, a range, a default and flags:
`kNonAutomatable` (1) for latency and calibration settings, and `kTransient` (2) for monitor, listen and
solo. IDs are frozen from the first beta. `P::Index` in `Source/Params.h` names them.

Each block, the processor fills a `ParamSnapshot` (`Source/dsp/ParamSnapshot.h`), a copy of every plain
value. Each module has a `read<Module>Params(const ParamSnapshot&)` function in its header, so the strip never
needs to know a module's parameters. A module provides:

- `prepare(double sampleRate, int maxBlock)`: sizes every buffer for the worst case. This is the only place
  that allocates.
- `reset()`: clears the filter and detector state.
- `setParams(const XParams&)`: applies the settings. Called from `updateParams` only when its range changed.
- `latency()`: the latency in samples. It follows the constant-latency rule (section 5).
- `process(float* const* ch, int numCh, int n, const float* const* key = nullptr)`: runs one block. The key
  is the sidechain input.

The dynamics modules also report gain reduction (`getGainReductionDb`), and the EQ reports its dynamic band
gains (`getDynGainDb`). The strip reads these values for the meters and the Hub.

## 5. Constant-latency policy

`ChannelStrip::refreshLatency()` sums the module latencies, adds the master latency, and stores the total.
It runs when a latency-relevant parameter changes. Each module's latency is clamped to a cap, so a bad value
cannot overflow the delay lines.

Latency depends only on:

- the quality setting: PRE at HQ adds 48 samples (`SatSection::kHqLatency`);
- the lookahead settings of the gate, de-esser and compressor, which are host-only (`dyn::lookaheadSamples`
  gives Off, 1, 2 or 5 ms);
- the limiter mode: Safety Clip is 0 samples in Eco and 60 in HQ; Brickwall and True Peak use
  round(0.0015 × sample rate), which is 66, 72, 144 and 288 samples at 44.1, 48, 96 and 192 kHz.

It never depends on whether a module or the limiter is switched on. A bypassed module keeps its delay
(`bypassDelay`), so switching a module off does not shift the timing. When a module is switched on, its first
`latency` output samples are held off (`holdoff`) so that the silent start does not reach the output.

Tests for the latency sums are in `tools/tests/TestEngine.cpp`.

## 6. Meters, scopes and analysers

- **MeterHub** (`Source/dsp/MeterHub.h`) lives in the master section and runs on the audio thread. It uses
  K-weighting, 4× true peak, and gated histograms with 9000 bins from −70 to +20 LUFS. Momentary loudness covers
  40 sub-blocks (400 ms). Each block it publishes a snapshot through a seqlock write, and the
  editor reads it through a seqlock read.
- **Meter frames** go to the editor through an `SpscRing<MeterFrame>` (`Source/dsp/Meters.h`), one per block.
  They carry the gain reduction of each module and the limiter.
- **Scope taps** (`Source/dsp/ScopeTap.h`) hold one ring per module. Points sit on a 5 ms grid, indexed by a
  persistent 64-bit counter, and the ring is single-producer and single-consumer.
- **Analysers** (`Source/gui/Analyser.*`) run on their own thread. They use a 4096-point FFT with 75 % overlap,
  show 256 display bins with a +4.5 dB per octave tilt, and publish each frame through `Seqlock`.

## 7. Processor, state and presets

- **State.** The session holds the parameter values and the non-parameter state: the module order, the focus
  (Spotlight), the preset name, the undo history and the A/B slots (saved as "AB"). The current version is
  `kStateVersion = 2`. `makeSnapshot()` builds a state from the non-transient parameters and the order, focus
  and preset name. `resetTransientParams()` clears the monitor and listens when a state loads.
- **Migration.** `migrateSessionV1` and `migrateSnapshotV1` in `Source/PluginProcessor.cpp` convert version 1
  sessions and presets through `migrateValuesV1`. It maps `in_phase` to `in_polarity`, `level_match` to
  `match_mode`, `lim_lookahead` to `lim_mode` (1 Brickwall, 0 Safety Clip), `vu_ref` to `op_level`, `sat_hq` to
  `quality`, `thump_on` and `thump_amt` to `weight_on` and `weight_amt`, and the monitor modes Mid (4) to Mono (1)
  and Side (5) to Side (4).
- **Undo.** 32 labelled steps. Each step is a full state. Preset loads, A/B selections, module reorders,
  Smart Start results and "Reset all settings" each make one step.
- **A/B.** Two slots (`slots[2]`) and an `activeSlot`. Copying between slots is a message-thread operation.
- **Presets.** The factory table is `FactoryDef` in `Source/FactoryPresetsA.cpp` and `FactoryPresetsB.cpp`
  (160 entries). `Source/Presets.*` loads factory and user presets. User presets are `.nlpreset` files in
  `Documents\Northline Audio\Northline Channel\Presets`. Module presets are `.nlmodule` files in the sibling
  `Modules` folder. Favourites are stored in `favourites.xml` under `AppData\Roaming\Northline Audio\Northline Channel`.

## 8. Instance Hub

`Source/hub/InstanceRegistry.*` holds a process-wide registry of the Northline instances in the current host
process. It is a singleton, used on the message thread only. It holds weak references: an instance registers
in the processor constructor and removes itself in the destructor, so a destroyed instance disappears from the
registry. `limitedToProcess` is always true, because a plug-in cannot see other host processes.

`InstanceInfo` holds the loudness, six gain-reduction values, the latency and the CPU of an instance. A timer
in the registry polls the instances every 30 ms (`kPollIntervalMs`), and the Hub panel (`Source/gui/HubPanel.*`)
redraws from the result. A `GroupInfo` links parameters across member instances. The propagation rules are in
`InstanceRegistry.h`.

## 9. Editor

`PluginEditor` owns the top bar, the chain view, the module panels, the master column and the overlays: the
preset browser, Smart Start, the Hub, Settings and the first-run tour. The base canvas is 1320 × 780 px, and
the scale is 75 to 200 %. `Source/gui/Tokens.h` holds the colour tokens for the dark and high-contrast themes.
`UiSettings` stores the editor settings (theme, scale, default size, CPU overlay). The editor never writes to
audio-thread state directly. It sends parameter changes through the parameter tree.

## 10. Tests and measurements

| Area | Test file | Measure suite |
|---|---|---|
| engine (strip, latency, Level Match, determinism, state) | `tools/tests/TestEngine.cpp` | `engine` (`tools/measure/MeasureEngine.cpp`) |
| pre | `tools/tests/TestPre.cpp` | `pre` (`MeasurePre.cpp`) |
| dynamics (COMP, GATE, DE-ESS) | `tools/tests/TestDynamics.cpp` | `dynamics` (`MeasureDynamics.cpp`) |
| eq | `tools/tests/TestEq.cpp` | `eq` (`MeasureEq.cpp`) |
| master (limiter, MeterHub, EBU 3341/3342) | `tools/tests/TestMaster.cpp` | `master` (`MeasureMaster.cpp`) |
| masterUi | `tools/tests/TestMasterUi.cpp` | none |
| presets | `tools/tests/TestPresets.cpp` | none |
| smart, compare, delta | `tools/tests/TestSmart.cpp` | none |
| hub | `tools/tests/TestHub.cpp` | none |
| ui | `tools/tests/TestUiShell.cpp` | none |
| views | `tools/tests/TestViews.cpp` | none |

Shared support: `tools/TestUtil.h` (the `NL_TEST` macro, signal generators and checks), `tools/EqHelpers.h`,
`tools/PreHarness.h`, `tools/tests/DynamicsTestKit.h` and `tools/tests/MasterSignals.h`. `tools/measure/Measure.h`
defines `NL_MEASURE`. `tools/Bench.cpp` is the CPU benchmark, and `tools/ToolMain.cpp` dispatches the
`NorthlineTool` commands: `test`, `measure`, `bench`, `host-bench` and `snapshot`.

The reports are written to `docs/measurements`, and their index is in [measurements/README.md](measurements/README.md).
