# Northline Channel

A zero-latency channel strip (VST3 / Standalone, AU on macOS) built with JUCE 9:
PRE (HP/LP filters, ADAA saturation with optional 2x HQ, Thump), Gate/Expander,
2-band dynamic De-esser, 4-band EQ, Compressor (VCA / FET / OPT / SOFT), plus a
master section with level match, brickwall limiter (zero-latency or 1.5 ms
lookahead), BS.1770 loudness, true peak, VU and correlation metering. Modules
can be reordered, run in stereo, mid or side, and are skipped entirely when off.

![Northline Channel](docs/screenshot.png)

The design plan and the research behind it are in [docs/PLAN.md](docs/PLAN.md)
and [docs/research](docs/research).

## Build

Requires CMake 3.25+ and a C++20 compiler (VS 2022+ on Windows, Xcode on macOS).

```
git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git external/JUCE
cmake -B build
cmake --build build --config Release --target NorthlineChannel_VST3 NorthlineChannel_Standalone NorthlineTool
```

The VST3 lands in `build/NorthlineChannel_artefacts/Release/VST3/Northline Channel.vst3`.
Install it by copying that folder to `C:\Program Files\Common Files\VST3` (Windows)
or `~/Library/Audio/Plug-Ins/VST3` (macOS).

## Test and benchmark

```
build/NorthlineTool_artefacts/Release/NorthlineTool test
build/NorthlineTool_artefacts/Release/NorthlineTool bench [--quick]
build/NorthlineTool_artefacts/Release/NorthlineTool host-bench "<path to any .vst3>" [name filter] [--quick]
```

`test` checks filter/EQ responses, ADAA flatness and aliasing, compressor static
curves, limiter ceilings, gate and de-esser behaviour, BS.1770 loudness and true
peak, block-size independence, zero heap allocation on the audio thread, state,
undo and A/B. `host-bench` loads another VST3 (for example a competing channel
strip) and benchmarks it under exactly the same conditions as `bench`.

## Licence

JUCE is used under its Starter licence (free below the revenue limit, splash
screen optional since JUCE 8) or AGPLv3. Check the current JUCE licence terms
before selling the plug-in.
