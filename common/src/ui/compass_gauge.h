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
  void set_unit_label(const char* unit) override { lcd_.set_prefix(unit); }

 private:
  struct CardLabel {
    float bearing;
    float radius_frac;
    std::unique_ptr<TextImage> image;
  };

  void draw_face();
  void draw_card(lv_layer_t* layer) const;
  void invalidate_card();
  static void card_draw_cb(lv_event_t* e);

  std::vector<CardLabel> labels_;
  lv_obj_t* card_obj_;
  LcdReadout lcd_;
  float shown_heading_;
};

}  // namespace ui
}  // namespace skgauges
