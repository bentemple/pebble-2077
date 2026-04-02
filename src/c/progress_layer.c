#include <pebble.h>
#include "constants.h"
#include "settings.h"
#include "globals.h"
#include "progress_layer.h"

// ============================================================
// LAYER AND STATE
// ============================================================
Layer *s_progress_layer;
int s_progress_percent;

// ============================================================
// PROGRESS BAR COLOR
// ============================================================
GColor get_progress_bar_color(int percent) {
  #if defined(PBL_PLATFORM_EMERY)
    switch (settings.progress_color_mode) {
      case COLOR_MODE_DISABLED:
        return color_fg;
      case COLOR_MODE_STATIC:
        return settings.progress_static_color;
      case COLOR_MODE_DYNAMIC:
      default: {
        // O(1) lookup using compile-time LUT
        if (percent <= 0) {
          return (GColor){ .argb = PROGRESS_COLOR_0 };
        }
        if (percent >= 100) {
          return (GColor){ .argb = PROGRESS_COLOR_100 };
        }
        return (GColor){ .argb = s_progress_lut[percent] };
      }
    }
  #else
    return color_fg;
  #endif
}

// ============================================================
// PROGRESS UPDATE PROC (DRAW CALLBACK)
// ============================================================
static void progress_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int width = (s_progress_percent * bounds.size.w) / 100;

  // Draw the background
  graphics_context_set_fill_color(ctx, color_bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Draw the progress bar
  GColor bar_color = get_progress_bar_color(s_progress_percent);
  graphics_context_set_fill_color(ctx, bar_color);
  graphics_fill_rect(ctx, GRect(0, 0, width, bounds.size.h), 0, GCornerNone);
}

// ============================================================
// LOAD PROGRESS LAYER
// ============================================================
void load_progress_layer(int x, int y) {
  s_progress_layer = layer_create(GRect(x + PROGRESS_BAR_OFFSET_X, y + PROGRESS_BAR_OFFSET_Y,
                                         PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT));
  layer_set_update_proc(s_progress_layer, progress_update_proc);
}

// ============================================================
// UNLOAD PROGRESS LAYER
// ============================================================
void unload_progress_layer(void) {
  layer_destroy(s_progress_layer);
}

// ============================================================
// UPDATE PROGRESS
// ============================================================
void update_progress(void) {
  int new_percent = 0;

  // When charging, always show battery level regardless of mode
  if (s_is_charging) {
    new_percent = battery_state_service_peek().charge_percent;
  } else {
    switch (settings.progress_bar_mode) {
      case PROGRESS_MODE_BATTERY:
        new_percent = battery_state_service_peek().charge_percent;
        break;

      case PROGRESS_MODE_STEPS:
        #if defined(PBL_HEALTH)
        {
          // Use cached step count from update_steps()
          int goal = settings.step_goal > 0 ? settings.step_goal : DEFAULT_STEP_GOAL;
          new_percent = (s_last_step_count * 100) / goal;
        }
        #endif
        break;

      case PROGRESS_MODE_SLEEP:
      default:
        #if defined(PBL_HEALTH)
        {
          HealthMetric metric = HealthMetricSleepSeconds;
          time_t start = time_start_of_today();
          time_t end = time(NULL);
          HealthServiceAccessibilityMask mask = health_service_metric_accessible(metric, start, end);
          if (mask & HealthServiceAccessibilityMaskAvailable) {
            int sleep_seconds = (int)health_service_sum_today(metric);
            int goal_seconds = settings.sleep_goal_mins > 0 ? settings.sleep_goal_mins * 60 : DEFAULT_SLEEP_GOAL_MINS * 60;
            new_percent = (sleep_seconds * 100) / goal_seconds;
          }
        }
        #endif
        break;
    }
  }

  if (new_percent > 100) {
    new_percent = 100;
  }

  // Only redraw if changed
  if (new_percent != s_last_progress_percent) {
    s_last_progress_percent = new_percent;
    s_progress_percent = new_percent;
    layer_mark_dirty(s_progress_layer);
  }
}
