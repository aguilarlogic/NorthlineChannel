# 14. Accessibility

This chapter lists what the editor provides for people who use a keyboard, a screen reader, or who
need high contrast. It also lists what is not verified.

## Screen readers

The editor gives its controls to the operating system's accessibility interface:

| Control | Exposed as | Notes |
|---|---|---|
| Knob | Slider. The value is the formatted value with its unit, and a screen reader can set it by typing a value. | |
| Module card in the chain | Button. Pressing it opens the module. | |
| Segmented control (for example the saturation type, the gate type or the compressor type) | Group, with its value | |
| Chip (on or off button) | Button | |
| Gain-reduction bar | Progress bar, named "Gain reduction" | |

Screen reader testing is **not recorded** in this repository. The roadmap plans tests with NVDA on
Windows and VoiceOver on macOS. Until those tests are recorded, treat the screen reader support as
unverified.

## Keyboard

Knobs, module cards, dropdowns and buttons take keyboard focus. The keyboard controls are listed in
[chapter 13](13-settings-and-controls.md#keyboard). The main points:

- Arrow keys and Page Up and Page Down change a focused knob.
- Return opens the text entry for a focused knob, and Return or Space opens a focused module card.
- Esc closes an overlay.
- Ctrl+Z (Cmd+Z) undoes, and Ctrl+Shift+Z or Ctrl+Y redoes.

Keys that the editor does not use pass through to the host.

## Colour and contrast

- The colour themes are **Dark** (the default) and **High contrast**. Choose one in Settings.
- The test `ui_tokens_contrast` checks both themes. It requires text to reach a contrast ratio of
  4.5:1 against its background, and UI elements to reach 3:1. The test passes on this build.
- Amber marks Level Match while it learns, and loudness that is outside the target. Each module has
  its own accent colour.
- The listen buttons are an exception. They turn on in the PRE accent colour, not amber, in this build.
  Their tooltips still say amber. See [Compare and Delta](10-compare-and-delta.md#listen-buttons).

## Help and tooltips

- Turn on help mode with the **?** button in the chain strip. Hover over a control to see a short
  explanation of what it does, its range and its shortcut.
- Most controls have a tooltip. Hover over the control to read it.
- The first-run tour explains the chain, control editing, the right-click menu and Spotlight. Open it
  again from Settings.

## Size

- The editor scales from 75 % to 200 %, with the Scale list in the top bar, or by dragging the window
  corner. See [chapter 13](13-settings-and-controls.md#layout-and-size).

## What is not verified

- Screen reader behaviour, as described above.
- Keyboard focus order across the whole editor. The controls take focus, but the order has not been
  recorded in a test.
- Minimum text size. No test checks the font sizes against the roadmap targets.
