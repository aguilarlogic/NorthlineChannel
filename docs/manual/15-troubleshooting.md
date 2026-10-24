# 15. Troubleshooting and FAQ

## The plug-in does not appear in my host

- Check the install location. On Windows, the VST3 folder is `C:\Program Files\Common Files\VST3`.
  On macOS, it is `/Library/Audio/Plug-Ins/VST3` for a system install, or
  `~/Library/Audio/Plug-Ins/VST3` for one user.
- Rescan the plug-ins in your host. Some hosts keep a cache, so you may need to clear it as well.
- Check the host's plug-in format list. The build provides VST3 and Standalone on Windows, and VST3,
  AU and Standalone on macOS.

## CPU use is high

- Quality HQ runs the PRE saturation and the Safety Clip limiter at 4×. With PRE in HQ and every
  module on, PRE alone costs about 2.5 % of a core at 48 kHz, against a budget of 1.5 % for the whole
  strip. Switch PRE to Eco on the busy tracks.
- The EQ dynamic bands cost more while their gain glides. At 96 kHz, four gliding dynamic bands cost
  about 1.6 % of a core.
- The top bar's **CPU** readout shows the current load. **Show CPU** in Settings shows the load per module.

## The Hub shows only one instance

The Hub shows instances in its own plug-in process only. Hosts that run plug-ins in separate processes
(plug-in sandboxing, bridged 32-bit and 64-bit plug-ins) keep the other instances out of view. Check
whether your host does this. The Hub states the limit when it opens.

## Clicks when I change quality

Switching Eco and HQ is not click-free. It changes the latency by 48 samples, and the delay lines
reset. Switch quality when the audio is silent, and do not switch it during a mix pass.

## The limiter lets peaks above the ceiling through

Check the limiter mode, and the meter you are reading.

- **Safety Clip** is a soft clipper. It does not hold the ceiling on inter-sample peaks. In the
  measurements the true-peak meter reads up to 1.9 dB above a −1 dB ceiling.
- **Brickwall** holds the sample peaks. Its true-peak reading can still go above the ceiling.
- **True Peak** holds the true peak, with 0.1 dB of margin.

For a true-peak delivery limit, choose True Peak. See [Master section](09-master.md#measured-peaks).

## Latency is higher than I expect

The strip reports the sum of its module latencies and the limiter latency. Check the top bar's **Lat**
readout. Quality HQ adds 48 samples in PRE, and the Safety Clip in HQ adds 60. Brickwall and True Peak
add about 1.5 ms (72 samples at 48 kHz). The latency is the same whether a module is on or off.

## Level Match shows Idle, or it does not change

- **Idle** means Level Match is Off. Off applies no gain, so the trim you set has no effect until you
  choose Learn or Live.
- Learn needs at least 3 seconds of active program. Silence does not count. Play the program, then
  wait for **Learned**.
- Press **RELEARN** to start a new Learn pass after you change the processing.

## The EQ Q knob does nothing

The Q knob is used only by Bell and Param bands. Shelf, Res Shelf, Wide and Narrow have a fixed Q, so
the knob has no effect on them. Change the band type to Bell or Param before you set Q. See
[EQ](07-eq.md#band-types).

## The EQ dynamic bands are missing from the editor

The dynamic-band settings have no control in this build. The EQ display shows the dynamic gain when a host
sets it. See [EQ, dynamic bands](07-eq.md#dynamic-bands-host-only).

## The sidechain key does not trigger the module

- Turn on **EXT SC** (or **EXT** in the de-esser, or **EXT SC** in the gate and compressor). Without it,
  the module detects from its own input.
- Check that your host sends the sidechain signal to the plug-in.
- Use **KEY** (COMP) or **LISTEN** (GATE) to hear what the detector is hearing.

## Smart Start result is not what I expected

Smart Start has no information about the rest of the mix. Its result is a starting point. Undo it
(Ctrl+Z), and adjust the controls it changed. Those controls are highlighted in Spotlight. Try a
longer, cleaner capture of the source. Smart Start listens for 12 seconds.

## The listen buttons look pink, not amber

The tooltips say amber, but this build lights the listen buttons in the PRE accent colour. The listen
still works. See [Compare and Delta](10-compare-and-delta.md#listen-buttons).

## My presets are missing

- Check the presets folder: `Documents\Northline Audio\Northline Channel\Presets` on Windows.
- A user preset is a `.nlpreset` file. Check the folder list in the browser, and the root folder.
- A preset you moved to the trash is in the operating system's trash, and you can restore it there.

## The window is the wrong size

Use the **Scale** list in the top bar, or drag the window corner. Settings → **Apply default to all open
instances** sets every open instance to the default size.

## FAQ

**Does the plug-in report its latency to the host?**
Yes. The strip reports its total latency, and the host compensates for it.

**Why does the limiter add latency?**
Brickwall and True Peak look ahead by 1.5 ms so that they can lower the gain before a peak arrives.
Safety Clip in Eco has no lookahead.

**Does turning a module off change the timing?**
No. A disabled module keeps the same delay, so the signal timing is the same with or without it.

**Is the Operating Level the same as the output level?**
No. The Operating Level is the reference for the calibrated processing and the VU meter. The output gain
is a separate control. See [Signal flow](02-signal-flow.md#operating-level).

**Can I automate the quality or the limiter mode?**
No. Those settings are not automatable.

**Which parameters are not in the editor?**
Chapter 13 lists them, with their ranges and defaults.

**Where are the test and measurement reports?**
In the repository, under `docs/measurements`. See [its index](../measurements/README.md).
