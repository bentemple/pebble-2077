#pragma once
#include <pebble.h>

// ============================================================
// SETTINGS STRUCTURE
// ============================================================
typedef struct ClaySettings {
  bool show_steps;
  bool show_weather;
  bool weather_use_metric;
  bool skip_location;
  bool hour_vibe;
  bool disconnect_alert;
  int temperature;
  int temperature_high;
  int temperature_high_tomorrow;
  int sunset_hour;
  int progress_bar_mode;  // 0=battery, 1=steps, 2=sleep
  int step_goal;
  int sleep_goal_mins;
  // Color options (Emery only)
  int progress_color_mode;
  int temperature_color_mode;
  int weather_color_mode;
  GColor progress_static_color;
  GColor temperature_static_color;
  GColor weather_static_color;
  bool colorize_date;
  bool colorize_colon;
  bool bold_hours;
  GColor date_color;
  GColor colon_color;
  char custom_text[32];
  char bottom_text[32];
  char condition[32];
} ClaySettings;

// Global settings instance (defined in settings.c)
extern ClaySettings settings;

// Cached derived values (defined in settings.c)
extern int s_cached_temp_f;
extern int s_cached_temp_high_f;
extern bool s_is_24h_style;
extern const char *s_time_format;

// ============================================================
// SETTINGS FUNCTIONS
// ============================================================
void default_settings(void);
void load_settings(void);
void save_settings(void);
void cache_derived_values(void);
