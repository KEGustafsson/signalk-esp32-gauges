"""Copy one gauge unit's pages to another, over the units' web API.

    python tools/copy-pages/copy_pages.py --from gauge-21-stbd.local \\
        --to gauge-21-port.local \\
        --replace stb_engine=prt_engine --replace "STB Engine=PRT Engine" \\
        --restart

Reads the source's Pages card (how many pages) and each page card
(/api/config/Pages/<n>), applies the --replace text substitutions to their
text fields, and writes them to the target. Each save carries the target's
own `_rev`, or the gauge refuses it as an out-of-date form. A target with
fewer pages has no card for the extra ones until it restarts with the new
count: with --restart the script restarts it, waits for it and goes on;
without, it stops and says so. Page settings apply at the target's next
restart (--restart does that too).

The web UI login is SECRET_ADMIN_USER / SECRET_ADMIN_PASSWORD from
include/secrets.h (digest authentication); nothing secret is printed.
Python 3 standard library only.
"""

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
SECRETS = os.path.join(ROOT, "include", "secrets.h")
# The fields of a page card that are text and may be --replaced.
TEXT_FIELDS = ("title", "sk_path")


def secret(name):
    try:
        text = open(SECRETS, encoding="utf-8").read()
    except OSError:
        return ""
    m = re.search(r'^\s*#define\s+%s\s+"([^"]*)"' % name, text, re.M)
    return m.group(1) if m else ""


class Unit:
    def __init__(self, host):
        self.host = host
        mgr = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        user, password = secret("SECRET_ADMIN_USER"), secret("SECRET_ADMIN_PASSWORD")
        if password:
            mgr.add_password(None, "http://%s/" % host, user, password)
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPDigestAuthHandler(mgr))

    def _open(self, req):
        return self.opener.open(req, timeout=20)

    def get(self, path):
        with self._open("http://%s/api/config%s" % (self.host, path)) as r:
            return json.loads(r.read().decode())["config"]

    def put(self, path, config):
        req = urllib.request.Request(
            "http://%s/api/config%s" % (self.host, path), method="PUT",
            data=json.dumps(config).encode(),
            headers={"Content-Type": "application/json"})
        with self._open(req) as r:
            return r.status

    def restart(self):
        req = urllib.request.Request(
            "http://%s/api/device/restart" % self.host, method="POST", data=b"",
            headers={"Origin": "http://%s" % self.host})
        with self._open(req) as r:
            r.read()

    def wait_up(self, seconds=180):
        time.sleep(5)  # Let it go down first
        end = time.time() + seconds
        while time.time() < end:
            try:
                self.get("/Pages/count")
                return True
            except (urllib.error.URLError, OSError):
                time.sleep(3)
        return False


def read_pages(unit):
    count = int(unit.get("/Pages/count").get("count", 0))
    return count, [unit.get("/Pages/%d" % n) for n in range(1, count + 1)]


def substitute(card, replacements):
    card = dict(card)
    for field in TEXT_FIELDS:
        value = card.get(field)
        if isinstance(value, str):
            for old, new in replacements:
                value = value.replace(old, new)
            card[field] = value
    return card


def restart_and_wait(unit):
    print("restarting %s ..." % unit.host)
    unit.restart()
    if not unit.wait_up():
        sys.exit("%s did not come back within 3 min" % unit.host)
    print("%s is back" % unit.host)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--from", dest="source", required=True,
                    help="unit to copy from (address or .local name)")
    ap.add_argument("--to", dest="target", required=True,
                    help="unit to copy to")
    ap.add_argument("--replace", action="append", default=[],
                    metavar="OLD=NEW",
                    help="text to replace in titles and paths (repeatable)")
    ap.add_argument("--restart", action="store_true",
                    help="restart the target as needed and at the end")
    ap.add_argument("--dry-run", action="store_true",
                    help="show what would be written, write nothing")
    args = ap.parse_args()

    replacements = []
    for r in args.replace:
        if "=" not in r:
            sys.exit("--replace needs OLD=NEW, not %r" % r)
        replacements.append(tuple(r.split("=", 1)))

    source, target = Unit(args.source), Unit(args.target)
    count, cards = read_pages(source)
    cards = [substitute(c, replacements) for c in cards]
    print("%s: %d pages" % (source.host, count))
    for n, card in enumerate(cards, 1):
        print("  page %d: %s (%s) %s" % (n, card.get("title"), card.get("style"),
                                         card.get("sk_path")))
    if args.dry_run:
        return

    if int(target.get("/Pages/count").get("count", 0)) != count:
        print("%s: page count -> %d" % (target.host, count),
              target.put("/Pages/count", {"count": count}))
    for n, card in enumerate(cards, 1):
        path = "/Pages/%d" % n
        try:
            current = target.get(path)
        except urllib.error.HTTPError as e:
            if e.code != 404:
                raise
            if not args.restart:
                sys.exit("%s has no %s yet: restart it (or run again with "
                         "--restart) so the new page count applies, then run "
                         "this again" % (target.host, path))
            restart_and_wait(target)
            current = target.get(path)
        card = dict(card)
        if "_rev" in current:
            card["_rev"] = current["_rev"]  # The target's, or it is refused
        print("%s: %s %s" % (target.host, path, target.put(path, card)))
    if args.restart:
        restart_and_wait(target)
    else:
        print("Restart %s to apply the pages." % target.host)


if __name__ == "__main__":
    main()
