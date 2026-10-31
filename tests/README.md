# Host unit tests

The shared library's logic that needs no board, run on a PC:

| File | Covers |
|---|---|
| `test_resolve.cpp` | Display units, unit conversion, scales (`nice_step`, `nice_range`, `convert_range`), the server's zones and `displayScale`, compass and sea-horizon specifics (`gauge_resolve`, `units`) |
| `test_factory.cpp` | The factory pages by side, the style templates, factory ticks (`gauge_defs`) |
| `test_page_rules.cpp` | What a page card holds, the rules a save follows (style change, unit change, empty path, validation) and the card's schema (`page_rules`) |
| `test_value_gate.cpp` | The replay window after a (re)connect, `null` handling, staleness and `millis()` wraparound (`value_gate`) |
| `test_card_store.cpp` | A card's file replaced all or nothing: with a failure at every step, and a power cut at every step followed by the boot's recovery, a swap ends with both cards old or both new (`card_store`) |
| `test_sk_parse.cpp` | Only a real JSON number is a reading; attitude parsing (`sk_parse`), against the firmware's ArduinoJson |

```bash
cmake -S tests -B build/tests && cmake --build build/tests -j && build/tests/gauge_tests
```

Built with `-Wall -Wextra -Werror` and AddressSanitizer + UndefinedBehaviorSanitizer.
CI runs them on every push to `main` and every pull request.
