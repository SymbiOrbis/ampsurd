"""
AMPSURD - connect this machine to your Google Drive (rclone remote "gdrive"), once per machine.

Run:   python setup_gdrive.py
Paste the code that `rclone authorize "drive" "eyJzY29wZSI6ImRyaXZlLmZpbGUifQ"` printed on your PC (the line between
"Paste the following into your remote machine --->" and "<---End paste") and press Enter.
Accepts both forms rclone prints: a packed code (eyJ...) or the token itself ({"access_token": ...}).
The code is only written into rclone's config on this machine; it is not printed or sent anywhere else.
"""

import base64
import json
import subprocess
import sys


def main() -> int:
    code = input("Paste the code from your PC and press Enter: ").strip()
    if not code:
        print("Nothing pasted.")
        return 1
    scope = "drive.file"
    if code.startswith("{"):
        token = code
    else:
        try:
            packed = json.loads(base64.b64decode(code + "=" * (-len(code) % 4)))
        except Exception:  # noqa: BLE001
            print("That does not look like the code from rclone authorize - copy the whole line and try again.")
            return 1
        token = packed.get("token", "")
        scope = packed.get("scope", scope) or scope
    if not token:
        print("No token in that code - copy the whole line and try again.")
        return 1
    subprocess.run(["rclone", "config", "delete", "gdrive"], capture_output=True)
    # config_refresh_token=false: keep the pasted token (otherwise rclone starts its own browser login and waits)
    r = subprocess.run(["rclone", "config", "create", "gdrive", "drive", f"scope={scope}", f"token={token}",
                        "config_refresh_token=false"],
                       capture_output=True, text=True, stdin=subprocess.DEVNULL, timeout=120)
    if r.returncode != 0:
        print("rclone could not save it:", r.stderr.strip()[-300:])
        return 1
    t = subprocess.run(["rclone", "mkdir", "gdrive:AMPSURD NAMs"], capture_output=True, text=True,
                       stdin=subprocess.DEVNULL, timeout=120)
    if t.returncode != 0:
        print("Saved, but Google Drive did not answer:", t.stderr.strip()[-300:])
        return 1
    print('Google Drive connected: the folder "AMPSURD NAMs" is ready.')
    return 0


if __name__ == "__main__":
    sys.exit(main())
