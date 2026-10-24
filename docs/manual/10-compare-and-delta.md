# 10. Compare and Delta

Two tools in the editor let you check what the processing does: the A/B slots, and the listen
buttons on the DE-ESS and COMP modules. Use the listens to hear what a module removes. Use A/B to
switch between two full settings.

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
- The tooltips say "amber while on". In this build an active listen lights up in the PRE accent
  colour (pink in the default theme), on every module.

## What the engine supports without an editor control

The engine supports the following. The editor has no button for them in this build, so you cannot
reach them from the screen:

- **Whole-strip Compare.** Bypasses the whole chain and plays the input, aligned for latency, while
  the output gain stays the same.
- **Module Compare.** Bypasses one module and plays it at the level it had when it was on, learned
  from the signal.
- **Delta for PRE, GATE and EQ.** Plays what each of those modules adds or removes.

These paths run in the audio engine and are covered by the tests in `tools/tests/TestSmart.cpp`.
They are not in the editor.

## Tips

- To check whether a de-esser is removing the voice, turn on DELTA. You should hear sibilance, not
  the body of the voice.
- To check what the compressor does to the level, turn on DELTA and move Thresh. The removed signal
  should follow the loud phrases.
- To check a gate, turn on LISTEN. You should hear the key signal open the gate.
