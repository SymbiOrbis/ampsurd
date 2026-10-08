# Third-party components

| Component | Version | Licence | Use |
|---|---|---|---|
| JUCE | 9.0.3 | Used under **AGPLv3** (AMPSURD is AGPLv3, see LICENSE.md) | Plugin framework |
| VST3 SDK (bundled in JUCE) | 3.8.0 | MIT | VST3 format |
| NeuralAmpModelerCore | v0.6.0 | MIT, © Steven Atkinson | NAM inference |
| AudioDSPTools | 0827c6c | MIT, © Steven Atkinson; resampler files: iPlug2 zlib-style licence | Resampling, WAV reading (tools) |
| Eigen | bc3b398 | MPL-2.0 | Linear algebra (used by NAM Core) |
| nlohmann/json (vendored in NAM Core) | — | MIT | `.nam` parsing |
| Inter typeface | 4.x (Google Fonts build, subset) | SIL OFL 1.1 (`plugin/assets/fonts/Inter-OFL.txt`) | UI font, embedded |

AMPSURD's own source code is licensed under the GNU AGPLv3 (`LICENSE.md`). The AMPSURD name,
the band/label logos in the footer and any capture packs are **not** covered by that licence.

Before public distribution: ship these notices in the installer / About box and verify
fonts/artwork licences.

`.nam` captures are **not** part of this repository and are never embedded in presets or
plugin state. Each capture remains under its creator's licence.

## Steinberg ASIO SDK headers (Windows standalone app)

Bundled with JUCE (`juce_audio_devices/native/asio`). (c) 2025 Steinberg Media Technologies GmbH.
Dual-licensed under the Steinberg ASIO License or the GNU GPL v3; AMPSURD uses them under the
GPL v3 (compatible with AMPSURD's AGPL v3). ASIO is a trademark of Steinberg Media Technologies GmbH.
