#include "gauge_ui.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <lvgl.h>

#include <memory>
#include <vector>

#include "sensesp_app.h"
#include "sensesp_base_app.h"
#include "sk_data.h"
#include "ui/gauge_page.h"

namespace skgauges {

namespace {

constexpr uint32_t kUiPeriodMs = 30;
constexpr uint32_t kInfoShowMs = 5000;
constexpr uint32_t kToastShowMs = 1300;
// Page and brightness are written to flash this long after the last change,
// not on every swipe: an NVS commit stalls the loop (and the RGB panel's
// PSRAM frame buffer reads) for a few to tens of ms.
constexpr uint32_t kPrefsSaveDelayMs = 3000;
constexpr int kBrightnessLevels[] = {100, 60, 30, 10};
constexpr int kBrightnessCount =
    sizeof(kBrightnessLevels) / sizeof(kBrightnessLevels[0]);
const char* const kPrefsNamespace = "skgauges";
// A hold (brightness) may drift this far, as a fraction of the radius; more
// is a slow swipe.
constexpr float kHoldSlop = 0.08f;

struct UiState {
  UiConfig config;
  ui::Geometry geometry;
  std::vector<size_t> pages;  // Indexes into kGauges shown on this unit
  size_t current = 0;
  std::unique_ptr<ui::GaugePage> page;
  int brightness_index = 0;
  lv_obj_t* status_dot = nullptr;
  uint32_t status_color = 0xFFFFFFFF;
  lv_obj_t* overlay = nullptr;  // Info / toast box on the top layer
  lv_obj_t* overlay_label = nullptr;
  lv_timer_t* overlay_timer = nullptr;
  bool info_visible = false;
  lv_point_t press_point = {0, 0};
  lv_timer_t* prefs_timer = nullptr;
  String saved_page;      // What NVS holds, to skip writes that change nothing
  int saved_bright = -1;
};

UiState* state = nullptr;

void write_prefs(lv_timer_t*) {
  lv_timer_pause(state->prefs_timer);
  const char* page = kGauges[state->pages[state->current]].id;
  if (state->saved_page == page &&
      state->saved_bright == state->brightness_index) {
    return;
  }
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    if (state->saved_page != page) prefs.putString("page", page);
    if (state->saved_bright != state->brightness_index) {
      prefs.putInt("bright", state->brightness_index);
    }
    prefs.end();
    state->saved_page = page;
    state->saved_bright = state->brightness_index;
  }
}

// Schedules a write of the current page and brightness (debounced).
void save_prefs() {
  lv_timer_reset(state->prefs_timer);
  lv_timer_resume(state->prefs_timer);
}

void load_prefs() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return;  // First boot: namespace does not exist yet
  }
  String id = prefs.isKey("page") ? prefs.getString("page") : String();
  int bright = prefs.isKey("bright") ? prefs.getInt("bright") : 0;
  prefs.end();
  state->saved_page = id;
  state->saved_bright = bright;
  for (size_t i = 0; i < state->pages.size(); i++) {
    if (id == kGauges[state->pages[i]].id) {
      state->current = i;
    }
  }
  if (bright >= 0 && bright < kBrightnessCount) {
    state->brightness_index = bright;
  }
}

void hide_overlay(lv_timer_t*) {
  lv_obj_add_flag(state->overlay, LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(state->overlay_timer);
  state->info_visible = false;
}

void show_overlay(const char* text, uint32_t duration_ms, bool info) {
  lv_label_set_text(state->overlay_label, text);
  lv_obj_remove_flag(state->overlay, LV_OBJ_FLAG_HIDDEN);
  lv_timer_set_period(state->overlay_timer, duration_ms);
  lv_timer_reset(state->overlay_timer);
  lv_timer_resume(state->overlay_timer);
  state->info_visible = info;
}

void show_page(size_t index, bool announce) {
  const uint32_t start = millis();
  state->current = index;
  state->page.reset();  // Frees the previous page's canvas first
  const size_t gauge = state->pages[index];
  state->page = ui::create_page(lv_screen_active(), kGauges[gauge],
                                state->geometry);
  state->page->set_unit_label(
      unit_for_path(kGauges[gauge], sk_data().sk_path(gauge).c_str()));
  // Page builds run on the loop under its 5 s watchdog; log how long they
  // take on the real hardware.
  ESP_LOGI("gauge_ui", "Page %s built in %u ms", kGauges[state->pages[index]].id,
           (unsigned)(millis() - start));
  if (announce) {
    char text[48];
    snprintf(text, sizeof(text), "%u / %u\n%s",
             (unsigned)(index + 1), (unsigned)state->pages.size(),
             kGauges[state->pages[index]].title);
    show_overlay(text, kToastShowMs, false);
    save_prefs();
  }
}

void show_info() {
  const GaugeDef& def = kGauges[state->pages[state->current]];
  String text;
  text += state->config.unit_name;
  text += "  ";
  text += sensesp::sensesp_app != nullptr
              ? sensesp::SensESPBaseApp::get_hostname()
              : String("-");
  text += "\nIP ";
  text += WiFi.isConnected() ? WiFi.localIP().toString() : String("-");
  text += "\nSignal K: ";
  text += sk_data().server_connected() ? "connected" : "not connected";
  text += "\n";
  text += def.title;
  text += ": ";
  text += sk_data().sk_path(state->pages[state->current]);
  text += "\n" LV_SYMBOL_LEFT " swipe " LV_SYMBOL_RIGHT "   hold: brightness";
  show_overlay(text.c_str(), kInfoShowMs, true);
}

void screen_event_cb(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t* indev = lv_indev_active();

  if (indev == nullptr) {
    return;
  }
  // A swipe in any direction must not also count as a tap or a long press.
  // wait_release covers later input reads; the gesture_dir check covers a
  // LONG_PRESSED that LVGL sends in the same read as the gesture (the page
  // switch below can take longer than the long-press time).
  const bool swiped = lv_indev_get_gesture_dir(indev) != LV_DIR_NONE;

  if (code == LV_EVENT_GESTURE) {
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_NONE) {
      return;
    }
    const size_t n = state->pages.size();
    lv_indev_wait_release(indev);
    if (dir == LV_DIR_LEFT) {
      show_page((state->current + 1) % n, true);
    } else if (dir == LV_DIR_RIGHT) {
      show_page((state->current + n - 1) % n, true);
    }
  } else if (swiped) {
    return;
  } else if (code == LV_EVENT_PRESSED) {
    lv_indev_get_point(indev, &state->press_point);
  } else if (code == LV_EVENT_SHORT_CLICKED) {
    if (state->info_visible) {
      hide_overlay(nullptr);
    } else {
      show_info();
    }
  } else if (code == LV_EVENT_LONG_PRESSED) {
    // LVGL fires a long press after the hold time however far the finger
    // has moved, and a slow swipe never reaches gesture speed: a finger
    // that travelled is a swipe in progress, not a hold.
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    const int32_t dx = point.x - state->press_point.x;
    const int32_t dy = point.y - state->press_point.y;
    const int32_t slop = state->geometry.px(kHoldSlop);
    if (dx * dx + dy * dy > slop * slop) {
      return;
    }
    state->brightness_index = (state->brightness_index + 1) % kBrightnessCount;
    const int percent = kBrightnessLevels[state->brightness_index];
    if (state->config.set_brightness) {
      state->config.set_brightness(percent);
    }
    char text[32];
    snprintf(text, sizeof(text), "Brightness %d%%", percent);
    show_overlay(text, kToastShowMs, false);
    save_prefs();
    lv_indev_wait_release(indev);
  }
}

void ui_timer_cb(lv_timer_t*) {
  if (state->page) {
    state->page->update(sk_data().get(state->pages[state->current]),
                        millis());
  }
}

void create_status_dot() {
  const ui::Geometry& g = state->geometry;
  const int32_t size = LV_MAX(8, g.px(0.035f));
  lv_obj_t* dot = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(dot);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(dot, size, size);
  lv_obj_set_pos(dot, g.cx - size / 2, g.cy + g.px(0.925f) - size / 2);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(0x303030), 0);
  lv_obj_set_style_border_width(dot, 1, 0);
  lv_obj_set_style_border_color(dot, lv_color_hex(0x101010), 0);
  state->status_dot = dot;
}

void create_overlay() {
  const ui::Geometry& g = state->geometry;
  lv_obj_t* box = lv_obj_create(lv_layer_top());
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(box, g.px(1.5f), 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_80, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(0x9A9AA0), 0);
  lv_obj_set_style_border_width(box, 2, 0);
  lv_obj_set_style_radius(box, g.px(0.05f), 0);
  lv_obj_set_style_pad_all(box, g.px(0.05f), 0);
  lv_obj_center(box);

  lv_obj_t* label = lv_label_create(box);
  lv_obj_set_style_text_color(label, lv_color_hex(0xF0F0F0), 0);
  lv_obj_set_style_text_font(label, ui::font_for_px(g.px(0.07f)), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_max_width(label, g.px(1.4f), 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

  lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
  state->overlay = box;
  state->overlay_label = label;
  state->overlay_timer = lv_timer_create(hide_overlay, kInfoShowMs, nullptr);
  lv_timer_pause(state->overlay_timer);
}

}  // namespace

void ui_begin(const UiConfig& config) {
  state = new UiState();
  state->config = config;

  lv_display_t* display = lv_display_get_default();
  const int32_t w = lv_display_get_horizontal_resolution(display);
  const int32_t h = lv_display_get_vertical_resolution(display);
  state->geometry = {w / 2, h / 2, (w < h ? w : h) / 2};

  const size_t count = kGaugeCount < kMaxGauges ? kGaugeCount : kMaxGauges;
  const Side side = effective_side(config.side);  // As SkData subscribes
  for (size_t i = 0; i < count; i++) {
    if (shown_on(kGauges[i], side)) {
      state->pages.push_back(i);
    }
  }
  load_prefs();
  if (state->config.set_brightness) {
    state->config.set_brightness(kBrightnessLevels[state->brightness_index]);
  }

  lv_obj_t* screen = lv_screen_active();
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(screen, screen_event_cb, LV_EVENT_GESTURE, nullptr);
  lv_obj_add_event_cb(screen, screen_event_cb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(screen, screen_event_cb, LV_EVENT_SHORT_CLICKED,
                      nullptr);
  lv_obj_add_event_cb(screen, screen_event_cb, LV_EVENT_LONG_PRESSED,
                      nullptr);

  create_status_dot();
  create_overlay();
  state->prefs_timer = lv_timer_create(write_prefs, kPrefsSaveDelayMs, nullptr);
  lv_timer_pause(state->prefs_timer);
  show_page(state->current, false);
  lv_timer_create(ui_timer_cb, kUiPeriodMs, nullptr);
}

void ui_show_notice(const char* text) {
  if (state == nullptr) {
    return;
  }
  state->page.reset();
  show_overlay(text, kInfoShowMs, false);
  lv_timer_pause(state->overlay_timer);  // Stays until the restart
  lv_refr_now(nullptr);
}

void ui_set_status_color(uint8_t r, uint8_t g, uint8_t b) {
  if (state == nullptr || state->status_dot == nullptr) {
    return;
  }
  // Dark phases of a blink pattern show as an unlit LED, not as black. The
  // colour is quantised: SensESP fades its LED every 5 ms, and restyling the
  // dot on every step would keep the display from ever going idle.
  uint32_t rgb = (((uint32_t)r << 16) | ((uint32_t)g << 8) | b) & 0xE0E0E0;
  if ((rgb & 0xC0C0C0) == 0) {  // Every channel below 0x40
    rgb = 0x303030;
  }
  if (rgb == state->status_color) {
    return;
  }
  state->status_color = rgb;
  lv_obj_set_style_bg_color(state->status_dot, lv_color_hex(rgb), 0);
}

}  // namespace skgauges
