#include <pebble.h>
#include "constants.h"
#include "settings.h"
#include "globals.h"
#include "custom_text.h"

// ============================================================
// SETTINGS INSTANCE
// ============================================================
ClaySettings settings;

// ============================================================
// CACHED DERIVED VALUES
// ============================================================
int s_cached_temp_f = 0;
int s_cached_temp_high_f = 0;
bool s_is_24h_style = true;
const char *s_time_format = "%H:%M";

// ============================================================
// DEFAULT SETTINGS
// ============================================================
void default_settings(void) {
  settings.show_steps = true;
  settings.show_weather = true;
  settings.weather_use_metric = false;
  settings.skip_location = false;
  settings.hour_vibe = false;
  settings.disconnect_alert = true;
  settings.temperature = -999;  // Sentinel: no weather data yet
  settings.temperature_high = -999;
  settings.temperature_high_tomorrow = -999;
  settings.sunset_hour = 18;  // Default 6pm
  settings.progress_bar_mode = PROGRESS_MODE_SLEEP;
  settings.step_goal = DEFAULT_STEP_GOAL;
  settings.sleep_goal_mins = DEFAULT_SLEEP_GOAL_MINS;
  settings.progress_color_mode = COLOR_MODE_DYNAMIC;
  settings.progress_static_color = GColorWhite;
  settings.temperature_color_mode = COLOR_MODE_DYNAMIC;
  settings.temperature_static_color = GColorWhite;
  settings.weather_color_mode = COLOR_MODE_DYNAMIC;
  settings.weather_static_color = GColorWhite;
  settings.colorize_date = true;
  settings.colorize_colon = true;
  settings.bold_hours = true;
  settings.date_color = GColorWhite;
  settings.colon_color = GColorWhite;
  strncpy(settings.custom_text, "PBL_%m%U%j", sizeof(settings.custom_text));
  strncpy(settings.bottom_text, "%Y.%m.%d", sizeof(settings.bottom_text));
  strncpy(settings.condition, "", sizeof(settings.condition));
}

// ============================================================
// CACHE DERIVED VALUES
// ============================================================
void cache_derived_values(void) {
  s_cached_temp_f = settings.temperature * 9 / 5 + 32;
  s_cached_temp_high_f = settings.temperature_high * 9 / 5 + 32;
  s_is_24h_style = clock_is_24h_style();
  s_time_format = s_is_24h_style ? "%H:%M" : "%I:%M";
}

// ============================================================
// LOAD SETTINGS
// ============================================================
void load_settings(void) {
  default_settings();
  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
  cache_derived_values();
  recalculate_update_periods();
}

// ============================================================
// SAVE SETTINGS
// ============================================================
void save_settings(void) {
  persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));
  invalidate_all_caches();
}
