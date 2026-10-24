# 10. Compare and Delta

Four tools in the editor let you check what the processing does: Compare, Delta, the listen buttons,
and the A/B slots. Use Compare to hear the strip, or one module, out of the chain at the same
loudness. Use Delta and the listens to hear what a module adds or removes. Use A/B to switch between
two full settings.

## Compare

Compare is a loudness-matched bypass. A louder signal sounds better, so Compare plays the bypassed
signal at the level of the processed one, and you hear only the difference in tone and dynamics.

- **Whole strip.** Hold **COMPARE** in the top bar to hear the input instead of the processed output.
  Release it to go back. Shift-click COMPARE to latch it on, and click it again to release it.
  The strip learns the level of its output against its input while Compare is off, and holds that
  level while Compare is on. Level Match does not need to be on.
- **One module.** Click **CMP** in a module header to take that module out of the chain. The rest
  of the chain keeps running, and the module's learned level is added back. Click CMP on another
  module to move Compare there, or click it again to turn it off.
- Compare fades in and out over 20 ms, and it does not change the latency.

## Delta

Click **Δ** in a module header to hear only what that module adds or removes: its output minus its
input, aligned for latency. On DE-ESS and COMP, Δ turns on the module's DELTA listen.

## A and B: setting slots

The **A** and **B** buttons in the top bar hold two full settings.

- Each slot holds every parameter, the module order, the Spotlight and the preset name. Transient
  settings (monitor and the listen buttons) are not stored.
- Choosing A or B saves the current settings into the active slot, then loads the other slot. Each
  change appears in the history as `Show setting A` or `Show setting B`.
- **A>B** and **B>A** copy the active slot into the other slot.
- Both slots are saved with the session, so they survive a save and reload of the project.

The slots switch the whole state. They do not match loudness. A louder setting sounds better in an
A/B test, so set the same output level on both slots before you judge them. The VS TARGET readout
(chapter 9) and the Level Match mode both help with this. Level Match is part of each slot, so set
it on both slots if you want the same trim on each.

## Listen buttons

The listen buttons play a signal in place of the module output. They are for checking. They do not
change the sound.

| Module | Button | Plays |
|---|---|---|
| DE-ESS | BAND 1 | The signal of band 1 |
| DE-ESS | BAND 2 | The signal of band 2 |
| DE-ESS | DELTA | What the de-esser removes: its output minus its input |
| COMP | KEY | The signal that drives the detector |
| COMP | DELTA | What the compressor removes: its output minus its input |
| GATE | LISTEN | The key signal of the gate |

- Only one DE-ESS listen can be on at a time, and only one COMP listen at a time. The gate has one
  listen.
- The listen buttons are transient. They are not saved in presets, slots or undo. They turn off when
  a preset or session loads.
- Compare, Δ and the listen buttons light up amber while they are on, like every other transient
  state.
- Compare and Δ are transient too: they are not saved, and they turn off when a preset or session
  loads.

## Tips

- To check whether a de-esser is removing the voice, turn on DELTA. You should hear sibilance, not
  the body of the voice.
- To check what the compressor does to the level, turn on DELTA and move Thresh. The removed signal
  should follow the loud phrases.
- To check a gate, turn on LISTEN. You should hear the key signal open the gate.
