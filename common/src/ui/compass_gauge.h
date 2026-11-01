#pragma once

// Steel compass: a card that rotates under a fixed red lubber mark, cardinal
// and numeric bearings rotated with the card, and an LCD heading readout.

#include <memory>
#include <vector>

#include "gauge_page.h"
#include "lcd_readout.h"

namespace skgauges {
namespace ui {

class CompassGaugePage : public GaugePage {
 public:
  CompassGaugePage(lv_obj_t* parent, const GaugeDef& def,
                   const Geometry& geometry);

  void update(const GaugeValue& value, uint32_t now_ms) override;

 private:
  struct CardLabel {
    float bearing;
    float radius_frac;
    std::unique_ptr<TextImage> image;
  };

  void draw_face();
  void draw_card(lv_layer_t* layer) const;
  static void card_draw_cb(lv_event_t* e);

  std::vector<CardLabel> labels_;
  lv_obj_t* card_obj_;
  LcdReadout lcd_;
  float shown_heading_;
  float card_target_;  // The heading the card turns to (kCardDeadbandDeg)
  int32_t ring_in_ = 0;  // The ring the marks sweep, from the centre
  int32_t ring_out_ = 0;
};

}  // namespace ui
}  // namespace skgauges
