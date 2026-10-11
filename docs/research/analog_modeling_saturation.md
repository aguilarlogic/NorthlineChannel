# Analog Character Modeling and Saturation for a Lightweight Real-Time Channel Strip

Research date: 2026-10-10. Method: 15 search and lookup calls (web search plus a research-paper index). Primary PDFs were mostly not fetched, so most findings come from abstracts or search-result summaries. Those are marked "(abstract)" or "(secondary summary; primary not read)". Items with no URL returned are marked "(no URL returned)". Items from general knowledge are marked "(unverified)".

Premise correction: the DAFx 2016 ADAA paper is by Julian D. Parker, Vadim Zavalishin and Efflam Le Bivic (Native Instruments). "Zambon" is not an author, and Välimäki is not on that paper. Välimäki is on the follow-up 2017 IEEE SPL paper with Bilbao, Esqueda and Parker.

## Key Question 1: Static memoryless waveshaping vs dynamic/stateful models

### Takeaway
Memoryless waveshapers are the cheapest source of harmonics, but they alias and cannot reproduce frequency-dependent or hysteretic behaviour. Practical designs wrap the static core in pre/de-emphasis filters and add state (bias, hysteresis, envelope-dependent gain) as separate stages.

### Cited Findings
- A study of neural guitar-amp modelling reports that A-weighted pre-emphasis in the training loss was the best of the pre-emphasis filters tested. Note: this is a training loss, not a runtime model; the pre-emphasis idea transfers only by analogy, and whether listening tests confirmed it was not verified. [arXiv:1911.08922](https://arxiv.org/abs/1911.08922) (abstract)
- A comparison of LSTM, state-space and LRU models found that encoder-decoder LSTM and state-space models excel on some effects, that no model effectively emulated the low-pass filter, and that LRU results were inconsistent. [arXiv:2405.04124](https://arxiv.org/abs/2405.04124) (abstract)
- Simply scaling TCN or RNN width or depth did not capture long-time-scale effects (fuzz, compression); time-varying FiLM modulation helped. [arXiv:2211.00497](https://arxiv.org/abs/2211.00497) (abstract)
- Jiles-Atherton (JA) hysteresis can produce nonphysical solutions: negative differential permeability, non-closure of minor loops, and asymmetric centred minor loops (a 2022 IEEE Trans. Magnetics paper, per a search listing). [Search listing](https://bohrium.dp.tech/paper/arxiv/0103573) (secondary; listing only)
- The Faust hysteresis library includes a JA core for audio, using 4x cascaded substeps and cubic Hermite interpolation. [Faust hysteresis library](https://faustlibraries.grame.fr/libs/hysteresis/) (secondary summary)

### Inferences
- Stateful elements are the cheapest route to "character" beyond a static curve: pre/de-emphasis around the nonlinearity, bias or asymmetry offsets, and hysteresis or envelope stages. The comparison in arXiv:2405.04124 does not test static versus stateful designs directly.
- Keep the static core ADAA-capable and place state in filters before and after it.

### Gaps
- No listening-score comparison of static and stateful models was retrieved.
- Even-harmonic asymmetry formulations (bias offset, asymmetric curves) were not sourced in this session.

## Key Question 2: Antiderivative anti-aliasing (ADAA)

### Takeaway
ADAA is peer-reviewed and cheap for memoryless waveshapers. First order gives a large alias reduction, and second order adds more with divide-by-zero handling. ADAA is weaker in feedback and stateful systems (fractional delay, low-pass effect), and the literature has proposed fixes. No quantitative ADAA-vs-oversampling figures were retrieved.

### Cited Findings
- Parker, Zavalishin and Le Bivic, "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution," DAFx 2016. Abstract per search summary: the method markedly reduces aliasing, most noticeably with low-order oversampling, and is extended to feedback systems. [DAFx archive](https://dafx.de/paper-archive/details/vem_XXF5qBbfiWOH2RVVAA) (archive page not read; author names and abstract from search summary)
- Bilbao, Esqueda, Parker and Välimäki, "Antiderivative Antialiasing for Memoryless Nonlinearities," IEEE Signal Processing Letters 24(7), pp. 1049-1053, 2017. Some references list 2016. (no URL returned)
- Higher-order ADAA: Bilbao et al. extended the method to higher-order antiderivatives. Stated drawbacks are a low-pass effect and a fractional delay of p/2 samples; the delay is a problem in feedback paths. (secondary summary; primary not read) [CCRMA ADAA notebook](https://ccrma.stanford.edu/~jatin/Notebooks/adaa.html) and [Aalto thesis record](https://aaltodoc.aalto.fi/items/a46ae786-3014-4c49-a409-957b4c330bcd)
- Second-order ADAA uses the second antiderivative and divides by x[n] - x[n-2], so it needs a fallback when neighbouring samples are equal. [CCRMA ADAA notebook](https://ccrma.stanford.edu/~jatin/Notebooks/adaa.html) (secondary)
- Stateful systems: La Pastina and D'Angelo (DAFx 2022) found standard ADAA in stateful systems can alter frequency response and cause stability problems, and proposed a cascaded filter that compensates (secondary summary; no URL returned).
- Zheleznov and Bilbao (DAFx 2024) compared interpolation kernels for ADAA: cubic kernels reduced aliasing for memoryless nonlinearities, while linear interpolation performed better for stateful systems (secondary summary; no URL returned).
- A DAFx 2025 paper (Gabrielli and Squartini, per summary) replaces closed-form antiderivatives with numerical integration of lookup tables. [DAFx25 paper 30](https://dafx25.dii.univpm.it/wp-content/uploads/2025/09/DAFx25_paper_30.pdf) (link from same result set; mapping to this paper not verified)
- A DAFx 2026 paper, "PolyADAA," replaces linear interpolation in ADAA with higher-order Lagrange interpolation, which it identifies as the aliasing bottleneck. [DAFx26 paper 47](https://www.dafx.de/paper-archive/2026/papers/DAFx26_paper_47.pdf) (link from same result set; mapping not verified)
- Neural analogue: fine-tuning a neural amp model against an aliasing-free target reduced aliasing more than 2x oversampling in most cases, with side effects on harmonic distortion. LSTM models gave the best balance. [arXiv:2505.11375](https://arxiv.org/abs/2505.11375) (abstract). This compares fine-tuning, not ADAA, so it is not a direct ADAA benchmark.

### Inferences
- The sources do not quantify ADAA as a replacement for oversampling in analog-stage modelling. A defensible starting point is first-order ADAA on each memoryless waveshaper, with 2x oversampling only on the highest-drive stage. Validate this with an alias measurement (see Key Question 7).
- Avoid ADAA around a waveshaper inside a feedback loop unless the delay and low-pass error are compensated. This follows the cited drawbacks.

### Gaps
- No dB figures comparing ADAA with 2x or 4x oversampling were retrieved. The 2016 paper's figures were not read.
- Primary texts for Bilbao 2017, Holters, La Pastina and D'Angelo 2022, and Zheleznov and Bilbao 2024 were not fetched.
- A Holters ADAA extension to stateful systems was mentioned in secondary material but not confirmed in retrieved text, so it is not cited.

## Key Question 3: Oversampling (polyphase IIR vs FIR; dynamic factors; vendor practice)

### Takeaway
A polyphase IIR half-band (HIIR-style) is the usual low-latency choice. Linear-phase FIR costs latency and suits offline rendering or cases where phase matters. A 2025 multirate study recommends IIR unless linear phase is needed. Vendor practice (FabFilter, Kilohearts, TDR) was not retrieved.

### Cited Findings
- HIIR is an oversampling and Hilbert transform library. Its IIR option uses allpass-based recursive filters, which give steeper filtering, lower latency and better performance, but the phase delay is not constant across frequency. A linear-phase FIR option also exists. [docs.rs halfband](https://docs.rs/halfband) (secondary description from a project that builds on HIIR; the original HIIR page was not retrieved; the author attribution came from the search query, not a retrieved source)
- A GitHub fork added double precision and AVX. Per a secondary source, HIIR 1.30 merged multichannel SIMD, including 16-channel single precision via AVX512. [unevens/hiir](https://github.com/unevens/hiir) (secondary)
- arXiv:2501.18470 (multirate neural audio effects): a half-band IIR stage followed by a Kaiser-window FIR cascade is reported with under 1 ms latency and nearly identical FIR and IIR aliasing results. For 2x to 8x, the authors recommend IIR unless linear phase is needed. [arXiv:2501.18470](https://arxiv.org/abs/2501.18470) (search excerpt; comparison with prior methods and operation counts not verified)
- Kahles et al., JAES 2019: for hard clipping at 8x oversampling, linear interpolation with a high-shelf correction was adequate for upsampling. For decimation, a cascaded integrator-comb filter was the only tested option giving perceptually sufficient suppression across the range. Only hard clipping was tested. [Kahles et al. 2019](https://acris.aalto.fi/ws/portalfiles/portal/34665796/ELEC_Kahles2019_Oversampling_JAES.pdf) (search excerpt)
- Custom polyphase IIR implementations are error-prone: a paid job listing describes a custom polyphase IIR with phase-alignment problems and frequency-response notches, and asks for standard HIIR integration for cascaded 2x to 16x. [JUCE forum listing](https://forum.juce.com/t/paid-seeking-dsp-developer-to-fix-hiir-oversampling-in-juce-plugin-c-juce/67658)
- A JUCE forum thread on oversampling (topic 20934) appeared in search results; its content was not read, so it supports no claim here. [JUCE forum](https://forum.juce.com/t/oversampling/20934)
- ADAA reduces oversampling need only partially; see Key Question 2.

### Inferences
- For a lightweight plugin: default to 2x polyphase IIR, use 4x or 8x only where a stage's drive or measured alias energy exceeds a threshold, and reserve linear-phase FIR for offline bounce.
- IIR phase is not constant, so dry/wet blending and parallel paths need latency or phase compensation. This follows from the non-constant phase delay noted above.
- Higher factors are most justified for offline rendering (inference; no source quantifies this).

### Gaps
- Vendor oversampling choices (FabFilter, Kilohearts, TDR) were not retrieved.
- The original HIIR homepage and licence were not retrieved.
- A forum anecdote reported polyphase IIR stable to 256x while FIR failed at 128x. No URL was confirmed for it, and it is a single debugging report, not a controlled test.
- No latency figures for HIIR versus the JUCE built-in oversampler were retrieved.

## Key Question 4: WDF, nodal DK and state-space; when circuit modelling is worth it

### Takeaway
Circuit-level methods (WDF, nodal DK) model tube, op-amp and transformer behaviour from topology and are used in real-time plugins. Their cost lies in the nonlinear solver and implementation effort. Use them for one or two stages where topology matters, such as a tube gain stage or an output transformer, rather than the whole strip.

### Cited Findings
- chowdsp_wdf: header-only C++ library for real-time WDF circuit models, with a run-time API (`chowdsp::wdf`) and a compile-time fixed-topology API (`chowdsp::wdft`) using template metaprogramming and optional explicit SIMD. Requires C++14 or later; CMake 3.1+ and XSIMD 8.0.0+ are optional. [arXiv:2210.12554](https://arxiv.org/abs/2210.12554) (paper ID from search); [chowdsp_wdf docs](https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_wdf). The repo URL github.com/Chowdhury-DSP/chowdsp_wdf comes from a DAFx 2023 footnote and was not opened. The licence was not found.
- RT-WDF: a modular, open-source C++ WDF library supporting arbitrary topologies and multiple nonlinearities (DAFx 2016). [DAFx archive](https://dafx.de/paper-archive/details/ilZF4akpmSxzyiusoAOolg)
- The Koren triode model is phenomenological, based on the 3/2 power law. A DAFx 2023 paper states that most WDF tube implementations use the Koren or Cardarilli models. [Giampiccolo et al., DAFx 2023](https://re.public.polimi.it/retrieve/a1844311-c89f-4257-a993-126fe4a5304c/GiampiccoloEtAl_DAFx23.pdf) (search result; claim as summarized)
- A study of a Brazilian reverb preamp (first two 12AX7 stages, Koren-based equations) concluded that existing triode models do not cover all circuit topologies. [SBCM 2013 paper](https://compmus.ime.usp.br/sbcm/2013/pt/docs/art_tec_3.pdf) (search result)
- D'Angelo, Pakarinen and Välimäki (IEEE, 2013) introduced a family of wave-digital triode models: each triode is a memoryless three-port, solved by a local secant iteration. The authors report about half the CPU time of earlier models (abstract summary). (no URL returned)
- Zhang and Smith (DAFx 2018) used a modified blockwise method for cascaded tube amps, splitting circuits into two-triode stages to reduce cost. [DAFx archive](https://dafx.de/paper-archive/details/qrKr9p5zcERRuq0iGcB36Q)
- A DAFx 2020 paper replaced a WDF multiport nonlinearity with a wave-domain neural network. [DAFx20in22 paper 13](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx20in22_paper_13.pdf) (mapping to this claim not verified)
- Mačák, Holters and Schimmel (2012) simulate a two-triode Fender-type guitar preamp with a tone stack using approximation and state-space modelling. The DK-versus-WDF comparison in their work was not verified. [Mačák et al. 2012](https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2017/10/Mačák-Holters-Schimmel-2012-Simulation-of-Fender-Type-Guitar-Preamp-using-Approximation-and-State-Space-Model.pdf) (secondary summary)
- Holters and Zölzer (DAFx 2011): in nodal DK, matrix inversion is costly when components such as potentiometers change; they proposed cheaper recomputation of the variable parts and demonstrated it on a Dunlop Crybaby wah. [Holters and Zölzer 2011](https://www.hsu-hh.de/ant/wp-content/uploads/sites/699/2017/10/Holters-Zölzer-2011-Physical-Modelling-of-a-Wah-wah-Effect-Pedal-as-a-Case-Study-for-Application-of-the-Nodal-DK-Method-to-Circuit.pdf)
- Holters and Zölzer (EUSIPCO 2015) propose a more flexible way to integrate arbitrary element models, at the cost of larger matrices. [EUSIPCO 2015](https://new.eurasip.org/Proceedings/Eusipco/Eusipco2015/papers/1570103545.pdf) (mapping not verified)
- Libraries noted in the sources: chowdsp_wdf, RT-WDF, HIIR, AnalogTapeModel (Key Question 5), Faust hysteresis library (Key Question 1).

### Inferences
- Use WDF or nodal DK for at most one or two stages with a clear topology payoff, such as an output transformer or tube stage. The sources do not quantify solver cost per stage.
- DK recomputes matrices when parts vary at runtime, which argues for WDF-style structures where many controls change the circuit. WDF adaptors also need updating on parameter change, and the sources do not compare these costs. Benchmark both if the choice matters.

### Gaps
- No CPU benchmarks for chowdsp_wdf or nodal-DK stages were retrieved.
- Yeh's DK work, Kurt Werner's WDF work and ACME.jl were not searched.
- The chowdsp_wdf licence was not confirmed.

## Key Question 5: Tape, transformer, tube and console modelling

### Takeaway
Tape (Chowdhury, DAFx 2019) and transformer hysteresis (Mačák, TSP 2011, secondary) are cited physical-hysteresis examples with real-time implementations; the JA use in the tape paper was not verified. JA needs safeguards against nonphysical behaviour. Console character is mostly vendor claims. Brainworx's tolerance method is reportedly tied to a US patent, so check its claims before reusing the idea.

### Cited Findings
- Chowdhury, "Real-Time Physical Modelling For Analog Tape Machines," DAFx 2019 (Birmingham): a physics-based tape model, implemented in real time for a Sony TC-260, begun as a Stanford CCRMA Music 420 project. [DAFx archive](https://dafx.de/paper-archive/details/Z5Ow_E5YHVxvndTAyeEuzA) (search did not confirm the JA content; it is an unverified recollection that the paper uses JA)
- AnalogTapeModel open-source repository. [GitHub](https://github.com/jatinchowdhury18/AnalogTapeModel)
- CHOW Tape Model's hysteresis section exposes bias, drive and saturation controls; the product also covers tape distortion, playhead loss, wow and flutter. [ChowTape manual](https://ccrma.stanford.edu/~jatin/chowdsp/Products/ChowTapeManual.pdf) (search result)
- Mačák (TSP 2011): a simplified nonlinear transformer model using JA and Fröhlich formulations, with the algorithm tested in real time and its cost measured. (secondary summary; no URL returned) A companion 2011 article drops leakage inductance and uses a state-space form with a memoryless nonlinearity. (no URL returned)
- A 2023 EURASIP paper uses recurrent neural networks inside WDFs to model rate-dependent hysteresis in a tube amp output stage with a nonlinear transformer. [EURASIP JASM 2023 (DOAJ listing)](https://doaj.org/article/ce82677e70f84a40923c88ce9fb28a64) (listing; secondary)
- JA can produce nonphysical solutions; see Key Question 1 for the cited listing.
- Brainworx TMT models nominal values and the part-to-part spread implied by manufacturer tolerances. The vendor reports more than 150 parts from a Neve VXS 72 desk, with tolerances of about 1% to 20%, and 72 selectable channel variations. Analog Stereo Mode runs slightly different left and right modules. [bx_console N product page](https://brainworx.audio/products/brainworx/bx-console-n.html); [MixOnline](https://www.mixonline.com/news/plugin-alliance-brainworx-debut-bxconsole-plug-425939); [KVR news](https://www.kvraudio.com/news/plugin-alliance-and-brainworx-release-bx_console---console-emulation-featuring-72-different-authentic-channel-variations-31476) (vendor and press; marketing claims)
- The bx_console N product page lists a Virtual Gain control that adds simulated noise independently per channel, and a THD control that adds per-channel saturation. [Plugin Alliance bx_console N](https://www.plugin-alliance.com/en/products/bx_console_n.html) (vendor)
- The product page reportedly references a US patent for TMT. A patent number appeared in a search snippet; it was not checked.
- A Gearspace user reported a residual noise floor of about -105 dB on SSL-based Brainworx plugins with no audio playing. This is anecdotal and unconfirmed. [Gearspace thread](https://gearspace.com/threads/plugin-alliance-brainworx-ssl-console-problem.1456930/)
- No source describing how Brainworx models crosstalk was found.

### Inferences
- Per-instance random tolerance seeds, applied to filter and compressor coefficients, plus a small L/R mismatch, are cheap to implement and give unit-to-unit variation. Design your own distribution and parameter set rather than copying vendor values or claim language, given the patent reference.
- Expose a noise-floor control with a noise-off option, and measure the resulting floor in dBFS.
- For tape or transformer JA, use internal substeps (as in the Faust library) and guard against nonphysical loops, or use a simpler hysteresis model.

### Gaps
- Koren model parameters and the Chowdhury 2019 JA equations were not read.
- Crosstalk modelling sources: none found.
- Console harmonic profiles, slew behaviour and input-stage specifics: none found.
- The US patent claims were not read; legal review is needed before implementation.
- A 2025 IEEE Access paper on an analog JA circuit was mentioned in a summary, without a URL.

## Key Question 6: Neural and black-box modelling

### Takeaway
Black-box neural amp models are mature. Reported listening tests (mostly authors' claims in abstracts) compare LSTM, WaveNet/TCN, state-space and FiLM-conditioned models with SPICE, hardware or NAM; parity was not independently checked. Cost is significant, aliasing is a known issue, and sample-rate dependence must be handled. For a lightweight plugin, treat neural as an optional stage, profiled first, with pruning or small recurrent networks if used.

### Cited Findings
- An LSTM was used for real-time emulation of a tube amp. [arXiv:1804.07145](https://arxiv.org/abs/1804.07145) (abstract; quantitative error figures not confirmed)
- A feedforward WaveNet-style model trained on SPICE output of a Fender Bassman 56F-A responds to control changes. [arXiv:1811.00334](https://arxiv.org/abs/1811.00334) (abstract)
- End-to-end data pipeline: an LSTM trained against offline SPICE output was compared with the SPICE reference in a listening test. [arXiv:2403.08559](https://arxiv.org/abs/2403.08559) (abstract)
- PANAMA ("Parametric Neural Amp Modeling with Active Learning"): with 75 datapoints (knob settings), the model matched NAM's perceptual quality in MUSHRA tests. NAM is described as the leading open-source non-parametric amp modeler. [arXiv:2507.02109](https://arxiv.org/abs/2507.02109) (abstract; authors' claim)
- Time-varying FiLM conditioning improved fuzz and compressor modelling over scaled TCNs. [arXiv:2211.00497](https://arxiv.org/abs/2211.00497) (abstract)
- Comparative study: see Key Question 1 (arXiv:2405.04124).
- DDSP guitar amp: comparable to black-box baselines on time- and frequency-domain metrics, using under 10% of the computational operations per sample. [arXiv:2408.11405](https://arxiv.org/abs/2408.11405) (abstract; authors' claim)
- A pruned WaveNet for iOS: aggressive magnitude pruning removed 90% of weights with no perceptible loss (authors' claim), and a sparse C++ engine delivered real-time CPU operation. [arXiv:2607.10086](https://arxiv.org/abs/2607.10086) (abstract; authors' claim)
- Sample-rate dependence: RNNs trained at one rate misbehave at others. For integer oversampling, a delay-based approach gave high-fidelity conversion and reduced aliasing. For non-integer ratios, cubic Lagrange interpolation of a delay line worked best. [arXiv:2406.06293](https://arxiv.org/abs/2406.06293) (abstract)
- Aliasing in neural models: the authors introduce an aliasing-to-signal ratio (ASR) metric, and find smoother activation functions lower ASR without a large ESR penalty. [arXiv:2505.04082](https://arxiv.org/abs/2505.04082) (abstract)
- Fine-tuning against an aliasing-free target: see Key Question 2 (arXiv:2505.11375).
- Perceptual pre-emphasis in the training loss: see Key Question 1 (arXiv:1911.08922).
- Adversarial timbre transfer without paired data, reported to run in real time on a PC; listening-test details not confirmed. [arXiv:2211.00943](https://arxiv.org/abs/2211.00943) (abstract)
- Benchmark and dataset: a comparison of black-box and gray-box models across many nonlinear effects, with the open ToneTwist AFx dataset (open to community contributions). [arXiv:2502.14405](https://arxiv.org/abs/2502.14405); [ToneTwist AFx dataset](https://github.com/mcomunita/tonetwist-afx-dataset)

### Inferences
- A gray-box model (physics-inspired structure with a small learned correction) is probably more budget-friendly than a full WaveNet or LSTM, based on the DDSP and pruning results. This is an inference, not a benchmark.
- Dataset capture should sweep control settings, include noise floors, and include program material. Active learning (PANAMA) can reduce the number of settings to record.
- Neural stages alias, so apply the same alias measurement as for analytic stages.
- Using neural for every stage multiplies cost. If used at all, limit it to one stage.

### Gaps
- Per-sample CPU numbers for NAM or WaveNet on a desktop CPU were not retrieved.
- RTNeural (GitHub) was named in the brief but not searched or verified.
- Hardware reamping workflow was not sourced.
- The Neural Amp Modeler core library was not searched.
- Legal issues around training on hardware captures were not researched.

## Key Question 7: Measurement and validation

### Takeaway
Use a Farina log swept sine to separate harmonics per level and frequency. Compare harmonic and alias error, then ESR for model-vs-hardware, with perceptual checks (MUSHRA or ABX). Plugin Doctor, null-test procedure and IMD methods were not retrieved.

### Cited Findings
- Farina, "Simultaneous measurement of impulse response and distortion with a swept-sine technique," AES paper 5093 (year 2000 per search metadata; some secondary material dates the method to the mid-to-late 1990s, and this discrepancy was not resolved). A logarithmic swept sine yields the linear impulse response and separate impulse responses per harmonic; harmonic responses appear at separate negative-time positions and can be cut out and analysed individually. THD follows from summing the harmonic spectra. [AES E-Library](https://aes2.org/publications/elibrary-page/?id=10211) (the link's mapping to paper 5093 was not confirmed; the description is from secondary summary)
- Kite later reported that log chirp measurements of audio devices compare with traditional swept-sine results. (secondary summary; no URL returned)
- Farina (2007) extended the method to systems that are not time-invariant or linear. (secondary summary; no URL returned)
- Error-to-signal ratio (ESR) is the conventional neural-model metric; the aliasing-to-signal ratio (ASR) is proposed as a complementary alias metric. [arXiv:2505.04082](https://arxiv.org/abs/2505.04082) (abstract)
- MUSHRA listening tests were used to compare neural models with a reference amp and with NAM. [arXiv:2507.02109](https://arxiv.org/abs/2507.02109) (abstract)
- A listening test compared a neural model with a SPICE reference. [arXiv:2403.08559](https://arxiv.org/abs/2403.08559) (abstract)
- A-weighted pre-emphasis in the training loss was reported as the best of the tested filters (abstract); listening-test confirmation was not verified. [arXiv:1911.08922](https://arxiv.org/abs/1911.08922) (abstract)

### Inferences (recommended protocol; design proposal, not sourced)
- Harmonic test matrix: H2 and H3 (and higher) against level from about -30 to 0 dBFS, at several frequencies and control settings, using a Farina sweep each time. Compare harmonic magnitudes with hardware captures where available.
- Alias test: high-frequency sine at high drive, measuring energy in non-harmonic bins (an ASR-style figure). Compare 1x, 2x and 4x oversampling and ADAA on and off.
- Model-vs-hardware: ESR and null-test residual on reamped material, then a MUSHRA panel of at least three listeners, and an ABX test against the hardware.

### Gaps
- IMD methods (SMPTE, CCIF), Plugin Doctor, null-test procedure and blind-ABX protocol were not sourced in this session.
- No ESR or ASR thresholds for perceptual transparency were retrieved.

## Key Question 8: CPU budgeting and lightweight vs heavy emulations

### Takeaway
The sources favour a small set of cheap elements for most analog feel per CPU cycle: a static waveshaper with ADAA, filter-based pre/de-emphasis and hysteresis, and one circuit-accurate stage. Neural and high-order Volterra methods cost more. No source benchmarks these on the same machine, and the named emulations (Airwindows, Kiive, Analog Obsession, Acustica) were not retrieved.

### Cited Findings
- DDSP guitar amp: under 10% of operations per sample compared with black-box baselines (authors' claim). [arXiv:2408.11405](https://arxiv.org/abs/2408.11405)
- Pruned WaveNet: 90% of weights removed with no perceptible loss (authors' claim). [arXiv:2607.10086](https://arxiv.org/abs/2607.10086)
- Wave-digital triode models: about half the CPU time of earlier triode models (abstract summary; no URL returned).
- Zhang and Smith: blockwise decomposition of cascaded tube stages lowered cost. [DAFx archive](https://dafx.de/paper-archive/details/qrKr9p5zcERRuq0iGcB36Q)
- Half-band IIR plus Kaiser FIR: under 1 ms latency (neural context; operation-count comparison not verified). [arXiv:2501.18470](https://arxiv.org/abs/2501.18470)
- PANAMA: 75 datapoints sufficed for NAM-level perceptual quality (authors' claim). [arXiv:2507.02109](https://arxiv.org/abs/2507.02109)
- SSM and LSTM comparison: [arXiv:2405.04124](https://arxiv.org/abs/2405.04124)

### Inferences
- Rough cost ordering (inferred from the above, not benchmarked): static waveshaper with ADAA and oversampling (cheapest), then filter-based pre/de-emphasis and hysteresis (JA with substeps), then a single WDF or DK stage, then a gray-box neural model, then LSTM or WaveNet (most expensive).
- Benchmark each candidate stage on the target hardware before committing.

### Gaps
- No cycle or per-sample CPU comparison across classical methods was retrieved.
- Airwindows, Kiive and Analog Obsession were not searched. Their CPU and design claims are unverified.
- Acustica Volterra kernels were not searched.

## Key Question 9: Legal and trademark considerations for behavioural modelling

### Takeaway
Only one relevant item was found: a vendor product page reportedly references a US patent for its tolerance-modelling technology; the patent was not checked. No legal analysis of behavioural modelling, reverse engineering, or trademark use in plugin naming was retrieved. This is not legal advice; consult counsel.

### Cited Findings
- The Brainworx product page reportedly references a US patent for its TMT technology; the patent number was not checked. [bx_console N product page](https://brainworx.audio/products/brainworx/bx-console-n.html) (search summary; the patent was not opened)

### Inferences
- Implementing the same technique (per-part tolerance sampling) may fall within the patent's claims. Check the claims before shipping a similar method.
- Hardware brand and model names (Neve, SSL, API and similar) in product names or marketing carry trademark risk. This is general knowledge and was not sourced in this session.

### Gaps
- Not researched in this session: legality of modelling hardware behaviour, reverse engineering, and trademark use in plugin names.
