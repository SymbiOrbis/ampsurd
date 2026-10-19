# Code signing policy

> **Current status:** Windows releases are signed by **Open Source Developer Radovan Glovňa (Certum)**
> (Certum "Open Source Code Signing in the Cloud" certificate, RSA 3072, valid until 2027-10-19).
> A signed release says "Signed by Open Source Developer Radovan Glovňa (Certum)" at the top of its
> release page (first: 0.9.1, signed 2026-10-19). A release without that line (0.9.0, and every
> release before its signing step) is
> **not** signed. macOS files are not signed by Apple (see below).

AMPSURD's Windows releases (installer `AMPSURD-Setup-<version>.exe`, the VST3 plugin and the
standalone app inside it, the portable zips) are built from this repository by GitHub Actions only
(`.github/workflows/build-windows.yml`); the workflow publishes them unsigned. Signing then happens
on the maintainer's own PC, because the certificate's key lives in Certum's cloud and is unlocked by an
interactive SimplySign login: `installer/sign_release.ps1` downloads exactly the files the CI built for
that version, signs `AMPSURD.exe` and the plugin DLL, rebuilds the installer from the same tag (Inno
Setup signs Setup, its temporary copy and the uninstaller), verifies every signature and timestamp
(Certum TSA) and replaces the release's Windows files and its unsigned warning.
Steps: [docs/RELEASE_SIGNING.md](RELEASE_SIGNING.md).

**macOS** builds (`AMPSURD-<version>-macOS.pkg`, made by the same workflow) are not signed or
notarized by Apple: the app and plugins carry only an ad-hoc signature (needed to run on Apple
Silicon, it identifies no one). Apple signing needs an Apple Developer Program membership (99 USD per
year); the Certum certificate cannot be used for it.

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
