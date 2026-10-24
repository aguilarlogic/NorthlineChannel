# 16. Glossary

**ADAA (anti-derivative anti-aliasing).** A way to reduce aliasing in a nonlinear shaper by averaging
its antiderivative. Eco uses first order, HQ uses second order at 4×.

**A/B slots.** Two stored full settings, switched with the A and B buttons. See
[Compare and Delta](10-compare-and-delta.md).

**Attack.** The time for a dynamics module to respond to a rising level. In the compressor and gate it
is measured as the 10 to 90 % rise time of the gain change.

**Auto gain.** Calibrated make-up gain. In PRE it keeps the output level at the Operating Level for each
saturation type. In COMP it sets the make-up gain for a program at the Operating Level.

**Band.** One filter section of the EQ or the de-esser.

**Bell.** A peaking filter: it boosts or cuts around its centre frequency.

**BS.1770.** The ITU recommendation that defines loudness (LUFS) with K-weighting and gating.

**Bypass.** Passing a module without processing. The strip bypasses a disabled module with its latency
delay kept, so timing does not change.

**Character.** A preset family of obvious colour. Compare Starting points.

**Compare.** Switching between two settings to hear the difference. See A/B slots.

**Delta (Δ).** A listen mode that plays what a module removes: its output minus its input.

**dBFS.** Decibels relative to full scale. 0 dBFS is the largest sample value a digital system can
represent.

**dBTP.** Decibels relative to full scale, for true peak. It is the peak level of the signal after
reconstruction between samples.

**Dual mono (DUAL).** Each channel has its own detector, with no link between them.

**DE-ESS.** A module that reduces sibilance in one or two bands.

**EBU R128.** The EBU recommendation for loudness: a target of −23 LUFS and a true-peak limit.

**Eco.** The base quality. Saturation runs at the base rate, and the limiter's Safety Clip has no
oversampler. Zero latency.

**Expander.** A gate type that lowers the level below its threshold in a smooth curve.

**Gain reduction (GR).** The amount a module lowers the level, in dB. The Instance Hub shows it as a positive number.

**Gate.** A module that cuts the signal when its level falls below the threshold.

**Group.** In the Instance Hub, a set of parameters linked across instances. See
[Instance Hub](11-instance-hub.md).

**High shelf, low shelf.** A filter that boosts or cuts all frequencies above or below its corner
frequency.

**HQ.** The higher quality. Saturation runs at 4× with second-order ADAA. The limiter's Safety Clip runs at
4×. The latency is higher.

**Hysteresis (Hyst).** The gap between the level at which a gate opens and the level at which it
closes. It stops a gate from chattering.

**Integrated loudness.** The gated average loudness of a whole program, in LUFS.

**Init.** The preset with every module at its default value.

**K-weighting.** The frequency weighting used for loudness measurement. It boosts high frequencies and
cuts very low ones, to match the ear.

**Key.** The signal a detector reads to decide the gain. Also called the sidechain.

**Knee.** The width of the transition between no gain reduction and full gain reduction at the threshold.

**LRA (loudness range).** The spread between quiet and loud parts of a program, in LU. EBU Tech 3342.

**LU (loudness unit).** A relative loudness unit. 1 LU equals 1 dB of loudness.

**LUFS.** Loudness units relative to full scale, the unit of integrated, short-term and momentary loudness.

**Lookahead.** The delay of a dynamics module's audio, so that it can react before a transient arrives.
It adds latency.

**Latency.** The delay the plug-in adds, in samples. The host compensates for it.

**Level Match.** A trim that matches the loudness of the processed signal to the input. Modes: Off, Learn
and Live.

**Limiter.** A module that holds the output below the ceiling. Modes: Safety Clip, Brickwall and True Peak.

**M/S (mid and side).** Mid is (L + R) / 2 and side is (L − R) / 2. MS mode processes the two parts
separately.

**Mix.** The blend between the dry and processed signal.

**Momentary loudness.** Loudness over 400 ms, in LUFS.

**Monitor.** A mode that changes the output for listening: Stereo, Mono, Left, Right, Side or Swap.

**Operating Level.** The reference level for the calibrated processing. The default is −18 dBFS RMS.

**Param (Parametric).** A bell type whose Q is set by the Q control.

**PLR.** Peak-to-loudness ratio: the true-peak maximum minus the integrated loudness, in LU.

**Preset.** A saved setting of the whole strip, or of one module.

**Q.** The bandwidth of a bell: a higher Q is a narrower bell.

**REL (relative detection).** The de-esser mode that compares the band level with the full-band level.

**Release.** The time for a dynamics module to recover after the level falls.

**RMS.** Root mean square: the average power of a signal over a window. The Operating Level is an RMS value.

**Safety Clip.** The soft clipping limiter mode. Zero latency in Eco, and not a true-peak limiter.

**Shelf.** A filter that boosts or cuts everything beyond its corner frequency.

**Short-term loudness.** Loudness over 3 seconds, in LUFS.

**Sidechain (SC).** The detector input. EXT SC uses another signal as the key.

**Smart Start.** The listen-based starting point for the chain. See [Getting started](01-getting-started.md#smart-start).

**Spotlight.** The set of controls a preset highlights. Focus mode dims the others.

**ST (stereo).** The stereo mode in which the detector is linked across the two channels.

**Starting point.** A preset family with gentle, source-specific settings.

**Threshold.** The level at which a dynamics module starts to act.

**True peak.** The peak level of the reconstructed analog signal, measured with 4× oversampling.

**VU.** A meter with a standard ballistic that reads 0 VU at the reference level. In this plug-in, the
reference is the Operating Level.
