#pragma once
#include <pebble.h>

// ============================================================
// GLOBAL COLORS
// ============================================================
extern GColor color_fg;
extern GColor color_bg;

// ============================================================
// GLOBAL FONTS
// ============================================================
extern GFont s_time_font;
extern GFont s_date_font;
extern GFont s_text_font;
#if defined(PBL_PLATFORM_EMERY)
extern GFont s_time_font_bold;
extern GFont s_time_font_regular;
#endif

// ============================================================
// EMERY-ONLY: EFFECTIVE COLORS (pre-computed)
// ============================================================
#if defined(PBL_PLATFORM_EMERY)
extern GColor s_effective_date_color;
extern GColor s_effective_colon_color;
extern GColor s_effective_temp_color;
extern GColor s_effective_temp_high_color;
extern GColor s_effective_condition_color;
extern GColor s_effective_progress_color;
#endif

// ============================================================
// CHARGING STATE
// ============================================================
extern bool s_is_charging;

// ============================================================
// CACHE INVALIDATION FLAGS
// ============================================================
extern int s_last_progress_percent;
extern int s_last_day;
extern int s_last_minute;
extern int s_last_hour;
extern int s_last_step_count;
extern int s_last_temperature;
extern int s_last_temperature_high;
extern bool s_last_steps_visible;

// ============================================================
// UPTIME/SLEEP TRACKING FLAGS
// ============================================================
extern bool s_custom_needs_uptime;
extern bool s_bottom_needs_uptime;
extern bool s_any_needs_uptime;
extern bool s_needs_sleep_tracking;

// ============================================================
// HELPER FUNCTION TO INVALIDATE ALL CACHES
// ============================================================
void invalidate_all_caches(void);
