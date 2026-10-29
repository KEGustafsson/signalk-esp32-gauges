"""Keep the OTA password out of the upload log.

The espressif32 builder runs espota with --debug (builder/main.py:
UPLOADERFLAGS=["--debug", "--progress", "-i", "$UPLOAD_PORT"]), and espota
then logs every parsed option, the --auth password from ota_auth.py
included. That flag is set by the platform builder, after pre: scripts run,
so this is a post: script: it drops --debug again. Uploads print the same
progress bar as before.
"""

Import("env")  # noqa: F821 -- injected by SCons

flags = env.get("UPLOADERFLAGS", [])  # noqa: F821
if "--debug" in flags:
    env.Replace(UPLOADERFLAGS=[f for f in flags if f != "--debug"])  # noqa: F821
