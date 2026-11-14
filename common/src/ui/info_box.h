// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The box in the middle of the screen: the info a swipe down shows, with a
// row of Day / Dusk / Night buttons under it, and the short notes (a page's
// name, the brightness) without them. Pure LVGL, so the preview can draw it;
// when it hides and what it says is the page manager's (gauge_ui.cpp).

#include <lvgl.h>

#include "../colour_mode.h"
#include "draw_utils.h"

namespace skgauges {
namespace ui {

class InfoBox {
 public:
  // on_mode: a mode button was tapped (also the one already chosen).
  InfoBox(lv_obj_t* parent, const Geometry& geometry,
          void (*on_mode)(ColourMode));
  ~InfoBox();
  InfoBox(const InfoBox&) = delete;
  InfoBox& operator=(const InfoBox&) = delete;

  // Shows `text`, with the mode buttons when `with_modes`.
  void show(const char* text, bool with_modes);
  void hide();
  // Draws itself again in the current colour mode (colour_mode()), with
  // its button lit.
  void restyle();

 private:
  static void button_cb(lv_event_t* e);

  const Geometry g_;
  void (*on_mode_)(ColourMode);
  lv_obj_t* box_;
  lv_obj_t* label_;
  lv_obj_t* row_;
  lv_obj_t* buttons_[kColourModeCount];
  lv_obj_t* button_labels_[kColourModeCount];
};

}  // namespace ui
}  // namespace skgauges
