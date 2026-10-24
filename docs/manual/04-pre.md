# 4. PRE

PRE is the first module in the default chain. It holds the saturation, the high-pass and low-pass
filters, and Weight, a low-frequency bump. Saturation adds harmonics in a shape you can choose. The
filters remove the rumble and the air you do not need, before the gate and the compressor see the
signal.

![PRE detail: saturation, transfer curve, harmonics and filters](images/main-editor.png)

## Controls

Values are the defaults of a new instance.

| Control | Range and units | Default | What it does |
|---|---|---|---|
| **SAT** | on or off | off | Switches the saturation on. When off, the transfer curve shows the engine at its current settings, and the harmonics display shows no signal. |
| Type | Odd, Even, Heavy, Crush, Tape, Tube, Transformer | Odd | Sets the shaping curve. See the table below. A type change crossfades over 20 ms. |
| **HQ** | Eco or HQ | Eco | Runs the saturation at 4× with second-order ADAA. It is the same switch as the top bar quality. |
| **AUTO** | on or off | on | Auto gain. Calibrates the output level so that the saturation keeps the level at the Operating Level. |
| Drive | 0 to 100 % | 30 % | Pushes the signal into the curve. At 0 % the pre-gain is −12 dB. At 100 % it reaches the drive cap of the type. |
| Tone | −100 to +100 % | 0 % | Tilts the signal going into the shaper, by up to 6 dB at 600 Hz. Positive values raise the lows and cut the highs before the shaper. An output stage restores the balance. The clean signal stays flat. |
| Mix | 0 to 100 % | 100 % | Blends the saturated signal with the clean signal. The mix is phase-coherent. |
| **HP** | on or off | off | Enables the high-pass filter. |
| HP Freq | 20 Hz to 20 kHz | 80 Hz | Corner frequency of the high-pass filter. |
| HP slope | 6, 12, 18, 24, 36 or 48 dB per octave | 12 dB | Steepness of the high-pass filter. |
| HP Res | 0.1 to 4 | 0.71 | Resonance of the high-pass filter. 0.71 is the Butterworth value, the flattest passband. |
| **LP** | on or off | off | Enables the low-pass filter. |
| LP Freq | 20 Hz to 20 kHz | 16 kHz | Corner frequency of the low-pass filter. |
| LP slope | 6, 12, 18, 24, 36 or 48 dB per octave | 12 dB | Steepness of the low-pass filter. |
| LP Res | 0.1 to 4 | 0.71 | Resonance of the low-pass filter. |
| Filter target | Both, Mid / L, Side / R | Both | Channel or M/S component the filters act on. In ST mode, Mid / L means left and Side / R means right. |
| **WEIGHT** | on or off | off | Enables Weight. |
| Wt Freq | 40 to 120 Hz | 72 Hz | Frequency of the Weight bump. |
| Weight amount | +2, +4 or +6 dB | +2 dB | Height of the Weight bump at its frequency. |

The PRE mode chip (ST or MS) sets the stereo mode of the module. See
[Signal flow](02-signal-flow.md#stereo-modes).

## Saturation types

Each type has its own shaping curve. Measured at the Operating Level (−18 dBFS RMS), with a 1 kHz
sine, HQ, 48 kHz, and auto gain on:

| Type | Character | 2nd harmonic at 100 % drive | 3rd harmonic at 100 % drive |
|---|---|---|---|
| Odd | Odd harmonics only, `u / sqrt(1 + u²)` | −148.8 dB | −34.8 dB |
| Even | Biased sigmoid, even harmonics from the bias | −17.5 dB | −40.2 dB |
| Heavy | Cubic soft clip with a hard knee at 1.5 | −148.7 dB | −35.8 dB |
| Crush | Asymmetric knee clip, with a 4 dB high shelf before the clip | −40.9 dB | −40.0 dB |
| Tape | Atan soft knee, a level-dependent high-frequency loss, and a 60 Hz head bump | −146.4 dB | −35.7 dB |
| Tube | Bias driven by the signal envelope, so even harmonics rise with level | −20.3 dB | −33.5 dB |
| Transformer | Odd harmonics, low-frequency weighted by a 5 dB bell at 90 Hz | −157.5 dB | −34.5 dB |

The values are from the HQ path in `docs/measurements/pre.md`. That report also has the Eco values
and the values at 50 % drive.

## Display

- **Saturation transfer.** The curve of the shaper, with the input in dBFS on the horizontal axis.
  The label reads "saturation off" when SAT is off, and the curve then shows the engine at its
  current settings.
- **Harmonics (dBc).** Live bars of the harmonic levels, measured from the signal leaving PRE.
- **HP / LP.** The filter curve at the current settings. It reads "filters off" when both are off.

## Tips

- Set the Operating Level first (chapter 2). Auto gain keeps the output level, so you can compare
  the saturation and the bypass without a level difference.
- Odd and Transformer add odd harmonics only. Even and Tube add even harmonics. Crush is the most
  aggressive type.
- Use Tone to shape the harmonics. Use Mix to blend the effect. Mix does not set the level.
- Put HP first on vocals and bass. Use LP only for the air you want to remove.
- Set Wt Freq first, then the amount. Weight is one bump at one frequency.

## Latency and CPU

| Quality | Latency | CPU, stereo, 48 kHz, 512-sample blocks |
|---|---|---|
| Eco | 0 samples | Odd at 50 %: 0.23 %. Everything on (48 dB high-pass and low-pass, Weight, Tone): 0.56 % |
| HQ | 48 samples (1.0 ms) | Odd at 50 %: 1.55 %. Everything on: 2.47 % |

The source is `docs/measurements/pre.md`. The strip budget in the roadmap is 0.6 % for everything on
in Eco and 1.5 % for HQ, for the whole strip. PRE alone is at the Eco budget and over the HQ budget.

## Limits

- **Quality switching dips the output.** Switching Eco and HQ changes the latency by 48 samples. The
  output fades out for 10 ms, the new quality is applied in silence, and the output fades back in.
- **Tape's high-frequency loss depends on level.** At 100 % drive the 10 kHz level moves by about
  3 dB between −40 and −12 dBFS. At 0 % drive, quiet signals sit up to 0.25 dB above flat at 10 kHz.
  Tape is not flat for this reason.
- **HQ is over its CPU budget.** Use Eco on busy sessions, and switch to HQ on the tracks where you
  hear the difference.
- **Eco reports 0 samples of latency.** The first-order ADAA delays the Eco path by about half a
  sample at low frequencies. This is not measured.
- **Weight Track HP is host only.** It is not in the editor. See
  [Parameters without an editor control](13-settings-and-controls.md#parameters-without-an-editor-control).
