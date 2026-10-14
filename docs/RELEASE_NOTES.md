This is a public beta. Please test it and report problems via GitHub Issues.

Windows warning: this beta is NOT code-signed yet (signing through the
SignPath Foundation is in progress). Windows 11 Smart App Control blocks
unsigned software and offers no per-app exception, so the installer only
runs with Smart App Control turned off. On some Windows versions it cannot
be turned back on without reinstalling Windows. If that matters to you,
wait for the signed release.
SmartScreen may also show "Windows protected your PC": More info, Run anyway.

Feedback: please share your experience in [GitHub Discussions](https://github.com/SymbiOrbis/ampsurd/discussions/1)
("AMPSURD beta feedback") or report bugs as [Issues](https://github.com/SymbiOrbis/ampsurd/issues).
If you like AMPSURD, a star on GitHub helps a lot: it's how a small
open-source project qualifies for free code signing, so Windows stops
blocking it.

## AMPSURD 0.9.0 (beta)

First public beta for Windows 10 / 11 (64-bit): **AMPSURD-Setup-0.9.0.exe** installs the VST3
plugin and the standalone app (with player / recorder). Your presets and recordings
(Documents\AMPSURD) stay untouched when you update or uninstall.

Highlights: up to five NAM captures blended (any NAM architecture), Create Frankenstein (TONE / NOTES),
per-amp EQ / PAN / cabinet IR with IR EQ, Global EQ, gate + tuner, delays / reverb / flanger /
PITCH (re-tuning), input calibration, ECO (A2 Lite), safety limiter (never above -1 dBFS),
standalone player / recorder with punch-in corrections and crossfaded edits, presets.

New since the first test builds: per-amp INPUT (loudness kept automatically) and OUTPUT in EDIT,
MUTE moves the fader to 0 % and back, a gentler gate, A2 captures load as Lite (ECO) by default like
in TONE3000 (settings menu), BUY AMPSURD AMP PACKS link, MIDI footswitches (program change, MIDI
learn), preset packs (.ampsurdpack with captures and IRs), undo / redo and A/B compare.

Portable zips (no installer) are attached as well.
