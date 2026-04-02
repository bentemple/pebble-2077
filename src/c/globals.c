#include <pebble.h>
#include "globals.h"

// ============================================================
// GLOBAL COLORS
// ============================================================
GColor color_fg = { .argb = GColorWhiteARGB8 };
GColor color_bg = { .argb = GColorBlackARGB8 };

// ============================================================
// GLOBAL FONTS
// ============================================================
GFont s_time_font;
GFont s_date_font;
GFont s_text_font;
#if defined(PBL_PLATFORM_EMERY)
GFont s_time_font_bold;
GFont s_time_font_regular;
#endif

// ============================================================
// EMERY-ONLY: EFFECTIVE COLORS (pre-computed)
// ============================================================
#if defined(PBL_PLATFORM_EMERY)
GColor s_effective_date_color = { .argb = GColorWhiteARGB8 };
GColor s_effective_colon_color = { .argb = GColorWhiteARGB8 };
GColor s_effective_temp_color = { .argb = GColorWhiteARGB8 };
GColor s_effective_temp_high_color = { .argb = GColorWhiteARGB8 };
GColor s_effective_condition_color = { .argb = GColorWhiteARGB8 };
GColor s_effective_progress_color = { .argb = GColorWhiteARGB8 };
#endif

// ============================================================
// CHARGING STATE
// ============================================================
bool s_is_charging = false;

// ============================================================
// CACHE INVALIDATION FLAGS
// ============================================================
int s_last_progress_percent = -1;
int s_last_day = -1;
int s_last_minute = -1;
int s_last_hour = -1;
int s_last_step_count = -1;
int s_last_temperature = -999;
int s_last_temperature_high = -999;
bool s_last_steps_visible = false;

// ============================================================
// UPTIME/SLEEP TRACKING FLAGS
// ============================================================
bool s_custom_needs_uptime = false;
bool s_bottom_needs_uptime = false;
bool s_any_needs_uptime = false;
bool s_needs_sleep_tracking = false;

// ============================================================
// INVALIDATE ALL CACHES
// ============================================================
void invalidate_all_caches(void) {
  s_last_minute = -1;
  s_last_hour = -1;
  s_last_day = -1;
  s_last_progress_percent = -1;
  s_last_step_count = -1;
  s_last_temperature = -999;
  s_last_temperature_high = -999;
  s_last_steps_visible = false;
}
