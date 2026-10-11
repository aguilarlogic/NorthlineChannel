# Waves Scheps Omni Channel 2 (and v1) Teardown

Scope: technical and UX teardown for a competing channel-strip VST. Sources are mostly the Waves v2 user guide PDF and the Waves v2 product page (both vendor-authored), plus review snippets. Review bodies were not fetched in full, and the pass ran close to its tool-call budget, so several items are listed under Gaps. Where sources conflict, both values are given.

Source labels used below:
- [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) = Waves user guide PDF (v2 text, with OCR artifacts from diagram labels)
- [Product page](https://www.waves.com/plugins/scheps-omni-channel) = Waves Omni Channel 2 product page
- [Waves Ultimate](https://www.waves.com/subscriptions/ultimate) = subscription tile
- [SOS](https://www.soundonsound.com/reviews/waves-scheps-omni-channel-2), [Tape Op](https://tapeop.com/reviews/gear/127/scheps-omni-channel-plug-in), [Gearspace](https://gearspace.com/threads/who-is-still-using-waves-scheps-omni-channel-2.1456967/), [Reddit](https://www.reddit.com/r/audioengineering/comments/18fzrua/does_anyone_here_consistently_use_sheps_omni/), [YouTube review](https://www.youtube.com/watch?v=h9yiqhBgxxQ), [YouTube features](https://www.youtube.com/watch?v=9WND-vSDR1A)

Version note: the scratch file named product_v1_page.md is the v2 product page ("Scheps Omni Channel 2"), not v1. No v1 product or manual content was retrieved.

## Q1. Release timeline and v1 vs v2 changes

### Takeaway
v2 is a feature expansion of v1, not a redesign. It adds a SOFT (soft-knee) compressor, a CRUSH saturation mode, a 24 dB/oct resonant HP/LP filter with RES control, FOCUS presets for the new features, and VST3 hosting (any brand) in the insert slot. The v1 control set is unverified because the v1 manual was not retrieved.

### Cited Findings
- Waves says v2 adds a soft-knee compressor, a 24 dB/oct filter with resonance control, and a crush saturation mode, "beyond the classic features in the original version." — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Introduction)
- Waves lists the v2 additions as CRUSH saturation, SOFT compressor (alongside VCA, FET, OPT), the 24 dB/oct resonant filter, and VST3 hosting in the insert point. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- v2 FOCUS presets were added to cover CRUSH, SOFT, and the 24 dB filter. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- Insert hosting: the v2 guide says the insert supports most VST3 plugins but not VST2. Waves plugins are shown in the menu, and VST3 plugins need a scan. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Insert Plugin Slot)
- Non-Waves VST3 plugins cannot be hosted when Omni is opened inside SuperRack or eMotion LV1. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- Date conflict: the fetched SOS v2 review reads "Published January 2024," but an earlier search snippet carried metadata dated 2023-12-21. Not resolved. — [SOS](https://www.soundonsound.com/reviews/waves-scheps-omni-channel-2)

### Inferences
- v1 most likely had VCA/FET/OPT compression, ODD/EVEN/HEAVY saturation, 6/12/18 dB filters without RES, and Waves-only insert hosting. This is inferred from the "added in v2" wording, not from a v1 source.
- The v2 "Host any Waves plugin or any VST3" framing suggests non-Waves hosting is new in v2, but no source states v1's limit directly.

### Gaps
- v1 control ranges and the v1 manual (manualslib or manua.ls listing, roughly 41 pages) were not retrieved. A full v1 vs v2 diff needs that.
- Original v1 release date: not retrieved.
- v2 release date: earlier research found a Waves press release and intro dated 16 to 17 July 2023, and MusicTech coverage dated 18 July 2023. The URLs were not retained in this pass, so re-verify before citing.
- Whether v2 changed any existing parameter defaults or ranges: unknown.

## Q2. Module and control inventory with parameter ranges

### Takeaway
Five processing modules (PRE, GATE, DS², EQ, COMP), an INSERT slot, and a MASTER section with a brickwall limiter. Most ranges are consistent across the guide's Controls section, but gate timing ranges and some diagram labels conflict and are flagged below.

### Cited Findings

PRE (Saturation, Filters, Thump)
- Saturation: on/off, level 0 to 100 percent. Type selector: ODD, EVEN, HEAVY, CRUSH. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Pre module)
- HEAVY is described as a clipper with a "custom response," not a standard clipper. CRUSH is described as adding heavy distortion similar to a distortion pedal. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Filters: separate HP and LP on/off and frequency, 20 Hz to 20 kHz each. Slope: 6, 12, 18, or 24 dB/oct. RES 0.1 to 4.0 applies to the 12, 18, and 24 dB slopes only. The 24 dB slope is described as a resonant HP/LP. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Filters)
- Thump: on/off, boost +2 dB or +4 dB, described as a wide boost that feels more like resonance than EQ. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)

GATE
- GATE/EXP mode switch. Threshold: described as -Infinity to 0 dB in the Controls text, shown as -144 to 0 on diagram labels. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (conflict)
- Attenuation meter: -6 to -60 dB labels. Floor (maximum gain reduction): -60 to 0 dB. Close (secondary close threshold): -48 to 0 dB; it only takes effect when set and below Threshold. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Attack: 0.1 ms to 10,000 ms in the Controls text, but 0.1 ms to 1 s on diagram labels. Release: 0.1 ms to 10,000 ms in the Controls text, but 1 to 10 s on diagram labels. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (conflict)
- Sidechain: EXT SC on/off. HP and LP sidechain filters, each 20 Hz to 15 kHz. A Link control turns HP and LP into a band-pass that sweeps together. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)

DS² (two identical sections)
- Each section: on/off, sidechain filter type, FREQ 20 Hz to 20 kHz, THRESH -48 to 0 dB, sidechain listen, and a four-step GR indicator (-3, -6, -9, -12 dB). — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (DS²)
- Filter type: low shelf, notch, bell, high shelf (one section lists "narrow cut" in place of notch). — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Threshold uses "adaptive sensing," which the guide says gives more natural results. The mechanism is not described. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Module OUTPUT: -12 to +12 dB. EXT SC feeds both sections. Expanded view gives independent L/R or M/S freq and threshold. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)

EQ (four bands)
- HIGH: fixed-Q Resonant Shelf or Shelf, or Parametric bell with Q. LOW: same three types. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (EQ)
- MID and TONE: bell with Wide, Narrow, or Parametric shapes. They are identical in Narrow and Parametric; in Wide, Mid is wider and cleaner, and Tone is narrower and "smeary." — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- All bands: gain -18 to +18 dB, frequency 20 Hz to 20 kHz, Q 0.2 to 10 (Parametric mode only). Each band can be bypassed individually. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)

COMP
- Four modes: VCA, FET, OPT, SOFT. Threshold -50 to 0 dB. GR meter labels -1 to -15 dB. Ratio 1:1 to Inf:1. Attack 0.4 to 150 ms. Release 50 ms to 5 s. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- MIX (wet/dry) 0 to 100 percent. OUTPUT trim -12 to +12 dB. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Sidechain EQ: low shelf ±18 dB, bell ±18 dB (both 20 Hz to 20 kHz), and HP 20 Hz to 20 kHz. Sidechain listen is available. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Changing compressor type does not change the other parameter values. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)

INSERT
- One slot. Waves plugins and most VST3 plugins are supported, not VST2. The insert menu displays "Overall Latency: 0.0 ms (0 samples)." — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)

MASTER
- VU meter source: input, output, or total gain reduction. Meter trim 4 to 26 dB, used to align to a reference tone. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Monitor (stereo component only): stereo or mono, L or R, M or S. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Limiter: brickwall, range -30 to 0 dB, placed just before output. The guide warns that lowering it significantly will likely cause audible distortion. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Full-scale input and output meters with clip indicators and peak hold; click to clear. Input and output faders can link L/R, with offsets preserved. Input phase invert is present. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)

Component differences
- The Mono component has no ST/DUO/MS selector and has its own default presets. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)

### Inferences
- Gate timing and threshold conflicts are probably documentation errors, since the Controls text and the diagram labels disagree. Verify on the plugin before copying any range.
- The per-module stereo mode implies a per-module channel configuration, which is a design decision for a competitor (global versus per module).

### Gaps
- Q values for Wide and Narrow Mid/Tone are not stated.
- Keys and Buss² buttons appear in the header bar of the guide, but their function is not described in the text read. Unknown.
- Some diagram labels are OCR artifacts (for example "Schpes Omin," "REL 1 10h," "16 F 21K"). Stereo-component floor ranges (-50 to 0 on one diagram versus -60 to 0 in the Controls text) are unresolved.
- Exact fader ranges (input label shows +12 to -Inf) are not confirmed.

## Q3. Signal flow, routing, and stereo modes

### Takeaway
The default chain runs left to right: PRE, GATE, DS², EQ, COMP, INSERT, then MASTER. Modules can be reordered by dragging. Each module's stereo mode (ST, DUO, or MS) changes how dynamics sidechains are summed, and changes processing to mid/side for MS. Module output is always stereo, and conversion to M/S happens at the input of the next module.

### Cited Findings
- Default layout order and module names: PRE, GATE, DS², EQ, COMP, INSERT, then MASTER. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Components)
- Reordering: drag the arrows above a module name. Changing order can dramatically change the sound. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Rearranging Processing Sequence)
- Any module, including the insert, can be placed anywhere in the chain. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- The module's output is always stereo, and it is converted to M/S at the input of the next module if needed, which lets modules be reordered without changing stereo processing. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Stereo Mode)
- In all modules, the stereo switch sets whether the input is processed as L/R (ST or DUO) or converted to M/S (MS). Duo "does not alter the stereo signal itself." — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Controls)
- In dynamics modules (DS², COMP, GATE), the mode sets the sidechain summing. ST sums L and R (same gain reduction on both sides). DUO processes L and R sidechains separately. MS processes mid and side sidechains separately. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Pro Tools provides only a mono sidechain, so the ST/DUO/MS sidechain distinction does not apply there. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (footnote)
- Linking: Collapsed view always links L and R (or M and S). Expanded view has a Link button. Linked switches set both sides. Relinking keeps offsets. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Linking Controls)
- Copy L-R and R-L: right-click a module's blank area in Collapsed view. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Copy Channel Parameters)
- Internal sidechain: the detector is built into the dynamics unit and can be filtered. The external sidechain is shared by all modules in the instance, toggled per module with EXT SC. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Sidechain)
- Sidechain listen: Ctrl-drag vertically on a frequency control sweeps the sidechain frequency. DS² listen mutes the main outputs and band-limits the sidechain. In sidechain listen, the output faders control the monitor level. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Example workflows: M/S EQ on overheads (add low-mids to the mid, top shelf to the sides); DUO compression on drums to widen; a DS² solo-sidechain search for bleed. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Practical examples and Working with M/S)

### Inferences
- For non-dynamic modules (PRE, EQ), ST and DUO appear to process identically, since the guide says Duo does not alter the stereo signal. This is an inference from the wording, not stated outright for EQ.
- Per-module M/S conversion at every module boundary implies a matrix at each junction. Its latency and CPU cost are undocumented.

### Gaps
- Whether ST and DUO differ for EQ and PRE processing is not explicitly stated.
- Insert routing details (stereo or mono hosting, M/S handling) are not documented.

## Q4. Analog character: saturation and compressor types

### Takeaway
Character is described qualitatively. Waves names four saturation flavors and four compressor types but publishes no transfer curves, harmonic data, knee width, or oversampling information in the text read. The "analog" claim rests on marketing language.

### Cited Findings
- VCA: modern-sounding, the most transparent of the three, feedforward design, very fast attacks, very little distortion, and no compression in its sidechain. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Compressor)
- FET: the guide describes it as tending to be more "peaky" and says it has compression in the sidechain. Other FET traits from earlier notes (feedback-style topology, extra distortion, pumping at high ratios with slow attack and fast release) were not re-verified in the final pass. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- OPT: based on photocell designs, the slowest and smoothest, with long memory and rich tone that preserves transients. Compression in the sidechain. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- SOFT: applies compression gradually after the threshold, described as a soft knee. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Saturation: ODD and EVEN content is described as content-dependent. HEAVY is a clipper with a custom response. CRUSH is a strong distortion. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- The product page lists four harmonic saturation options (ODD, EVEN, HEAVY, CRUSH) and describes the strip as combining the efficient design and cohesive sound of classic analog strips with digital flexibility. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- The product page includes a feature heading "Precision Modeled Transformers" (image alt text only). It does not say which module or what is modeled. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- The guide describes the PRE module as a processor for warmth and color that can sit anywhere in the chain. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)

### Inferences
- The FET and VCA descriptions indicate topology choices (feedforward versus feedback detector) that a competitor could match and document. Waves gives no measurements to confirm the sound.
- HEAVY's "custom response" is undefined, so it is a black box to replicate.
- Because the guide describes odd and even harmonics as content-dependent, a competitor should test on program material rather than trusting a fixed curve.

### Gaps
- Harmonic spectra, saturation transfer curves, and whether saturation runs oversampled: not found.
- SOFT knee width in dB and the compressor's makeup-gain behavior: not documented in the guide. A Reddit user (version unclear) said makeup gain cannot be disabled, which the guide does not describe. See Q8.
- Which module, if any, the "Precision Modeled Transformers" heading refers to: unknown.

## Q5. Metering, workflow, and UX

### Takeaway
The UX centers on Collapsed and Expanded module views, a FOCUS mode that highlights relevant controls for each preset, a preset bar with 32-level undo, A/B compare, and per-module solo/listen tools that are central to the workflow. Metering is concentrated in the Master section.

### Cited Findings
- Collapsed view has one control per function for stereo or mono. Expanded view has larger knobs for touch displays and separate L/R or M/S controls. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Expanded view)
- Expanded view top bar: module name (click to bypass), Link, ST/DUO/MS, SC Filters, EXT SC, and collapse. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Bypass is per module (click name) and per section for PRE saturation and thump, each EQ band, and each DS² band. The control being edited is outlined in yellow. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Common Controls)
- FOCUS mode: loading a preset highlights the controls most likely to be used with it. All controls remain active. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Focus Mode)
- FOCUS presets by Andrew Scheps cover vocals, drums, bass, and other sources. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- Preset bar: Load (factory and user), Save (new file, copy to other instances), Setup, A/B compare, A-to-B copy, and Full Reset. Default preset is set per stereo or mono component. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Using Presets)
- Undo and redo: 32 levels, including module reordering. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Preset file location (Windows): C:\Users\Public\Waves Audio\Plug-In Settings. The Mac path was not confirmed in this pass. Pro Tools needs a relaunch after creating a user preset. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Presets can be shared between instances and hosts through the WaveSystem toolbar and its guide. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf); [Waves System Guide](https://www.waves.com/1lib/pdf/plugins/waves-system-guide.pdf)
- GUI size options in the toolbar: 75, 100, 125, 150, and 200 percent, with an option to apply to all. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Insert and VST3 pages)
- Factory presets (manual list): Full Reset, PA Default, and folders for Andrew Scheps, Billy Bush, Brad Divens, Dave Darlington, Eddie "El Brujo" Caipo, Ken "Pooch" Van Druten, Tony Visconti, and Yoad Nevo. The product page names a larger set (for example Jacquire King, Tony Maserati, Steve Lillywhite, Frank Filipetti) and says over 250 presets. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf); [Product page](https://www.waves.com/plugins/scheps-omni-channel). Preset count and artist list differ by source and date.
- Master metering: VU source selection (input, output, GR), meter trim 4 to 26 dB, full-scale meters with clip and peak hold. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Per-module GR indicators: DS² uses four LED steps (-3 to -12 dB). COMP uses labeled steps (-1 to -15 dB). GATE uses an attenuation meter (-6 to -60 dB). — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- VST3 management in the toolbar: scan plugins, scan a single plugin, clear caches and rescan, set plugin folder, sort by vendor or category. A first-open prompt offers a scan. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)

### Inferences
- FOCUS is a preset-level metadata feature (a list of highlighted controls per preset). A competitor would need to store that per preset.
- The Expanded view is explicitly sized for touch displays, which suggests a scalable-UI decision that competitors can match.

### Gaps
- Keys and Buss² features are not documented in the text read.
- Keyboard shortcuts beyond Ctrl-drag and Cmd-click (Mac, to delete a preset via Load) are not documented.
- DAW-specific UI behavior (for example, how preset sharing works in Pro Tools versus other hosts) beyond the note above is not covered.

## Q6. CPU, latency, and oversampling

### Takeaway
Waves claims zero latency. Its latency table lists 0 samples for Omni Channel 2 at 44.1, 48, 88.2, and 96 kHz. CPU figures and oversampling details were not found in the sources read.

### Cited Findings
- Waves markets the plugin as zero latency, for studio and live use. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- The product page embeds latency data: Scheps Omni Channel 2 reports 0 samples at 44.1, 48, 88.2, and 96 kHz. — [Product page](https://www.waves.com/plugins/scheps-omni-channel) (embedded latency table)
- The insert menu displays "Overall Latency: 0.0 ms (0 samples)." — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Insert Plugin Slot)
- A module that is switched off uses no CPU. The Startup section says sections bypassed independently do not affect CPU. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Startup Condition)
- Contradiction within the manual: the Common Controls text says turning off a section will reduce CPU load. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (Section on/off)

### Inferences
- Zero latency with no insert is consistent with no lookahead in the gate or DS² path. This is an inference; lookahead is not discussed in the guide.
- Any latency from a hosted VST3 in the insert will add to the chain. The guide does not say how it is reported to the host.

### Gaps
- CPU benchmarks, percent CPU per module, and sample-rate scaling: not found.
- Oversampling, including for HEAVY or CRUSH saturation: not found. Aliasing behavior is unknown.
- Plugin delay compensation behavior in specific hosts (Pro Tools, Logic, Cubase, Ableton): not verified.
- AAX DSP or SoundGrid versions, and any latency differences: not verified.

## Q7. Formats, platforms, pricing, and licensing

### Takeaway
Price reference: full price $149, with the product page showing a $39.99 sale at scrape time (undated). Subscription access is through Waves Creative Access, with the Ultimate tile listing $24.99 per month. Formats and OS requirements were not confirmed from the pages read in this pass.

### Cited Findings
- Product page: full price $149, with a "Price $39.99" promotional figure shown. The page was undated when scraped. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- Product page rating from earlier research: 4.87 from about 1,578 reviews. Not re-checked in this pass. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- The Ultimate subscription tile lists a price of $24.99 per month and about 1,648 reviews. — [Waves Ultimate](https://www.waves.com/subscriptions/ultimate)
- Waves Creative Access is linked from the product page as the subscription path. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- Insert hosting: VST3 only (not VST2). — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Pro Tools mono sidechain limitation. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Older price snippets from earlier research (MusicTech $29.99 intro; Beat Community $49.99 with code SAVE40) are older promotions. The URLs were not retained in this pass, so treat them as unverified.

### Inferences
- The $149 full price is the anchor reference. The $39.99 figure is a promotion and should not be used as the baseline.
- Subscription inclusion makes the plugin's perceived value depend on the Waves bundle, so a competitor's price should be framed against both the single-plugin and subscription options.

### Gaps
- Plugin format list (VST3, AU, AAX, VST2): the scraped pages only confirm VST3 for the insert. The earlier note listing VST2, VST3, AU, and AAX came from 2023 coverage whose URL was not retained. Unverified.
- OS minimums, Apple Silicon support, and AAX DSP support: not found.
- Licensing mechanism (Waves Update Plan, activation, or iLok): not found in this pass.
- Upgrade pricing from v1 to v2: not found.

## Q8. Reviewer praise and complaints

### Takeaway
Only search-result excerpts were available in this pass, not full review text. The excerpts point to: depth and density of per-module options (Tape Op), an all-in-one strip built on Scheps' processors (SOS), a favorable view of the EQ (a Gearspace user), and one specific complaint about compressor makeup gain (a Reddit user, version unclear). Full reviews are needed before drawing praise and complaint conclusions.

### Cited Findings
- Tape Op excerpt: the reviewer says each module has more options than is easy to take in, and the processors became clearly useful after two instances were kept in place on a session. — [Tape Op](https://tapeop.com/reviews/gear/127/scheps-omni-channel-plug-in)
- SOS excerpt: describes the plugin as an all-in-one five-module strip based on the sonic character of Scheps' processors. — [SOS](https://www.soundonsound.com/reviews/waves-scheps-omni-channel-2)
- Gearspace forum excerpt: a user says the EQ blends EQ types Scheps likes for different frequencies and rates it as strong as the rest of the strip. — [Gearspace](https://gearspace.com/threads/who-is-still-using-waves-scheps-omni-channel-2.1456967/)
- Reddit excerpt: a user says the compressor has makeup gain that cannot be turned off, calling it their only complaint, and implies it is poorly suited to ducking. The thread may refer to v1; the excerpt does not say. — [Reddit](https://www.reddit.com/r/audioengineering/comments/18fzrua/does_anyone_here_consistently_use_sheps_omni/)
- YouTube review (v2 "new features"): title only, body not read. — [YouTube review](https://www.youtube.com/watch?v=h9yiqhBgxxQ). Second video ("5 new features") also title only. — [YouTube features](https://www.youtube.com/watch?v=9WND-vSDR1A)

### Inferences
- The makeup-gain complaint may conflict with the manual, which describes an OUTPUT trim and a MIX control but not automatic makeup gain. Either the compressor auto-compensates, or the user is describing the output level behavior. Verify before using.
- Density of options is the recurring theme. A competitor can differentiate on clarity, though the Tape Op excerpt frames the density as a value.

### Gaps
- Not retrieved: Production Expert, MusicRadar, MusicTech, KVR, Tape Op full text, SOS full text, Gearspace thread bodies, Reddit thread bodies, and YouTube transcripts.
- Known bugs, CPU complaints, and DAW compatibility issues: not searched in this pass.
- The SOS date conflict (see Q1) is unresolved.

## Q9. Andrew Scheps' design philosophy

### Takeaway
Scheps frames the plugin as both an artist-signature tool and a general channel strip. His stated goal is a unified, easy-to-use strip that packages the tools he uses in mixes, with color options and flexibility. Waves credits his "careful design" for the cohesive analog-style sound.

### Cited Findings
- In the guide's foreword, Scheps defines a channel strip as everything needed to make tracks sound better in one unified interface. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (A note from Andrew Scheps)
- The foreword says the plugin is not only a set of processors and not only an artist signature; it is both. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Presets are meant to show how Scheps and other engineers would use the plugin, and FOCUS mode points the user to the go-to controls for each preset. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- Scheps is quoted on the product page saying the original plugin was his dream tool, with everything he uses in one window, and that after years as his main mix tool he added options he uses regularly. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- Waves credits "time-tested combinations of compression, EQ, and saturation tones" and "careful design" for a cohesive analog channel strip sound. — [Product page](https://www.waves.com/plugins/scheps-omni-channel)
- The guide frames the PRE module as warmth and color that can sit anywhere in the signal path, with saturation at its core. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf)
- EQ is described as four bands that each have a distinct musical sound and blend with each other, with any band able to become fully parametric. — [Manual](https://assets.wavescdn.com/pdf/plugins/scheps-omni-channel-2.pdf) (EQ Module)

### Inferences
- The design philosophy implies five principles a competitor would need to address: (1) one-window workflow; (2) color and correction kept as separate tool groups; (3) signal order as a creative choice; (4) artist-curated presets as teaching material; (5) analog-style flavors delivered with digital flexibility (per-module stereo modes, reorderable chain, sidechain EQ).
- A competitor should copy the pattern (named modules with distinct character options, plus focus presets) without copying the artist-signature branding.

### Gaps
- No interview or Scheps-authored essay on design process was retrieved.
- Which hardware or plugins the modules emulate is not named in the sources read. Scheps' statement that the tools are ones he uses is the only attribution.
- Scheps' view on specific trade-offs (for example, why FET is "peaky" or why HEAVY has a custom curve) is not documented here.
