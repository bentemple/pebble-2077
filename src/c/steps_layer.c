#include <pebble.h>
#include "constants.h"
#include "settings.h"
#include "globals.h"
#include "steps_layer.h"
#include "refresh.h"

// ============================================================
// LAYER
// ============================================================
TextLayer *s_step_layer;

// ============================================================
// LOAD STEP LAYER
// ============================================================
void load_step_layer(int y) {
  s_step_layer = text_layer_create(GRect(MARGIN_SIZE, y, INFO_LAYER_WIDTH, TEXT_HEIGHT));
  text_layer_set_background_color(s_step_layer, GColorClear);
  text_layer_set_text_color(s_step_layer, color_fg);
  text_layer_set_font(s_step_layer, s_text_font);
  // Hide initially so it doesn't show on unsupported devices
  layer_set_hidden(text_layer_get_layer(s_step_layer), true);
}

// ============================================================
// UNLOAD STEP LAYER
// ============================================================
void unload_step_layer(void) {
  text_layer_destroy(s_step_layer);
}

// ============================================================
// FETCH STEP COUNT
// ============================================================
// Last time the health service was actually read.
static time_t s_last_step_fetch = 0;

void fetch_step_count(void) {
  #if defined(PBL_HEALTH)
  time_t end = time(NULL);

  // Rate limited to once a minute. health_handler calls this on every
  // MovementUpdate, which fires every few seconds while the user is
  // walking - two health syscalls each time, for a number that is only
  // rendered once a minute.
  //
  // The negative-count check is the escape hatch: invalidate_all_caches()
  // resets s_last_step_count to -1, and that has to force a real read or
  // the step layer would render nothing until the interval expires.
  if (s_last_step_count >= 0 && !steps_should_refresh(s_last_step_fetch, end)) {
    return;
  }
  s_last_step_fetch = end;

  HealthMetric metric = HealthMetricStepCount;
  time_t start = time_start_of_today();

  HealthServiceAccessibilityMask mask = health_service_metric_accessible(metric, start, end);

  if (mask & HealthServiceAccessibilityMaskAvailable) {
    s_last_step_count = (int)health_service_sum_today(metric);
  } else {
    s_last_step_count = 0;
  }
  #endif
}

// ============================================================
// UPDATE STEPS
// ============================================================
void update_steps(void) {
  #if defined(PBL_HEALTH)
  // Always fetch to keep cache current (needed for progress bar too)
  int old_count = s_last_step_count;
  fetch_step_count();

  if (settings.show_steps) {
    // Only update text layer if step count changed
    if (s_last_step_count != old_count) {
      static char s_step_buffer[16];
      snprintf(s_step_buffer, sizeof(s_step_buffer), "STEPS: %d", s_last_step_count);
      text_layer_set_text(s_step_layer, s_step_buffer);
    }
    layer_set_hidden(text_layer_get_layer(s_step_layer), false);
  } else {
    layer_set_hidden(text_layer_get_layer(s_step_layer), true);
  }
  #endif
}
