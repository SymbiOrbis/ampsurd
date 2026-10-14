# Installer

`AMPSURD.iss` (Inno Setup 6) packs the VST3 plugin and the standalone app into
`AMPSURD-Setup-<version>.exe`. GitHub Actions builds it on every push (artifact
"AMPSURD-Setup-windows-x64") and attaches it to a GitHub Release when a tag `v<version>` is pushed, or when the workflow is run by
hand on main (Actions -> Build Windows -> Run workflow -> publish_release): that creates the tag
`v<CMake version>` on the commit and a pre-release with docs/RELEASE_NOTES.md.
The version comes from `project(AMPSURD VERSION ...)` in the top-level CMakeLists.txt.

Code signing (planned): the installer and the binaries inside it are signed in CI before upload
(see docs/CODE_SIGNING.md).
