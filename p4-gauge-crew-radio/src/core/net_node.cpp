// SPDX-License-Identifier: EUPL-1.2
#include "net_node.h"

#include <string.h>

namespace crewradio {

NetNode::NetNode(const uint8_t key[ChannelCrypto::kKeyBytes], int32_t self_id,
                 const Config& config)
    : crypto_(key), config_(config), self_(self_id) {}

bool NetNode::on_packet(const uint8_t* p, size_t len, Via via, uint32_t from,
                        int64_t now_ms, int64_t now_s, Audio* audio,
                        Relay* relay) {
  c_.received++;
  if (relay != nullptr) *relay = Relay();
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
  if ((r.kind == Ingress::Kind::kAccept ||
       r.kind == Ingress::Kind::kRelayOnly) &&
      r.relay_ttl > 0 && config_.relay && relay != nullptr &&
      len <= sizeof(relay_)) {
    memcpy(relay_, p, len);
    packet::set_ttl(relay_, r.relay_ttl);
    relay->to_lan = via == Via::kReticulum;
    relay->to_reticulum = via == Via::kLan;
    relay->sender = h.sender_id;
    relay->hello = h.codec == Codec::kHello;
    relay->data = relay_;
    relay->len = len;
    c_.relayed++;
  }

  switch (r.kind) {
    case Ingress::Kind::kAccept:
      // A first copy, fresh, that opened with the channel key: its address
      // is a crew member's. Not from a copy or a stale packet, which anyone
      // can replay from any address they like to fill the table and send
      // the unit's voice elsewhere; a crew member's own first copy (one a
      // second, its hello) still teaches it. Only over the WLAN: a packet
      // that came over Reticulum has no LAN address.
      if (via == Via::kLan) peers_.confirm(from, h.sender_id, now_ms);
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

  if (h.codec == Codec::kHello) {
    c_.hellos++;
    const int gap = ingress_.hello_gap(h.sender_id, h.seq);
    Hello hello;
    if (Hello::decode(plain_, (size_t)r.plain_len, &hello)) {
      roster_.hello(h.sender_id, hello, hello.ttl - h.ttl, now_ms, gap,
                    via == Via::kReticulum);
    } else {
      roster_.seen(h.sender_id, now_ms);
    }
    return false;
  }

  const int gap = ingress_.admit_audio(h.sender_id, h.seq);
  if (gap < 0) {  // Late: its slot is concealed already
    c_.late++;
    return false;
  }
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
  h.seq = hello_seq_++;
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
