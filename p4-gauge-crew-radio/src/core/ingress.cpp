// SPDX-License-Identifier: EUPL-1.2
#include "ingress.h"

namespace crewradio {

Ingress::Ingress(const RateLimiter::Config& limits, size_t audio_cache,
                 size_t hello_cache)
    : limiter_(limits), seen_(audio_cache), seen_hellos_(hello_cache) {}

int Ingress::relay_ttl(const Header& h, int max_hops) {
  const int ttl = h.ttl < h.hops ? h.ttl : h.hops;
  const int relayed = h.hops - ttl;  // Relays passed; this one would be +1
  return (ttl > 1 && relayed + 1 < max_hops) ? ttl - 1 : 0;
}

Ingress::Result Ingress::after_open(const Header& h, int64_t now_ms,
                                    int64_t now_s, int max_hops,
                                    int plain_len) {
  if (!packet::is_fresh(h.time, now_s)) {
    return {Kind::kStale, Why::kNone, 0, 0};
  }
  SeenCache& cache = h.codec == Codec::kHello ? seen_hellos_ : seen_;
  const uint64_t k = SeenCache::key(h.sender_id, h.seq);
  const int ttl = relay_ttl(h, max_hops);
  uint8_t* forwarded = cache.find(k);
  if (forwarded != nullptr) {
    // Heard already, so it is not played twice. But the ttl is outside the
    // AAD: a captured frame replayed first with its ttl lowered would take
    // this slot and silence the far side of the mesh. A copy that reaches
    // further than the best forwarded so far is forwarded again; each must
    // beat the last and the ttl is capped at the signed budget, so this
    // costs at most that budget in extra forwards per packet.
    if (ttl <= *forwarded) return {Kind::kDuplicate, Why::kNone, 0, 0};
    *forwarded = (uint8_t)ttl;
    return {Kind::kRelayOnly, Why::kNone, ttl, 0};
  }
  if (!limiter_.allow_sender(h.sender_id, now_ms)) {
    return {Kind::kRejected, Why::kSenderBudget, 0, 0};
  }
  cache.insert(k, (uint8_t)ttl);
  return {Kind::kAccept, Why::kNone, ttl, plain_len};
}

}  // namespace crewradio
