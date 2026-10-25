# Third-party notices

Northline Channel is licensed under the GNU Affero General Public License v3 (see [LICENSE](LICENSE)).
It is built with the following third-party components, under their own licences.

| Component | Used for | Licence | Where |
|---|---|---|---|
| [JUCE](https://juce.com) 9.0.3 | Plug-in framework, GUI, audio and hosting | AGPLv3 (used under it; JUCE is also available under a commercial licence) | `external/JUCE` (not in this repository; cloned at build time) |
| [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (bundled with JUCE) | VST3 plug-in format | MIT, © Steinberg Media Technologies GmbH | `external/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK` |
| [Inter](https://github.com/rsms/inter) 4.1 | Interface font, embedded in the binary | SIL Open Font License 1.1, © The Inter Project Authors | `Resources/fonts`, licence in `Resources/fonts/OFL.txt` |
| [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) (optional) | CLAP plug-in format | MIT, © Paul Walker | `external/clap-juce-extensions` (not in this repository; cloned at build time) |
| [CLAP](https://github.com/free-audio/clap) and clap-helpers (optional, via clap-juce-extensions) | CLAP plug-in format | MIT | `external/clap-juce-extensions/clap-libs` |

VST is a trademark of Steinberg Media Technologies GmbH. CLAP is an open standard by the free-audio
project. The names are used only to describe the plug-in formats.
