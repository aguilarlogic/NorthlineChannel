# 9. Master section

The master column runs down the right of the editor, 300 px wide. It has the input and output
controls, the meters, the limiter, Level Match, the loudness readouts and the vectorscope.

![Master column: meters, limiter, Level Match and loudness](images/main-editor.png)

## Input, output and meters

| Control | Range | Default | What it does |
|---|---|---|---|
| Input | −24 to +24 dB | 0 dB | Gain at the start of the chain. |
| Output | −24 to +24 dB | 0 dB | Gain after the global mix, before the limiter. |
| Op Level | −24 to −10 dBFS RMS | −18 dBFS | The Operating Level. See [Signal flow](02-signal-flow.md#operating-level). |
| Polarity | Normal, Invert L, Invert R, Invert L+R | Normal | Flips the phase of one or both channels. |
| VU source | Input, Output, GR | Output | Signal the VU meter shows. |
| Monitor | Stereo, Mono, Left, Right, Side, Swap | Stereo | Changes what the plug-in outputs. See below. |

- **IN and OUT** show peak and RMS for each channel. A clip lights the meter. Click a meter to clear
  its clip indicator.
- **VU OUT** shows the VU reading. The reading at the Operating Level is 0 VU, and the readout beside
  it shows the level in dBFS.
- **GR** shows the gain reduction of the limiter. The history below shows the last 10 seconds.

### Monitor

Monitor modes help you hear one part of the signal:

| Mode | Output |
|---|---|
| Stereo | Left and right, unchanged. The normal setting. |
| Mono | Both channels carry (L + R) / 2. |
| Left | Both channels carry the left channel. |
| Right | Both channels carry the right channel. |
| Side | Both channels carry (L − R) / 2. |
| Swap | Left and right are exchanged. |

The monitor changes the plug-in output, so the host records it. Set it to Stereo before you print
a track. It is transient: it is not saved in presets, A/B slots or undo, and it returns to Stereo
when a session or preset loads.

## Limiter

The limiter is the last processing stage. It is off by default.

| Control | Range | Default | What it does |
|---|---|---|---|
| LIMIT | on or off | off | Enables the limiter. When it is off, the output is not limited. |
| Mode | Safety Clip, Brickwall, True Peak | Safety Clip | Sets the algorithm. See below. |
| Ceiling | −30 to 0 dB | −0.3 dB | Maximum output level for the limiter. |
| Release | 10 to 1000 ms | 100 ms | Recovery time of the gain after a peak. It depends on the program. |

The mode is not automatable. Latency depends on the mode and on quality, not on LIMIT.

### Modes

- **Safety Clip.** A smooth soft clipper with zero latency at Eco. In HQ it runs at 4× and adds 60
  samples of latency. Its output is transparent below 0.7 × the ceiling. Near and above the ceiling
  it bends the peaks. **It is a clipper, not a true-peak limiter.** It does not hold the ceiling on
  inter-sample peaks. See the measurements below.
- **Brickwall.** A lookahead peak limiter with 1.5 ms of lookahead. It holds the sample peaks at the
  ceiling. It does not control inter-sample peaks.
- **True Peak.** The Brickwall limiter with a 4× detector for inter-sample peaks. It holds the true
  peak at the ceiling, with 0.1 dB of margin.

### Latency

The limiter reports the latency below, in samples. The value is the same for LIMIT on and off.

| Mode | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| Safety Clip, Eco | 0 | 0 | 0 | 0 |
| Safety Clip, HQ | 60 | 60 | 60 | 60 |
| Brickwall | 66 | 72 | 144 | 288 |
| True Peak | 66 | 72 | 144 | 288 |

The strip adds the module latency to this value. With PRE in HQ, the total is 108 samples at any
rate in Safety Clip HQ mode, and 120 samples at 48 kHz with Brickwall in HQ.

### Measured peaks

From `docs/measurements/master.md`. The input was driven 10 dB over a −1 dBFS ceiling. The 4× meter
is the BS.1770-4 true-peak meter that MeterHub reads.

| Mode | Signal | Sample peak (dBFS) | True peak, 4× meter (dBTP) |
|---|---|---|---|
| Safety Clip, Eco | Pink noise | −1.00 | 0.08 |
| Safety Clip, Eco | Band-limited drums | −1.00 | 0.40 |
| Safety Clip, Eco | Sine sweep, 20 Hz to 0.45 fs | −1.00 | 1.26 |
| Safety Clip, HQ | Band-limited drums | −1.00 | 1.89 |
| Brickwall | Band-limited drums | −1.00 | 1.75 |
| True Peak | Pink noise | −1.10 | −1.10 |
| True Peak | Band-limited drums | −1.10 | −1.10 |
| True Peak | Sine sweep | −1.10 | −1.10 |

The sample peak holds at the ceiling in every mode. The true peak reads above the ceiling in Safety
Clip and Brickwall. **For a true-peak delivery limit, use True Peak mode.** Use Brickwall only when
sample peaks are the limit.

A sine at the ceiling gets no gain reduction. A sine 6 dB over the ceiling gets 6 dB of gain
reduction, in each ceiling tested from −1 to −12 dB.

### Release

Release is program dependent. A burst 12 dB over the ceiling takes this long to recover 63 % of the
gain of a quiet 1 kHz tone that follows it (Brickwall, ceiling −6 dB):

| Release knob | 4 ms burst | 50 ms burst | 300 ms burst | 1.5 s burst |
|---|---|---|---|---|
| 30 ms | 14 ms | 95 ms | 122 ms | 122 ms |
| 100 ms | 30 ms | 110 ms | 396 ms | 402 ms |
| 300 ms | 80 ms | 111 ms | 662 ms | 1194 ms |

## Level Match

Level Match matches the loudness of the processed signal to the input. Use it to judge the
processing by ear, not the volume change. It acts after the module chain, before the global mix and
the output gain.

| Mode | What it does |
|---|---|
| Off | No trim. The default. |
| Learn | Measures the loudness difference over 3 to 10 seconds of program, then holds a fixed trim. Nothing changes after that. |
| Live | Follows the loudness difference as the program changes. It averages over 3 seconds and changes by at most 1 dB per second. |

- **Trim** is the fixed gain Learn writes. You can set it by hand, from −12 to +12 dB.
- **RELEARN** starts a new Learn pass, or restarts Live tracking.
- The status line reads **Idle**, **Learning** (with a progress bar), **Learned** or **Live**.
- Learn needs at least 3 seconds of active signal. It stops after 10 seconds, or as soon as the estimate
  has been stable within 0.2 LU for the last 10 measurement blocks.
- Live counts only active blocks. It holds its gain during silence. It is clamped to ±12 dB.

The engine tests require Learn to match the true loudness difference to within 0.3 LU. They also
require the peak after a gap to come back within 0.3 dB of its steady value, and the noise floor
to move by no more than 0.5 dB during the gap.

## Loudness

The loudness panel measures the output. It runs whether or not the editor is open.

### Target

Choose a target from the list:

| Target | Integrated | True peak |
|---|---|---|
| Streaming | −14 LUFS | −1 dBTP |
| Apple Music | −16 LUFS | −1 dBTP |
| Podcast | −16 LUFS | −1 dBTP |
| EBU R128 | −23 LUFS | −1 dBTP |
| ATSC A/85 | −24 LUFS | −2 dBTP |
| Custom | −14 LUFS (default) | −1 dBTP |

Custom sets the integrated target in 0.5 LU steps, from −40 to −5 LUFS.

The target is a reference for the readout. It does not change the audio.

### Readouts

| Readout | Meaning |
|---|---|
| MOMENTARY | Loudness over the last 400 ms, in LUFS. |
| SHORT-TERM | Loudness over the last 3 seconds, in LUFS. |
| INTEGRATED | Gated loudness of the whole program, in LUFS. |
| LRA | Loudness range, EBU Tech 3342, in LU. |
| TRUE PEAK | Maximum true peak, from the 4× meter, in dBTP. |
| PLR | True-peak maximum minus integrated loudness, in LU. |
| OVERS | Output samples above the limiter ceiling. |
| VS TARGET | Integrated loudness minus the target, in LU. Green within 1 LU of the target, amber outside it. |

**RESET** clears the integrated loudness, LRA, the maxima and the overs counter. **OPTIONS** sets
whether integration pauses when the host transport stops, and whether it resets when playback starts.

The loudness numbers match between realtime and offline rendering (`master.md`): the integrated
loudness and true peak are identical at block sizes from 1 to 4096 and for the whole file.

### Vectorscope

The vectorscope shows left and right, rotated 45 degrees. Mono is a vertical line. The correlation
bar reads +1 for mono, 0 for wide, and −1 for out of phase.

## Latency and CPU

- Latency is set by the limiter mode and by quality. See the table above.
- The MeterHub (loudness and peak metering) costs about 0.09 % of a core at 48 kHz stereo.
- The limiter costs 0.07 % of a core with LIMIT off, and 0.14 % (Brickwall) to 0.42 % (True Peak) with LIMIT
  on and +10 dB of drive. Safety Clip in HQ costs 0.99 % with LIMIT on at the same drive.
