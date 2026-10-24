# AMPSURD

Free, open-source guitar plugin: one DI signal → up to **five Neural Amp Modeler captures in
parallel** → blended into one sound. Load up to five NAMs. Blend them. Optional corrective
editing. Play.

![AMPSURD](docs/screenshots/03_edit_slot2.png)

- Loads any standard `.nam` file supported by the official NeuralAmpModelerCore (A2 Full, A2 Lite,
  slimmable A2, A1 WaveNet, LSTM, …), unmodified, from any source.
- Linked mix faders with constant perceived loudness, automatic level match and alignment,
  per-amp 10-band EQ, never clips (−1 dBFS safety limiter), complete-rig presets.

**Status:** `PROJECT_STATUS.md` · **Build:** `docs/BUILD_WINDOWS.md`, `docs/BUILD_MACOS.md` ·
**Design:** `docs/ARCHITECTURE.md`, `docs/UI_SPEC.md`, `docs/REVIEW.md`, `docs/ROADMAP.md` · **Log:** `docs/DEVLOG.md`

**Download (Windows 10 / 11, 64-bit):** [Releases](https://github.com/SymbiOrbis/ampsurd/releases) - installer
`AMPSURD-Setup-<version>.exe` (VST3 plugin + standalone app). [Code signing policy](docs/CODE_SIGNING.md) ·
[Privacy](PRIVACY.md)

**macOS 10.15 Catalina or newer (Apple Silicon and Intel):** from the next beta on, `AMPSURD-<version>-macOS.pkg`
installs the standalone app, the VST3 plugin and the Audio Unit (Logic Pro, GarageBand, MainStage). Builds
made by the project's GitHub workflow are signed and notarized (Developer ID: Radovan Glovna) - they install
with a double-click. (A build without the project's Apple key, e.g. from a fork, is unsigned: then System
Settings → Privacy & Security → "Open Anyway".)

> **Public beta. Code signing (Windows):** a signed release says "Signed by Open Source Developer
> Radovan Glovňa (Certum)" at the top of its release page ([how to check](docs/CODE_SIGNING.md)). A
> release without that line is not signed: Windows 11 Smart App Control blocks it (it only runs with
> Smart App Control turned off), and SmartScreen may show "Windows protected your PC" (More info, Run
> anyway).

Feedback: please share your experience in [GitHub Discussions](https://github.com/SymbiOrbis/ampsurd/discussions/1)
("AMPSURD beta feedback") or report bugs as [Issues](https://github.com/SymbiOrbis/ampsurd/issues).
If you like AMPSURD, a star on GitHub helps a lot: it's how a small
open-source project qualifies for free code signing, so Windows stops
blocking it.

Licence: GNU AGPLv3 (`LICENSE.md`). Third-party components: `THIRD_PARTY.md`. The AMPSURD name,
the footer logos and capture packs are not covered by the code licence.

Author: Radovan Glovňa
