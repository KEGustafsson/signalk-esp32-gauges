// SPDX-License-Identifier: EUPL-1.2
#include "curve25519.h"

#include <string.h>

#include <mbedtls/sha512.h>

namespace crewradio {
namespace rns {
namespace curve25519 {

namespace field {

// d = -121665/121666, 2d, sqrt(-1) = 2^((p-1)/4), the base point (x even,
// y = 4/5): computed from those definitions and checked by the tests.
const Fe kD = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
               0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
const Fe kD2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
const Fe kSqrtM1 = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f,
                    0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d,
                    0xdf0b, 0x4fc1, 0x2480, 0x2b83};
const Fe kBx = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
                0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
const Fe kBy = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};

}  // namespace field

namespace {

using field::Fe;

// The group order, little-endian.
const int64_t kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
                        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                        0,    0,    0,    0,    0,    0,    0,    0,
                        0,    0,    0,    0,    0,    0,    0,    0x10};
const Fe kGf0 = {0};
const Fe kGf1 = {1};
const Fe kA24 = {0xdb41, 1};  // 121665

void set(Fe o, const Fe a) { memcpy(o, a, sizeof(Fe)); }

// Carries each limb into the next, the top one back into the bottom times
// 38. (Multiplications stand in for shifts of values that may be negative.)
void car(Fe o) {
  int64_t c = 1;
  for (int i = 0; i < 16; i++) {
    const int64_t v = o[i] + c + 65535;
    c = v >> 16;
    o[i] = v - c * 65536;
  }
  o[0] += c - 1 + 37 * (c - 1);
}

// Swaps p and q when b is 1, leaves them when 0, without a branch.
void sel(Fe p, Fe q, int b) {
  const int64_t c = ~(int64_t)(b - 1);
  for (int i = 0; i < 16; i++) {
    const int64_t t = c & (p[i] ^ q[i]);
    p[i] ^= t;
    q[i] ^= t;
  }
}

void add(Fe o, const Fe a, const Fe b) {
  for (int i = 0; i < 16; i++) o[i] = a[i] + b[i];
}
void sub(Fe o, const Fe a, const Fe b) {
  for (int i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

void sq(Fe o, const Fe a) { field::mul(o, a, a); }

bool neq(const Fe a, const Fe b) {
  uint8_t c[32], d[32];
  field::pack(c, a);
  field::pack(d, b);
  return memcmp(c, d, 32) != 0;  // Public values only (signature checks)
}

int par(const Fe a) {
  uint8_t d[32];
  field::pack(d, a);
  return d[0] & 1;
}

void pow2523(Fe o, const Fe i) {
  Fe c;
  set(c, i);
  for (int a = 250; a >= 0; a--) {
    sq(c, c);
    if (a != 1) field::mul(c, c, i);
  }
  set(o, c);
}

void sha512(uint8_t out[64], const uint8_t* a, size_t a_len, const uint8_t* b,
            size_t b_len, const uint8_t* c = nullptr, size_t c_len = 0) {
  mbedtls_sha512_context ctx;
  mbedtls_sha512_init(&ctx);
  mbedtls_sha512_starts(&ctx, 0);
  mbedtls_sha512_update(&ctx, a, a_len);
  if (b_len > 0) mbedtls_sha512_update(&ctx, b, b_len);
  if (c_len > 0) mbedtls_sha512_update(&ctx, c, c_len);
  mbedtls_sha512_finish(&ctx, out);
  mbedtls_sha512_free(&ctx);
}

struct Point {
  Fe x, y, z, t;
};

void padd(Point& p, const Point& q) {
  Fe a, b, c, d, e, f, g, h, t;
  sub(a, p.y, p.x);
  sub(t, q.y, q.x);
  field::mul(a, a, t);
  add(b, p.x, p.y);
  add(t, q.x, q.y);
  field::mul(b, b, t);
  field::mul(c, p.t, q.t);
  field::mul(c, c, field::kD2);
  field::mul(d, p.z, q.z);
  add(d, d, d);
  sub(e, b, a);
  sub(f, d, c);
  add(g, d, c);
  add(h, b, a);
  field::mul(p.x, e, f);
  field::mul(p.y, h, g);
  field::mul(p.z, g, f);
  field::mul(p.t, e, h);
}

void cswap(Point& p, Point& q, int b) {
  sel(p.x, q.x, b);
  sel(p.y, q.y, b);
  sel(p.z, q.z, b);
  sel(p.t, q.t, b);
}

void pack_point(uint8_t r[32], const Point& p) {
  Fe tx, ty, zi;
  field::inv(zi, p.z);
  field::mul(tx, p.x, zi);
  field::mul(ty, p.y, zi);
  field::pack(r, ty);
  r[31] ^= (uint8_t)(par(tx) << 7);
}

// p = s * q (q is used up), over all 256 bits of s.
void scalar_mult(Point& p, Point& q, const uint8_t s[32]) {
  set(p.x, kGf0);
  set(p.y, kGf1);
  set(p.z, kGf1);
  set(p.t, kGf0);
  for (int i = 255; i >= 0; i--) {
    const int b = (s[i / 8] >> (i & 7)) & 1;
    cswap(p, q, b);
    padd(q, p);
    padd(p, p);
    cswap(p, q, b);
  }
}

void scalar_base(Point& p, const uint8_t s[32]) {
  Point q;
  set(q.x, field::kBx);
  set(q.y, field::kBy);
  set(q.z, kGf1);
  field::mul(q.t, field::kBx, field::kBy);
  scalar_mult(p, q, s);
}

// Reduces the 64 limbs of x modulo the group order into r (32 bytes).
void mod_l(uint8_t r[32], int64_t x[64]) {
  int64_t carry;
  for (int i = 63; i >= 32; i--) {
    carry = 0;
    int j = i - 32;
    for (; j < i - 12; j++) {
      x[j] += carry - 16 * x[i] * kL[j - (i - 32)];
      carry = (x[j] + 128) >> 8;
      x[j] -= carry * 256;
    }
    x[j] += carry;
    x[i] = 0;
  }
  carry = 0;
  for (int j = 0; j < 32; j++) {
    x[j] += carry - (x[31] >> 4) * kL[j];
    carry = x[j] >> 8;
    x[j] &= 255;
  }
  for (int j = 0; j < 32; j++) x[j] -= carry * kL[j];
  for (int i = 0; i < 32; i++) {
    x[i + 1] += x[i] >> 8;
    r[i] = (uint8_t)(x[i] & 255);
  }
}

void reduce(uint8_t r[32], const uint8_t h[64]) {
  int64_t x[64];
  for (int i = 0; i < 64; i++) x[i] = h[i];
  mod_l(r, x);
}

void expand(uint8_t d[64], const uint8_t seed[32]) {
  sha512(d, seed, 32, nullptr, 0);
  d[0] &= 248;
  d[31] = (uint8_t)((d[31] & 127) | 64);
}

bool unpack_neg(Point& r, const uint8_t p[32]) {
  Fe t, chk, num, den, den2, den4, den6;
  set(r.z, kGf1);
  field::unpack(r.y, p);
  sq(num, r.y);
  field::mul(den, num, field::kD);
  sub(num, num, r.z);
  add(den, r.z, den);
  sq(den2, den);
  sq(den4, den2);
  field::mul(den6, den4, den2);
  field::mul(t, den6, num);
  field::mul(t, t, den);
  pow2523(t, t);
  field::mul(t, t, num);
  field::mul(t, t, den);
  field::mul(t, t, den);
  field::mul(r.x, t, den);
  sq(chk, r.x);
  field::mul(chk, chk, den);
  if (neq(chk, num)) field::mul(r.x, r.x, field::kSqrtM1);
  sq(chk, r.x);
  field::mul(chk, chk, den);
  if (neq(chk, num)) return false;
  if (par(r.x) == (p[31] >> 7)) sub(r.x, kGf0, r.x);
  field::mul(r.t, r.x, r.y);
  return true;
}

// S < L, compared from the top byte down (S is public).
bool canonical(const uint8_t s[32]) {
  for (int i = 31; i >= 0; i--) {
    if (s[i] < kL[i]) return true;
    if (s[i] > kL[i]) return false;
  }
  return false;  // Equal to L
}

}  // namespace

namespace field {

// Limbs entering a multiplication stay below 2^18 in magnitude (a sum or
// difference of two carried results, or an unpacked input), so each product
// is a 32 x 32 -> 64-bit one: two instructions on the P4's RV32, where a
// 64 x 64 one is several.
void mul(Fe o, const Fe a, const Fe b) {
  int64_t t[31] = {0};
  for (int i = 0; i < 16; i++) {
    const int32_t ai = (int32_t)a[i];
    for (int j = 0; j < 16; j++) t[i + j] += (int64_t)ai * (int32_t)b[j];
  }
  for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
  for (int i = 0; i < 16; i++) o[i] = t[i];
  car(o);
  car(o);
}

void inv(Fe o, const Fe i) {
  Fe c;
  set(c, i);
  for (int a = 253; a >= 0; a--) {
    sq(c, c);
    if (a != 2 && a != 4) mul(c, c, i);
  }
  set(o, c);
}

void pack(uint8_t o[32], const Fe n) {
  Fe t, m;
  set(t, n);
  car(t);
  car(t);
  car(t);
  for (int k = 0; k < 2; k++) {
    m[0] = t[0] - 0xffed;
    for (int i = 1; i < 15; i++) {
      m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
      m[i - 1] &= 0xffff;
    }
    m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
    const int b = (int)((m[15] >> 16) & 1);
    m[14] &= 0xffff;
    sel(t, m, 1 - b);
  }
  for (int i = 0; i < 16; i++) {
    o[2 * i] = (uint8_t)(t[i] & 0xff);
    o[2 * i + 1] = (uint8_t)((t[i] >> 8) & 0xff);
  }
}

void unpack(Fe o, const uint8_t n[32]) {
  for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
  o[15] &= 0x7fff;
}

}  // namespace field

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u[32]) {
  uint8_t z[32];
  memcpy(z, scalar, 32);
  z[31] = (uint8_t)((z[31] & 127) | 64);
  z[0] &= 248;
  Fe x, a = {0}, b, c = {0}, d = {0}, e, f;
  field::unpack(x, u);
  set(b, x);
  a[0] = 1;
  d[0] = 1;
  for (int i = 254; i >= 0; i--) {
    const int r = (z[i >> 3] >> (i & 7)) & 1;
    sel(a, b, r);
    sel(c, d, r);
    add(e, a, c);
    sub(a, a, c);
    add(c, b, d);
    sub(b, b, d);
    sq(d, e);
    sq(f, a);
    field::mul(a, c, a);
    field::mul(c, b, e);
    add(e, a, c);
    sub(a, a, c);
    sq(b, a);
    sub(c, d, f);
    field::mul(a, c, kA24);
    add(a, a, d);
    field::mul(c, c, a);
    field::mul(a, d, f);
    field::mul(d, b, x);
    sq(b, e);
    sel(a, b, r);
    sel(c, d, r);
  }
  field::inv(c, c);
  field::mul(a, a, c);
  field::pack(out, a);
  memset(z, 0, sizeof(z));
}

void x25519_public(uint8_t out[32], const uint8_t scalar[32]) {
  uint8_t nine[32] = {9};
  x25519(out, scalar, nine);
}

void ed25519_public(uint8_t out[32], const uint8_t seed[32]) {
  uint8_t d[64];
  expand(d, seed);
  Point p;
  scalar_base(p, d);
  pack_point(out, p);
  memset(d, 0, sizeof(d));
}

void ed25519_sign(uint8_t sig[64], const uint8_t seed[32],
                  const uint8_t pub[32], const uint8_t* msg, size_t len) {
  uint8_t d[64], h[64], r[32], k[32];
  expand(d, seed);
  sha512(h, d + 32, 32, msg, len);
  reduce(r, h);
  Point p;
  scalar_base(p, r);
  pack_point(sig, p);
  sha512(h, sig, 32, pub, 32, msg, len);
  reduce(k, h);
  int64_t x[64] = {0};
  for (int i = 0; i < 32; i++) x[i] = r[i];
  for (int i = 0; i < 32; i++) {
    for (int j = 0; j < 32; j++) x[i + j] += (int64_t)k[i] * d[j];
  }
  mod_l(sig + 32, x);
  memset(d, 0, sizeof(d));
  memset(r, 0, sizeof(r));
}

bool ed25519_verify(const uint8_t pub[32], const uint8_t* msg, size_t len,
                    const uint8_t sig[64]) {
  if (!canonical(sig + 32)) return false;
  Point q;
  if (!unpack_neg(q, pub)) return false;
  uint8_t h[64], k[32];
  sha512(h, sig, 32, pub, 32, msg, len);
  reduce(k, h);
  Point p, b;
  scalar_mult(p, q, k);
  scalar_base(b, sig + 32);
  padd(p, b);
  uint8_t t[32];
  pack_point(t, p);
  return memcmp(t, sig, 32) == 0;  // Public values
}

}  // namespace curve25519
}  // namespace rns
}  // namespace crewradio
