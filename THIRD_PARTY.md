# Third-party components

| Component | Version | Licence | Use |
|---|---|---|---|
| JUCE | 9.0.3 | AGPLv3 **or** JUCE 9 EULA (Starter tier: free ≤ USD 20k annual revenue) | Plugin framework |
| VST3 SDK (bundled in JUCE) | 3.8.0 | MIT | VST3 format |
| NeuralAmpModelerCore | v0.6.0 | MIT, © Steven Atkinson | NAM inference |
| AudioDSPTools | 0827c6c | MIT, © Steven Atkinson; resampler files: iPlug2 zlib-style licence | Resampling, WAV reading (tools) |
| Eigen | bc3b398 | MPL-2.0 | Linear algebra (used by NAM Core) |
| nlohmann/json (vendored in NAM Core) | — | MIT | `.nam` parsing |

Before public distribution: ship these notices in the installer / About box, confirm the
JUCE licence tier (see `docs/REVIEW.md` §3), and verify fonts/artwork licences.

`.nam` captures are **not** part of this repository and are never embedded in presets or
plugin state. Each capture remains under its creator's licence.
