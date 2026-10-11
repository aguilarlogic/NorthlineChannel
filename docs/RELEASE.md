# Release process

How a Northline Channel release is built, validated, signed and published. Automated by
[.github/workflows/release.yml](../.github/workflows/release.yml) on a `v*` tag.

## Checklist

1. All CI checks green on `main` (tests, pluginval strictness 10 on every format and OS, auval, clap-validator).
2. Golden files regenerated only with a listening sign-off in the PR that changed the sound.
3. Version bumped in `CMakeLists.txt` (`project(... VERSION x.y.z)`), changelog written.
4. Host matrix pass recorded (roadmap 14.9) for this build.
5. Tag `vX.Y.Z` and push. The workflow builds Windows x64 and ARM64 and macOS universal,
   runs the validators, signs, packages, notarizes and drafts a GitHub release with checksums.
6. Install the drafted artefacts on clean machines (Windows 11 x64, Windows 11 ARM64, macOS
   arm64, macOS x86_64) and smoke-test in one host each, then publish.

## Formats

| Format | Windows | macOS | Notes |
|---|---|---|---|
| VST3 | x64, ARM64 | universal | |
| AU v2 | | universal | `auval -v aufx Nlch Nrth` |
| CLAP | x64, ARM64 | universal | via `free-audio/clap-juce-extensions` in `external/` (CMake option `NL_BUILD_CLAP`) |
| Standalone | x64, ARM64 | universal | |
| AAX | | | Planned for 1.1: needs an Avid developer account, the AAX SDK and PACE signing |

## Code signing

### Windows ([packaging/windows/sign.ps1](../packaging/windows/sign.ps1))
- **Azure Artifact Signing** (formerly Trusted Signing), if the publisher is eligible
  (organisations; individuals only in the US and Canada at the time of writing).
  Secrets: `NL_SIGN_MODE=azure`, `NL_AZURE_DLIB`, `NL_AZURE_METADATA`.
- Otherwise an **OV code-signing certificate** on a hardware token or cloud HSM.
  Secrets: `NL_SIGN_MODE=cert`, `NL_CERT_SHA1` (a self-hosted runner is needed for a USB token).
- Every binary and the installer are signed with SHA-256 and an RFC 3161 timestamp, then verified.

### macOS ([packaging/macos/package.sh](../packaging/macos/package.sh))
- Developer ID Application (bundles) and Developer ID Installer (pkg) certificates.
- Hardened runtime is enabled in CMake. Bundles are signed with `--timestamp --options runtime`.
- The pkg is notarized with `xcrun notarytool submit --wait`, stapled, and checked with `spctl`.
- Secrets: `NL_DEV_ID_APP`, `NL_DEV_ID_INSTALLER`, `NL_CERTS_P12` (base64), `NL_CERTS_PASSWORD`,
  `NL_NOTARY_APPLE_ID`, `NL_NOTARY_TEAM_ID`, `NL_NOTARY_PASSWORD` (app-specific password).

## Installers

- Windows: Inno Setup ([packaging/windows/NorthlineChannel.iss](../packaging/windows/NorthlineChannel.iss)),
  component choice per format, uninstaller. VST3 goes to `Common Files\VST3`, CLAP to `Common Files\CLAP`.
- macOS: `pkgbuild` per component plus `productbuild` with a distribution file and choices.
- User presets (`Documents/Northline Audio/Northline Channel/Presets`) are never touched by
  installers or uninstallers.

## Licensing, privacy, legal

- Licensing (roadmap 11.4) is not implemented yet: choose Moonbase (JUCE SDK, offline activation)
  or self-hosted Ed25519-signed licence files. An expired trial must pass audio through
  unprocessed and show a banner; it must never inject noise or corrupt sessions.
- No telemetry. Crash reporting, if added, is opt-in.
- `docs/legal/EULA.txt` and `docs/legal/PRIVACY.md` are drafts and need review by a lawyer
  before the first public release.
- Before launch: trademark search for "Northline" (USPTO, EUIPO), register the domain and replace
  `COMPANY_WEBSITE` in CMakeLists.txt, and budget the JUCE licence tier that matches revenue.
