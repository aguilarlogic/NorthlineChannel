# Developing Northline Channel

The engineering rules every change must follow. The product plan is in [ROADMAP.md](ROADMAP.md).

## Build and run

```bash
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target NorthlineTool -- -m
./build/NorthlineTool_artefacts/Release/NorthlineTool.exe test            # all tests
./build/NorthlineTool_artefacts/Release/NorthlineTool.exe test comp       # tests whose name contains "comp"
./build/NorthlineTool_artefacts/Release/NorthlineTool.exe measure list    # measurement suites
./build/NorthlineTool_artefacts/Release/NorthlineTool.exe measure all --out docs/measurements
./build/NorthlineTool_artefacts/Release/NorthlineTool.exe bench --quick
```

JUCE lives in `external/JUCE` (not tracked). In a git worktree, link it instead of cloning:
`New-Item -ItemType Junction -Path external\JUCE -Target <main checkout>\external\JUCE` (PowerShell).

Source files are globbed by CMake: add a `.cpp` and re-run the configure step.

## Layout

| Path | Contents |
|---|---|
| `Source/Params.*` | The parameter table (IDs, ranges, defaults, flags) |
| `Source/dsp/ParamSnapshot.h` | Per-block copy of all parameter values |
| `Source/dsp/*Module.h` | One class per strip module plus its `read*Params()` mapping |
| `Source/dsp/MasterSection.h` | Limiter and processor-owned meters (`MeterHub`) |
| `Source/dsp/ChannelStrip.*` | Signal flow: input stage, module chain, latency alignment, output, monitor |
| `Source/PluginProcessor.*` | Host interface, state, presets, undo, A/B |
| `Source/gui/` | Editor |
| `tools/Tests.cpp`, `tools/tests/` | Test suites (`NL_TEST`) |
| `tools/measure/` | Measurement suites (`NL_MEASURE`), reports in `docs/measurements/` |

## Rules for audio code

1. **Real-time safe.** `process()` never allocates, locks, waits, logs or touches the file system.
   Size every buffer in `prepare()` for the worst case. `core_realtimeSafety` counts heap
   allocations during processing and must stay at zero.
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

## Tests and measurements

- Tests: `tools/tests/Test<Area>.cpp` with `NL_TEST (area_whatItChecks) { ... tool::check (...); }`.
  Helpers (`makeProc`, `render`, `renderWithAutomation`, `sine`, `pinkNoise`, `rmsDb`, `maxDiffDb`,
  ...) are in `tools/TestUtil.h`.
- Measurement suites: `tools/measure/Measure<Area>.cpp` with `NL_MEASURE (name, "description")`.
- A fixed defect gets a regression test that fails on the `v0.9.0-audit` tag.

## Style

JUCE style: 4-space indents, braces on their own lines for functions and classes, a space before
call parentheses (`f (x)`), `camelCase` names, comments only where the reason is not obvious.
Commit messages: imperative summary line, a body explaining why, ending with the
co-author line used by the project.
