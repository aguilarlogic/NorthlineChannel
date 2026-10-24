# Measurement reports: index

Each report is written by one measurement suite of `NorthlineTool`. Regenerate a report with
`NorthlineTool measure <suite> --out docs/measurements` (or `measure all`). Run `measure list` to see
the suites. The build date is printed in each report's header.

| Report | Suite | What it covers | Build date |
|---|---|---|---|
| [pre.md](pre.md) | `pre` | PRE saturation: alias, gain staging at the Operating Level, distortion against level, filter accuracy, Weight, click-free switching, latency and CPU | 23 Oct 2026 |
| [dynamics.md](dynamics.md) | `dynamics` | COMP, GATE and DE-ESS: static curves, timing against the knob, auto make-up, gate chatter and clicks, de-esser band isolation, CPU | 23 Oct 2026 |
| [eq.md](eq.md) | `eq` | EQ: static band accuracy against the analog prototype, dynamic band gain law and speed, display against the DSP, CPU | 23 Oct 2026 |
| [master.md](master.md) | `master` | Limiter: latency by mode and rate, output peaks (sample and true peak), distortion, release, EBU 3341 and 3342 conformance, MeterHub CPU | 24 Oct 2026 |
| [engine.md](engine.md) | `engine` | Strip: latency by configuration, Level Match accuracy and gap behaviour, block-size determinism, CPU of the strip stages | 11 Oct 2026 (see the note below) |
| [competitors.md](competitors.md) | none (`host-bench` and `bench`) | CPU of one installed third-party VST3 channel strip, against this plug-in, under the same harness. The machine was shared, so read the numbers as an order of magnitude | Not dated |

## Note on engine.md

`engine.md` was generated on 11 October 2026, before PRE reported its HQ latency and before the
determinism fix for the PRE path. Two of its tables are out of date:

- **Latency, row "quality HQ, all modules on".** The report shows 0 samples. The current build reports
  108 samples (PRE 48 plus Safety Clip HQ 60).
- **Determinism, row "PRE".** The report shows differences up to 0.32 at block size 1. The current build
  shows 0 for PRE, and at most about 7e-8 for the whole strip with everything on, against a bound of 1e-6.

Regenerate `engine.md` with `NorthlineTool measure engine --out docs/measurements` before you use its
latency or determinism figures. The tests in `NorthlineTool test engine` check both properties on every run.
