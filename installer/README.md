# Installer

`AMPSURD.iss` (Inno Setup 6) packs the VST3 plugin and the standalone app into
`AMPSURD-Setup-<version>.exe`. GitHub Actions builds it on every push (artifact
"AMPSURD-Setup-windows-x64") and attaches it to a GitHub Release when a tag `v<version>` is pushed, or when the workflow is run by
hand on main (Actions -> Build Windows + macOS -> Run workflow -> publish_release): that creates the tag
`v<CMake version>` on the commit and a pre-release with docs/RELEASE_NOTES.md.
The version comes from `project(AMPSURD VERSION ...)` in the top-level CMakeLists.txt.

Code signing: the CI publishes the release unsigned; the maintainer then signs it on his PC with
`sign_release.ps1` (Certum certificate via SimplySign Desktop): signed app, plugin DLL and installer
(Inno Setup's SignTool setting also signs the uninstaller), verified, and uploaded over the unsigned
files. Steps: docs/RELEASE_SIGNING.md.

macOS: `macos/build_pkg.sh` builds `AMPSURD-<version>-macOS.pkg` (standalone app -> /Applications,
VST3 -> /Library/Audio/Plug-Ins/VST3, AU -> /Library/Audio/Plug-Ins/Components; parts selectable) and
zips of the three bundles. The CI macOS job runs it; the release job attaches the Windows and macOS
files to the same GitHub Release. Not signed / notarized by Apple (ad-hoc signature only), see
docs/BUILD_MACOS.md.
