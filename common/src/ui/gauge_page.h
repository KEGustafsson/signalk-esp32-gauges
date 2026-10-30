#pragma once

// One full-screen gauge. A page owns its LVGL objects (all children of one
// root object) and deletes them when destroyed, so only the visible page uses
// memory -- the static dial face is a full-screen RGB565 canvas.

#include <lvgl.h>

#include <memory>

#include "../gauge_defs.h"
#include "../gauge_value.h"
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

  // Pages are kept once built (gauge_ui.cpp): only the visible one is drawn.
  void set_visible(bool visible);

  // Replaces the table's unit label where a page shows it next to live data
  // (see unit_for_path()).
  virtual void set_unit_label(const char* unit) { (void)unit; }

 protected:
  // A transparent full-screen object whose draw callback paints the moving
  // parts (needle, compass card, horizon). Invalidate only the areas that
  // change; the canvas underneath supplies the static face.
  lv_obj_t* create_overlay(lv_event_cb_t draw_cb);

  // Dims `obj` while its data is stale, so a parked compass card or level
  // boat symbol cannot be mistaken for a live reading. LVGL applies a style
  // opacity only to descriptors it initialises itself, so the overlay's draw
  // callback must copy `layer->opa` (set from this style while the object
  // draws) into every descriptor it should dim. Changing the style
  // invalidates `obj`, so the dimming shows at once.
  static void set_stale_look(lv_obj_t* obj, bool stale);
  // The same for an overlay whose strokes overlap (the boat symbol): LVGL
  // renders it on a layer and blends that once, so overlaps do not show as
  // darker seams. The layer costs a little memory, only while stale.
  static void set_stale_look_layered(lv_obj_t* obj, bool stale);

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

// True if a box around (x1,y1)-(x2,y2), grown by `pad`, touches the layer's
// clip area. Draw callbacks run once per partial-render strip; skipping what
// lies outside it saves allocating draw tasks LVGL would discard anyway.
bool in_clip(const lv_layer_t* layer, float x1, float y1, float x2, float y2,
             float pad);

}  // namespace ui
}  // namespace skgauges
