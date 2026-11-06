#pragma once

// The radio halo (MOCK-UP; see crew_view.h): a ring over the bezel that
// lights while someone talks, with their name set along the top arc, and a
// small radio pip beside the status dot the rest of the time. It sits on the
// top layer, above whatever page is showing: the radio never takes a gauge
// page away, and the band stays outside the dial's ticks (the face ends at
// 0.85 R), so no reading is covered.

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

  const Geometry g_;
  lv_obj_t* obj_;
  CrewView view_;
  bool lit_ = false;
  bool dashed_ = false;
  uint32_t accent_ = 0;
  ArcText top_;
  ArcText bottom_;
};

}  // namespace ui
}  // namespace skgauges
