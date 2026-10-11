# Dynamics DSP for a Channel-Strip Plugin (Compressors, De-esser, Gate, Transient Shaper, Limiter)

Research status: partial. About 15 tool calls were used. Firecrawl hit a rate limit, and the WebFetch of the Giannoulis/Massberg/Reiss tutorial PDF failed with a certificate error, so no full text was read. Everything below comes from search-result abstracts and secondary summaries unless a source is marked as full text. Items under "Unverified recall" are from background knowledge and must be checked against a primary source before use. Sections for gates/expanders, transient shapers, parallel mix/latency, sidechain/stereo linking, metering, and CPU optimisation have no sourced findings yet.

## Compressor architecture: topology, detectors, static curve

### Takeaway
The most-cited modern reference is Giannoulis, Massberg and Reiss (JAES 2012), which recommends feedforward topology and log-domain level detection placed after the gain computer. Full-text equations were not verified this session.

### Cited Findings
- Giannoulis, Massberg & Reiss, "Digital Dynamic Range Compressor Design - A Tutorial and Analysis", JAES vol. 60, no. 6, pp. 399-408 (2012). Covers digital versions of classic analog designs, RMS vs peak detection, feedforward vs feedback, and linear vs log detection. Source and citation: [MathWorks reference](https://au.mathworks.com/help/audio/ref/compressor.html); [Academia.edu record](https://www.academia.edu/31481096/Digital_Dynamic_Range_Compressor_Design_A_Tutorial_and_Analysis). Code and audio examples are said to be online. The full PDF is restricted to AES members.
- The tutorial endorses feedforward algorithms as stable and predictable. It places the level detector in the log domain after the gain computer, which is said to give a smooth envelope, no attack lag, and a variable knee width. Source: [Academia.edu record](https://www.academia.edu/31481096/Digital_Dynamic_Range_Compressor_Design_A_Tutorial_and_Analysis). The search summary flagged that page's "key takeaways" as apparently AI-generated, so treat this claim as secondary until checked against the AES text ([AES e-library](https://aes2.org/publications/elibrary-page/?id=16354), not read in full).
- A follow-up by the same authors, "Parameter Automation in a Dynamic Range Compressor" (JAES 2013, vol. 61, no. 10, pp. 716-726), is listed in the [Reiss slide deck record](https://www.scribd.com/document/515732811/Reiss-Tutorialondynamicrangecompression). Content not checked.
- chowdsp_utils (JUCE modules, Jatin Chowdhury) has a `CompressorLevelDetector` with peak, RT-tracking peak, and RMS modes. Setters take attack and release in ms and threshold in dB. Most modules need C++17; non-Common/DSP modules need JUCE 6+. Source: [chowdsp_utils docs](https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_utils/classchowdsp_1_1compressor_1_1CompressorLevelDetector.html), [library root](https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_utils).
- Faust compressors.lib uses a "strength" parameter instead of ratio: 0 means no compression, 1 means hard limiting, above 1 over-compresses. The stated reason is that ratio is awkward in a Faust UI. Helpers include a mono gain computer that outputs dB. Its generic stereo algorithm has four parameters, and the 1176-style limiter is a specialised fixed-parameter version. Source: [Faust compressors library](https://faustlibraries.grame.fr/libs/compressors/). A CCRMA student project, comp.dsp, implements feedforward and feedback architectures ([CCRMA projects page](https://ccrma.stanford.edu/~mjolsen/projects)).
- A differentiable PyTorch compressor (diffFx) is documented with source; per the search summary it cites Giannoulis et al. ([diffFx compressor source](https://difffx-pytorch.readthedocs.io/en/latest/_modules/diffFx_pytorch/processors/dynamics/compressor.html)). Not read.

### Inferences
- Feedforward plus log-domain detection after the gain computer appears to be the common modern default. chowdsp_utils and the Faust library both expose detector mode choices (peak / RMS / RT-peak) and a strength or ratio control, which suggests a channel-strip design should expose detector type as a parameter.
- Feedback topology is not described in any source found, so its sound character cannot be stated from these notes.

### Gaps
- No source found on how peak, RMS, or hybrid detectors sound, or on the "smooth branching" and "decoupled" peak detector structures. The Giannoulis full text is needed for these.
- The Giannoulis full-text equations for the soft-knee gain computer and the attack/release smoothing coefficient were not verified (see Unverified recall below).

## Analog character modelling: VCA, FET (1176), opto (LA-2A), and neural models

### Takeaway
The 1176 "All Buttons" mode is well documented qualitatively, but no published measurement compares a digital model against hardware. The LA-2A T4 cell has a documented two-stage release, but the sources disagree on total recovery time. Neural modelling of the LA-2A is active academic work with abstract-level evidence.

### Cited Findings
**1176 / FET**
- All-buttons-in mode was an unintended discovery. Pressing all four ratio buttons together raised harmonic distortion. Source: [Wikipedia, 1176 Peak Limiter](https://en.wikipedia.org/wiki/1176_Peak_Limiter); [Moore, Huddersfield](https://eprints.hud.ac.uk/id/eprint/27391/1/Journal%20on%20the%20Art%20of%20Record%20Production%20%C2%BB%20All%20Buttons%20In_%20An%20investigation%20into%20the%20use%20of%20the%201176%20FET%20compressor%20in%20popular%20music%20production.pdf).
- Will Shanks, as quoted on Wikipedia, says the all-buttons curve is "more like severe plateau" compared with the gentle 4:1 slope. He also describes a lag on the attack of initial transients, which he calls "reverse look-ahead". Source: [Wikipedia, 1176 Peak Limiter](https://en.wikipedia.org/wiki/1176_Peak_Limiter).
- Austin Moore's analysis of a transfer-function diagram in an older UREI manual found that the threshold changes with ratio. He also noted that the knee appears to change. The qualitative investigation is in [Moore, Huddersfield](https://eprints.hud.ac.uk/id/eprint/27391/1/Journal%20on%20the%20Art%20of%20Record%20Production%20%C2%BB%20All%20Buttons%20In_%20An%20investigation%20into%20the%20use%20of%20the%201176%20FET%20compressor%20in%20popular%20music%20production.pdf).
- Digital emulations: UA 1176 Classic claims component-level modelling of transformers and FET amplifiers (vendor claim) ([UA product page](https://www.uaudio.com/products/ua-1176-fet)). Arturia FET-76 claims a TAE-modelled FET, transformer, and transistor path with an "All Buttons" mode ([JRR listing, vendor text](https://jrrshop.com/arturia-fet-76-1176-compressor-plugin)). Black Rooster VLA-FET claims a modelled FET stage, transformer flux saturation, and a ratio switch past 20:1 ([Black Rooster](https://blackroosteraudio.com/en/products/vla-fet)). Native Instruments VC 76 has no all-buttons mode and tops out at 16:1 (search summary; source not separately confirmed).
- Standard UA manual: [1176 Classic FET Compressor Manual](https://help.uaudio.com/hc/en-us/articles/34530260482324-1176-Classic-FET-Compressor-Manual).

**LA-2A / T4 opto cell**
- The T4 uses an electroluminescent panel driven by the audio signal, which lights a cadmium sulfide photocell that sets gain reduction. There are no user attack or release controls. Source: [Sound On Sound](https://www.soundonsound.com/node/4913644) (search summary; the page was not read in full).
- Release is two-stage. About half of the resistance recovers in roughly 40-80 ms and the rest over several seconds. The slow stage ("memory effect") depends on how long and how brightly the cell was lit before the signal dropped. Source: [Mix Online](https://www.mixonline.com/technology/universal-audio-la-2a-370120).
- Sources disagree on full recovery time. Sound On Sound: about 1 to 15 s, slowest after long time above threshold ([SOS](https://www.soundonsound.com/node/4913644)). UA manual: "can take a few minutes" ([UA LA-2A manual](https://help.uaudio.com/hc/en-us/articles/19378009641748-LA-2A-Tube-Compressor-Manual)). Vintage King spec: 0.5 to 5 s for complete release, 0.06 s for 50% ([Vintage King](https://vintageking.com/products/universal-audio-la-2a)). Treat these as rough until measured.
- Commercial T4 emulations: Niviem's free OPT4 (vendor description: EL panel behaviour, CdS gamma curve, two-stage release, memory effect) ([KVR listing](https://www.kvraudio.com/product/opt4-by-niviem)). Cakewalk CA-2A T-Type also models the tubes (vendor claim).
- Vactrol modelling (not T4-specific): Najnudel, Müller, Hélie and Roze (DAFx 2023) propose a physical, passive vactrol model based on semiconductor first principles and port-Hamiltonian systems, fitted to a real vactrol and placed in a simple optical compressor simulation. Source: [DAFx23 paper 50](https://www.dafx.de/paper-archive/2023/DAFx23_paper_50.pdf). The T4 uses an EL panel and CdS cell rather than an LED/LDR pair, so this is a method reference, not a T4 model. That relationship is my inference.
- Eichas (HSU Hamburg) models an optocoupler-based dynamic range control circuit and says the LA-2A's optical element is the T4 cell ([Eichas PDF](https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2017/10/Eichas-Modeling-of-an-optocoupler-based-audio-dynamic-range-control-circuit-99480W.pdf)). Not read in full.

**Neural models**
- Steinmetz & Reiss, "Efficient neural networks for real-time modeling of analog dynamic range compression" (arXiv 2102.06200; AES 152nd Convention, 2022). Modifies temporal convolutional networks with very sparse kernels and rapidly growing dilations to get a large receptive field with fewer layers. According to the abstract, the model is causal, reaches state-of-the-art accuracy on the LA-2A, runs in real time on CPU, and needs about 10 minutes of training data. The first arXiv version claimed a 4x speedup and used about 1% of prior training data. Source: [arXiv 2102.06200](https://arxiv.org/pdf/2102.06200).
- "Modeling Analog Dynamic Range Compressors using Deep Learning and State-space Models" (arXiv 2403.16331). Per the search summary, it uses S4 layers to model the LA-2A causally, with about the same quality as earlier deep models and fewer parameters. Author list not verified. Source: [arXiv 2403.16331](https://arxiv.org/pdf/2403.16331).
- Simionato & Fasciani (2024), "Modeling Time-Variant Responses of Optical Compressors with Selective State Space Models" (arXiv 2408.12549). Conditions attack and release on external parameters using FiLM and GLU, aimed at low-latency live use. Source: [arXiv 2408.12549](https://arxiv.org/html/2408.12549v1). Abstract-level evidence only.

### Inferences
- A channel-strip design can expose a "program-dependent release" model for the opto character: a fast stage plus a slow stage whose time constant depends on accumulated gain reduction. The sources support this structure qualitatively, not numerically.
- The 1176 all-buttons effects (plateau curve, threshold shift with ratio, transient lag) can be treated as deliberate test cases for any model, since the sources describe them qualitatively and no published measurement exists.
- Neural models are an option for opto (LA-2A) character with abstract-level evidence of real-time CPU viability. The peer-reviewed status of the 2024 arXiv preprints was not checked.

### Gaps
- No DAFx or AES paper on modelling the 1176 was found. No DAFx paper specifically on the T4 cell was found (the closest is the vactrol paper above).
- No independent measurement compares a digital 1176 model's all-buttons nonlinearity to hardware (THD or GR curves).
- No source found for VCA (SSL G-bus, dbx 202/2150) or vari-mu (Fairchild) modelling, or for Distressor-style circuits.
- Model comparisons using THD, GR curves, or a listening test were not found.

## Aliasing and oversampling in compressors and nonlinear stages

### Takeaway
Parker, Zavalishin and Le Bivic (DAFx 2016) introduced a continuous-time convolution approach to reduce aliasing in waveshapers, and antiderivative antialiasing (ADAA) is the main lightweight follow-up. No source found says when a compressor's gain path needs oversampling. The ADAA results apply to memoryless nonlinearities, which covers clippers and saturators more directly than gain computers.

### Cited Findings
- Parker, Zavalishin, Le Bivic, "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution", DAFx 2016. The method builds a continuous-time approximation of the discrete signal, applies the nonlinearity, and filters with analytically applied convolution. According to the abstract it markedly reduces aliasing, especially with low-order oversampling, and extends to feedback systems. Source: [DAFx archive](https://dafx.de/paper-archive/details/vem_XXF5qBbfiWOH2RVVAA). Full text not read.
- A CCRMA notebook credits the same trio (Native Instruments at the time) with introducing antiderivative antialiasing in that DAFx 2016 paper. Source: [Chowdhury ADAA notebook](https://ccrma.stanford.edu/~jatin/Notebooks/adaa.html). Attribution is from the notebook, not verified.
- The memoryless ADAA IEEE paper by Bilbao, Esqueda, Parker and Välimäki is cited in a Vicanek reference list. The core ADAA method swaps the nonlinearity for its antiderivative and takes a discrete difference. Source: [Vicanek ADAA PDF](https://www.vicanek.de/articles/AADistortion.pdf). Attribution secondary.
- Esqueda et al. (Applied Sciences 2017) apply first-order ADAA to Lockhart and Serge wavefolders without high oversampling. Source: [MDPI Applied Sciences 7(12):1328](https://www.mdpi.com/2076-3417/7/12/1328) (search summary).
- La Pastina, D'Angelo and Gabrielli (DAFx 2020) propose arbitrary-order IIR ADAA, arguing FIR kernels are sub-optimal. Source: [DAFx20in21 paper 27](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx20in21_paper_27.pdf) (search summary).
- Gabrielli and Squartini (DAFx 2025) use numerical integration of lookup tables to approximate the antiderivative, for measured nonlinearities. Source: [DAFx25 paper 30](https://dafx25.dii.univpm.it/wp-content/uploads/2025/08/DAFx25_paper_30.pdf). Author attribution from the search summary, not verified.
- A further paper extends ADAA to any piecewise polynomial waveshaper and smooths piecewise-linear corners. Source: [DAFx archive, piecewise polynomial waveshapers](https://dafx.de/paper-archive/details/cu51KU_JsiXj1cvMi-2DOw).

### Inferences
- Gain-computer output is a control signal. When it multiplies audio, the sidebands it creates are the aliasing risk. Oversampling the control path alone is not what the aliasing literature addresses. Smoothing the gain signal (a fixed attack/release floor) reduces that risk. This is an inference from the cited ADAA work, not something a source states about compressors.
- For a clipper or saturator in the output stage, ADAA (first or higher order) is a cheap candidate before reaching for oversampling. This is inference from the cited papers.

### Gaps
- No source found on how vendors (Waves, FabFilter, UA, etc.) mitigate compressor aliasing. No source found on whether oversampling is used in compressors.
- No measured comparison of ADAA against oversampling for compressor or clipper aliasing was found.

## Sidechain, stereo linking, lookahead, parallel mix, auto makeup

### Takeaway
Only partial material. The chowdsp_utils detector set (peak / RT-peak / RMS) and the Faust strength parameter are the only sourced items. Stereo linking options, sidechain HPF/key filter design, lookahead latency, parallel-mix latency, and auto makeup were not covered by sources found.

### Cited Findings
- De-esser sidechain filtering exists in FabFilter Pro-DS (custom sidechain filter path; adjustable detection HPF/LPF) ([FabFilter Pro-DS help](https://www.fabfilter.com/help/pro-ds)). This is a sidechain key-filter example, not a general sidechain design.
- Lookahead up to 15 ms is quoted for Pro-DS (same source).

### Inferences
- Stereo linking, lookahead latency compensation for parallel mixes, and makeup gain choices are standard channel-strip features. The sources found do not say how to implement them, so these notes do not cover them.

### Gaps
- No sourced guidance on stereo linking (max, average, M/S), sidechain listen, lookahead latency reporting, or parallel-mix phase alignment.
- No sourced guidance on auto makeup gain or its pitfalls.

## De-esser designs

### Takeaway
Three main designs are documented: wideband, split-band, and dynamic EQ. Vendor docs describe Waves DeEsser's split and wideband modes and FabFilter Pro-DS's Single Vocal and Allround detectors, but neither vendor explains its detector in detail. Advice on wideband artifacts and crossover choice comes from practitioner write-ups and is secondary.

### Cited Findings
- Waves DeEsser: highpass or bandpass sidechain mode; frequency range 2 kHz to 16 kHz; split mode attenuates only the high band; wideband attenuates the whole band; hard-knee compression logic; warns that a low threshold causes muffled sound. Source: [Waves DeEsser manual](https://images.equipboard.com/uploads/item/manual/4729/waves-deesser-manual.pdf) (search summary; text not read).
- FabFilter Pro-DS: "Single Vocal" mode separates sibilance from non-sibilance in individual vocal recordings (claimed to work without manual tuning). "Allround" mode uses a custom sidechain filter path for full mixes or HF limiting. Processing is wideband or linear-phase split-band. Lookahead up to 15 ms. Source: [FabFilter Pro-DS help](https://www.fabfilter.com/help/pro-ds). Vendor marketing; the detection method is not described.
- Wideband de-essing turns the whole signal down on each sibilant. Thresholds cannot go as low without audible artifacts, and it can pump on exposed vocals. Source: [Waves DeEsser manual](https://images.equipboard.com/uploads/item/manual/4729/waves-deesser-manual.pdf) (search summary attributes these points to Waves).
- Dynamic EQ dips a parametric band centred on the sibilant frequencies. It gives Q, bell/shelf, and multi-band control, and leaves soft consonants untouched. Tradeoff: more setup time. Source: practitioner summary in [Pro Tools Expert](https://www.pro-tools-expert.com/production-expert-1/the-dos-and-donts-of-using-a-de-esser) (search summary; not read in full).
- Sidechain filtering: a broadband de-esser EQs the sidechain so sibilant frequencies dominate detection. Source: [Antares de-esser explainer](https://www.antarestech.com/blog/what-is-a-de-esser-how-it-works-and-how-to-use-one-on-vocals) (search summary).

### Inferences
- A channel-strip de-esser can expose a sidechain bandpass or highpass with an adjustable corner (Waves uses 2-16 kHz) and a split/wideband switch. Split-band avoids pumping on the full signal but adds a crossover in the audio path. Waves says its wideband mode avoids this (vendor claim).
- Sibilance detection beyond a fixed bandpass (relative energy, adaptive detection) is only sketched by vendors (Pro-DS "Single Vocal"). No source found explains an actual method.

### Gaps
- The sibilance band of 4-10 kHz that one summary gives is uncited. Treat it as unverified.
- One summary cites a 6 kHz two-way crossover example for split-band. The source was not confirmed.
- No source found on relative-energy or adaptive sibilance detection methods. No independent comparison of Waves vs Pro-DS was found.

## Gate and expander (hysteresis, hold, range, lookahead, chatter)

### Takeaway
No sourced findings. Not covered in this session.

### Cited Findings
- None found.

### Inferences
- None sourced.

### Gaps
- No sources were searched or found on gate hysteresis, hold time, range, lookahead, chatter avoidance, or key filters. This section needs a separate pass.

## Transient shaper (envelope-difference designs)

### Takeaway
No sourced findings. Not covered in this session.

### Cited Findings
- None found.

### Inferences
- None sourced.

### Gaps
- No sources found on the SPL Transient Designer or envelope-difference designs. This section needs a separate pass.

## Output limiter / clipper: lookahead brickwall, true-peak, soft clipping

### Takeaway
ITU-R BS.1770 true-peak metering is a 12.04 dB pad, 4x oversampling, lowpass filter, then absolute value. Vendors argue for higher oversampling ratios than 4x, but these are vendor claims. The sample-rate requirement in BS.1770-5 Annex 2 could not be verified.

### Cited Findings
- True-peak chain: attenuate 12.04 dB, 4x oversample, lowpass, absolute value. Essentia's tutorial gives the same sequence with a 12.04 dB offset before the oversampler and another after log conversion. Essentia defaults to BS.1770-4 and also supports BS.1770-2. Sources: [Essentia true-peak tutorial](https://essentia.upf.edu/tutorial_audioproblems_truepeakdetector.html); [Essentia std TruePeakDetector](https://essentia.upf.edu/reference/std_TruePeakDetector.html); [trade article on true-peak metering](https://www.tvtechnology.com/opinions/advantages-of-true-peak-metering) (for the chain description).
- ITU spec: [ITU-R BS.1770-4 PDF](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-4-201510-I!!PDF-E.pdf). Not fetched.
- A FabFilter forum post says the spec favours higher oversampling ratios, calls 4x generally sub-optimal, and says Pro-L 2 meters at 8x with steeper filtering. Source: [FabFilter forum](https://prod.fabfilter.com/forum/add-comment?id=3097). Vendor claim.
- The trade article warns that oversampling can raise peaks by up to 3 dB, and filter phase shift can add overshoot. It also notes that true-peak meters carry some inherent reading error. Source: [TV Technology](https://www.tvtechnology.com/opinions/advantages-of-true-peak-metering).

### Inferences
- A limiter's true-peak detector should oversample at least 4x per BS.1770 practice (secondary-source support). Higher ratios are a quality choice, not a compliance requirement, based on the sources found.
- The limiter output stage can use an ADAA clipper (see aliasing section) before any oversampling.

### Gaps
- One JavaScript library claims BS.1770-5 Annex 2 requires 192 kHz for oversampling, and that 96 kHz input needs only 2x. Not confirmed against the ITU text. Source to check: [cacophony.js TruePeakDetector](https://cacophony.js.org/classes/TruePeakDetector.html).
- No source found on lookahead brickwall design, soft-clipper curve design, or latency handling.

## Gain-reduction metering conventions

### Takeaway
No sourced findings. Not covered.

### Cited Findings
- The UA 1176 all-buttons VU meter behaviour is described as "strange in GR mode" (search summary; from the UA/Moore discussion, not verified). Source: [Moore, Huddersfield](https://eprints.hud.ac.uk/id/eprint/27391/1/Journal%20on%20the%20Art%20of%20Record%20Production%20%C2%BB%20All%20Buttons%20In_%20An%20investigation%20into%20the%20use%20of%20the%201176%20FET%20compressor%20in%20popular%20music%20production.pdf).

### Inferences
- None sourced.

### Gaps
- No sources found on GR display conventions (dB reduction sign, peak-hold, VU ballistics).

## CPU: log/exp costs, block-based envelope, SIMD

### Takeaway
No sourced findings. Not covered.

### Cited Findings
- None found.

### Inferences
- None sourced.

### Gaps
- No sources searched on fast log/exp approximations, block-based envelope processing, or SIMD across channels.

## Open-source references

### Takeaway
Usable starting points: chowdsp_utils (detector types, JUCE), Faust compressors.lib (strength parameter, several dynamic processors), ZLEComp (GPLv3, JUCE CMake), ATKColoredExpander v2 (open source, JUCE), LSP (licence not verified), Airwindows engines (Logical4, Pressure6, Dynamics3, Pop3, via third-party wrappers), and a differentiable PyTorch compressor. Licences and JUCE's built-in API were not verified.

### Cited Findings
- chowdsp_utils: [repo docs](https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_utils); compressor detector: [CompressorLevelDetector](https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_utils/classchowdsp_1_1compressor_1_1CompressorLevelDetector.html).
- Faust compressors library: [faustlibraries.grame.fr/libs/compressors](https://faustlibraries.grame.fr/libs/compressors/). Includes compressors, limiters, expanders and gates (the search summary says so; not all verified).
- ZLEComp: GPLv3, JUCE CMake, built on pamplejuce template. Source: [gitee ZL-Audio/ZLEComp](https://gitee.com/ZL-Audio/ZLEComp). Licence per the search summary.
- ATKColoredExpander v2: open-source compression plugin by Matthieu Brucher, JUCE, VST2/VST3/AU. Source: [SynthAnatomy](https://synthanatomy.com/2017/12/atkcoloredexpander-v-2-is-a-free-open-source-compressor-plugin-for-pc-mac.html).
- LSP (Linux Studio Plugins): compressor family includes mono, stereo, LR, MS, sidechain and multiband variants. Supports LADSPA, LV2, LinuxVST, CLAP, and VST3 experimentally from 1.2.15. Source: [LSP 1.2.2 announcement](https://librearts.org/2022/06/lsp-plugins-1-2-2/); [Ardour forum, 1.2.15](https://discourse.ardour.org/t/lsp-plugins-1-2-15-released/109985). Licence not verified.
- Airwindows engines via Air-G plugins: Logical4 (Air-G Volt), Pressure6 (Air-G Pulse; no attack/release controls), Dynamics3 (Bezier-spline gain reduction shaping), Pop3 (ConsoleX). Source: [Air-G Pulse KVR listing](https://www.kvraudio.com/product/air-g-pulse-by-air-g-audio). Air-G says Airwindows code is MIT-licensed. This is a third-party claim; check Airwindows' own repo.
- A mirror of Airwindows Compresaturator source at [code.volse.net](https://code.volse.net/audio/plugins/airwindows-lv2-port.git/tree/plugins/MacVST/Compresaturator/source/CompresaturatorProc.cpp) carries a 2016 "All rights reserved" notice. Check upstream before reusing.
- diffFx differentiable compressor: [diffFx source](https://difffx-pytorch.readthedocs.io/en/latest/_modules/diffFx_pytorch/processors/dynamics/compressor.html).

### Inferences
- chowdsp_utils and the Faust library are the two most directly reusable C++/DSP-level references found. The Faust strength-parameter convention can be reused for a ratio-free UI.

### Gaps
- JUCE's built-in `dsp::Compressor` / `dsp::Limiter` API was not confirmed by a source. Check the JUCE docs.
- Not searched: Tokyo Dawn Labs posts, x42-dpl, IEM plugin suite compressor (Rudrich), and LSP's licence terms.
- Airwindows official repository and licence were not checked directly.

## Unverified recall (background knowledge, NOT sourced in this session)

Do not use these in a report without checking the primary source.

- Giannoulis et al. static soft-knee gain computer, with knee width W and threshold T in dB, three regions: below T - W/2 the output equals the input; inside the knee a quadratic term (1/R - 1)(x - T + W/2)^2 / (2W) is added; above T + W/2 the slope is 1/R. Check exact form in the paper.
- Attack/release one-pole smoothing coefficient alpha = exp(-ln(9) / (T_s * t)), which gives 10-90% rise time t. Check exact form.
- Branching smooth peak detector and decoupled peak detector are the tutorial's named structures. Check definitions.
- Fast exp approximation: Schraudolph (1999), "A Fast, Compact Approximation of the Exponential Function", Neural Computation. Not checked.
- JUCE `juce::dsp::Compressor` and `juce::dsp::Limiter` exist in JUCE's DSP module. Not checked.
- A safe ADAA default for a tanh-style clipper is first-order ADAA with the antiderivative of tanh = log(cosh(x)). Not checked.

## Source status summary

- Full text read: none. The Giannoulis PDF fetch failed (certificate error) and the Steinmetz and DAFx papers were read only as abstracts or search summaries.
- Most reliable: DAFx/arXiv abstracts (Steinmetz 2102.06200, Parker 2016), vendor manuals for hardware claims, ITU BS.1770 structure via Essentia docs.
- Least reliable: vendor marketing (UA, Arturia, Black Rooster, FabFilter forum), the Academia.edu "key takeaways" page, and unnamed search-summary claims.
- Next steps for the report writer or a follow-up pass: fetch the Giannoulis PDF (try a different host or the AES elibrary), fetch the ITU BS.1770-4 PDF, read the Steinmetz arXiv full text for LA-2A training and metric details, and run separate passes on gate, transient shaper, metering, and CPU.
