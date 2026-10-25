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

- Northline Channel is open source under the GNU AGPLv3 (`LICENSE`), as JUCE's free licence requires.
  There is no licence key, activation or trial. Third-party licences are listed in
  `THIRD_PARTY_NOTICES.md`, and the installers ship both files.
- No network access, telemetry or crash reporting (`docs/legal/PRIVACY.md`).
- Signing and notarization are optional. The release workflow skips them when the secrets are not
  set; unsigned Windows installers show a SmartScreen warning, and unsigned macOS packages must be
  opened with right-click > Open (or allowed in System Settings > Privacy & Security).
- A closed-source or commercial release would need a commercial JUCE licence, a trademark search for
  "Northline", and an EULA.
