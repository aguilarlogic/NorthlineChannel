# 5. GATE

GATE is the second module in the default chain. It has three types. A gate closes below its
threshold and cuts the signal to the range you set. An expander lowers the level below its
threshold in a smooth curve. A duck lowers the signal while a key signal is above the threshold.

![GATE on the chain card and in the detail area](images/main-editor.png)

## Controls

Values are the defaults of a new instance.

| Control | Range and units | Default | What it does |
|---|---|---|---|
| **GATE** chip | on or off | off | Enables the module. |
| Type | Gate, Expander, Duck | Gate | Sets the gain law. See below. |
| Mode | ST, DUAL, MS | ST | Stereo mode. In ST the detector is linked across the channels. See [Signal flow](02-signal-flow.md#stereo-modes). |
| Thresh | −80 to 0 dB | −40 dB | Level at which the gate opens or the expander and duck act. |
| Hyst | 0 to 24 dB | 4 dB | Gap between the open and close levels. The gate closes at Thresh minus Hyst. |
| Range | −80 to 0 dB | −40 dB | Lowest gain. The gate cuts to this level when closed. The duck cuts by this amount. |
| Attack | 0.02 to 100 ms | 0.5 ms | Time for the gate to open. The gain rises along a raised-cosine curve in dB. |
| Hold | 0 to 500 ms | 30 ms | Time the gate stays open after the signal drops below the close level. |
| Release | 5 to 5000 ms | 150 ms | Time for the gate to close again. |
| Ratio | 1.2 to 8 : 1 | 2 : 1 | Expander only. The slope below the threshold. |
| Knee | 0 to 12 dB | 3 dB | Expander only. Width of the soft knee at the threshold. |
| SC HP | 20 Hz to 15 kHz | 20 Hz | High-pass on the detector. Cuts low frequencies from the key. |
| SC LP | 200 Hz to 20 kHz | 20 kHz | Low-pass on the detector. |
| **EXT SC** | on or off | off | Uses the sidechain input as the key instead of the track. |
| **LISTEN** | on or off | off | Plays the key signal in place of the output. Transient. |

Ratio and Knee are visible only when the type is Expander.

## Types

- **Gate.** Opens when the key is above the threshold. Closes when the key falls below the
  threshold minus the hysteresis, and the hold has run out.
- **Expander.** A downward expander with the ratio and knee above. It has no hard close, so the
  gain changes without a jump at the threshold. Range sets the lowest gain.
- **Duck.** Lowers the signal by the range while the key is above the threshold.

## Display

The scope shows the input level, the threshold, the hysteresis band and the open spans. The shaded
area marks the time the gate is open. The readout at the bottom shows the key level in dB.

## Tips

- Set Thresh first. Hyst keeps the gate from opening and closing repeatedly when the level sits
  near the threshold.
- Hold keeps the gate open through short gaps, such as the decay of a note.
- Keep Attack at 0.3 ms or more when the Range is deep. A shorter attack makes a click above 8 kHz.
  At a −60 dB range the splatter above 8 kHz is −60 dB with a 0.3 ms attack and −39 dB with 0.02 ms.
  The default 0.5 ms gives −69 dB.
- Use SC HP to stop low rumble from opening a gate on a kick or a bass.
- The Lookahead parameter (`gate_lookahead`) is host only. It delays the gate by up to 5 ms so it
  opens before the transient. It adds latency, and the default is off.

## Measured behaviour

From `docs/measurements/dynamics.md`:

- Detector chatter. A 40 Hz sine decays through the threshold at 20 dB/s with hold 0. The report
  counts one open or close transition at sidechain high-pass frequencies of 20 Hz and 30 Hz. The
  spec allows one open and close cycle.
- Kick transient loss. A closed gate opens on a click and 60 Hz body with a 0.5 ms opening. Loss in
  the first 5 ms is 0.25 dB without lookahead, and 0 dB with 1 ms or more.
- Opening click. The table in the report gives splatter at −39 dB for 0.02 ms, −60 dB for 0.3 ms
  and −69 dB for 0.5 ms (1 kHz tone, −60 dB floor).

## Latency and CPU

- Latency is the lookahead in samples, which is 0 by default. Lookahead is host only.
- CPU is 0.08 % of a core for Gate or Expander and 0.10 % for Duck, at 48 kHz.

## Limits

- The opening is not click-free at the 0.02 ms floor. Use 0.3 ms or more for deep ranges.
- The gate's Lookahead is host only, so the editor cannot turn it on.
