#pragma once

// One full-screen gauge. A page owns its LVGL objects (all children of one
// root object) and deletes them when destroyed, so only the visible page uses
// memory -- the static dial face is a full-screen RGB565 canvas.

#include <lvgl.h>

#include <memory>

#include "../gauge_defs.h"
#include "../sk_data.h"
#include "draw_utils.h"

namespace skgauges {
namespace ui {

class GaugePage {
 public:
  GaugePage(lv_obj_t* parent, const GaugeDef& def, const Geometry& geometry);
  virtual ~GaugePage();
  GaugePage(const GaugePage&) = delete;
  GaugePage& operator=(const GaugePage&) = delete;

  // Called from the UI timer (~30 ms) with the latest value for this page.
  virtual void update(const GaugeValue& value, uint32_t now_ms) = 0;

 protected:
  // A transparent full-screen object whose draw callback paints the moving
  // parts (needle, compass card, horizon). Invalidate only the areas that
  // change; the canvas underneath supplies the static face.
  lv_obj_t* create_overlay(lv_event_cb_t draw_cb);

  const GaugeDef& def_;
  const Geometry g_;
  lv_obj_t* root_;
  lv_obj_t* face_;  // Static dial face canvas
};

std::unique_ptr<GaugePage> create_page(lv_obj_t* parent, const GaugeDef& def,
                                       const Geometry& geometry);

// Moves `shown` toward `target` with exponential smoothing; returns true if
// it moved enough to need a redraw. For angles, pass wrap = true to take the
// shortest way round.
bool smooth_toward(float* shown, float target, float factor, float epsilon,
                   bool wrap = false);

}  // namespace ui
}  // namespace skgauges
