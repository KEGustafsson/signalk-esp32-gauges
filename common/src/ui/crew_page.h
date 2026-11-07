#pragma once

// The Crew page (see crew_view.h): the channel's roster as a
// compass, in the classic steel style. Each member is a pip on a ring at an
// evenly spaced bearing with their name and signal bars; the talker's pip
// glows. The centre holds the channel name, the head count in the gauges'
// LCD and one status line; the volume sits on the bottom arc.
//
// While the radio runs, the arc is a control (crew_tap.h): a tap left of
// its label lowers, right of it raises, one step a tap (the radio's
// core/levels.h); a tap on the label switches the arc to the mic gain and
// back. Ten seconds after the last tap it shows the volume again.
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

  // How long the arc shows the mic gain after the last tap.
  static constexpr uint32_t kMicShownMs = 10000;

  // A tap at (x, y), screen px. True when it was the arc's and did
  // something: a step went to the radio (crew_adjust), or the arc
  // switched. The caller then reads the view again (set()).
  bool tap(int32_t x, int32_t y, uint32_t now_ms);
  // Back to the volume kMicShownMs after the last tap; from the UI timer.
  void tick(uint32_t now_ms);
  // What the arc shows, for a toast: "Speaker volume 75", "Mic gain 30 dB".
  std::string level_text() const;

 private:
  void build();
  bool showing_mic() const;
  static void draw_cb(lv_event_t* e);
  void draw(lv_layer_t* layer) const;
  // step_deg: the angle to its neighbours (0 when it is alone), which sets
  // how wide its name may be.
  void draw_member(lv_layer_t* layer, const CrewMember& m, float angle,
                   float step_deg) const;
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
  CrewLevel shown_ = CrewLevel::kVolume;  // What the arc shows
  uint32_t tapped_ms_ = 0;
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
  bool tap(int32_t x, int32_t y, uint32_t now_ms, std::string* toast) override;

 private:
  CrewPage page_;
  CrewView last_;
  uint32_t polled_ms_ = 0;
  bool first_ = true;
};

}  // namespace ui
}  // namespace skgauges
