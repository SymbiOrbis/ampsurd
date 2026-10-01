# MONSTROSITY

Free multi-capture guitar amp mixer: one guitar DI feeds up to five Neural Amp Modeler
captures in parallel, mixed into one tone.

**Status:** Milestone 1 — one capture, VST3, Windows-first. See `docs/DEVLOG.md`.

- Loads any standard `.nam` file supported by the official NeuralAmpModelerCore (A2 Full,
  A2 Lite, slimmable A2, A1 WaveNet, LSTM, ConvNet…), unmodified, from any source.
- Build on Windows: `docs/BUILD_WINDOWS.md`
- Design: `docs/ARCHITECTURE.md` · Spec review & decisions: `docs/REVIEW.md`
- Third-party licences: `THIRD_PARTY.md`

```
core/      engine (no JUCE)      plugin/   JUCE VST3 shell
tools/     render / bench / compat tests    tests/   VST3 end-to-end host test
cmake/     pinned dependencies   docs/     review, architecture, build guide, dev log
```
