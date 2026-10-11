# Real-time audio architecture, CPU optimization, and plugin frameworks for a lightweight channel-strip plugin

Scope: real-time audio architecture, CPU optimization, and alternative plugin frameworks, as of 2026-10-10. Excluded per the brief: JUCE 8/9 licensing, VST3 MIT relicensing, CLAP host adoption, AAX signing. The tool-call budget (about 15) is used up. Several questions have only partial coverage, and the Gaps under each section list what was not verified. Most sources are repository pages, primary documentation, or search excerpts; the full text of several pages was not retrieved.

## Real-time thread rules and how to enforce them

### Takeaway
Clang's RealtimeSanitizer can fail a test when code marked `[[clang::nonblocking]]` allocates or calls a blocking function, but the sourced examples use clang++ and Windows/MSVC support was not confirmed. The full rule set from Ross Bencina's guidance was not retrieved (two scrapes failed), so the rules below are only partly sourced.

### Cited Findings
- RTSan is enabled with `-fsanitize=realtime`. A `std::vector::resize` call inside a function marked `[[clang::nonblocking]]` produced a runtime error reporting an unsafe library call (malloc) in a real-time context — [Clang RealtimeSanitizer docs](https://clang.llvm.org/docs/RealtimeSanitizer.html)
- The docs expose `__rtsan::ScopedDisabler` (header `<sanitizer/rtsan_interface.h>`) to exempt a scoped region from checking — [Clang RealtimeSanitizer docs](https://clang.llvm.org/docs/RealtimeSanitizer.html)
- The docs say ScopedDisabler is preferred for performance; the `function-name-matches` and `call-stack-contains` suppression types cost more at run time — [Clang RealtimeSanitizer docs](https://clang.llvm.org/docs/RealtimeSanitizer.html)
- In comments on his post, Bencina (the author) says audio I/O APIs or subsystems usually schedule buffer-processing threads and callbacks at high priority. He assumes the OS or driver guarantees the callback is scheduled reliably, and that the client's job is to return audio by the deadline. He also notes Windows has time-critical threads and MMCSS — [Bencina, "Real-time audio programming 101"](http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing) (comment excerpt only; main article not retrieved)
- An aggregator summary lists the core rules as: do not block the callback, avoid memory allocation, and keep algorithms within predictable execution time — [daily.dev summary of Bencina](https://daily.dev/posts/ross-bencina-real-time-audio-programming-101-time-waits-for-nothing-58hpkwawp). Secondary source.
- Talks and articles found in search, titles and metadata only (content not read):
  - Timur Doumler, "Using Locks in Real-time Audio Processing, Safely" — [ADC20 talk](https://www.youtube.com/watch?v=zrWYJ6FdOFQ), [article](https://timur.audio/using-locks-in-real-time-audio-processing-safely)
  - Renn-Giles and Rowland, "Real-time 101" Parts I and II — [Part I](https://www.youtube.com/watch?v=Q0vrQFyAdWI), [Part II](https://www.youtube.com/watch?v=PoZAo2Vikbo)
  - Timur Doumler, RCU talk — [video](https://www.youtube.com/watch?v=7fKxIZOyBCE)
  - ADC 2019 "Real-time 101" slide index — [drowaudio/presentations](https://github.com/drowaudio/presentations)
- Attribution correction to the brief: the brief credits "Real-time Confessions" (ADC23) to Dave Rowland. The indexed ADC23 talk is listed under Fabian Renn-Giles — [ADC23 talk](https://www.youtube.com/watch?v=JG7lqt7V1uY)

### Inferences
- RTSan can gate the "no allocation and no blocking calls on the audio thread" rule in CI, but only for code paths a test actually runs. It checks those specific calls; it does not by itself prove bounded worst-case compute or the absence of priority inversion (inference from the RTSan model described in the docs).
- Because the host owns the audio thread and its priority (per the Bencina comment above), the plugin's responsibility is bounded work per callback, not thread scheduling.
- Exemptions for init-time or GUI-to-audio hand-off code should use ScopedDisabler rather than suppression files, given the docs' performance note.

### Gaps
- Bencina's full rule list (lock-free queues, priority inversion, avoiding locks) could not be retrieved; two scrapes failed with ERR_TUNNEL_CONNECTION_FAILED. Those rules are unsourced here.
- RTSan support on Windows/MSVC is not established. The docs' example compiles with clang++; whether clang-cl works was not checked. This matters because the target host is Windows.
- Lock-free SPSC queue design, sizing, and the readerwriterqueue library were not verified (the scrape was rate-limited and not retried).
- The content of the Timur Doumler and Renn-Giles talks was not read, so no sourced claims about specific lock-free or RCU patterns.
- RTSAN_OPTIONS, `[[clang::blocking]]`, and suppression-file syntax were not verified against the docs in this pass.

## Scheduling, buffer budget, and host CPU metering

### Takeaway
The per-callback time budget is set by the host's buffer size and sample rate. REAPER's "RT CPU" reading is the most directly relevant host-side measure found, but it is described only in secondary sources. The Windows scheduling-floor figure is an unverified commenter claim.

### Cited Findings
- REAPER's Performance Meter is described as designed to show real-time pressure rather than only resource consumption, with an RT CPU reading among its options (excerpt truncated) — [Tim Inglis Audio, "Understanding REAPER CPU Performance"](https://timinglis.com.au/understanding-reaper-cpu-performance/)
- A VI-Control forum post says to right-click the performance meter and enable the "RT CPU" display, which it calls a more accurate measure of what to expect from the system — [VI-Control thread](https://vi-control.net/community/threads/reaper-cpu-performance.134254/). Forum source.
- A Reddit user reports that the RT CPU display reaching 100% indicates the CPU is struggling — [r/Reaper thread](https://www.reddit.com/r/Reaper/comments/1ewvuqe/reaper_keeps_going_crunchy_even_though_memory_and/). User report.
- A commenter on the Bencina post says time slicing on modern Windows only gets down to roughly 20 ms unless everything runs in real-time mode. This is a commenter's claim with no measurement cited — [Bencina comments](http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing). Unverified.

### Inferences
- The callback deadline is buffer size divided by sample rate (own arithmetic): 64 samples at 48 kHz gives about 1.33 ms; 512 samples at 48 kHz gives about 10.67 ms.
- Because the user chooses the buffer size, the plugin's worst-case cost per block should be measured at the smallest buffer a user is likely to run, not at a convenient default.
- An in-plugin or test-harness equivalent of "RT CPU" (time per process call divided by block deadline) would mirror what REAPER users see. The forum sources suggest this is the number users watch.

### Gaps
- No measured data on host callback overhead or on plugin-side scheduling latency under Windows, macOS, or Linux.
- The REAPER documentation itself was not consulted. All REAPER findings come from secondary sources.
- The 20 ms Windows figure is unverified; no Microsoft or driver documentation was retrieved.

## SIMD and CPU optimization

### Takeaway
Highway and xsimd are the two portable SIMD options with sourced details. Most of the stated trade-offs come from Highway's own maintainers' document and should be benchmarked before adoption. Profilers, denormal handling, and benchmark tooling have no sources in this pass.

### Cited Findings
- Highway is described as a C++ library providing portable SIMD and vector intrinsics — [google/highway](https://github.com/google/highway)
- Highway's maintainers' comparison document says Highway was built for runtime dispatch (selecting the ISA at run time) and is deployed widely inside Google — [Why we recommend Highway over std::simd](https://github.com/google/highway/blob/b93ad441d9cbeb7d7629281992b796a73033996a/g3doc/std_simd_comparison.md). Vendor-authored.
- The same document argues that `std::simd` lacks runtime dispatch and that a class-wrapper design does not fit scalable vectors (Arm SVE, RISC-V V). Vendor-authored claim about a standard library feature — [same document](https://github.com/google/highway/blob/b93ad441d9cbeb7d7629281992b796a73033996a/g3doc/std_simd_comparison.md)
- The same document says xsimd offers limited runtime dispatch, but only when the target set is listed explicitly and each target lives in a separate source file compiled with different flags, which it calls a maintenance burden — [same document](https://github.com/google/highway/blob/b93ad441d9cbeb7d7629281992b796a73033996a/g3doc/std_simd_comparison.md). Vendor-authored claim about a competitor.
- xsimd is described as a header-only C++ library — [google/highway project page, "Other libraries" section](https://github.com/google/highway)
- xsimd provides C++ wrappers for SIMD intrinsics and optimized math functions for SSE, AVX, AVX-512, NEON, SVE, WebAssembly, VSX, and RISC-V — [LibHunt comparison](https://www.libhunt.com/compare-highway-vs-xsimd). Aggregator.

### Inferences
- For a channel strip (gain, filters, metering, simple dynamics), runtime ISA selection matters mainly if the plugin ships a baseline older than AVX2 or runs on ARM. Highway's stated design targets that case; xsimd's per-target compile model is the cost its competitor describes. Benchmark both before choosing.
- The Highway-versus-xsimd claims above come from one side's own document. They are plausible but not independently confirmed.

### Gaps
- No sourced guidance on denormal handling (flush-to-zero, DAZ/FTZ) in filters or feedback paths.
- No sourced comparison of profilers. Tracy, Superluminal, VTune, Instruments, and perf were not confirmed: the search returned only REAPER pages.
- No benchmark numbers for Highway, xsimd, or hand-written intrinsics.
- Google Benchmark and nanobench were not researched.
- Whether Highway or xsimd builds cleanly under MSVC on Windows and on Apple Silicon was not verified.

## Alternative plugin frameworks (Visage, nih-plug, iPlug2)

### Takeaway
Of the three frameworks checked, iPlug2 is the only one whose repository page lists AAX alongside a permissive license. Visage is MIT with GPU-rendered UI backends, but its production track record is unverified. nih-plug is ISC-licensed, but it is in maintenance mode and its VST3 path depends on GPLv3 bindings. No CPU or GUI cost was measured for any of them.

### Cited Findings
- Visage is MIT-licensed and renders its UI on the GPU, with D3D11, Metal, Vulkan, and WebGL backends — [VitalAudio/visage](https://github.com/VitalAudio/visage)
- nih-plug is a Rust plugin framework under the ISC license, targeting VST3 and CLAP — [robbert-vdh/nih-plug](https://github.com/robbert-vdh/nih-plug)
- The nih-plug repository page indicates the project is in maintenance mode — [robbert-vdh/nih-plug](https://github.com/robbert-vdh/nih-plug)
- nih-plug's VST3 path uses vst3-sys bindings, which are licensed GPLv3 — [robbert-vdh/nih-plug](https://github.com/robbert-vdh/nih-plug)
- The nih-plug repository page indicates upstream is no longer actively maintained and that a fork is hosted on Codeberg — [robbert-vdh/nih-plug](https://github.com/robbert-vdh/nih-plug)
- iPlug2 is under a zlib-style license, supports many plugin formats including AAX, and has an IGraphics UI layer with NanoVG and Skia backends — [iPlug2/iPlug2](https://github.com/iPlug2/iPlug2)

### Inferences
- A closed-source commercial channel strip built on nih-plug's VST3 path may hit the GPLv3 obligation carried by vst3-sys. This is an inference from the repository page, not legal advice; get a licensing review before choosing nih-plug.
- Among these three, iPlug2 is the only framework with AAX and a permissive license on its repository page. If AAX is in scope, it is the first candidate to evaluate.
- A maintenance-mode framework carries upstream risk. Adopting it would likely mean owning a fork or patches (inference).
- Visage is a plausible way to keep GUI cost consistent across platforms, but a GPU-rendered UI brings its own per-platform costs, which were not researched.

### Gaps
- No measured CPU, memory, or idle-cost figures for Visage, iPlug2's IGraphics, or nih-plug.
- Visage's production maturity (shipping plugins, bug history, API stability) was not verified.
- Whether the nih-plug Codeberg fork is active was not verified.
- Vendor architectures (FabFilter, Tokyo Dawn, u-he, Kilohearts, Valhalla, Airwindows, Vital) were not researched.

## Build and CI templates

### Takeaway
Pamplejuce and a 2025 writeup show a CMake plus GitHub Actions path for cross-platform JUCE builds. Both are JUCE-specific, so they apply only if the framework stays on JUCE. The pluginval validator was not checked.

### Cited Findings
- Pamplejuce is described as a JUCE 8 audio plugin template with GitHub Actions integration — [sudara/pamplejuce](https://github.com/sudara/pamplejuce). Search excerpt only; the repository was not fetched.
- Its author describes Pamplejuce as a JUCE, CMake, Catch2, and GitHub Actions plugin template — [Melatonin, "How to use CMake with JUCE"](https://melatonin.dev/blog/how-to-use-cmake-with-juce/)
- A 2025 writeup describes shipping a C++ reverb plugin for macOS, Windows, and Linux using JUCE's CMake API and GitHub Actions — [Reilly Spitzfaden](https://reillyspitzfaden.com/posts/2025/08/plugins-for-everyone-crossplatform-juce-with-cmake-github-actions/). Search excerpt only.
- A JUCE forum user says GitHub Actions compiles universal macOS binaries and that it works fine — [JUCE forum thread](https://forum.juce.com/t/building-a-juce-plugin-for-any-mac/62574). Anecdotal.

### Inferences
- Pamplejuce depends on JUCE, so a non-JUCE choice (Visage, iPlug2, nih-plug) would need its own CI template. Reusing the CMake and GitHub Actions structure is plausible but not verified.
- A macOS-universal, Windows, and Linux matrix appears to be the common JUCE-community setup, based on the two writeups. The runner configuration was not read in full.

### Gaps
- pluginval was not searched or fetched, so no sourced claims about it.
- The Pamplejuce README was not fetched (rate-limited). Its current CI steps and JUCE version are unverified.
- DPF (DISTRHO) as a non-JUCE alternative was not fetched (rate-limited), so it has no sourced findings.
- Whether any of these templates run RTSan or other real-time checks was not established.

## GUI cost, state and preset versioning, denormals, and vendor architectures

### Takeaway
No sourced findings for these sub-questions in this pass. The only GUI-cost sources are framework-level descriptions, covered in the frameworks section above.

### Cited Findings
- GUI rendering backends are described in the framework sources above: Visage uses GPU backends (D3D11, Metal, Vulkan, WebGL) — [VitalAudio/visage](https://github.com/VitalAudio/visage); iPlug2 IGraphics offers NanoVG and Skia backends — [iPlug2/iPlug2](https://github.com/iPlug2/iPlug2). No cost data is attached to either.
- Lock-free state exchange (RCU) is the subject of a Timur Doumler talk, title and metadata only, content not read — [video](https://www.youtube.com/watch?v=7fKxIZOyBCE)

### Inferences
- The GUI-thread-to-audio-thread state hand-off probably belongs in the same design as the RCU or double-buffer pattern, but no source in this pass establishes the specifics.

### Gaps
- No measured GUI cost (idle CPU, redraw cost, memory) for any framework.
- State and preset versioning (stable parameter IDs, migration hooks, undo) has no retrieved source. nih-plug's persist-and-migration mechanism appears in my working notes but was not confirmed against a fetched page. Verify before relying on it.
- Denormal handling, profilers, and benchmark tooling: no sources (see the SIMD section gaps).
- Vendor plugin architectures: not researched.
