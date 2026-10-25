# Measurement reports: index

Each report is written by one measurement suite of `NorthlineTool`. Regenerate a report with
`NorthlineTool measure <suite> --out docs/measurements` (or `measure all`). Run `measure list` to see
the suites. The build date is printed in each report's header.

| Report | Suite | What it covers | Build date |
|---|---|---|---|
| [pre.md](pre.md) | `pre` | PRE saturation: alias, gain staging at the Operating Level, distortion against level, filter accuracy, Weight, click-free switching, latency and CPU | 24 Oct 2026 |
| [dynamics.md](dynamics.md) | `dynamics` | COMP, GATE and DE-ESS: static curves, timing against the knob, auto make-up, gate chatter and clicks, de-esser band isolation, CPU | 24 Oct 2026 |
| [eq.md](eq.md) | `eq` | EQ: static band accuracy against the analog prototype, dynamic band gain law and speed, display against the DSP, CPU | 24 Oct 2026 |
| [master.md](master.md) | `master` | Limiter: latency by mode and rate, output peaks (sample and true peak), distortion, release, EBU 3341 and 3342 conformance, MeterHub CPU | 24 Oct 2026 |
| [engine.md](engine.md) | `engine` | Strip: latency by configuration, Level Match accuracy and gap behaviour, block-size determinism, CPU of the strip stages | 24 Oct 2026 |
| [cpu.md](cpu.md) | none (`bench`) | CPU of the whole plug-in by configuration, rate and block size, against the roadmap budgets, and per module | 24 Oct 2026 |
| [competitors.md](competitors.md) | none (`host-bench` and `bench`) | CPU of one installed third-party VST3 channel strip, against this plug-in, under the same harness. The machine was shared, so read the numbers as an order of magnitude | Not dated |
