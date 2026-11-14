// SPDX-License-Identifier: EUPL-1.2
#include "info_box.h"

namespace skgauges {
namespace ui {

namespace {
constexpr uint32_t kBoxColor = 0x000000;
constexpr uint32_t kBorderColor = 0x9A9AA0;
constexpr uint32_t kTextColor = 0xF0F0F0;
constexpr uint32_t kButtonColor = 0x2A2A2E;  // A mode not chosen
constexpr uint32_t kChosenColor = 0xE6E6E6;  // The chosen one, dark text
constexpr uint32_t kChosenText = 0x101012;
// Sizes, as fractions of the radius: each button a finger's width tall on
// the 2.1" (43 px), three side by side inside the box's widest.
constexpr float kButtonW = 0.38f;
constexpr float kButtonH = 0.18f;
constexpr float kButtonGap = 0.04f;
}  // namespace

InfoBox::InfoBox(lv_obj_t* parent, const Geometry& geometry,
                 void (*on_mode)(ColourMode))
    : g_(geometry), on_mode_(on_mode) {
  box_ = lv_obj_create(parent);
  // Not clickable: a tap on the box goes to the screen, which closes it.
  lv_obj_remove_flag(box_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(box_, g_.px(1.5f), 0);
  lv_obj_set_style_bg_opa(box_, LV_OPA_80, 0);
  lv_obj_set_style_border_width(box_, 2, 0);
  lv_obj_set_style_radius(box_, g_.px(0.05f), 0);
  lv_obj_set_style_pad_all(box_, g_.px(0.05f), 0);
  lv_obj_set_style_pad_row(box_, g_.px(0.05f), 0);
  lv_obj_set_flex_flow(box_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_center(box_);

  const lv_font_t* font = font_for_px(g_.px(0.07f));
  label_ = lv_label_create(box_);
  lv_obj_set_style_text_font(label_, font, 0);
  lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_max_width(label_, g_.px(1.4f), 0);
  lv_label_set_long_mode(label_, LV_LABEL_LONG_WRAP);

  row_ = lv_obj_create(box_);
  lv_obj_remove_style_all(row_);
  lv_obj_remove_flag(row_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(row_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_column(row_, g_.px(kButtonGap), 0);
  lv_obj_set_flex_flow(row_, LV_FLEX_FLOW_ROW);
  for (int i = 0; i < kColourModeCount; i++) {
    lv_obj_t* b = lv_obj_create(row_);
    lv_obj_remove_style_all(b);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    // A swipe that starts on a button still closes the box or turns the
    // page: the gesture goes on to the screen.
    lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(b, g_.px(kButtonW), g_.px(kButtonH));
    lv_obj_set_style_radius(b, g_.px(0.04f), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_add_event_cb(b, button_cb, LV_EVENT_CLICKED, this);
    lv_obj_t* text = lv_label_create(b);
    lv_obj_set_style_text_font(text, font, 0);
    lv_label_set_text(text, colour_mode_name((ColourMode)i));
    lv_obj_center(text);
    buttons_[i] = b;
    button_labels_[i] = text;
  }
  restyle();
  lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN);
}

InfoBox::~InfoBox() { lv_obj_delete(box_); }

void InfoBox::show(const char* text, bool with_modes) {
  lv_label_set_text(label_, text);
  if (with_modes) {
    lv_obj_remove_flag(row_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(row_, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_remove_flag(box_, LV_OBJ_FLAG_HIDDEN);
}

void InfoBox::hide() { lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN); }

void InfoBox::restyle() {
  lv_obj_set_style_bg_color(box_, ink(kBoxColor), 0);
  lv_obj_set_style_border_color(box_, ink(kBorderColor), 0);
  lv_obj_set_style_text_color(label_, ink(kTextColor), 0);
  const int chosen = (int)colour_mode();
  for (int i = 0; i < kColourModeCount; i++) {
    const bool lit = i == chosen;
    lv_obj_set_style_bg_color(buttons_[i],
                              ink(lit ? kChosenColor : kButtonColor), 0);
    lv_obj_set_style_border_color(buttons_[i], ink(kBorderColor), 0);
    lv_obj_set_style_text_color(button_labels_[i],
                                ink(lit ? kChosenText : kTextColor), 0);
  }
}

void InfoBox::button_cb(lv_event_t* e) {
  InfoBox* self = static_cast<InfoBox*>(lv_event_get_user_data(e));
  lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  for (int i = 0; i < kColourModeCount; i++) {
    if (self->buttons_[i] == target && self->on_mode_ != nullptr) {
      self->on_mode_((ColourMode)i);
      return;
    }
  }
}

}  // namespace ui
}  // namespace skgauges
