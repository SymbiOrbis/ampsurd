# Privacy

AMPSURD (plugin and standalone app) does not collect, store or send any personal data, and has
no telemetry, analytics, accounts or advertising.

- **Network:** the only connection AMPSURD ever makes is when you click **Check for updates...** in the
  settings menu. It then asks GitHub (`api.github.com`) for the list of AMPSURD releases. Like any
  web request, this reveals your IP address to GitHub ([GitHub's privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement)).
  Nothing is sent automatically.
- **Your files:** captures (.nam), IRs, presets and recordings stay on your computer. Presets and
  recordings are saved in Documents\AMPSURD; settings (e.g. input calibration, "Show dragon") in
  your user's application data folder.
- **Camera (standalone app, VIDEO takes):** the camera is opened only while VIDEO is switched on in the
  player (its window shows the picture). It films only while you record a VIDEO take. The camera files and
  the videos you save stay on your computer (Documents/AMPSURD/Recordings); nothing is uploaded. macOS asks
  for permission the first time.
- **Installer / uninstaller:** installs the VST3 plugin and the app, and removes them again. Your
  presets and recordings are never deleted.

Contact: https://github.com/SymbiOrbis/ampsurd/issues
