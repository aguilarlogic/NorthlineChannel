# EQ and Filter DSP for a Channel-Strip Plugin (analog-style and clean)

Scope note: this run used about 16 tool calls. Several primary PDFs were not readable through the fetch tool (Vicanek, Zavalishin, Cytomic SVF PDFs were only read at the index level). Sections marked "Gaps" were not covered and need a second pass. Source-quality tags: [primary] = official paper/spec/README read this session; [secondary] = search-snippet summary or blog; [forum] = forum opinion or hobbyist measurement.

---

## 1. Filter topologies: RBJ biquads vs. TPT / zero-delay-feedback SVF

### Takeaway
The RBJ Audio EQ Cookbook biquad is the verified, cheapest reference implementation. The literature pointed to by the Cytomic index (trapezoidal / TPT SVF papers, coefficient-computation and noise papers) is the usual choice when parameters change quickly (dynamic EQ, sweeps). The noise, stability and precision claims in those papers were not verified because the PDFs were not read.

### Cited Findings
- RBJ cookbook biquad coefficients, verified this session [primary]: A = 10^(dBgain/40), w0 = 2*pi*f0/Fs, alpha = sin(w0)/(2Q) (or bandwidth-in-octaves form alpha = sin(w0)*sinh((ln2/2)*BW*w0/sin(w0)), or shelf-slope form). Peaking: b0 = 1 + alpha*A, b1 = -2cos(w0), b2 = 1 - alpha*A, a0 = 1 + alpha/A, a1 = -2cos(w0), a2 = 1 - alpha/A. Direct Form I: y[n] = (b0/a0)x[n] + (b1/a0)x[n-1] + (b2/a0)x[n-2] - (a1/a0)y[n-1] - (a2/a0)y[n-2]. — [Audio EQ Cookbook](https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html) (reached via a 301 redirect from w3.org/2011/audio/audio-eq-cookbook.html)
- Cytomic technical-papers index lists 14 PDFs under 8 titles [primary index, contents of PDFs not read]: trapezoidal-integrated one-pole LP, SVF (SvfLinearTrapOptimised2.pdf), Sallen-Key LP; "SVF vs DF1 noise plots"; "Convert trapezoidal SVF and DF1 coefficients"; "SVF with simultaneous computation of all outputs"; "Sin only coefficient computation in state increment form" (SVF and Sallen-Key, described as low-noise, high-accuracy coefficient computation); "Input mixing multimode filters"; "Matching of Forward Euler SVF to Trapezoidal SVF"; "Dynamic Smoothing" (two-pole SVF with self-adjusting cutoff for smoothing noisy control signals); "Nyquist matched filters" (one-pole mixing SVF matched at Nyquist to an analog prototype). — [Cytomic technical papers](https://cytomic.com/technical-papers/)
- Simper SVF is ported in open source and labeled as copied from the Cytomic paper [secondary]: the simper_filter Rust crate documents its SVF as copied from Andrew Simper and links the SvfLinearTrapOptimised2 PDF. — [simper_filter crate docs](https://docs.rs/simper-filter)
- A resonant lowpass SVF setup described on JUCE forum [forum]: tangent-based frequency term, damping = 1/Q, feedback coefficients derived from those two values; the poster points to the Cytomic paper. — [JUCE forum: SVF resonant](https://forum.juce.com/t/svf-resonant/56889/17)
- The phrase "zero-delay feedback" is contested [forum]: Cytomic's Andy argued the Chamberlin SVF is topology-preserving; another poster disputed the term under a different definition; a further poster argued a passive RC filter and its digital model sound identical, so the label is applied inconsistently. — [KVR: practical approach to ZDF filters](https://www.kvraudio.com/forum/viewtopic.php?p=5470754); [KVR SVF thread](https://www.kvraudio.com/forum/viewtopic.php?p=5572735)
- chowdsp_filters (GPLv3) provides StateVariableFilter, OnePoleSVF, ModFilterWrapper, first/second-order HPF/LPF/BPF/shelf/peak/notch, higher-order Butterworth, Chebyshev II and elliptic designs, FIRFilter, and ConformalMaps (bilinear and alpha transforms) [primary README]. — [chowdsp_utils](https://github.com/Chowdhury-DSP/chowdsp_utils)

### Inferences
- The RBJ peaking biquad is a 5-coefficient, 4-state (DF1) structure per channel per band; coefficients are recomputed from gain, f0 and Q, so they are cheap to recompute at block rate. This follows directly from the cited formulas.
- Because b0/b2 and a1/a2 all move together with gain and Q, fast parameter changes in a DF form can produce transients. Cytomic's coefficient-computation and "Dynamic Smoothing" topics point to state-based (TPT/SVF) forms as the mitigation for modulation. This is an inference from paper titles, not from paper contents.

### Gaps
- Not read: the contents of the Cytomic SVF PDFs. No verified claims yet on noise (SVF vs DF1), low-frequency numerical stability, or float-vs-double behavior of the SVF.
- Zavalishin, "The Art of VA Filter Design": the PDF URL I tried returned 404, and the current location was not found. A Cycling74 forum thread mentions a "VAFilterDesign" PDF. Next step: locate the current PDF (search by title and author).
- Direct Form I vs II vs transposed numerical behavior with float: not sourced.

---

## 2. Frequency warping and Nyquist cramping

### Takeaway
Vicanek's 2016 note "Matched Second Order Digital Filters" is the cited candidate for closed-form biquads that match analog responses better than the bilinear transform near Nyquist, and Cytomic has a "Nyquist matched filters" SVF paper. I could not read the Vicanek formulas or its cramping analysis, so the specific error figures are unverified.

### Cited Findings
- Vicanek, "Matched Second Order Digital Filters" (dated 14 Feb 2016): gives closed-form coefficient expressions for lowpass, bandpass, highpass and peaking EQ; the note compares against the bilinear transform, impulse invariance, matched Z-transform, Orfanidis and Massberg methods; it warns that coefficient labeling and sign conventions differ across authors [secondary, based on search summary of the PDF]. — [Vicanek PDF](https://www.vicanek.de/articles/BiquadFits.pdf) (WebFetch could not extract its text; the PDF is binary-compressed in the fetch result)
- A community Reaktor patch built on Vicanek's note says it improves on an earlier Orfanidis-based EQ and changes the structure from DF1 to TDF2 [secondary, community patch]. — [Vicanek EQ (Reaktor User Library)](https://native-instruments.com/en/reaktor-community/reaktor-user-library/entry/show/15369)
- Orfanidis, "High-Order Digital Parametric Equalizer Design," JAES vol. 53, Nov 2005, pp. 1026-1046 [primary citation, PDF paywalled]: builds digital parametric EQs from high-order analog Butterworth, Chebyshev and elliptic prototypes; inputs are center frequency, peak gain, bandwidth and bandwidth gain; the design starts from a high-order analog low-pass shelving prototype, digitizes it, and transforms to a peaking EQ at the peak frequency. — [AES e-library](https://aes2.org/publications/elibrary-page/?id=13397); procedure summary [MathWorks designParamEQ](https://au.mathworks.com/help/audio/ref/parametricequalizerdesign.html)
- MathWorks retired its older iirparameq function in favor of designParamEQ [secondary, vendor doc]. — [MathWorks iirparameq](https://www.mathworks.com/help/dsp/ref/iirparameq.html)
- Cytomic lists "Nyquist matched filters": a one-pole mixing SVF matched at Nyquist to an analog prototype (PDF: https://cytomic.com/files/dsp/matched-svf1-ivanstov.pdf, not read). — [Cytomic technical papers](https://cytomic.com/technical-papers/)
- chowdsp_filters includes ConformalMaps with bilinear and alpha transforms [primary README]. — [chowdsp_utils](https://github.com/Chowdhury-DSP/chowdsp_utils)

### Inferences
- The practical options for avoiding high-frequency cramping, from the sources found: (a) use a matched or prescribed-response biquad (Vicanek, Orfanidis) instead of plain bilinear; (b) use an SVF that is matched at Nyquist (Cytomic); (c) use an alpha/conformal warping map (chowdsp ConformalMaps). Which one sounds best was not established.

### Gaps
- Vicanek's formulas and error plots: not obtained.
- Oversampling (cost, latency, which vendors use it): not searched.
- How FabFilter and Tokyo Dawn avoid cramping: not searched; no vendor source found.

---

## 3. Analog EQ behaviors to emulate (SSL E/G, API 550/560, Neve 1073/1084, Pultec EQP-1A)

### Takeaway
Sources disagree on which SSL series is constant-Q and which is proportional-Q, so treat that as unresolved. Neve 1073 and Pultec behaviors are described consistently by several secondary sources (Q rising with frequency on Neve, boost/cut interaction on Pultec), but none includes measured frequency-response data. API 550/560 was not researched.

### Cited Findings

SSL E and G series:
- G-series (introduced 1987) has Q that changes with gain; the original G introduced Q "proportional to gain settings"; x3 / /3 buttons replaced bell/shelf switches; cuts broaden the curve, and the G shows a level-dependent lift in bass and top end [secondary, studio blog; summary of search snippets]. — [Abbey Road Institute: SSL E/G series EQ](https://abbeyroadinstitute.nl/blog/ssl-e-g-series-eq/)
- A Korean-language blog states E is constant-Q (bandwidth stays the same at any gain) and G is variable-Q, narrowing as boost increases [secondary; attribution to this URL is inferred from the search result list]. — [blog.imseankim.com/?p=4836](https://blog.imseankim.com/?p=4836)
- Gearspace posters contradict this: one labels the 292 (G) as constant-Q and the 242 (Black E) as constant-bandwidth [forum; the poster is not identified in the search output]. — [Gearspace: Waves SSL G/E channel difference](https://gearspace.com/threads/waves-ssl-g-e-channel-difference.468336/)
- E-series Brown has a sweepable mid Q, LF/HF bell/shelf switches, 12 dB/oct HPF/LPF; the Black 242 has more capacitors in the path and is described as smearing [forum/secondary]. — [Abbey Road Institute blog](https://abbeyroadinstitute.nl/blog/ssl-e-g-series-eq/)
- Vendor claim: Waves G-Channel models the G EQ curves together with VCA and Class A behavior, with noise/THD toggles [vendor marketing]. — [ToolFarm listing](https://toolfarm.com/buy/waves_ssl_g_channel)
- Forum view on emulation: Q range and curve shape can differ between hardware and emulations [forum]. Source: the Gearspace threads above.

Neve 1073:
- Shelves are active (feedback around a discrete class A stage); the midrange peaking bands use an inductor and capacitor; the high-pass is passive with magnetics giving a third-order response; mid Q rises with center frequency [secondary, UA 2006 interview summary]. — [Universal Audio WebZine 2006](https://uaudio.com/webzine/2006/may/text/content2.html)
- With a fixed bandwidth, Q scales with center frequency (Q = f0/BW): 500 Hz BW gives Q = 1 at 500 Hz and Q = 2 at 1 kHz [forum analysis]. — [GroupDIY: Neve 1073 inductor questions](https://groupdiy.com/threads/neve-1073-inductor-questions.2368/post-28966)
- In the same forum analysis of the presence section, lower bands switch both L and C (roughly constant Q) and upper bands switch only C (Q rises with frequency) [forum analysis; not verified against schematic]. — same URL
- Parasitic capacitance in the inductors pulls responses toward unity at high frequency (quoting UA) [forum]. — same URL
- Band interaction: a boost produces a small adjacent dip and a cut a nearby boost [reviewer]. — [Sound on Sound node 4911866](https://www.soundonsound.com/node/4911866) or [node 4930253](https://www.soundonsound.com/node/4930253) (the search did not show which one holds this text)
- Benchmark: a Sound on Sound review reports the UAD plugin with controls zeroed still shows a broad shallow dip near 500 Hz and a similar boost near 5 kHz [secondary; same SOS URLs as above, not confirmed which]
- Matching curves is considered the easy part; tubes and transformers are the hard part [forum opinion, KVR]. — [KVR thread p=7376250](https://www.kvraudio.com/forum/viewtopic.php?p=7376250)

Pultec EQP-1A:
- Low boost and low cut at the same frequency interact: with no buffering between stages, the cut picks up slight resonance and the bass is both tightened and boosted [secondary, UA WebZine 2004 summary]. — [UA WebZine 2004](https://uaudio.com/webzine/2004/july/text/content4.html)
- Boost and cut turnover frequencies sit about an octave apart at the same switch setting; a forum poster says the cut's corner rises with more boost (deliberate coupling) [forum]. — [GroupDIY: Pultec turnover frequencies](https://groupdiy.com/threads/pultec-eqp-1a-turnover-frequencies.43095/)
- Hobbyist measurement: low cut turnover about 13.5x the set frequency (17-18 dB at the set point), low boost turnover about 6x (12-13 dB) [forum, hobbyist measurement; not lab data]. — same GroupDIY URL
- WDF model readme: boosting and cutting 30 Hz together gives a boost near 80 Hz and a dip near 200 Hz on a kick drum. This is a documented emergent curve for a modeled unit [primary readme for an open model, not an original measurement]. — [EQP1A-WDF (ABSounds)](https://github.com/ABSounds/EQP1A-WDF)
- An unofficial "MKII" redraw says the low-cut load returns to the top of the low-boost pot, which reduces the cut when boost is engaged [forum, unverified against original schematic]. — no URL retrieved
- HF section: boost is a peaking filter with bandwidth control, and cut is a shelf at 5, 10, 20 kHz up to about 16 dB [secondary; source not identified in search output].

API 550/560: not searched.

### Inferences
- A constant-Q vs proportional-Q (SSL G-style) behavior can be modeled by making Q a function of gain at block rate, since RBJ alpha = sin(w0)/(2Q) depends only on Q. This follows from the cited RBJ formulas.
- A fixed-bandwidth Neve-style band (Q = f0/BW) is simplest in the RBJ bandwidth-in-octaves form, where BW stays constant as f0 changes. This follows from the cited formula and the Q = f0/BW analysis.
- Pultec-style boost/cut interaction is a coupled network. Summing two independent biquads will not reproduce the interaction, because the sources describe shared-component loading. This is an inference from the UA and forum descriptions.
- Emulation accuracy (the parasitics, the transformers, nonlinear behavior) matters more than the nominal curve, according to forum sources. Not independently verified.

### Gaps
- No schematics, measured frequency-response sweeps, or published emulation papers were found for any of these units.
- Curve capture methodology (measurement sweep vs SPICE vs curve fitting): not researched.
- SSL E vs G Q behavior: sources conflict. Needs measurement, not more blog reading.
- API 550/560 proportional-Q behavior: not searched.

---

## 4. HPF / LPF design (slopes, Butterworth vs resonant analog-style, min-phase vs linear-phase)

### Takeaway
Not researched beyond library capability. chowdsp_filters offers higher-order Butterworth, Chebyshev II and elliptic designs, and Orfanidis describes Butterworth/Chebyshev/elliptic prototypes. Slope choices, resonance behavior and phase-type trade-offs have no sourced claims yet.

### Cited Findings
- chowdsp_filters lists higher-order Butterworth, Chebyshev Type II and elliptic designs [primary README]. — [chowdsp_utils](https://github.com/Chowdhury-DSP/chowdsp_utils)
- Orfanidis (2005) covers Butterworth, Chebyshev and elliptic prototypes for parametric EQ [primary citation, paywalled]. — [AES e-library](https://aes2.org/publications/elibrary-page/?id=13397)

### Inferences
- The likely implementation pattern (cascade of biquads or SVF sections for a Butterworth slope, with Q set from the prototype poles) follows from the library and paper descriptions. The exact pole-to-Q mapping was not verified.

### Gaps
- Slope recommendations (6-48 dB/oct), analog-style resonant HPFs, and minimum- vs linear-phase trade-offs: no sources found.

---

## 5. Dynamic EQ and split-band de-essing

### Takeaway
Not researched. The only related source is the Cytomic "Dynamic Smoothing" paper title (self-adjusting cutoff for control-signal smoothing), which is not the same thing.

### Cited Findings
- Cytomic index lists "Dynamic Smoothing": a two-pole SVF with a self-adjusting cutoff for smoothing low-bit-depth or noisy control signals [primary index description, PDF not read]. — [Cytomic technical papers](https://cytomic.com/technical-papers/)

### Inferences
- A sidechain-filtered, gain-controlled bell is the standard structure. The Cytomic "Dynamic Smoothing" topic suggests control-signal smoothing is a known concern. Artifact-free modulation depends on the detector and smoothing design, which was not sourced.

### Gaps
- Sidechain filter design, detector ballistics, artifact avoidance, and split-band de-esser topology: no sources found.

---

## 6. Linear-phase EQ (FFT convolution, latency, pre-ringing)

### Takeaway
A linear-phase EQ option exists in open source (chowdsp_eq's LinearPhaseEQ and ChowMultiTool), but the cost, latency and pre-ringing trade-offs were not sourced.

### Cited Findings
- chowdsp_eq provides EQBand, EQProcessor, and LinearPhaseEQ, which builds a linear-phase EQ from a prototype EQ [primary README]. — [chowdsp_utils](https://github.com/Chowdhury-DSP/chowdsp_utils)
- ChowMultiTool (free, GPLv3 per search summary) includes a linear-phase EQ with drawable editing [secondary, search summary; not verified on the page]. — [ChowMultiTool (KVR)](https://www.kvraudio.com/product/chowmultitool-by-chowdhury-dsp)

### Inferences
- Linear-phase EQ is available as an option in the chowdsp family, so it can be included as an optional mode without designing it from scratch.

### Gaps
- Latency, pre-ringing, CPU cost of FFT convolution, and whether it is worth including in a lightweight channel strip: not researched.

---

## 7. Mid/side and stereo-link EQ handling

### Takeaway
Not researched.

### Gaps
- M/S filter processing, stereo-link of dynamic EQ detectors, and mono-compatibility of M/S EQ: no sources found.

---

## 8. Parameter smoothing and coefficient updates (zipper noise, per-sample vs per-block)

### Takeaway
Not researched beyond titles. The Cytomic index contains a "Sin only coefficient computation" paper (low-noise, high-accuracy coefficient computation for SVF and Sallen-Key) and "Dynamic Smoothing."

### Cited Findings
- Cytomic index: "Sin only coefficient computation in state increment form" (SVF and Sallen-Key), described as low-noise, high-accuracy coefficient computation [primary index description, PDF not read]. — [Cytomic technical papers](https://cytomic.com/technical-papers/)

### Inferences
- Coefficient-update cost can be cut by computing coefficients at block rate and smoothing parameters rather than coefficients, which is consistent with the RBJ structure (coefficients are closed-form functions of gain, f0 and Q). Not verified against a source on zipper noise.

### Gaps
- Smoothing time constants, which quantities to smooth (parameter vs coefficient), per-sample vs per-block cost, and zipper-noise measurements: not sourced.

---

## 9. Performance (cost per filter, SIMD, double vs float, denormals)

### Takeaway
Only arithmetic-level costs can be stated, and they are inferences from the cited formulas. SIMD and precision claims are not sourced.

### Cited Findings
- chowdsp_utils includes a chowdsp_simd module (BSD) and a chowdsp_math module (BSD) [primary README]. — [chowdsp_utils](https://github.com/Chowdhury-DSP/chowdsp_utils)

### Inferences
- A DF1 RBJ biquad needs 5 multiplies and 4 adds per sample per channel plus 4 state values, using the cited difference equation. SVF-type structures need more multiplies per sample in exchange for better modulation behavior (inference from structure; not quantified by a source).
- Vectorizing across channels or bands is a standard approach; the chowdsp_simd module suggests the same family does it, but this is not verified.

### Gaps
- Measured cost per biquad vs SVF, double-vs-float needs at low frequencies, denormal handling (FTZ/DAZ or DC offset), and SIMD speedups: no sources found.

---

## 10. Spectrum analyzer

### Takeaway
Not researched. chowdsp_visualizers provides EqualizerPlot (filter response plots), which is not a spectrum analyzer.

### Cited Findings
- chowdsp_visualizers includes EqualizerPlot, a component for plotting filter frequency responses [primary README]. — [chowdsp_utils](https://github.com/Chowdhury-DSP/chowdsp_utils)

### Gaps
- FFT size, windowing, smoothing, and threading the analyzer off the audio thread: not researched.

---

## 11. Open-source references and licensing

### Takeaway
chowdsp_utils is the most useful verified open-source reference for EQ and filters, but its EQ and filter modules are GPLv3, which matters for a closed-source commercial plugin.

### Cited Findings
- chowdsp_utils modules and licenses [primary README]: chowdsp_eq (GPLv3), chowdsp_filters (GPLv3), chowdsp_simd (BSD), chowdsp_math (BSD), chowdsp_visualizers (GPLv3). The README says non-GPL licensing for GPL modules is available by contacting chowdsp@gmail.com. Requires C++17 or later and JUCE 6 or later for modules outside Common/DSP. — [chowdsp_utils](https://github.com/Chowdhury-DSP/chowdsp_utils)
- Cytomic technical papers are a set of PDFs, with ports in open source (simper_filter Rust crate). — [Cytomic technical papers](https://cytomic.com/technical-papers/); [simper_filter](https://docs.rs/simper-filter)
- EQP1A-WDF open model (ABSounds) [primary readme]. — [GitHub](https://github.com/ABSounds/EQP1A-WDF)

### Inferences
- If the product must stay closed-source and commercial, use the BSD modules or the Cytomic papers as reference, and re-derive the GPLv3 EQ/filter code, or get a commercial license from chowdsp.

### Gaps
- JUCE dsp module, LSP plugins, x42 eq, Airwindows, Faust libraries, Will Pirkle's book, KVR DSP forum threads beyond those cited: not searched.

---

## Key gaps for the report writer (priority order)

1. Vicanek and Cytomic PDF contents (noise, cramping error, coefficient formulas) are not read. Retrieve text via a local PDF tool.
2. Zavalishin VA Filter Design: current PDF URL not found.
3. SSL E vs G Q behavior: sources conflict; needs sweep measurements.
4. API 550/560, Neve 1073 schematic-level, and Pultec measured responses: no primary data.
5. Dynamic EQ, linear-phase latency/pre-ringing, M/S, zipper noise, denormals, SIMD, spectrum analyzer: no sources gathered.
6. Oversampling and vendor practice (FabFilter, Tokyo Dawn): not searched.
