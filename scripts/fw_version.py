"""Stamps the build's version into the firmware as the FW_VERSION macro.

The release workflow passes the tag it is about to publish in $FW_VERSION.
Local builds fall back to `git describe`, so a hand-flashed board still says
which commit it came from.
"""

import os
import subprocess

Import("env")


def _version():
    version = os.environ.get("FW_VERSION")
    if version:
        return version
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env.subst("$PROJECT_DIR"),
            stderr=subprocess.DEVNULL,
            universal_newlines=True,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


version = _version()
print("FW_VERSION: %s" % version)
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
