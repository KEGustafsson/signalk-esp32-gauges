#include "gauge_page.h"

#include <math.h>

#include "attitude_gauge.h"
#include "compass_gauge.h"
#include "radial_gauge.h"

namespace skgauges {
namespace ui {

GaugePage::GaugePage(lv_obj_t* parent, const GaugeDef& def,
                     const Geometry& geometry)
    : def_(def), g_(geometry) {
  root_ = lv_obj_create(parent);
  lv_obj_remove_style_all(root_);
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(root_, lv_pct(100), lv_pct(100));
  face_ = create_canvas(root_, lv_obj_get_width(parent),
                        lv_obj_get_height(parent));
}

GaugePage::~GaugePage() { lv_obj_delete(root_); }

lv_obj_t* GaugePage::create_overlay(lv_event_cb_t draw_cb) {
  lv_obj_t* obj = lv_obj_create(root_);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
  lv_obj_add_event_cb(obj, draw_cb, LV_EVENT_DRAW_MAIN, this);
  return obj;
}

void GaugePage::set_stale_look(lv_obj_t* obj, bool stale) {
  const lv_opa_t opa = stale ? LV_OPA_40 : LV_OPA_COVER;
  if (lv_obj_get_style_opa(obj, LV_PART_MAIN) != opa) {
    lv_obj_set_style_opa(obj, opa, 0);
  }
}

void GaugePage::set_stale_look_layered(lv_obj_t* obj, bool stale) {
  const lv_opa_t opa = stale ? LV_OPA_40 : LV_OPA_COVER;
  if (lv_obj_get_style_opa_layered(obj, LV_PART_MAIN) != opa) {
    lv_obj_set_style_opa_layered(obj, opa, 0);
  }
}

bool in_clip(const lv_layer_t* layer, float x1, float y1, float x2, float y2,
             float pad) {
  const lv_area_t& clip = layer->_clip_area;
  return fmaxf(x1, x2) + pad >= clip.x1 && fminf(x1, x2) - pad <= clip.x2 &&
         fmaxf(y1, y2) + pad >= clip.y1 && fminf(y1, y2) - pad <= clip.y2;
}

std::unique_ptr<GaugePage> create_page(lv_obj_t* parent, const GaugeDef& def,
                                       const Geometry& geometry) {
  switch (def.kind) {
    case GaugeKind::kCompass:
      return std::unique_ptr<GaugePage>(
          new CompassGaugePage(parent, def, geometry));
    case GaugeKind::kAttitude:
      return std::unique_ptr<GaugePage>(
          new AttitudeGaugePage(parent, def, geometry));
    case GaugeKind::kRadial:
    default:
      return std::unique_ptr<GaugePage>(
          new RadialGaugePage(parent, def, geometry));
  }
}

bool smooth_toward(float* shown, float target, float factor, float epsilon,
                   bool wrap) {
  if (isnan(*shown)) {
    *shown = target;
    return true;
  }
  float diff = target - *shown;
  if (wrap) {
    diff = fmodf(diff + 540.0f, 360.0f) - 180.0f;
  }
  if (fabsf(diff) < epsilon) {
    return false;
  }
  float step = fabsf(diff) * factor < epsilon ? diff : diff * factor;
  *shown += step;
  if (wrap) {
    *shown = fmodf(*shown + 360.0f, 360.0f);
  }
  return true;
}

}  // namespace ui
}  // namespace skgauges
