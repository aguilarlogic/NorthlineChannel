# 6. DE-ESS

DE-ESS is the third module in the default chain. It reduces sibilance, the hiss of s, sh and
ch sounds, and other sharp bursts in a band you choose. It has two bands. Each band is a dynamic
filter: it lowers its band only when the band level goes above its threshold.

## Controls

Values are the defaults of a new instance.

| Control | Range and units | Default | What it does |
|---|---|---|---|
| **DE-ESS** chip | on or off | off | Enables the module. |
| Mode | ST, DUAL, MS | ST | Stereo mode. In ST the detector is linked across the channels. |
| **BAND 1** chip | on or off | on | Enables band 1. |
| Band 1 Type | Low Shelf, Bell, Narrow, High Shelf | High Shelf | Filter shape of band 1. |
| Band 1 Freq | 20 Hz to 20 kHz | 6 kHz | Centre or corner frequency of band 1. |
| Band 1 Thresh | −48 to 0 dB | −20 dB | Band level at which band 1 starts to reduce gain. |
| Band 1 Range | 0 to 24 dB | 12 dB | Largest gain reduction band 1 can apply. |
| **BAND 2** chip | on or off | off | Enables band 2. |
| Band 2 Type | Low Shelf, Bell, Narrow, High Shelf | Bell | Filter shape of band 2. |
| Band 2 Freq | 20 Hz to 20 kHz | 3.5 kHz | Frequency of band 2. |
| Band 2 Thresh | −48 to 0 dB | −20 dB | Threshold of band 2. |
| Band 2 Range | 0 to 24 dB | 8 dB | Largest gain reduction band 2 can apply. |
| **REL** | on or off | on | Relative detection. See below. |
| Release | 10 to 500 ms | 60 ms | Time for the reduction to recover after the band drops below its threshold. |
| Output | −12 to +12 dB | 0 dB | Output gain of the module. |
| **EXT** | on or off | off | Uses the sidechain input as the key. |
| **BAND 1**, **BAND 2**, **DELTA** | on or off | off | Listen buttons. They are transient. See [Compare and Delta](10-compare-and-delta.md). |

## Relative detection (REL)

With REL on, the detector compares the band level with the level of the full signal. The threshold
then sets how far the band may sit above the overall level. A loud passage does not trigger the
band just because it is loud. With REL off, the threshold is an absolute band level.

Turn REL on for most vocal work. Turn it off when you want a fixed band level, for example on a
track whose level never changes much.

## Modes and bands

- **Split** (the only mode the editor uses). Each band changes the gain of its own band only. The
  spectrum outside the band does not change. Measured: at −12 dB of gain reduction, the change
  outside the band is 0.00 dB.
- **Wide** (not in the editor). The band's gain applies to the whole signal. Measured: at −12 dB of
  gain reduction, the change outside the band is −12 dB. Use Wide only through a host parameter
  list. The editor always uses Split.

Band 1 defaults to a high shelf at 6 kHz. Band 2 defaults to a bell at 3.5 kHz and is off.

## Tips

- Set Band 1 Freq first. Listen on the Band 1 button, and move the frequency until the sibilance
  is the only thing the band hears.
- Set Thresh next. Then set Band 1 Range. Start with a small range, and grow it until the sibilance
  is under control.
- Use Band 2 for a second sibilant peak that Band 1 does not cover.
- Use the Output control to make up the level you removed. It does not change the reduction.
- The **DELTA** button hears what the module removes. Use it to check that you are not removing the
  body of the voice.

## Measured behaviour

From `docs/measurements/dynamics.md`:

- Band isolation at matched reduction: 0.00 dB change outside the band in Split mode, against −12 dB
  in Wide mode.
- Attack, 10 to 90 %: 0.5 ms measured 0.52 ms (3.4 % error), 2 ms measured 2.03 ms, and 5 ms
  measured 5.07 ms. The attack is set in the host parameter list. It is not in the editor.
- Release, 10 to 90 %: 40 ms and 150 ms both measured within 0.0 %.

## Latency and CPU

- Latency is the lookahead, which is 0 by default. The lookahead is host only.
- CPU is about 0.27 % of a core for one band at 48 kHz. The report lists two bands at 0.26 %, which
  is not higher than one band. Treat the two-band figure as noise, and re-measure before you rely on it.

## Limits

- Attack (0.05 to 5 ms, default 0.4 ms) is not in the editor. Use the host parameter list to change it.
- The Wide mode and the lookahead are not in the editor.
- A band that is re-enabled starts from a clean state, so it does not click on enable.
