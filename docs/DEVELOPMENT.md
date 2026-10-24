# Developing Northline Channel

The engineering rules every change must follow. The product plan is in [ROADMAP.md](ROADMAP.md). The code
structure, threads and signal flow are in [ARCHITECTURE.md](ARCHITECTURE.md).

## Build and run

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target NorthlineTool -- -m
./build/NorthlineTool_artefacts/Release/NorthlineTool.exe test                      # all tests
./build/NorthlineTool_artefacts/Release/NorthlineTool.exe test comp                 # tests whose name contains "comp"
./build/NorthlineTool_artefacts/Release/NorthlineTool.exe measure list              # pre, dynamics, eq, master, engine
./build/NorthlineTool_artefacts/Release/NorthlineTool.exe measure all --out docs/measurements
./build/NorthlineTool_artefacts/Release/NorthlineTool.exe bench --quick
./build/NorthlineTool_artefacts/Release/NorthlineTool.exe snapshot out.png "Lead vocal - Upfront" hub
```

`snapshot <out.png> [preset] [browser|smart|hub|settings]` renders the editor to a PNG. The preset is a factory
preset name, or `-1` to turn every module on. The optional last argument opens one overlay. The screenshots in
the manual (`docs/manual/images/`) come from this command.

`host-bench <file.vst3> [filter] [--quick]` runs the same CPU benchmark on another VST3 plug-in.

JUCE lives in `external/JUCE` (not tracked). In a git worktree, link it instead of cloning:
`New-Item -ItemType Junction -Path external\JUCE -Target <main checkout>\external\JUCE` (PowerShell).

CMake globs `Source/*.cpp` and `tools/**/*.cpp` with `CONFIGURE_DEPENDS`, so a new `.cpp` file is picked up by
the next build.

## Layout

| Path | Contents |
|---|---|
| `Source/Params.*` | The parameter table (IDs, ranges, defaults, flags) |
| `Source/PluginProcessor.*` | Host interface, state, presets, undo, A/B |
| `Source/Presets.*`, `Source/FactoryPresetsA.cpp`, `Source/FactoryPresetsB.cpp` | Preset files and the 160 factory presets |
| `Source/dsp/` | The strip (`ChannelStrip.*`), the five modules, the master section, meters and filters. See [ARCHITECTURE.md](ARCHITECTURE.md), section 3 |
| `Source/gui/` | The editor (table below) |
| `Source/smart/` | Smart Start: capture, analysis (`SmartAnalysis.cpp`) and the worker thread (`SmartStart.cpp`) |
| `Source/hub/` | The Instance Hub registry and groups (`InstanceRegistry.*`) |
| `tools/` | `ToolMain.cpp` (the commands), `Bench.cpp`, `Tests.cpp`, `TestUtil.h`, `EqHelpers.h`, `PreHarness.h` |
| `tools/tests/` | The test areas (`Test<Area>.cpp`), `DynamicsTestKit.h` and `MasterSignals.h` |
| `tools/measure/` | The measurement suites (`Measure.h` and one `Measure<Suite>.cpp` each) |
| `docs/` | The manual, architecture and development notes, measurements, research and the roadmap |

Files in `Source/gui/`, by area:

| Area | Files |
|---|---|
| Shell | `PluginEditor`, `TopBar`, `ChainView`, `LookAndFeel`, `Tokens.h`, `UiSettings`, `ViewUtil.h` |
| Modules | `ModulePanels`, `PreView`, `EqView`, `Scopes`, `Widgets` |
| Master | `MasterPanel`, `MeterViews` |
| Analysis | `Analyser` (FFT and harmonic analysers, with the `Seqlock` frame publisher) |
| Overlays | `PresetBrowser`, `SmartStartPanel`, `HubPanel`, `SettingsOverlay`, `Onboarding` |

## Rules for audio code

1. **Real-time safe.** `process()` never allocates, locks, waits, logs or touches the file system.
   Size every buffer in `prepare()` for the worst case. `engine_realtimeSafetyEngine` (in
   `tools/tests/TestEngine.cpp`) counts heap allocations made on the rendering thread and must stay at zero.
2. **Deterministic.** Output must not depend on the host block size. Anything updated at
   control rate ticks on absolute sample indices that are multiples of `kControlBlock`, using a
   persistent `uint32_t` phase (`ControlClock`, `RampedGain` in `Common.h`). Never use a
   block-local counter or signed sample counters. Required: identical output (max difference
   ≤ 1e-6) for block sizes 1, 7, 32, 37, 64, 511, 512 and 2048, including under automation
   (`renderWithAutomation`, events on multiples of 2048).
3. **Constant latency.** `latency()` may depend only on non-automatable settings (`quality`,
   `lim_mode`, `*_lookahead`). Never on whether a module or the limiter is on, and never on
   an automatable parameter. Any internal dry or parallel path is delayed to match. The strip
   delays bypassed modules by their latency, so switching a module on or off never shifts timing.
   The sums are in [ARCHITECTURE.md](ARCHITECTURE.md), section 5.
4. **Level-honest.** A control whose job is not loudness (drive, character, type, curve) must
   not change loudness at the Operating Level (`op_level`, default -18 dBFS RMS) by more than
   ±0.5 dB when its auto-gain is on.
5. **Click-free.** Every switch, discrete or continuous, is smoothed or cross-faded. Target:
   spectral splatter ≤ -60 dB relative to the signal in a 50 ms window around the switch.
6. **Transient parameters** (`kTransient`: monitor, listen, solo) are never saved in presets,
   A/B, undo or session state, and they reset when anything loads.
7. **Measured, not claimed.** Any number in docs or the UI comes from a test or a measurement
   suite. Acceptance thresholds live in tests; suites report the numbers.

## Parameters

- The table in `Params.cpp` is the single source of truth. IDs are frozen from the first beta.
- Each module maps the snapshot to its own settings in `read<Module>Params()` in its header,
  so the strip never needs to know a module's parameters.
- Flags: `kNonAutomatable` for latency or calibration settings, `kTransient` for monitoring aids.

## Tests

- Test areas, one file each in `tools/tests/`:

  | Area | File |
  |---|---|
  | engine (strip, latency, Level Match, determinism, state, real-time safety) | `TestEngine.cpp` |
  | pre | `TestPre.cpp` |
  | dynamics (COMP, GATE, DE-ESS) | `TestDynamics.cpp` |
  | eq | `TestEq.cpp` |
  | master (limiter, MeterHub, EBU 3341 and 3342) | `TestMaster.cpp` |
  | masterUi (loudness panel and meter views) | `TestMasterUi.cpp` |
  | presets (library, file formats, undo labels) | `TestPresets.cpp` |
  | smart, compare and delta | `TestSmart.cpp` |
  | hub (instance registry and groups) | `TestHub.cpp` |
  | ui (shell, tokens, contrast, interaction) | `TestUiShell.cpp` |
  | views (module displays, scopes, EQ view) | `TestViews.cpp` |

- A test is written as `NL_TEST (area_whatItChecks) { ... check (condition, message); }`. The helpers
  `makeProc`, `renderWithAutomation`, `sine`, `pinkNoise`, `rmsDb` and `maxDiffDb` are in `tools/TestUtil.h`.
- A fixed defect gets a regression test that fails on the `v0.9.0-audit` tag.

## Measurements

- Suites: `pre`, `dynamics`, `eq`, `master` and `engine`, one `NL_MEASURE` each in `tools/measure/`.
- Reports are written to `docs/measurements/`, and [docs/measurements/README.md](measurements/README.md) indexes
  them. Regenerate a report before you use its figures. The `engine` report is stale: see the note in that index.

## Style

JUCE style: 4-space indents, braces on their own lines for functions and classes, a space before
call parentheses (`f (x)`), `camelCase` names, comments only where the reason is not obvious.
Commit messages: imperative summary line, a body explaining why, ending with the
co-author line used by the project.
