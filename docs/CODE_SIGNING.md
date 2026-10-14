# Code signing policy

> **Current status: releases are NOT code-signed yet** (including the 0.9.0 public beta). Signing through
> the SignPath Foundation is being applied for. This policy describes how releases will be signed once
> it is granted.

AMPSURD's Windows releases (installer `AMPSURD-Setup-<version>.exe`, the VST3 plugin and the
standalone app inside it) are built from this repository by GitHub Actions only
(`.github/workflows/build-windows.yml`). Once signing is active, they are signed in that same
automated build.

Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by
[SignPath Foundation](https://signpath.org/) *(applied for; until it is granted, releases are unsigned)*.

## Team roles

| Role | Members |
|---|---|
| Committers and reviewers | [Radovan Glovňa](https://github.com/SymbiOrbis) (owner) |
| Approvers (every signed release) | [Radovan Glovňa](https://github.com/SymbiOrbis) |

Changes are prepared with the help of an AI coding assistant; every change is reviewed and
pushed by the owner. Contributions from others (pull requests) are reviewed by the owner before
they are merged. All team members use multi-factor authentication for GitHub and SignPath.

## What will be signed

Only binaries built from this repository's source: `AMPSURD.exe`, `AMPSURD.vst3` and the installer.
Third-party components are compiled from source as part of the build (see `THIRD_PARTY.md`).

## Verifying a download

Signed releases (once signing is active): right-click the downloaded file -> Properties -> Digital
Signatures: the signer must be **SignPath Foundation**. Releases up to and including 0.9.0 have no
signature. Windows SmartScreen may still warn about a new release for a while,
until the file has built up a reputation.

## Privacy

See [PRIVACY.md](../PRIVACY.md): AMPSURD sends no data anywhere unless you click
*Check for updates*.
