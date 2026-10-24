# Code signing policy

> **Current status:** Windows releases are signed by **Open Source Developer Radovan Glovňa (Certum)**
> (Certum "Open Source Code Signing in the Cloud" certificate, RSA 3072, valid until 2027-10-19).
> A signed release says "Signed by Open Source Developer Radovan Glovňa (Certum)" at the top of its
> release page (first: 0.9.1, signed 2026-10-19). A release without that line (0.9.0, and every
> release before its signing step) is
> **not** signed. **macOS** builds are signed and notarized (Developer ID: Radovan Glovna) since 2026-10-24
> (see below).

AMPSURD's Windows releases (installer `AMPSURD-Setup-<version>.exe`, the VST3 plugin and the
standalone app inside it, the portable zips) are built from this repository by GitHub Actions only
(`.github/workflows/build-windows.yml`); the workflow publishes them unsigned. Signing then happens
on the maintainer's own PC, because the certificate's key lives in Certum's cloud and is unlocked by an
interactive SimplySign login: `installer/sign_release.ps1` downloads exactly the files the CI built for
that version, signs `AMPSURD.exe` and the plugin DLL, rebuilds the installer from the same tag (Inno
Setup signs Setup, its temporary copy and the uninstaller), verifies every signature and timestamp
(Certum TSA) and replaces the release's Windows files and its unsigned warning.
Steps: [docs/RELEASE_SIGNING.md](RELEASE_SIGNING.md).

**macOS** builds (`AMPSURD-<version>-macOS.pkg` and the zips, made by the same workflow) are signed and
notarized in CI since 2026-10-24 (Apple Developer Program, Individual, Team ID 93DWMMFZ87, valid until
2027-10-24): the app, VST3 and Audio Unit with "Developer ID Application: Radovan Glovna" (hardened
runtime, secure timestamp; the app's entitlements: audio input and camera - `installer/macos/
entitlements-app.plist`), the .pkg with "Developer ID Installer: Radovan Glovna"; notarized with
`xcrun notarytool` (App Store Connect API key), then the .pkg and the bundles are stapled and checked
(`codesign --verify --deep --strict`, `spctl`, `stapler validate`) - any problem fails the build. The
identities live in a temporary keychain on the runner that is deleted at the end of the job; the keys are
GitHub repository secrets (MACOS_DEVID_APP_P12, MACOS_DEVID_INSTALLER_P12, MACOS_P12_PASSWORD,
APPLE_NOTARY_KEY_P8, APPLE_NOTARY_KEY_ID, APPLE_NOTARY_ISSUER_ID, APPLE_TEAM_ID). Without them (forks, pull
requests) the workflow builds the old unsigned .pkg (ad-hoc signatures only). The build log says which one
was made ("macOS build: notarized" / "unsigned").

**Later option:** free signing in CI through the [SignPath Foundation](https://signpath.org/)
([SignPath.io](https://about.signpath.io/)) - applied for, not granted. It would sign inside the GitHub
build (no manual step) with SignPath Foundation as the signer. Until then, Certum is used.

## Team roles

| Role | Members |
|---|---|
| Committers and reviewers | [Radovan Glovňa](https://github.com/SymbiOrbis) (owner) |
| Approvers (every signed release) | [Radovan Glovňa](https://github.com/SymbiOrbis) |

Changes are prepared with the help of an AI coding assistant; every change is reviewed and
pushed by the owner. Contributions from others (pull requests) are reviewed by the owner before
they are merged. All team members use multi-factor authentication for GitHub and for the signing certificate (SimplySign).

## What is signed

Only binaries built from this repository's source: `AMPSURD.exe`, `AMPSURD.vst3` and the installer.
Third-party components are compiled from source as part of the build (see `THIRD_PARTY.md`).

## Verifying a download

Right-click the downloaded installer (or `AMPSURD.exe`) -> Properties -> Digital Signatures: the signer
must be **Open Source Developer Radovan Glovňa**, issued by **Certum**, with a timestamp. No
"Digital Signatures" tab means the file is not signed. Windows SmartScreen may still warn for a while
after a new certificate or release, until it has built up a reputation.

## Privacy

See [PRIVACY.md](../PRIVACY.md): AMPSURD sends no data anywhere unless you click
*Check for updates*.
