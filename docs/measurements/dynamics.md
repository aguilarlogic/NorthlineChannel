# Northline Channel measurement: dynamics

COMP, GATE and DE-ESS: static curves, honest timing, auto make-up, chatter, lookahead, band isolation, CPU

Build: Oct 23 2026, AMD Ryzen 7 9800X3D 8-Core Processor

## COMP


### Static curve: worst error against the knee formula (dB)

1 kHz sine, -60..0 dBFS in 3 dB steps, threshold -24 dB. Spec: within 0.2 dB.

| Type | Knee 0, 2:1 | Knee 6, 4:1 | Knee 12, 10:1 | Knee 24, 10:1 |
|---|---|---|---|---|
| VCA | 0.000 | 0.000 | 0.000 | 0.000 |
| FET | -0.016 | -0.006 | -0.003 | -0.003 |
| OPT | -0.020 | -0.030 | -0.037 | -0.037 |
| SOFT | -0.019 | -0.026 | -0.031 | -0.031 |


### Gain-reduction ripple on a steady sine (peak-to-peak dB, last second)

Sine at -18 dBFS, threshold -24 dB, 4:1, hard knee, default timing. A peak detector has no ripple; an RMS window passes a ripple at twice the signal frequency.

| Type | 100 Hz | 1 kHz |
|---|---|---|
| VCA | 0.000 | 0.000 |
| FET | 0.000 | 0.000 |
| OPT | 0.162 | 0.001 |
| SOFT | 0.094 | 0.000 |


### Measured attack: 10-90% of a 10 dB step, error against the knob

Threshold -40 dB, 4:1, hard knee. Spec: within 10 % for every type and sample rate. n/a: below the type's minimum attack.

| Type | Attack (ms) | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| VCA | 1 | 0.0 % | -0.0 % | -0.0 % | -0.0 % |
| VCA | 3 | -0.0 % | -0.0 % | -0.0 % | -0.0 % |
| VCA | 10 | -0.0 % | -0.0 % | -0.0 % | -0.0 % |
| VCA | 40 | -0.0 % | -0.0 % | -0.0 % | -0.1 % |
| FET | 1 | 0.0 % | -0.0 % | -0.0 % | -0.0 % |
| FET | 3 | -0.0 % | -0.0 % | -0.0 % | -0.0 % |
| FET | 10 | -0.0 % | -0.0 % | -0.0 % | -0.0 % |
| FET | 40 | -0.0 % | -0.0 % | -0.0 % | -0.1 % |
| OPT | 1 | -3.4 % | -3.4 % | -3.4 % | -3.4 % |
| OPT | 3 | 0.1 % | 0.1 % | 0.1 % | 0.1 % |
| OPT | 10 | 0.1 % | 0.1 % | 0.1 % | 0.0 % |
| OPT | 40 | 0.0 % | 0.0 % | -0.0 % | -0.1 % |
| SOFT | 1 | -3.4 % | -3.4 % | -3.4 % | -3.4 % |
| SOFT | 3 | 0.1 % | 0.1 % | 0.1 % | 0.1 % |
| SOFT | 10 | 0.1 % | 0.1 % | 0.1 % | 0.0 % |
| SOFT | 40 | 0.0 % | 0.0 % | -0.0 % | -0.1 % |


### Measured release: 10-90% recovery after a 30 dB step, 48 kHz

Spec for VCA, FET and SOFT: within 12 % of the knob. OPT's release is scaled by its gain-reduction memory, so the figure is for the state after a 30 dB step.

| Type | Release knob (ms) | Measured (ms) | Error |
|---|---|---|---|
| VCA | 40 | 40.0 | 0.0 % |
| VCA | 150 | 150.0 | 0.0 % |
| VCA | 600 | 600.0 | -0.0 % |
| FET | 40 | 40.0 | 0.0 % |
| FET | 150 | 150.0 | 0.0 % |
| FET | 600 | 600.0 | -0.0 % |
| OPT | 40 | 32.9 | -17.6 % |
| OPT | 150 | 123.3 | -17.8 % |
| OPT | 600 | 480.7 | -19.9 % |
| SOFT | 40 | 40.0 | -0.0 % |
| SOFT | 150 | 150.0 | 0.0 % |
| SOFT | 600 | 600.0 | -0.0 % |


### Program-dependent release (VCA, knob 100 ms)

| Auto release | After 50 ms of compression (ms) | After 4 s of compression (ms) |
|---|---|---|
| off | 100.001 | 100.001 |
| on | 152.045 | 530.934 |


### Auto make-up: integrated loudness error at the Operating Level (LU, pink noise at -18 dBFS RMS)

Spec grid: ratio 1.5-10, threshold -40..-10 dB (spec: within 1 LU). The held-out grid (ratio 3-8, threshold -35..-15 dB) and two other timings check that the fit generalises.

| Type | Spec grid, worst | Held-out, worst | Fast timing 3/80 ms, worst | Slow timing 30/400 ms, worst |
|---|---|---|---|---|
| VCA | 0.09 | 0.08 | 0.18 | 0.10 |
| FET | 0.09 | 0.08 | 0.18 | 0.10 |
| OPT | 0.31 | 0.14 | 0.29 | 0.21 |
| SOFT | 0.07 | 0.16 | 0.65 | 0.05 |


### CPU per module

| Configuration | % of one core |
|---|---|
| VCA | 0.14 |
| FET | 0.21 |
| OPT | 0.19 |
| SOFT | 0.20 |


## GATE


### Detector chatter: 40 Hz sine decaying 20 dB/s through the threshold, hold 0

Spec: at most one open/close cycle (two transitions).

| Sidechain high-pass (Hz) | Open/close transitions |
|---|---|
| 20 | 1 |
| 30 | 1 |


### Kick transient loss in the first 5 ms (0.5 ms opening)

A click and 60 Hz body opening a closed gate. Spec: at most 0.5 dB with 2 ms lookahead.

| Lookahead (ms) | Loss (dB) |
|---|---|
| 0 | 0.25 |
| 1 | 0.00 |
| 2 | 0.00 |
| 5 | 0.00 |


### Opening click at a -60 dB floor: splatter above 8 kHz

1 kHz tone through an externally keyed gate. Spec: -60 dB re signal for the 0.5 ms default.

| Attack (ms) | Splatter (dB re signal) | Largest derivative step (x) |
|---|---|---|
| 0.02 | -39.3 | 6.63 |
| 0.10 | -43.1 | 3.73 |
| 0.30 | -60.0 | 1.00 |
| 0.50 | -68.8 | 1.20 |
| 1.00 | -81.7 | 1.00 |


### CPU per module

| Type | % of one core |
|---|---|
| Gate | 0.08 |
| Expander | 0.08 |
| Duck | 0.10 |


## DE-ESS


### Band isolation at matched gain reduction (synthetic vocal, 100 Hz - 1.5 kHz)

Spec (Split mode): change outside the band within 0.3 dB at matched gain reduction.

| Mode | Gain reduction (dB) | Change outside the band (dB) |
|---|---|---|
| Split | -12.00 | -0.00 |
| Wide | -12.00 | -12.00 |


### Measured attack: 10-90% of a step, error against the knob (60 ms release)

| Mode | Attack knob (ms) | Measured (ms) | Error |
|---|---|---|---|
| Split | 0.5 | 0.52 | 3.4 % |
| Split | 2.0 | 2.03 | 1.5 % |
| Split | 5.0 | 5.07 | 1.4 % |
| Wide | 0.5 | 0.52 | 3.4 % |
| Wide | 2.0 | 2.03 | 1.5 % |
| Wide | 5.0 | 5.07 | 1.4 % |


### Measured release: 10-90% recovery, error against the knob

| Release knob (ms) | Measured (ms) | Error |
|---|---|---|
| 40 | 40.0 | 0.0 % |
| 150 | 150.0 | 0.0 % |


### CPU per module

| Configuration | % of one core |
|---|---|
| One band | 0.27 |
| Two bands, 2 ms lookahead | 0.26 |


## Notes

Timing figures are 10-90% rises of the gain-reduction step response. The RMS COMP types (OPT, SOFT) have a two-pole window of about 0.075 x the attack time per pole; its ripple at twice the signal frequency shows in the gain-reduction ripple table, and is largest for bass. The auto make-up is fitted on pink noise at the default timing and is not a bass-specific correction.

