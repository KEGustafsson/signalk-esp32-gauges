// Host preview: renders every gauge page to a PPM using the firmware's UI code,
// once with a live sample (out/<size>_<n>_<id>.ppm) and once after the sample
// has gone stale (out/stale<size>_<n>_<id>.ppm).
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <vector>
#include <string>
#include "gauge_defs.h"
#include "gauge_value.h"
#include "ui/gauge_page.h"

static std::vector<uint16_t> fb;
static int W, H;
static uint32_t now_ms() {
  using namespace std::chrono;
  static auto t0 = steady_clock::now();
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
static void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  int w = lv_area_get_width(a);
  for (int y = a->y1; y <= a->y2; y++)
    memcpy(&fb[y * W + a->x1], px + (size_t)(y - a->y1) * w * 2, w * 2);
  lv_display_flush_ready(d);
}
static bool save(const char* path) {
  FILE* f = fopen(path, "wb");
  if (f == nullptr) {
    perror(path);
    return false;
  }
  bool ok = fprintf(f, "P6 %d %d 255\n", W, H) > 0;
  for (int i = 0; ok && i < W * H; i++) {
    uint16_t c = fb[i];
    unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 0x1F) * 255 / 31),
                            (unsigned char)(((c >> 5) & 0x3F) * 255 / 63),
                            (unsigned char)((c & 0x1F) * 255 / 31)};
    ok = fwrite(rgb, 1, 3, f) == 3;
  }
  ok = (fclose(f) == 0) && ok;
  if (!ok) {
    perror(path);
  }
  return ok;
}

// Sample values in displayed units, by gauge id; gauges not listed here
// show the middle of their range.
static float sample_for(const skgauges::GaugeDef& def) {
  static const struct {
    const char* id;
    float value;
  } kSamples[] = {
      {"port_rpm", 1650},   {"stbd_rpm", 2240},    {"speed", 12.4f},
      {"heading", 291},     {"port_coolant", 82},  {"stbd_coolant", 79},
      {"port_oil", 96},     {"stbd_oil", 104},     {"attitude", -1.8f},
  };
  for (const auto& s : kSamples) {
    if (strcmp(s.id, def.id) == 0) return s.value;
  }
  return (def.min + def.max) / 2.0f;
}

int main(int argc, char** argv) {
  W = H = argc > 1 ? atoi(argv[1]) : 480;
  if (W < 64 || W > 4096) {
    fprintf(stderr, "usage: %s [size in px, 64..4096; default 480]\n", argv[0]);
    return 2;
  }
  fb.assign(W * H, 0);
  lv_init();
  lv_tick_set_cb(now_ms);
  lv_display_t* d = lv_display_create(W, H);
  static std::vector<uint8_t> buf(W * 60 * 2);
  lv_display_set_buffers(d, buf.data(), nullptr, buf.size(), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(d, flush_cb);
  lv_obj_t* scr = lv_screen_active();
  lv_obj_remove_style_all(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  skgauges::ui::Geometry g{W / 2, H / 2, W / 2};
  for (size_t i = 0; i < skgauges::kGaugeCount; i++) {
    const auto& def = skgauges::kGauges[i];
    auto page = skgauges::ui::create_page(scr, def, g);
    skgauges::GaugeValue v;
    v.value = sample_for(def);
    v.value2 = -0.7f;
    v.seen = true;
    for (int k = 0; k < 60; k++) {
      v.updated_ms = now_ms();
      page->update(v, now_ms());
      lv_refr_now(d);
    }
    char path[64];
    snprintf(path, sizeof(path), "out/%d_%zu_%s.ppm", W, i, def.id);
    if (!save(path)) {
      fprintf(stderr, "could not write %s (does out/ exist?)\n", path);
      return 1;
    }
    printf("saved %s\n", path);

    // The same page once the sample is older than kStaleAfterMs.
    for (int k = 0; k < 60; k++) {
      page->update(v, v.updated_ms + skgauges::kStaleAfterMs);
      lv_refr_now(d);
    }
    snprintf(path, sizeof(path), "out/stale%d_%zu_%s.ppm", W, i, def.id);
    if (!save(path)) {
      fprintf(stderr, "could not write %s\n", path);
      return 1;
    }
    printf("saved %s\n", path);
  }
  return 0;
}
