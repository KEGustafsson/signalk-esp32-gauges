// Which unit a gauge is, from its saved hostname (unit_identity.h).

#include "test.h"
#include "unit_identity.h"

using namespace skgauges;

TEST(hostnames_by_board_and_side) {
  CHECK(unit_hostname("21", Side::kStarboard) == "gauge-21-stbd");
  CHECK(unit_hostname("21", Side::kPort) == "gauge-21-port");
  CHECK(unit_hostname("4c", Side::kPort) == "gauge-4c-port");
}

TEST(side_follows_the_hostname) {
  CHECK(side_from_hostname("gauge-21-port") == Side::kPort);
  CHECK(side_from_hostname("gauge-4c-port") == Side::kPort);
  CHECK(side_from_hostname("gauge-21-stbd") == Side::kStarboard);
  CHECK(side_from_hostname("GAUGE-21-PORT") == Side::kPort);
  // A name of the user's own: port only if it ends in "-port".
  CHECK(side_from_hostname("saloon-port") == Side::kPort);
  CHECK(side_from_hostname("helm") == Side::kStarboard);
  CHECK(side_from_hostname("portside") == Side::kStarboard);
  CHECK(side_from_hostname("port") == Side::kStarboard);
  CHECK(side_from_hostname("-port") == Side::kPort);
  CHECK(side_from_hostname("") == Side::kStarboard);
}

TEST(dropdown_labels) {
  Side side = Side::kBoth;
  CHECK(parse_side_label(side_label(Side::kPort), &side) &&
        side == Side::kPort);
  CHECK(parse_side_label(side_label(Side::kStarboard), &side) &&
        side == Side::kStarboard);
  side = Side::kPort;
  CHECK(!parse_side_label("port", &side));  // Exactly as the dropdown sends it
  CHECK(!parse_side_label("", &side));
  CHECK(!parse_side_label("Both", &side));
  CHECK(side == Side::kPort);               // Untouched
}

TEST(round_trip_and_unit_names) {
  for (Side side : {Side::kStarboard, Side::kPort}) {
    CHECK(side_from_hostname(unit_hostname("21", side)) == side);
    CHECK(side_from_hostname(unit_hostname("4c", side)) == side);
  }
  CHECK(unit_name("2.1in", Side::kPort) == "2.1in PORT");
  CHECK(unit_name("4in", Side::kStarboard) == "4in STBD");
}
