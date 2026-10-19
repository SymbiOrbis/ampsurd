# Installer

`AMPSURD.iss` (Inno Setup 6) packs the VST3 plugin and the standalone app into
`AMPSURD-Setup-<version>.exe`. GitHub Actions builds it on every push (artifact
"AMPSURD-Setup-windows-x64") and attaches it to a GitHub Release when a tag `v<version>` is pushed, or when the workflow is run by
hand on main (Actions -> Build Windows + macOS -> Run workflow -> publish_release): that creates the tag
`v<CMake version>` on the commit and a pre-release with docs/RELEASE_NOTES.md.
The version comes from `project(AMPSURD VERSION ...)` in the top-level CMakeLists.txt.

Code signing: NOT active yet (releases so far are unsigned). Once SignPath Foundation signing is
granted, the installer and the binaries inside it will be signed in CI before upload (docs/CODE_SIGNING.md).

macOS: `macos/build_pkg.sh` builds `AMPSURD-<version>-macOS.pkg` (standalone app -> /Applications,
VST3 -> /Library/Audio/Plug-Ins/VST3, AU -> /Library/Audio/Plug-Ins/Components; parts selectable) and
zips of the three bundles. The CI macOS job runs it; the release job attaches the Windows and macOS
files to the same GitHub Release. Not signed / notarized by Apple (ad-hoc signature only), see
docs/BUILD_MACOS.md.
