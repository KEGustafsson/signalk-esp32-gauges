# Host unit tests

The shared library's logic that needs no board, run on a PC:

| File | Covers |
|---|---|
| `test_resolve.cpp` | Display units, unit conversion, scales (`nice_step`, `nice_range`, `convert_range`), the server's zones and `displayScale`, compass and sea-horizon specifics (`gauge_resolve`, `units`) |
| `test_factory.cpp` | The factory pages by side, the style templates, factory ticks (`gauge_defs`) |
| `test_page_rules.cpp` | What a page card holds, the rules a save follows (style change, unit change, empty path, validation) and the card's schema (`page_rules`) |
| `test_value_gate.cpp` | The replay window after a (re)connect, `null` handling, staleness and `millis()` wraparound (`value_gate`) |
| `test_card_store.cpp` | A card's file replaced all or nothing: with a failure at any one or two steps (a failed rollback too), and a power cut at every step, then the boot's recovery, a swap ends with both cards old or both new (`card_store`) |
| `test_link_policy.cpp` | When a dead link restarts the unit: Signal K unreachable 5 min with WiFi up, only after it worked this boot, across `millis()` wraparound; the C6's start at boot is not a restart, one after a heartbeat is, a link failure always is (`link_policy`) |
| `test_unit_identity.cpp` | Which unit a gauge is from its saved hostname: `-port` suffix (any case) is port, anything else starboard; hostnames and unit names by board and side (`unit_identity`) |
| `test_text_fold.cpp` | Names in the fonts' ASCII: Nordic and other Latin letters as their base letters (Mäkelä Makela, ß ss), typographic quotes and dashes plain, another script, an emoji or malformed UTF-8 as '?', controls dropped (`text_fold.h`) |
| `test_colour_mode.cpp` | Day, dusk and night: day as written, black stays black, dusk dimmer with the blue the most and hues kept, night red only with dark areas darker and the needle as bright as a marking, the stored mode; the mode from Signal K: `environment.sun`'s phases and `environment.mode`'s values, a switch only when the answer changes (a restated phase never undoes a tap), `environment.mode` against the sun by hand until the sun's next step, a stale night not holding back the morning, the first values in either order (`colour_mode.h`) |
| `test_crew_tap.cpp` | Where a tap on the Crew page lands: the volume arc's - and + ends and its label, wide zones across the bottom of the dial that leave the roster's lowest names, the middle and anything outside the round dial alone (`crew_tap.h`) |
| `test_arc_fit.cpp` | How much of a name fits along an arc (the radio halo's talker name): a text that fits is kept whole, the last glyph included; one that does not is cut with "..", never past the arc, never with a space before the dots (`arc_fit.h`) |
| `test_sk_parse.cpp` | Only a real JSON number is a reading; attitude parsing; a path's unit from its metadata (a missing one is no unit, not `"null"`) (`sk_parse`), against the firmware's ArduinoJson |

```bash
cmake -S tests -B build/tests && cmake --build build/tests -j && build/tests/gauge_tests
```

Built with `-Wall -Wextra -Werror` and AddressSanitizer + UndefinedBehaviorSanitizer.
On Windows with MinGW, which has no sanitizers, configure with
`-G "MinGW Makefiles" -DSANITIZE=OFF`.
CI runs them on every push to `main` and every pull request.
