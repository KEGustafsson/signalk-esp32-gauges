"""Take the OTA password and gauge address from include/secrets.h, not platformio.ini.

Adapted from KEGustafsson/drive-remote-controller (esp32/scripts/ota_auth.py).

`enable_ota()` compiles `SECRET_OTA_PASSWORD` out of include/secrets.h into the
firmware, so the gauge's OTA password comes from that file. The uploader needs
the same string; rather than writing it a second time into a committed
platformio.ini as `upload_flags = --auth=...`, this script reads the one copy in
secrets.h (gitignored) and hands it to espota. The gauge addresses come from the
same file.

secrets.h lives in the repository's top-level include/ directory and is shared
by both firmware projects. What this script expects in it -- see
include/secrets.example.h for the template:

    #define SECRET_OTA_PASSWORD     "..."   shared by all gauges
    #define SECRET_OTA_HOST_P4_STBD "..."   the address, or hostname, of each
    #define SECRET_OTA_HOST_P4_PORT "..."
    #define SECRET_OTA_HOST_S3_STBD "..."
    #define SECRET_OTA_HOST_S3_PORT "..."

The suffix is the environment name in upper case: env:p4_stbd ->
SECRET_OTA_HOST_P4_STBD.

Wiring, from platformio/builder/main.py:

    1. pre: extra scripts run          <- this script sets UPLOAD_PORT and UPLOAD_FLAGS
    2. the platform builder runs       <- espressif32 sets UPLOADERFLAGS for espota,
                                          and reads $UPLOAD_PORT
    3. if "UPLOAD_FLAGS" in env: env.Prepend(UPLOADERFLAGS=["$UPLOAD_FLAGS"])

The password is injected only when an upload was actually asked for, so a
checkout without secrets.h still builds. The address is also set on a plain
build when available, which silences the espressif32 builder's "Please specify
IP address or host name of ESP device" message on every espota build.

Overrides still work: `pio run -e p4_stbd -t upload --upload-port 1.2.3.4` sets
UPLOAD_PORT before this runs, and it is left alone.

It does nothing unless the upload goes over the air: the *_usb environments
flash over serial with esptool, which would reject an --auth= flag.
"""

import os
import re
import sys

Import("env")  # noqa: F821 -- injected by SCons

UPLOAD_TARGETS = {"upload", "uploadfs", "uploadfsota"}

# Characters that survive SCons substitution and an unquoted word in both the
# POSIX shell and Windows cmd.exe (where % and ^ are special).
SAFE_PASSWORD = re.compile(r"[A-Za-z0-9._~+=@:,/-]+")

# PlatformIO switches an unset protocol to espota when the port looks like an IP
# or an mDNS name (platform-espressif32 builder/main.py). Mirrored here so this
# script agrees with the uploader that eventually runs.
LOOKS_OTA = re.compile(r'"?((([0-9]{1,3}\.){3}[0-9]{1,3})|[^\\/]+\.local)"?$')


# The shape secrets.h uses: #define NAME "value" -- anything after the closing
# quote (such as a trailing // comment) is ignored.
def _define(name):
    return re.compile(r'^\s*#define\s+%s\s+"([^"]*)"' % re.escape(name), re.M)


def _fail(message):
    """Stop the run with an explanation rather than a doomed upload."""
    print("ota_auth: %s" % message, file=sys.stderr)
    env.Exit(1)  # noqa: F821


def main():
    targets = set(COMMAND_LINE_TARGETS)  # noqa: F821 -- injected by SCons
    uploading = bool(targets & UPLOAD_TARGETS)

    protocol = env.subst("$UPLOAD_PROTOCOL")  # noqa: F821
    given_port = env.subst("$UPLOAD_PORT")  # noqa: F821
    if protocol != "espota" and not (not protocol and LOOKS_OTA.match(given_port)):
        return

    project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
    secrets = os.path.normpath(
        os.path.join(project_dir, "..", "include", "secrets.h"))

    if not os.path.isfile(secrets):
        if uploading:
            _fail(
                "%s does not exist, so there is no OTA password or gauge address to\n"
                "          upload with. Copy include/secrets.example.h to include/secrets.h\n"
                "          and fill it in." % secrets
            )
        return

    with open(secrets, encoding="utf-8", errors="replace") as handle:
        text = handle.read()

    # --- the password, shared by all gauges ----------------------------------
    if uploading:
        match = _define("SECRET_OTA_PASSWORD").search(text)
        if not match or not match.group(1):
            _fail(
                "%s defines no non-empty SECRET_OTA_PASSWORD.\n"
                "          The gauge will reject an unauthenticated upload, so this stops\n"
                "          here rather than failing halfway through espota." % secrets
            )
            return
        password = match.group(1)
        # The flag goes through SCons variable expansion and then a shell
        # command line (sh, or cmd.exe on Windows), unquoted: $ ` " ' ; & | < >
        # ( ) % ^ and whitespace would be interpreted on the way and espota
        # would send a different secret (or the shell would run part of it).
        # Only characters that pass through untouched are accepted; refuse
        # anything else loudly.
        if not SAFE_PASSWORD.fullmatch(password):
            _fail(
                "SECRET_OTA_PASSWORD in %s may only contain letters, digits\n"
                "          and . _ ~ + = @ : , / - (it is passed on a shell\n"
                "          command line). Choose another password and flash once\n"
                "          over USB so the gauge knows it." % secrets
            )
            return
        # Never printed: this output goes to build logs. ota_quiet.py (post:)
        # also drops the builder's espota --debug, which would log it.
        env.Append(UPLOAD_FLAGS=["--auth=%s" % password])  # noqa: F821

    # --- the address of the gauge this env flashes ---------------------------
    if given_port:
        if uploading:
            print("ota_auth: OTA password from include/secrets.h; "
                  "upload port left as given")
        return

    pioenv = env.subst("$PIOENV")  # noqa: F821 -- e.g. "p4_stbd"
    key = "SECRET_OTA_HOST_%s" % pioenv.upper()
    match = _define(key).search(text)
    if not match or not match.group(1):
        if uploading:
            _fail(
                "%s defines no non-empty %s, and no --upload-port was given.\n"
                "          espota needs an address for env:%s. Add %s to secrets.h, or\n"
                "          pass --upload-port explicitly." % (secrets, key, pioenv, key)
            )
        return

    env.Replace(UPLOAD_PORT=match.group(1))  # noqa: F821
    if uploading:
        print("ota_auth: OTA password and %s taken from include/secrets.h" % key)


main()
