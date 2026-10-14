# AMPSURD

Free, open-source guitar plugin: one DI signal → up to **five Neural Amp Modeler captures in
parallel** → blended into one sound. Load up to five NAMs. Blend them. Optional corrective
editing. Play.

![AMPSURD](docs/screenshots/03_edit_slot2.png)

- Loads any standard `.nam` file supported by the official NeuralAmpModelerCore (A2 Full, A2 Lite,
  slimmable A2, A1 WaveNet, LSTM, …), unmodified, from any source.
- Linked mix faders with constant perceived loudness, automatic level match and alignment,
  per-amp 10-band EQ, never clips (−1 dBFS safety limiter), complete-rig presets.

**Status:** `PROJECT_STATUS.md` · **Build on Windows:** `docs/BUILD_WINDOWS.md` ·
**Design:** `docs/ARCHITECTURE.md`, `docs/UI_SPEC.md`, `docs/REVIEW.md`, `docs/ROADMAP.md` · **Log:** `docs/DEVLOG.md`

**Download (Windows 10 / 11, 64-bit):** [Releases](https://github.com/SymbiOrbis/ampsurd/releases) - installer
`AMPSURD-Setup-<version>.exe` (VST3 plugin + standalone app). [Code signing policy](docs/CODE_SIGNING.md) ·
[Privacy](PRIVACY.md)

> **Public beta, not code-signed yet.** Signing through the SignPath Foundation is in progress. Until
> then, Windows 11 Smart App Control blocks the installer (it only runs with Smart App Control turned
> off), and SmartScreen may show "Windows protected your PC" (More info, Run anyway).

Feedback: please share your experience in [GitHub Discussions](https://github.com/SymbiOrbis/ampsurd/discussions/1)
("AMPSURD beta feedback") or report bugs as [Issues](https://github.com/SymbiOrbis/ampsurd/issues).
If you like AMPSURD, a star on GitHub helps a lot: it's how a small
open-source project qualifies for free code signing, so Windows stops
blocking it.

Licence: GNU AGPLv3 (`LICENSE.md`). Third-party components: `THIRD_PARTY.md`. The AMPSURD name,
the footer logos and capture packs are not covered by the code licence.
