// SPDX-License-Identifier: EUPL-1.2
// Names the gauges' ASCII-only fonts can draw (text_fold.h).
#include "text_fold.h"
#include "test.h"

using skgauges::ascii_fold;

TEST(fold_ascii_passes_unchanged_and_controls_go) {
  CHECK(ascii_fold("Starboard 2") == "Starboard 2");
  CHECK(ascii_fold("") == "");
  CHECK(ascii_fold(std::string("a\tb\x7f" "c", 5)) == "abc");
}

TEST(fold_nordic_and_other_latin_letters_take_their_base_letters) {
  CHECK(ascii_fold("M\xc3\xa4kel\xc3\xa4") == "Makela");             // Mäkelä
  CHECK(ascii_fold("\xc3\x85sa \xc3\x96stman") == "Asa Ostman");     // Åsa Östman
  CHECK(ascii_fold("S\xc3\xb8ren") == "Soren");                      // Søren
  CHECK(ascii_fold("Stra\xc3\x9f" "e") == "Strasse");                // Straße
  CHECK(ascii_fold("\xc3\x86gir") == "AEgir");                       // Ægir
  CHECK(ascii_fold("\xc5\x81\xc3\xb3" "d\xc5\xba") == "Lodz");       // Łódź
  CHECK(ascii_fold("\xc5\x92uvre") == "OEuvre");                     // Œuvre
  CHECK(ascii_fold("Jos\xc3\xa9 M\xc3\xbcller") == "Jose Muller");
}

TEST(fold_typographic_marks_become_plain_ones) {
  CHECK(ascii_fold("Skipper\xe2\x80\x99s") == "Skipper's");          // ’
  CHECK(ascii_fold("\xe2\x80\x9cKia\xe2\x80\x9d") == "\"Kia\"");     // “ ”
  CHECK(ascii_fold("A\xe2\x80\x93" "B\xe2\x80\x94" "C") == "A-B-C");  // – —
  CHECK(ascii_fold("wait\xe2\x80\xa6") == "wait...");
  CHECK(ascii_fold("a\xc2\xa0" "b") == "a b");                       // No-break space
}

TEST(fold_other_scripts_emoji_and_malformed_utf8_become_question_marks) {
  CHECK(ascii_fold("\xce\xa3") == "?");               // Σ
  CHECK(ascii_fold("\xe8\x88\xb9") == "?");           // 船
  CHECK(ascii_fold("\xf0\x9f\x9a\xa2") == "?");       // 🚢, one mark for one character
  CHECK(ascii_fold("a\xffz") == "a?z");               // Not a lead byte
  CHECK(ascii_fold("a\xc3") == "a?");                 // Cut short
  CHECK(ascii_fold("\xc0\xaf") == "??");              // Overlong '/'
  CHECK(ascii_fold("\xc3(x") == "?(x");               // A bad continuation resynchronises
}
