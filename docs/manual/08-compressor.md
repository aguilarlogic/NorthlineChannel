# 8. COMP

COMP is the last module in the default chain. It reduces the gain of the signal above its
threshold. It has four detector and gain types: VCA, FET, OPT and SOFT. The Mix control blends the
compressed signal with the dry signal, so the compressor can run in parallel.

![COMP detail: threshold, ratio, timing and the sidechain filters](images/main-editor.png)

## Controls

Values are the defaults of a new instance.

| Control | Range and units | Default | What it does |
|---|---|---|---|
| **COMP** chip | on or off | off | Enables the module. |
| Type | VCA, FET, OPT, SOFT | VCA | Detector and gain character. See below. |
| Mode | ST, DUAL, MS | ST | Stereo mode. ST links the detector across channels. DUAL uses one detector per channel. |
| Thresh | −50 to 0 dB | −18 dB | Level above which gain reduction starts. |
| Ratio | 1 : 1 to ∞ : 1 | 3 : 1 | Slope above the threshold. The maximum setting reads "inf : 1". |
| Knee | 0 to 24 dB | 6 dB | Width of the soft knee at the threshold. |
| Attack | 0.05 to 150 ms | 10 ms | Time for the gain reduction to reach its target. |
| Release | 20 to 5000 ms | 150 ms | Time for the gain reduction to recover. |
| **AUTO REL** | on or off | off | Auto release. The release follows the gain-reduction memory. It applies to VCA, FET and SOFT. |
| Mix | 0 to 100 % | 100 % | Parallel mix. At 0 % the output is the input, with the output gain applied. |
| Range | 1 to 40 dB | 40 dB | Largest gain reduction. 40 dB means no limit. |
| Output | −12 to +24 dB | 0 dB | Output gain of the module. |
| **AUTO GAIN** | on or off | off | Auto make-up gain. It sets the make-up for a program at the Operating Level. |
| SC HP | 20 to 500 Hz | 20 Hz | High-pass on the detector. |
| SC LP | 1 kHz to 20 kHz | 20 kHz | Low-pass on the detector. |
| SC Freq | 20 Hz to 20 kHz | 3 kHz | Centre of the sidechain bell. |
| SC Gain | −18 to +18 dB | 0 dB | Gain of the sidechain bell. |
| **EXT SC** | on or off | off | Uses the sidechain input as the detector key. |
| **KEY** | on or off | off | Plays the key signal. Transient. |
| **DELTA** | on or off | off | Plays what the compressor removes. Transient. |

## Types

| Type | Detector | Gain character |
|---|---|---|
| VCA | Peak detector with a 5 ms hold | Clean. The reference type. |
| FET | Peak detector with a 5 ms hold | Adds odd-harmonic colour from a first-order anti-derivative shaper. |
| OPT | RMS detector, with a memory | Slow, program-dependent release. The gain reduction builds up on sustained material. |
| SOFT | RMS detector | Supports AUTO REL. Use it when you want an RMS detector without the OPT memory. |

Two stages are fixed in the detector:

- **Knee.** A quadratic soft knee, the Giannoulis, Massberg and Reiss curve. Its width is the Knee
  control.
- **Dry path.** The make-up gain never touches the dry signal. Mix at 0 % nulls against the input
  to better than −120 dB.

## Auto gain and release

- **Auto gain** computes the make-up from the expected detector level around the Operating Level. It
  is a fit. Use it as a starting point, then trim the Output control.
- **Auto release** (VCA, FET and SOFT) moves the release between the knob value and six times the knob
  value, using the gain-reduction memory. OPT already scales its release with its memory.

## Display

The scope shows the input level, the output level and the gain reduction over time. The threshold is
a line you can drag. Click or drag in the scope to set the threshold to the pointer's level. Hold
Shift for a fine move. The curve inset shows the static transfer curve.

## Tips

- Set Thresh first, with the scope showing gain reduction on the loud phrases. Smart Start aims for
  about 3.5 dB of median gain reduction as its starting point.
- A slower Attack lets more of the transient through.
- Use Release to match the groove. Use AUTO REL when the level changes a lot from phrase to phrase.
- Mix blends the compressed signal with the dry signal. Output sets the level after the blend.
- Use Range to cap the reduction, not to set the amount. Range at 40 dB has no effect.
- Use the sidechain filters (SC HP and SC LP) to keep bass and hiss from driving the gain reduction.
- Use DELTA to check what the compressor removes before you change the type.

## Measured behaviour

From `docs/measurements/dynamics.md`, at 48 kHz unless noted:

- **Attack.** 10 to 90 % of a 10 dB step matches the knob to within 10 % for VCA, FET, OPT and SOFT
  at 1, 3, 10 and 40 ms, at 44.1 to 192 kHz. OPT and SOFT at a 1 ms attack read 3.4 % short.
- **Release.** VCA and FET at 40, 150 and 600 ms match within 0.1 %. OPT reads about 18 to 20 % short
  after a 30 dB step, because its release follows its memory.
- **Auto gain.** Integrated loudness error at the Operating Level, pink noise at −18 dBFS RMS. The
  worst case on the spec grid is 0.09 LU for VCA and FET, 0.31 LU for OPT and 0.07 LU for SOFT.

## Latency and CPU

- Latency is the lookahead, which is 0 by default. The lookahead is host only.
- CPU at 48 kHz: VCA 0.14 %, FET 0.21 %, OPT 0.19 %, SOFT 0.20 %.

## Limits

- The stereo link control is not in the editor. Its default is 100 % in ST mode, so the detector is
  fully linked. Change it only through the host parameter list (`comp_link`).
- The lookahead is host only.
- OPT's release is program dependent. A 30 dB step leaves the gain reduction 18 to 20 % short of the
  knob value.
- The RMS window ripples on bass. At 100 Hz the OPT window has 0.16 dB of ripple, peak to peak.
- The auto gain fit holds for the timings tested. It has not been checked for other ratios or for
  program material.
