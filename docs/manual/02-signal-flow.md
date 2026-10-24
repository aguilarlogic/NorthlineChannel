# 2. Signal flow and the Operating Level

## The chain

The signal goes through these stages in this order:

```
 input: polarity, input gain (ramped over 20 ms)
   |
   +--> dry copy, delayed by the chain latency (used by the global mix and Level Match)
   |
 modules, in the order you set (default PRE, GATE, DE-ESS, EQ, COMP)
   |     a disabled module is faded out over 10 ms; its input is still delayed by its latency
   |
 Level Match trim (Off, Learn or Live)
   |
 global mix (dry / wet)                       host only: no editor control
   |
 output gain
   |
 limiter (Off, Safety Clip, Brickwall, True Peak), then the meters
   |
 monitor (Stereo, Mono, Left, Right, Side, Swap)
   |
 output
```

- **Input gain.** The input gain runs from −24 dB to +24 dB. The change is ramped over 20 ms, so
  moving it does not click.
- **Polarity.** Normal, Invert L, Invert R or Invert L+R. Use it to correct a phase reversal in a
  stereo pair.
- **Output gain.** Also −24 dB to +24 dB, ramped over 20 ms.
- **Monitor.** The monitor modes change the plug-in's output, so the host records them too. Set
  it back to Stereo before you print a track. Monitor is transient: it is not saved in presets,
  A/B slots or undo, and it returns to Stereo when a session or preset loads. See
  [Master section](09-master.md#monitor).

## Module order

The default order is PRE, GATE, DE-ESS, EQ, COMP. To change it:

- Drag the handle (≡) on the left of a module card to a new position, or
- Select a card and press Alt+Left or Alt+Right to move it one place.

Each move is one undo step. Undo reverses it.

## Stereo modes

Each module has a mode chip in its detail panel. The modes are:

| Mode | Meaning |
|---|---|
| ST | Stereo. The detector is linked across both channels (GATE, DE-ESS and COMP). PRE and EQ process L and R. |
| DUAL | Dual mono (GATE, DE-ESS and COMP only). Each channel has its own detector. |
| MS | Mid and side. The module works on mid and side. |

The strip converts between L/R and M/S as it runs the chain. It converts to M/S only when a module
in MS mode needs it, and it converts back before the next module that needs L/R.

## Operating Level

The Operating Level is the reference level for the processing and the meters. It sets 0 VU, and it
is the level the saturation and the compressor are calibrated for.

| Setting | Default | Range |
|---|---|---|
| Operating Level (`op_level`) | −18 dBFS RMS | −24 to −10 dBFS RMS |

The Operating Level affects:

- **PRE saturation.** Auto gain (AUTO) calibrates each saturation type so that the output level at
  the Operating Level stays within 0.4 dB of the input. The measured worst case over all types and
  drives is 0.39 dB (Eco) and 0.32 dB (HQ).
- **COMP auto make-up.** The make-up gain is computed for a program at the Operating Level.
- **Smart Start.** The input trim brings the program to the Operating Level.
- **VU meter.** The VU scale reads 0 VU at the Operating Level.

Set the Operating Level once to the level you mix at. If you change it, the saturation and
make-up gains change with it.

The Operating Level is non-automatable. Hosts cannot write it from automation.

## Latency

The strip reports one total latency to the host. It is the sum of:

- the module latency: PRE reports 48 samples when quality is HQ and 0 otherwise. GATE, DE-ESS and
  COMP report their lookahead, which is 0 by default. EQ reports 0.
- the master latency: the limiter reports 0 (Safety Clip, Eco), 60 (Safety Clip, HQ), or the
  lookahead for Brickwall and True Peak (about 1.5 ms).

The top bar shows the total as `Lat N smp`. Chapter 9 has the limiter values for each sample rate.
Latency changes only when you change quality or the limiter mode. Switching a module on or off
never changes the reported latency.

## Constant latency

Every module keeps its timing when you switch it off. A disabled module passes its input through a
delay that matches its latency. The dry path of the global mix and Level Match is delayed to match.
Your host sees the same latency, and the same signal timing, with any module on or off.
