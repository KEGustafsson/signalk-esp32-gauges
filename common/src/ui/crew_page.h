#pragma once

// The Crew page (MOCK-UP; see crew_view.h): the channel's roster as a
// compass, in the classic steel style. Each member is a pip on a ring at an
// evenly spaced bearing with their name and signal bars; the talker's pip
// glows. The centre holds the channel name, the head count in the gauges'
// LCD and one status line; the volume sits on the bottom arc.
//
// `station`: the standalone intercom's screen, where the centre is the
// talk disc instead of the LCD (hold to talk).

#include <lvgl.h>

#include <memory>

#include "../gauge_defs.h"
#include "../gauge_value.h"
#include "crew_draw.h"
#include "crew_view.h"
#include "draw_utils.h"
#include "gauge_page.h"
#include "lcd_readout.h"

namespace skgauges {
namespace ui {

class CrewPage {
 public:
  CrewPage(lv_obj_t* parent, const Geometry& geometry, bool station);
  // Drawing into a root and a full-screen face canvas someone else owns
  // (a GaugePage's): nothing of its own to delete.
  CrewPage(lv_obj_t* root, lv_obj_t* face, const Geometry& geometry,
           bool station);
  ~CrewPage();
  CrewPage(const CrewPage&) = delete;
  CrewPage& operator=(const CrewPage&) = delete;

  void set(const CrewView& view);

 private:
  void build();
  static void draw_cb(lv_event_t* e);
  void draw(lv_layer_t* layer) const;
  void draw_member(lv_layer_t* layer, const CrewMember& m, float angle) const;
  void draw_volume(lv_layer_t* layer) const;
  void draw_disc(lv_layer_t* layer) const;

  const Geometry g_;
  const bool station_;
  lv_obj_t* root_;
  lv_obj_t* face_;
  bool owns_root_;
  std::unique_ptr<LcdReadout> lcd_;
  lv_obj_t* dynamic_;
  CrewView view_;
  ArcText title_;  // Station: the channel name engraved in the bezel
};

// The Crew page as one of the gauge pages (page style "Crew radio"): it
// reads the radio's view (crew_view.h) four times a second and redraws
// only when it changed. The Signal K value it is handed is unused.
class CrewGaugePage : public GaugePage {
 public:
  static constexpr uint32_t kPollMs = 250;

  CrewGaugePage(lv_obj_t* parent, const GaugeDef& def,
                const Geometry& geometry);
  void update(const GaugeValue& value, uint32_t now_ms) override;

 private:
  CrewPage page_;
  CrewView last_;
  uint32_t polled_ms_ = 0;
  bool first_ = true;
};

}  // namespace ui
}  // namespace skgauges
