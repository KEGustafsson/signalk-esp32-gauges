// SPDX-License-Identifier: EUPL-1.2
#include "net_node.h"

#include <string.h>

namespace crewradio {

NetNode::NetNode(const uint8_t key[ChannelCrypto::kKeyBytes], int32_t self_id,
                 const Config& config)
    : crypto_(key), config_(config), self_(self_id) {}

// One packet from the WLAN or Reticulum: admitted (authentic, fresh, first copy),
// learnt from (local peers, link far ends), played, and where a copy goes.
bool NetNode::on_packet(const uint8_t* p, size_t len, Via via, uint32_t from,
                        int64_t now_ms, int64_t now_s, Audio* audio,
                        Relay* relay, uint32_t link_tag) {
  c_.received++;
  if (relay != nullptr) *relay = Relay();
  // Before anything else, even the parse: a flood from one host costs that
  // host's budget, never the crew's share of the global one.
  if (via == Via::kLan && !sources_.allow(from, now_ms)) {
    c_.flooded++;
    return false;
  }
  Header h;
  if (!packet::parse(p, len, &h)) {
    c_.malformed++;
    return false;
  }
  uint8_t aad[packet::kHeader];
  packet::aad_of(p, aad);
  int plain_len = -1;
  const Ingress::Result r = ingress_.admit(
      h, now_ms, now_s, config_.max_hops,
      [&]() {
        plain_len = crypto_.open(aad, sizeof(aad), p + packet::kHeader,
                                 len - packet::kHeader, plain_);
        return plain_len;
      },
      true, self_);
  const bool authentic = r.kind == Ingress::Kind::kAccept ||
                         r.kind == Ingress::Kind::kDuplicate ||
                         r.kind == Ingress::Kind::kRelayOnly;
  // Who is at the far end of this link: the sender of an authentic packet
  // that came on it unrelayed (a bridge's copy has its ttl spent by one).
  if (via == Via::kReticulum && link_tag != 0 && authentic &&
      h.hops == h.ttl) {
    forwarder_.link_heard(link_tag, h.sender_id);
  }
  bool to_lan = via == Via::kReticulum;
  bool to_reticulum = via == Via::kLan;
  if (h.codec != Codec::kHello) {
    const bool local_direct = via == Via::kReticulum && h.hops == h.ttl &&
                              forwarder_.is_local(h.sender_id, now_ms);
    const Forwarder::Route route = Forwarder::audio_route(
        via == Via::kLan, is_forwarder(now_ms),
        forwarder_.local_rns_up(h.sender_id, now_ms), local_direct);
    to_lan = route.to_lan;
    to_reticulum = route.to_reticulum;
  }
  if ((r.kind == Ingress::Kind::kAccept ||
       r.kind == Ingress::Kind::kRelayOnly) &&
      r.relay_ttl > 0 && config_.relay && relay != nullptr &&
      (to_lan || to_reticulum) && len <= sizeof(relay_)) {
    memcpy(relay_, p, len);
    packet::set_ttl(relay_, r.relay_ttl);
    relay->to_lan = to_lan;
    relay->to_reticulum = to_reticulum;
    relay->sender = h.sender_id;
    relay->hello = h.codec == Codec::kHello;
    relay->data = relay_;
    relay->len = len;
    c_.relayed++;
  }

  // A hello straight from its sender over the WLAN says it is on this boat,
  // whether or not it is the first copy: one that came over Reticulum first
  // must not hide that (the plugin's rule too). Opened before the seen-cache
  // looked, so a copy has its payload.
  if (h.codec == Codec::kHello && via == Via::kLan && h.hops == h.ttl &&
      (r.kind == Ingress::Kind::kDuplicate ||
       r.kind == Ingress::Kind::kRelayOnly) &&
      plain_len >= 0) {
    Hello copy;
    if (Hello::decode(plain_, (size_t)plain_len, &copy)) {
      forwarder_.local_hello(h.sender_id, copy.transports, now_ms);
    }
  }

  switch (r.kind) {
    case Ingress::Kind::kAccept:
      break;
    case Ingress::Kind::kDuplicate:
    case Ingress::Kind::kRelayOnly:  // Forwarded at most, never played twice
      c_.duplicates++;
      return false;
    case Ingress::Kind::kStale:
      c_.stale++;
      return false;
    case Ingress::Kind::kRejected:
      c_.rejected++;
      if (r.why == Ingress::Why::kJunkFlood) c_.junk_floods++;
      return false;
  }

  // Where the sender's unicast copies go, learnt as the plugin does: only
  // from a first copy, fresh, that opened with the channel key (not a copy
  // or a stale packet, which anyone can replay from any address), that came
  // straight from its sender (hops - ttl == 0: a relayed one arrives from
  // the relay's address, which would then get them instead), and that moved
  // its sender's sequence on (a late one, replayed from elsewhere, must not
  // re-point them). Only over the WLAN: Reticulum has no LAN address. The
  // address itself is not authenticated (the AEAD covers the packet, not
  // the IP source); a crew member's own next first copy, one a second,
  // corrects it.
  const bool direct = via == Via::kLan && h.hops == h.ttl;

  if (h.codec == Codec::kHello) {
    c_.hellos++;
    const int gap = ingress_.hello_gap(h.sender_id, h.seq);
    if (direct && gap >= 0) peers_.confirm(from, h.sender_id, now_ms);
    Hello hello;
    // A hello that does not decode says nothing about its sender, as in the
    // app: no roster entry from it.
    if (Hello::decode(plain_, (size_t)r.plain_len, &hello)) {
      // On this boat's WLAN, unrelayed: the forwarder's view.
      if (direct && gap >= 0) {
        forwarder_.local_hello(h.sender_id, hello.transports, now_ms);
      }
      roster_.hello(h.sender_id, hello, hello.ttl - h.ttl, now_ms, gap,
                    via == Via::kReticulum);
    }
    return false;
  }

  const int gap = ingress_.admit_audio(h.sender_id, h.seq);
  if (gap < 0) {  // Late: its slot is concealed already
    c_.late++;
    return false;
  }
  if (direct) peers_.confirm(from, h.sender_id, now_ms);
  c_.audio++;
  roster_.audio(h.sender_id, now_ms, gap);
  audio->sender = h.sender_id;
  audio->codec = h.codec;
  audio->gap = gap;
  audio->data = plain_;
  audio->len = (size_t)r.plain_len;
  return true;
}

size_t NetNode::build_hello(int64_t now_s, uint8_t out[kMaxHello]) {
  Hello hello;
  hello.name = config_.name;
  hello.transports = config_.transports;
  hello.ttl = config_.max_hops;
  hello.version_code = 0;  // As the plugin: no OLD BUILD mark on the phones
  uint8_t plain[Hello::kMaxSize];
  const size_t plain_len = hello.encode(plain);

  Header h;
  h.sender_id = self_;
  h.seq = (int32_t)hello_seq_++;
  h.codec = Codec::kHello;
  h.ttl = config_.max_hops;
  h.hops = config_.max_hops;
  h.time = (uint32_t)now_s;
  packet::encode_header(h, out);
  uint8_t aad[packet::kHeader];
  packet::aad_of(out, aad);
  if (!crypto_.seal(aad, sizeof(aad), plain, plain_len,
                    out + packet::kHeader)) {
    return 0;
  }
  return packet::kHeader + plain_len + ChannelCrypto::kOverhead;
}

}  // namespace crewradio
