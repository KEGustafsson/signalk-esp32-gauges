#pragma once

// Classic steel radial gauge: chrome bezel, 300-degree scale with radial
// numbers and coloured zones, red needle with a steel cap and an LCD readout
// under the pivot (engine rpm, speed, temperatures, ...).

#include "gauge_page.h"
#include "lcd_readout.h"

namespace skgauges {
namespace ui {

class RadialGaugePage : public GaugePage {
 public:
  RadialGaugePage(lv_obj_t* parent, const GaugeDef& def,
                  const Geometry& geometry);

  void update(const GaugeValue& value, uint32_t now_ms) override;

  // Scale geometry, shared with the needle stop pins.
  static constexpr float kStartAngle = -150.0f;
  static constexpr float kSweep = 300.0f;

 private:
  float value_to_angle(float value) const;
  void draw_face();
  void needle_area(float angle, lv_area_t* area) const;
  void draw_needle(lv_layer_t* layer, float angle) const;
  static void needle_draw_cb(lv_event_t* e);

  lv_obj_t* needle_obj_;
  LcdReadout lcd_;
  float shown_angle_;
};

}  // namespace ui
}  // namespace skgauges
