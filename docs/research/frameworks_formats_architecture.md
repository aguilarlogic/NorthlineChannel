# Audio Plugin Frameworks, Formats, and Real-Time Architecture (status as of 2026-10-10)

Scope note: This run used about 19 tool calls. Firecrawl search was rate-limited (429), so research used WebSearch and WebFetch. Two primary sources could not be read: the GitHub vst3sdk page (HTTP 504) and Steinberg's VST 3.8 press release PDF (no text extracted). Confidence is marked per item. Sections for real-time safety, CPU optimisation, state/presets, and build systems were not researched beyond leads.

## 1. Frameworks and licensing

### Takeaway
JUCE 9 (released 2026-07-21) kept JUCE 8's licensing and prices: free Starter tier under $20k revenue, Indie at $40/user/month or $800 perpetual up to $300k, Pro at $175/user/month or $3,500 perpetual with no revenue cap, and an AGPLv3 dual-licence option. The VST3 SDK reportedly moved to MIT with VST 3.8 (Oct 2025), but that rests on secondary sources. Other frameworks were not researched in this run.

### Cited Findings
- JUCE 9 released; the announcement is dated July 21, 2026 — [JUCE forum: JUCE 9 is Available Now](https://forum.juce.com/t/juce-9-is-available-now/69175) (fetched)
- juce.com tiers: Starter free, up to $20,000 revenue; Indie $40/user/month or $800 perpetual, up to $300,000, one-month minimum; Pro $175/user/month or $3,500 perpetual, unlimited revenue, 12-month minimum; Educational free. The FAQ says JUCE is "dual licensed under both the JUCE licence and the AGPLv3." No date is shown on the page — [juce.com/get-juce](https://juce.com/get-juce/) (fetched)
- JUCE 9 licensing and pricing unchanged from JUCE 8; JUCE 8 subscriptions were upgraded automatically — [JUCE 9: No pricing or EULA changes](https://forum.juce.com/t/juce-9-no-pricing-or-eula-changes/69000) (search summary, not fetched)
- JUCE 9 headline changes: lunasvg-based SVG pipeline (radial gradients, blend modes, clip paths), variable font axes, macOS CoreAudio moved to Apple's aggregate device API (announcement reports latency reductions of 11-25 ms in developer testing), faster software renderer, automatic OpenGL ES on embedded Linux, XInput multitouch on Linux — [JUCE forum: JUCE 9 is Available Now](https://forum.juce.com/t/juce-9-is-available-now/69175) (fetched)
- JUCE 9 post: AudioProcessor v2 (APv2) is in development, with "unique CLAP features" mentioned as part of that work. The post does not mention Direct2D, WebView, AU, AAX or VST3 — [JUCE forum: JUCE 9 is Available Now](https://forum.juce.com/t/juce-9-is-available-now/69175) (fetched)
- JUCE 9.0.1 published its WebView interop package as TypeScript on npm (`@juce-framework/webview`); 9.0.2 and 9.0.3 are referenced but unconfirmed — [JUCE 9.0.1 is out](https://forum.juce.com/t/uce-9-0-1-is-out/69269) (search summary, not fetched)
- VST 3.8 SDK moved to MIT, announced late Oct 2025. Per coverage, no signed agreement is needed. The licence covers VST3 only; VST2 still requires the separate VST2 licence agreement — [SonicState, 2025-10-30](https://sonicstate.com/news/2025/10/30/vst-3-now-available-under-mit-license); [CDM](https://cdm.link/?p=155957); [Steinberg forum: VST 3 SDK License](https://forums.steinberg.net/t/vst-3-sdk-license/201637) (search summaries, not fetched)
- CDM notes the SDK was already available under GPLv3 or a proprietary licence, so the change is GPLv3 to MIT rather than a new open release. ASIO now also has a GPLv3 option — [pixls.us: VST3 becomes open-source, ASIO goes GPL-compatible](https://discuss.pixls.us/t/vst3-becomes-open-source-asio-goes-gpl-compatible/53917) (search summary, not fetched)
- Primary check: Steinberg's press release PDF (dated 2025-10-15 per its title) was located but could not be read. The GitHub vst3sdk LICENSE page timed out. The MIT claim is therefore not confirmed from a primary source — [Steinberg press release PDF](https://ocl-steinberg-live.steinberg.net/_storage/asset/819253/storage/master/Press%20Release%20-%202025-10-29%20-%20VST%203.8%20-%20EN.pdf)
- Antares (a commercial vendor) lists FabFilter as having recently adopted CLAP, citing its open-source nature and MIT licence. This is a single unverified search-summary claim — [Antares: VST3, AU, AAX, CLAP in 2026](https://www.antarestech.com/blog/vst3-au-aax-clap-plugin-formats-2026)

### Inferences
- Indie-tier perpetual ($800) breaks even against Indie monthly ($40) at 20 months. Pro perpetual ($3,500) breaks even against Pro monthly ($175) at 20 months. These figures are my arithmetic on published prices.
- For a small commercial channel strip, the Starter tier is free under the $20k cap. The AGPL option is only useful if the product is distributed under AGPL terms.
- If the VST3 MIT change is confirmed, direct VST3 development without JUCE carries less licence friction than before. The cost of building a cross-platform GUI and host layer from scratch remains the main trade-off.

### Gaps
- Not researched: iPlug2, DPF (DISTRHO), nih-plug (Rust), Visage, Elk, and CHOC.
- Not researched: how FabFilter, TDR, u-he, Kilohearts, Valhalla, and Airwindows build their plugins.
- Direct2D renderer: not found in any JUCE 8 or 9 source reviewed.
- JUCE native CLAP support status: only the APv2 mention above.
- Primary confirmation of the VST3 MIT licence from Steinberg's press release or the vst3sdk LICENSE file.

## 2. Plugin formats and platform support

### Takeaway
VST3 is the baseline format. CLAP host support is strongest in Bitwig and REAPER, with FL Studio and Studio One disputed. Logic is AU-only. AAX signing costs come only from community posts and conflict. On Windows on Arm, x64 VST3 plug-ins load through Arm64EC on Windows 11, but the evidence is from 2024 and native ARM64 builds are recommended for performance.

### Cited Findings
- A 2026 vendor guide lists Bitwig Studio and REAPER as CLAP hosts. The guide is from Antares, which has a commercial interest — [Antares 2026](https://www.antarestech.com/blog/vst3-au-aax-clap-plugin-formats-2026) (search summary, not fetched)
- A November 2024 report says Bitwig, FL Studio, REAPER and Studio One have integrated CLAP — [SonicState: ADC 2024 CLAP progress](https://www.sonicstate.com/news/2024/11/13/adc-2024-clap-plugin-format-progress-and-future-plans/) (search summary, not fetched). An older Sweetwater article says only that Avid, Image Line and PreSonus expressed interest — [Sweetwater: CLAP](https://www.sweetwater.com/insync/clap-the-new-clever-audio-plug-in-format/)
- Logic Pro appears only under AU in the Antares table. Pro Tools: Avid has expressed interest only. Cubase: no supporting source found — [Antares 2026](https://www.antarestech.com/blog/vst3-au-aax-clap-plugin-formats-2026) (summary)
- Ardour developers argue CLAP adds nothing that LV2 does not already offer in that host — [Sweetwater: CLAP](https://www.sweetwater.com/insync/clap-the-new-clever-audio-plug-in-format/) (summary)
- AAX signing: one JUCE forum commenter says Avid waives the PACE signing fee. A HISE post says Avid assigns a free PACE EDEN Tools signing licence after registration, with an alternative at about $500/yr. Other posts conflict. An SSL code-signing certificate is needed. iLok copy protection is optional, and Pro Tools loads unprotected plug-ins — [JUCE forum: AAX only in Pro licence](https://forum.juce.com/t/solved-has-been-re-included-reasoning-behind-aax-only-being-included-in-pro-license/21896?page=2); [HISE forum: AAX SDK compiled](https://forum.hise.audio/post/60679) (community posts, not primary; unverified)
- Windows on Arm: Arm64EC lets x64 plug-in DLLs load into an Arm64 host process without recompiling. Windows 11 only. Microsoft recommends native Arm64 builds for battery life and performance — [Microsoft Windows Music Dev blog](https://devblogs.microsoft.com/windows-music-dev/?p=445)
- Cubase and Nuendo Arm previews (Oct 2024): VST3 and ARA supported, VST2 not supported. iLok-protected plug-ins did not run under Windows on Arm at the time — [Steinberg help: Windows on Arm](https://helpcenter.steinberg.de/hc/en/articles/21829527504530); [SonicState, 2024-10-29](https://www.sonicstate.com/news/2024/10/29/-steinberg-releases-windows-on-arm-previews/)
- Steinberg forum: VST2 plug-ins and hosts require the VST2 licence agreement — [Steinberg forum: VST 3 SDK License](https://forums.steinberg.net/t/vst-3-sdk-license/201637) (summary)
- CLAP wrappers: the free-audio/clap-wrapper project (VST3, AUv2 and standalone from a CLAP build) is the likely project per my own background knowledge. This run did not verify its status — [Awesome Ecosystems: CPLUG](https://awesome.ecosyste.ms/projects/github.com%2FTremus%2FCPLUG) describes a different single-API approach. Forum posts claim wrappers add no latency; this is unverified.

### Inferences
- Plan VST3 as the baseline, AU for Logic and macOS, and CLAP as an add-on. Wrapping a CLAP build could cover VST3 and AU from one codebase, but this needs verifying.
- For Windows on Arm, a native ARM64 build should beat emulated x64, per the Microsoft guidance.

### Gaps
- AAX official pricing and terms: Avid/PACE primary source not reached.
- Whether JUCE 9's licence still includes AAX in the Pro tier: only a thread title was read.
- AU v3 specifics, Apple universal binaries, and LV2 status: not researched.
- Current VST2 licensing (new licences available or not): not confirmed.
- Windows on Arm status after October 2024: not found.
- Primary-source confirmation of CLAP support for FL Studio, Studio One and Cubase.

## 3. Real-time safety

### Takeaway
The search for real-time safety sources yielded only talk and article leads, not verified technique content. The leads are Timur Doumler's CppCon 2015 talk "C++ in the Audio Industry" and a two-part "Real-time 101" by Fabian Renn-Giles and Dave Rowland. The user's attribution of "Real-time 101" to Doumler and Renn-Giles was not confirmed.

### Cited Findings
- Timur Doumler, CppCon 2015, "C++ in the Audio Industry" — [isocpp.org blog listing](https://isocpp.org/blog/2016/06/cppcon-2015-cpp-in-the-audio-industry-timur-doumler) (title only; content not read)
- A JUCE forum thread links "Real-time 101" Part I ("Investigating the real-time problem space") and Part II ("The real-time audio developer's toolbox"), both presented by Renn-Giles and Rowland — [JUCE forum: Lock free & real time stuffs for dummies](https://forum.juce.com/t/lock-free-real-time-stuffs-for-dummies/58870) (page is a list of links; event and date not confirmed)
- Doumler's "What is Low Latency C++?" (CppNow 2023, two parts) — search summary only, not fetched

### Inferences
- The Doumler and Renn-Giles/Rowland talks are the right starting point for audio-thread rules, but their content must be read before any checklist is relied on.

### Gaps
- Not verified in this run: no allocations, locks or syscalls on the audio thread; lock-free FIFOs; atomics for parameters; smoothing; sample-accurate automation; variable block sizes and sample-rate changes; setLatencySamples and dynamic latency; bypass with latency compensation; denormals (FTZ/DAZ).
- Ross Bencina's "time waits for nothing" was not searched.

## 4. CPU optimization

### Takeaway
Not researched in this run. No sourced findings.

### Gaps
- SIMD (xsimd, Highway, JUCE SIMDRegister), float vs double, -ffast-math, profiling tools (Tracy, VTune, Superluminal, Instruments), DAW per-instance CPU measurement, silence detection, and multi-mono vs stereo processing all need sourcing.
- Only related fact found: JUCE 9's faster software renderer uses auto-vectorised pixel fills — this is GUI, not DSP ([JUCE forum: JUCE 9 is Available Now](https://forum.juce.com/t/juce-9-is-available-now/69175)).

## 5. GUI technology

### Takeaway
JUCE 9 improves its software renderer, SVG support and variable fonts, and ships a WebView interop package on npm (9.0.1). No GPU backend or Direct2D information was verified.

### Cited Findings
- JUCE 9: lunasvg SVG, variable fonts, faster software renderer, OpenGL ES on embedded Linux — [JUCE forum: JUCE 9 is Available Now](https://forum.juce.com/t/juce-9-is-available-now/69175) (fetched)
- JUCE 9.0.1: WebView interop as TypeScript on npm — [JUCE 9.0.1 is out](https://forum.juce.com/t/uce-9-0-1-is-out/69269) (summary)

### Gaps
- Direct2D, Metal, and OpenGL deprecation on macOS: not verified.
- WebView CPU and memory cost: not measured.
- HiDPI, meter refresh rates, accessibility: not researched.
- CHOC and Visage: not researched.

## 6. State and presets

### Takeaway
Not researched in this run.

### Gaps
- Parameter ID stability, versioning, session backward compatibility, preset formats, A/B and undo, copy and paste between instances, inter-instance IPC pitfalls.

## 7. Build systems

### Takeaway
Not researched in this run. Only a lead found.

### Cited Findings
- Forum posts describe one codebase exposing VST3, AUv2 and CLAP through a single API (CPLUG). This is a different project from free-audio/clap-wrapper — [Awesome Ecosystems: CPLUG](https://awesome.ecosyste.ms/projects/github.com%2FTremus%2FCPLUG)

### Gaps
- CMake with JUCE or iPlug2, Pamplejuce, cross-compilation, and GitHub Actions CI: not researched.

## Provisional recommendation (low confidence, based on the findings above)
- Framework: JUCE 9 is the best-supported option with a clear licence path. Starter (free under $20k revenue) covers early development. Indie or Pro applies later. The AGPL option applies if the product is open source. This is provisional because the other frameworks were not compared.
- Formats: VST3 first, AU for macOS and Logic, CLAP as a secondary target. Check AAX terms against Avid's own developer pages before committing.
- Windows on Arm: plan a native ARM64 build.
- Before committing, verify: the VST3 MIT licence on the vst3sdk LICENSE file, JUCE 9 format list on juce.com, and the AAX signing terms with Avid.
