# Copy pages between units

Copies one gauge unit's pages (the Pages card and every page card) to
another, over the units' web API: for setting up a new unit like an
existing one, or the port unit like the starboard one with the engine
swapped.

```bash
python tools/copy-pages/copy_pages.py --from gauge-21-stbd.local --to gauge-21-port.local \
    --replace stb_engine=prt_engine --replace "STB Engine=PRT Engine" --restart
```

- `--replace OLD=NEW` (repeatable) changes text in the copied titles and
  Signal K paths; nothing else is changed.
- `--dry-run` shows the pages it would write and writes nothing.
- `--restart` restarts the target when it needs one: once when it gets
  more pages than it had (a page's card exists only after a restart with
  the new count), and at the end, which applies the pages. Without it the
  script says when to restart.

It logs in with `SECRET_ADMIN_USER` / `SECRET_ADMIN_PASSWORD` from
`include/secrets.h` and never prints them. Each card is written with the
target's own `_rev` (the gauge's check that a save is not made from an
out-of-date form). Python 3, standard library only. The units can be any
board: a page card is the same on both.
