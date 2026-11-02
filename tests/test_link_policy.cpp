// When a link that stopped working restarts the gauge (link_policy.h).

#include "link_policy.h"
#include "test.h"

using namespace skgauges;

TEST(sk_stall_needs_a_connection_first) {
  // A server that is down at boot must not restart the gauge.
  SkStallWatch w;
  for (uint32_t t = 0; t < 3 * kSkStallMs; t += 10000) {
    CHECK(!sk_stall_check(&w, true, false, t));
  }
}

TEST(sk_stall_restarts_after_the_wait_with_wifi_up) {
  SkStallWatch w;
  CHECK(!sk_stall_check(&w, true, true, 1000));     // Worked once
  CHECK(!sk_stall_check(&w, true, false, 2000));    // Down from here
  CHECK(!sk_stall_check(&w, true, false, 2000 + kSkStallMs - 1));
  CHECK(sk_stall_check(&w, true, false, 2000 + kSkStallMs));
}

TEST(sk_stall_wait_starts_over_on_reconnect_and_wifi_loss) {
  SkStallWatch w;
  CHECK(!sk_stall_check(&w, true, true, 0));
  CHECK(!sk_stall_check(&w, true, false, 1000));
  CHECK(!sk_stall_check(&w, true, true, 200000));   // Back: wait cleared
  CHECK(!sk_stall_check(&w, true, false, 250000));
  CHECK(!sk_stall_check(&w, true, false, 250000 + kSkStallMs - 1));
  // WiFi lost: not this rule's to judge, and the wait starts over.
  CHECK(!sk_stall_check(&w, false, false, 250000 + kSkStallMs));
  CHECK(!sk_stall_check(&w, true, false, 600000));
  CHECK(!sk_stall_check(&w, true, false, 600000 + kSkStallMs - 1));
  CHECK(sk_stall_check(&w, true, false, 600000 + kSkStallMs));
}

TEST(sk_stall_survives_millis_wraparound) {
  SkStallWatch w;
  const uint32_t t0 = 0xFFFFFFFFu - 1000;
  CHECK(!sk_stall_check(&w, true, true, t0 - 5000));
  CHECK(!sk_stall_check(&w, true, false, t0));
  CHECK(!sk_stall_check(&w, true, false, t0 + 2000));  // Wrapped
  CHECK(sk_stall_check(&w, true, false, t0 + kSkStallMs));
}

TEST(hosted_boot_handshake_is_not_a_restart) {
  HostedWatch w;
  CHECK(!hosted_event(&w, HostedEvent::kCoprocessorStart));
  CHECK(!hosted_event(&w, HostedEvent::kHeartbeat));
  CHECK(!hosted_event(&w, HostedEvent::kHeartbeat));
}

TEST(hosted_restart_of_the_c6_after_a_beat_restarts) {
  HostedWatch w;
  CHECK(!hosted_event(&w, HostedEvent::kCoprocessorStart));
  CHECK(!hosted_event(&w, HostedEvent::kHeartbeat));
  CHECK(hosted_event(&w, HostedEvent::kCoprocessorStart));
}

TEST(hosted_transport_failure_always_restarts) {
  HostedWatch w;
  CHECK(hosted_event(&w, HostedEvent::kTransportFailure));
  CHECK(!hosted_event(&w, HostedEvent::kHeartbeat));
  CHECK(hosted_event(&w, HostedEvent::kTransportFailure));
}
