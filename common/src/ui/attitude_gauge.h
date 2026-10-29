#pragma once

// Sea-horizon heel/trim indicator for navigation.attitude.
//
//  - Top arc: heel scale (+-40 deg) with green/yellow/red bands and a red
//    pointer that swings toward the low side (port heel -> left).
//  - Window: a fixed, level sea horizon with a trim ladder, and a boat symbol
//    (seen from astern) that heels with the boat and rises/sinks with trim.
//  - LCDs: heel with PORT/STBD, and TRIM (pitch, bow up positive).

#include <memory>

#include "gauge_page.h"
#include "lcd_readout.h"

namespace skgauges {
namespace ui {

class AttitudeGaugePage : public GaugePage {
 public:
  AttitudeGaugePage(lv_obj_t* parent, const GaugeDef& def,
                    const Geometry& geometry);

  void update(const GaugeValue& value, uint32_t now_ms) override;

 private:
  float heel_to_angle(float heel_deg) const;
  void draw_face();
  void draw_dynamic(lv_layer_t* layer) const;
  void draw_window(lv_layer_t* layer) const;
  void draw_boat(lv_layer_t* layer) const;
  void invalidate_dynamic();
  static void draw_cb(lv_event_t* e);

  lv_obj_t* overlay_;
  LcdReadout heel_lcd_;
  LcdReadout trim_lcd_;
  float shown_roll_;
  float shown_pitch_;
};

}  // namespace ui
}  // namespace skgauges
