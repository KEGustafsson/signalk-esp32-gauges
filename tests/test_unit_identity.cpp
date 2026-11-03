// Which unit a gauge is (unit_identity.h): the role, hostname and display
// name a fresh unit starts with, a role from an older unit's hostname, and
// what a Unit card save does.

#include "test.h"
#include "unit_identity.h"

using namespace skgauges;

TEST(default_hostnames_and_names) {
  CHECK(unit_hostname("21", Side::kStarboard) == "gauge-21-stbd");
  CHECK(unit_hostname("21", Side::kPort) == "gauge-21-port");
  CHECK(unit_hostname("4c", Side::kGeneral) == "gauge-4c-gen");
  CHECK(unit_name("2.1in", Side::kPort) == "2.1in PORT");
  CHECK(unit_name("4in", Side::kStarboard) == "4in STBD");
  CHECK(unit_name("4in", Side::kGeneral) == "4in GEN");
  CHECK(is_default_hostname("21", "gauge-21-gen"));
  CHECK(is_default_hostname("21", "gauge-21-port"));
  CHECK(!is_default_hostname("21", "gauge-4c-port"));  // Another board's
  CHECK(!is_default_hostname("21", "helm"));
}

TEST(role_from_an_older_units_hostname) {
  CHECK(role_from_hostname("gauge-21-port") == Side::kPort);
  CHECK(role_from_hostname("GAUGE-4C-PORT") == Side::kPort);
  CHECK(role_from_hostname("gauge-21-stbd") == Side::kStarboard);
  CHECK(role_from_hostname("saloon-stbd") == Side::kStarboard);
  CHECK(role_from_hostname("gauge-4c-helm") == Side::kGeneral);
  CHECK(role_from_hostname("portside") == Side::kGeneral);
  CHECK(role_from_hostname("") == Side::kGeneral);
  for (Side r : {Side::kStarboard, Side::kPort, Side::kGeneral}) {
    CHECK(role_from_hostname(unit_hostname("21", r)) == r);
  }
}

TEST(valid_hostnames_and_display_names) {
  CHECK(valid_hostname("gauge-21-port"));
  CHECK(valid_hostname("helm2"));
  CHECK(!valid_hostname(""));
  CHECK(!valid_hostname("-helm"));
  CHECK(!valid_hostname("helm-"));
  CHECK(!valid_hostname("Helm"));  // mDNS names are lower case here
  CHECK(!valid_hostname("helm display"));
  CHECK(!valid_hostname("helm.local"));
  CHECK(!valid_hostname(std::string(33, 'a')));
  CHECK(valid_hostname(std::string(32, 'a')));
  CHECK(valid_display_name("Helm NAV"));
  CHECK(valid_display_name("2.1in PORT"));
  CHECK(!valid_display_name(""));
  CHECK(!valid_display_name(std::string(25, 'x')));
  CHECK(!valid_display_name("bad\nline"));
}

TEST(dropdown_labels) {
  for (Side r : {Side::kStarboard, Side::kPort, Side::kGeneral}) {
    Side parsed = Side::kBoth;
    CHECK(parse_side_label(side_label(r), &parsed) && parsed == r);
  }
  Side role = Side::kPort;
  CHECK(!parse_side_label("port", &role));  // Exactly as the dropdown sends it
  CHECK(!parse_side_label("", &role));
  CHECK(!parse_side_label("Both", &role));
  CHECK(role == Side::kPort);  // Untouched
}

TEST(hostname_follows_the_role_only_while_built_in) {
  // A built-in name follows a role change.
  CHECK(hostname_after_save("21", "gauge-21-stbd", Side::kStarboard,
                            Side::kPort, "") == "gauge-21-port");
  CHECK(hostname_after_save("21", "gauge-21-port", Side::kPort,
                            Side::kGeneral, "gauge-21-port") ==
        "gauge-21-gen");  // The form sends the current name back unchanged
  // A name of the user's own is kept.
  CHECK(hostname_after_save("21", "helm", Side::kGeneral, Side::kPort, "") ==
        "helm");
  // A new name given wins, role change or not.
  CHECK(hostname_after_save("21", "gauge-21-stbd", Side::kStarboard,
                            Side::kGeneral, "nav") == "nav");
  CHECK(hostname_after_save("21", "nav", Side::kGeneral, Side::kGeneral,
                            "nav2") == "nav2");
  // Same role, no new name: unchanged.
  CHECK(hostname_after_save("21", "gauge-21-stbd", Side::kStarboard,
                            Side::kStarboard, "") == "gauge-21-stbd");
}
