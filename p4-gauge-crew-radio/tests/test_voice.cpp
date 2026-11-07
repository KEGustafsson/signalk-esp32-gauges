// SPDX-License-Identifier: EUPL-1.2
// The mic's cleanup (core/voice.h): speexdsp's echo canceller fed with the
// speaker's own signal as the reference, then its preprocessor. Synthetic:
// the "room" is a short impulse response (a direct path and two
// reflections), the near end a voice-like burst.
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <vector>

#include "core/voice.h"
#include "test.h"

using namespace crewradio;

namespace {

constexpr int kN = (int)audio::kFrameSamples;

// A repeatable noise, speech-band-ish (two one-pole filters): what the
// speaker plays.
struct Noise {
  uint32_t s = 12345;
  float lp = 0.0f, hp = 0.0f;
  int16_t next(float amp) {
    s = s * 1664525u + 1013904223u;
    const float w = ((int32_t)(s >> 8) - (1 << 23)) / (float)(1 << 23);
    lp += 0.4f * (w - lp);
    const float v = lp - hp;
    hp += 0.02f * (lp - hp);
    return (int16_t)lrintf(amp * v);
  }
};

// The speaker-to-mic path: 1 ms direct, reflections at 6 and 23 ms.
struct Room {
  std::vector<float> hist = std::vector<float>(1024, 0.0f);
  int at = 0;
  float push(int16_t played) {
    hist[at] = played;
    auto tap = [&](int delay) { return hist[(at - delay + 1024) % 1024]; };
    const float y = 0.8f * tap(16) - 0.35f * tap(96) + 0.15f * tap(368);
    at = (at + 1) % 1024;
    return y;
  }
};

double rms(const int16_t* f) {
  double e = 0.0;
  for (int i = 0; i < kN; i++) e += (double)f[i] * f[i];
  return sqrt(e / kN);
}

int16_t clip(float v) {
  return (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : lrintf(v));
}

}  // namespace

TEST(voice_allocates_its_states) {
  voice::Processor p;
  CHECK(p.ok());
  CHECK(p.echo_cancel());
  CHECK(p.noise_db() == voice::kDefaultNoiseSuppressDb);
}

TEST(voice_off_passes_the_mic_through_unchanged) {
  voice::Processor p;
  p.set(false, 0);
  Noise n;
  int16_t mic[kN], ref[kN], out[kN];
  for (int f = 0; f < 20; f++) {
    for (int i = 0; i < kN; i++) {
      mic[i] = n.next(3000.0f);
      ref[i] = n.next(3000.0f);
    }
    p.process(mic, ref, out);
    CHECK(memcmp(mic, out, sizeof(mic)) == 0);
  }
  int db = 99;
  CHECK(!p.take_echo_db(&db));  // The canceller is off: no figure
  CHECK(db == 99);
}

TEST(voice_settings_are_clamped) {
  voice::Processor p;
  p.set(true, 99);
  CHECK(p.noise_db() == voice::kMaxNoiseSuppressDb);
  p.set(true, -5);
  CHECK(p.noise_db() == 0);
}

// The speaker alone (the crew played, nobody talking here): after it has
// learned the room, what reaches the crew is far below what the mic heard.
TEST(voice_cancels_the_speakers_echo) {
  voice::Processor p;
  p.set(true, 0);  // The canceller and residual suppression, no denoise
  Noise far;
  Room room;
  int16_t mic[kN], ref[kN], out[kN];
  double mic_late = 0.0, out_late = 0.0;
  const int frames = 400;  // 8 s
  for (int f = 0; f < frames; f++) {
    for (int i = 0; i < kN; i++) {
      ref[i] = far.next(6000.0f);
      mic[i] = clip(room.push(ref[i]));
    }
    p.process(mic, ref, out);
    if (f >= frames - 100) {  // The last 2 s
      mic_late += rms(mic) * rms(mic);
      out_late += rms(out) * rms(out);
    }
  }
  const double erle_db = 10.0 * log10(mic_late / (out_late + 1.0));
  printf("    echo return loss enhancement %.1f dB\n", erle_db);
  CHECK(erle_db > 25.0);
  int db = 0;
  CHECK(p.take_echo_db(&db));
  CHECK(db > 10);  // Over the whole run, the learning included
}

// The near end talks while the speaker is silent: the voice goes through
// (the canceller has nothing to take out, steady-noise suppression is off).
TEST(voice_keeps_the_near_voice) {
  voice::Processor p;
  p.set(true, 0);
  Noise near;
  int16_t mic[kN], ref[kN], out[kN];
  memset(ref, 0, sizeof(ref));
  double in_e = 0.0, out_e = 0.0;
  for (int f = 0; f < 150; f++) {
    // Bursts: 200 ms on, 100 ms off, as syllables.
    const bool on = (f % 15) < 10;
    for (int i = 0; i < kN; i++) mic[i] = near.next(on ? 4000.0f : 0.0f);
    p.process(mic, ref, out);
    if (f >= 50) {
      in_e += rms(mic) * rms(mic);
      out_e += rms(out) * rms(out);
    }
  }
  const double loss_db = 10.0 * log10(in_e / (out_e + 1.0));
  printf("    near voice level change %.1f dB\n", -loss_db);
  CHECK(fabs(loss_db) < 3.0);
  int db = 0;
  CHECK(!p.take_echo_db(&db));  // The speaker never played
}

// Steady noise alone (a fan, an engine's hum): suppressed by about the
// setting once the preprocessor has its estimate.
TEST(voice_suppresses_steady_noise) {
  voice::Processor p;
  p.set(false, 20);
  Noise hum;
  int16_t mic[kN], ref[kN], out[kN];
  memset(ref, 0, sizeof(ref));
  double in_e = 0.0, out_e = 0.0;
  for (int f = 0; f < 300; f++) {
    for (int i = 0; i < kN; i++) mic[i] = hum.next(1500.0f);
    p.process(mic, ref, out);
    if (f >= 200) {
      in_e += rms(mic) * rms(mic);
      out_e += rms(out) * rms(out);
    }
  }
  const double cut_db = 10.0 * log10(in_e / (out_e + 1.0));
  printf("    steady noise cut %.1f dB\n", cut_db);
  CHECK(cut_db > 10.0);
}
