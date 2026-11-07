#pragma once

// The radio halo: a ring over the bezel that
// lights while someone talks, with their name set along the top arc, and a
// small radio pip beside the status dot the rest of the time. It sits on the
// top layer, above whatever page is showing: the radio never takes a gauge
// page away, and the band stays outside the dial's ticks (the face ends at
// 0.85 R), so no reading is covered. It redraws only when what it draws
// changes, and then only the band and the pip, never the page beneath.

#include <lvgl.h>

#include "crew_draw.h"
#include "crew_view.h"
#include "draw_utils.h"

namespace skgauges {
namespace ui {

class RadioHalo {
 public:
  RadioHalo(lv_obj_t* parent, const Geometry& geometry);
  ~RadioHalo();
  RadioHalo(const RadioHalo&) = delete;
  RadioHalo& operator=(const RadioHalo&) = delete;

  void set(const CrewView& view);

 private:
  static void draw_cb(lv_event_t* e);
  void draw(lv_layer_t* layer) const;
  void invalidate_drawn();

  const Geometry g_;
  lv_obj_t* obj_;
  CrewView view_;
  bool lit_ = false;
  bool dashed_ = false;
  uint32_t accent_ = 0;
  ArcText top_;
  ArcText bottom_;
  // What was drawn last: a view change that leaves all of it alone (a
  // member's bars, the volume) redraws nothing.
  std::string top_text_;
  std::string bottom_text_;
  RadioState pip_state_ = RadioState::kOff;
  bool pip_muted_ = false;
  bool drawn_once_ = false;
};

}  // namespace ui
}  // namespace skgauges
