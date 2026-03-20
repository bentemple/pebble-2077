#include <pebble.h>
#include "constants.h"
#include "settings.h"
#include "globals.h"
#include "utils.h"
#include "progress_layer.h"
#include "steps_layer.h"
#include "hud_layer.h"
#include "weather_layer.h"
#include "time_layer.h"
#include "custom_text.h"
#include "callbacks.h"

// ============================================================
// WINDOW
// ============================================================
static Window *s_main_window;

// ============================================================
// FONT LOADING
// ============================================================
static void load_fonts(void) {
  #if defined(PBL_PLATFORM_EMERY)
    s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_86));
    // Only load bold and regular fonts if bold setting is enabled
    if (settings.bold_hours) {
      s_time_font_bold = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_86_SEMI_BOLD));
      s_time_font_regular = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_86_REGULAR));
    }
    s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_25));
    s_text_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ORBITRON_17));
  #else
    s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_58));
    s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_24));
    s_text_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ORBITRON_12));
  #endif
}

static void unload_fonts(void) {
  fonts_unload_custom_font(s_time_font);
  #if defined(PBL_PLATFORM_EMERY)
  if (s_time_font_bold) {
    fonts_unload_custom_font(s_time_font_bold);
  }
  if (s_time_font_regular) {
    fonts_unload_custom_font(s_time_font_regular);
  }
  #endif
  fonts_unload_custom_font(s_date_font);
  fonts_unload_custom_font(s_text_font);
}

// ============================================================
// EMERY COLOR FUNCTIONS
// ============================================================
#if defined(PBL_PLATFORM_EMERY)
static void cache_effective_colors(void) {
  s_effective_colon_color = settings.colorize_colon ? settings.colon_color : color_fg;
  s_effective_date_color = settings.colorize_date ? settings.date_color : color_fg;

  // Determine effective high temp based on sunset
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  bool use_tomorrow = (t->tm_hour >= settings.sunset_hour);
  int effective_high = use_tomorrow ? settings.temperature_high_tomorrow : settings.temperature_high;
  int effective_high_f = effective_high * 9 / 5 + 32;

  // Temperature color based on mode
  switch (settings.temperature_color_mode) {
    case COLOR_MODE_DISABLED:
      s_effective_temp_color = color_fg;
      s_effective_temp_high_color = color_fg;
      break;
    case COLOR_MODE_STATIC:
      s_effective_temp_color = settings.temperature_static_color;
      s_effective_temp_high_color = settings.temperature_static_color;
      break;
    case COLOR_MODE_DYNAMIC:
    default:
      s_effective_temp_color = get_temperature_color(s_cached_temp_f);
      s_effective_temp_high_color = get_temperature_color(effective_high_f);
      break;
  }

  // Weather condition color based on mode
  switch (settings.weather_color_mode) {
    case COLOR_MODE_DISABLED:
      s_effective_condition_color = color_fg;
      break;
    case COLOR_MODE_STATIC:
      s_effective_condition_color = settings.weather_static_color;
      break;
    case COLOR_MODE_DYNAMIC:
    default:
      s_effective_condition_color = get_condition_color(settings.condition);
      break;
  }
}

void update_accent_colors(void) {
  cache_effective_colors();
  text_layer_set_text_color(s_time_colon_layer, s_effective_colon_color);
  text_layer_set_text_color(s_date_layer, s_effective_date_color);
  text_layer_set_text_color(s_temperature_layer, s_effective_temp_color);
  text_layer_set_text_color(s_condition_layer, s_effective_condition_color);
  // Mark progress layer dirty to redraw with new color
  layer_mark_dirty(s_progress_layer);
}
#endif

// ============================================================
// INBOX RECEIVED CALLBACK
// ============================================================
static void inbox_received_callback(DictionaryIterator *it, void *ctx) {
  Tuple *temperature_t = dict_find(it, MESSAGE_KEY_TEMPERATURE);
  Tuple *temperature_high_t = dict_find(it, MESSAGE_KEY_TEMPERATURE_HIGH);
  Tuple *conditions_t = dict_find(it, MESSAGE_KEY_CONDITIONS);
  if (temperature_t && conditions_t) {
    settings.temperature = temperature_t->value->int32;
    s_cached_temp_f = settings.temperature * 9 / 5 + 32;
    if (temperature_high_t) {
      settings.temperature_high = temperature_high_t->value->int32;
      s_cached_temp_high_f = settings.temperature_high * 9 / 5 + 32;
    }
    Tuple *temperature_high_tomorrow_t = dict_find(it, MESSAGE_KEY_TEMPERATURE_HIGH_TOMORROW);
    if (temperature_high_tomorrow_t) {
      settings.temperature_high_tomorrow = temperature_high_tomorrow_t->value->int32;
    }
    Tuple *sunset_hour_t = dict_find(it, MESSAGE_KEY_SUNSET_HOUR);
    if (sunset_hour_t) {
      settings.sunset_hour = sunset_hour_t->value->int32;
    }
    strncpy(settings.condition, conditions_t->value->cstring, sizeof(settings.condition));
  }

  Tuple *show_steps_t = dict_find(it, MESSAGE_KEY_PREF_SHOW_STEPS);
  if (show_steps_t) {
    settings.show_steps = show_steps_t->value->int32 == 1;
  }

  Tuple *show_weather_t = dict_find(it, MESSAGE_KEY_PREF_SHOW_WEATHER);
  if (show_weather_t) {
    settings.show_weather = show_weather_t->value->int32 == 1;
  }

  Tuple *weather_use_metric_t = dict_find(it, MESSAGE_KEY_PREF_WEATHER_METRIC);
  if (weather_use_metric_t) {
    settings.weather_use_metric = weather_use_metric_t->value->int32 == 1;
  }

  Tuple *skip_location_t = dict_find(it, MESSAGE_KEY_PREF_SKIP_LOCATION);
  if (skip_location_t) {
    settings.skip_location = skip_location_t->value->int32 == 1;
  }

  Tuple *hour_vibe_t = dict_find(it, MESSAGE_KEY_PREF_HOUR_VIBE);
  if (hour_vibe_t) {
    settings.hour_vibe = hour_vibe_t->value->int32 == 1;
  }

  Tuple *disconnect_alert_t = dict_find(it, MESSAGE_KEY_PREF_DISCONNECT_ALERT);
  if (disconnect_alert_t) {
    settings.disconnect_alert = disconnect_alert_t->value->int32 == 1;
  }

  Tuple *custom_text_t = dict_find(it, MESSAGE_KEY_PREF_CUSTOM_TEXT);
  if (custom_text_t) {
    strncpy(settings.custom_text, custom_text_t->value->cstring, sizeof(settings.custom_text));
  }

  Tuple *bottom_text_t = dict_find(it, MESSAGE_KEY_PREF_BOTTOM_TEXT);
  if (bottom_text_t) {
    strncpy(settings.bottom_text, bottom_text_t->value->cstring, sizeof(settings.bottom_text));
  }

  Tuple *progress_bar_mode_t = dict_find(it, MESSAGE_KEY_PREF_PROGRESS_MODE);
  if (progress_bar_mode_t) {
    settings.progress_bar_mode = atoi(progress_bar_mode_t->value->cstring);
  }

  Tuple *step_goal_t = dict_find(it, MESSAGE_KEY_PREF_STEP_GOAL);
  if (step_goal_t) {
    settings.step_goal = atoi(step_goal_t->value->cstring);
  }

  Tuple *sleep_goal_t = dict_find(it, MESSAGE_KEY_PREF_SLEEP_GOAL);
  if (sleep_goal_t) {
    settings.sleep_goal_mins = atoi(sleep_goal_t->value->cstring);
  }

  // Color mode settings (0=disabled, 1=static, 2=dynamic)
  Tuple *progress_color_mode_t = dict_find(it, MESSAGE_KEY_PREF_PROGRESS_COLOR_MODE);
  if (progress_color_mode_t) {
    settings.progress_color_mode = atoi(progress_color_mode_t->value->cstring);
  }

  Tuple *progress_static_color_t = dict_find(it, MESSAGE_KEY_PREF_PROGRESS_STATIC_COLOR);
  if (progress_static_color_t) {
    settings.progress_static_color = GColorFromHEX(progress_static_color_t->value->int32);
  }

  Tuple *temperature_color_mode_t = dict_find(it, MESSAGE_KEY_PREF_TEMPERATURE_COLOR_MODE);
  if (temperature_color_mode_t) {
    settings.temperature_color_mode = atoi(temperature_color_mode_t->value->cstring);
  }

  Tuple *temperature_static_color_t = dict_find(it, MESSAGE_KEY_PREF_TEMPERATURE_STATIC_COLOR);
  if (temperature_static_color_t) {
    settings.temperature_static_color = GColorFromHEX(temperature_static_color_t->value->int32);
  }

  Tuple *weather_color_mode_t = dict_find(it, MESSAGE_KEY_PREF_WEATHER_COLOR_MODE);
  if (weather_color_mode_t) {
    settings.weather_color_mode = atoi(weather_color_mode_t->value->cstring);
  }

  Tuple *weather_static_color_t = dict_find(it, MESSAGE_KEY_PREF_WEATHER_STATIC_COLOR);
  if (weather_static_color_t) {
    settings.weather_static_color = GColorFromHEX(weather_static_color_t->value->int32);
  }

  Tuple *colorize_date_t = dict_find(it, MESSAGE_KEY_PREF_COLORIZE_DATE);
  if (colorize_date_t) {
    settings.colorize_date = colorize_date_t->value->int32 == 1;
  }

  Tuple *date_color_t = dict_find(it, MESSAGE_KEY_PREF_DATE_COLOR);
  if (date_color_t) {
    settings.date_color = GColorFromHEX(date_color_t->value->int32);
  }

  Tuple *colorize_colon_t = dict_find(it, MESSAGE_KEY_PREF_COLORIZE_COLON);
  if (colorize_colon_t) {
    settings.colorize_colon = colorize_colon_t->value->int32 == 1;
  }

  Tuple *colon_color_t = dict_find(it, MESSAGE_KEY_PREF_COLON_COLOR);
  if (colon_color_t) {
    settings.colon_color = GColorFromHEX(colon_color_t->value->int32);
  }

  #if defined(PBL_PLATFORM_EMERY)
  Tuple *bold_hours_t = dict_find(it, MESSAGE_KEY_PREF_BOLD_HOURS);
  if (bold_hours_t) {
    bool new_bold = bold_hours_t->value->int32 == 1;
    if (new_bold != settings.bold_hours) {
      if (new_bold) {
        // Load bold and regular fonts on demand
        if (!s_time_font_bold) {
          s_time_font_bold = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_86_SEMI_BOLD));
        }
        if (!s_time_font_regular) {
          s_time_font_regular = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_86_REGULAR));
        }
      } else {
        // Unload bold and regular fonts to free memory
        if (s_time_font_bold) {
          fonts_unload_custom_font(s_time_font_bold);
          s_time_font_bold = NULL;
        }
        if (s_time_font_regular) {
          fonts_unload_custom_font(s_time_font_regular);
          s_time_font_regular = NULL;
        }
      }
      settings.bold_hours = new_bold;
      text_layer_set_font(s_time_hours_layer, settings.bold_hours ? s_time_font_bold : s_time_font);
      text_layer_set_font(s_time_mins_layer, settings.bold_hours ? s_time_font_regular : s_time_font);
    }
  }

  update_accent_colors();
  #endif

  // Save settings and refresh UI
  save_settings();
  recalculate_update_periods();

  // Initialize wake time if user enabled uptime display
  if (s_any_needs_uptime) {
    init_wake_time();
  }

  update_time();
  update_steps();
  update_progress();
  update_weather_layers();
  update_health_subscription();
}

// ============================================================
// WINDOW HANDLERS
// ============================================================
static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  load_fonts();

  // Set colors
  color_fg = GColorWhite;
  color_bg = GColorBlack;
  window_set_background_color(window, color_bg);

  // Calculate layer positions
  int hud_y = bounds.size.h - HUD_HEIGHT - MARGIN_SIZE - 3;
  int time_y = hud_y - TIME_LAYER_HEIGHT - 2;
  int custom_y = MARGIN_SIZE;
  int bt_y = custom_y;
  int step_y = time_y + 2;
  int condition_y = step_y - TEXT_HEIGHT;
  int temperature_y = condition_y - TEXT_HEIGHT;

  // Load all layers
  load_hud(MARGIN_SIZE, hud_y);
  load_progress_layer(MARGIN_SIZE, hud_y);
  #if defined(PBL_PLATFORM_EMERY)
  load_time_layer(MARGIN_SIZE + TIME_X_OFFSET, time_y);
  #else
  load_time_layer(MARGIN_SIZE, time_y);
  #endif
  load_custom_text_layers(custom_y, bt_y);
  load_step_layer(step_y);
  load_weather_layers(temperature_y, condition_y);

  // Add children to window
  layer_add_child(window_layer, s_progress_layer);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_hud_layer));
  layer_add_child(window_layer, bitmap_layer_get_layer(s_charge_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_day_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_custom_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_bt_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_step_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_condition_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_temperature_layer));
  #if defined(PBL_PLATFORM_EMERY)
  layer_add_child(window_layer, text_layer_get_layer(s_temp_slash_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_temp_high_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_time_hours_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_time_colon_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_time_mins_layer));
  update_accent_colors();
  #else
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
  #endif
}

static void main_window_unload(Window *window) {
  unload_time_layer();
  unload_custom_text_layers();
  unload_step_layer();
  unload_weather_layers();
  unload_hud();
  unload_progress_layer();
  unload_fonts();
}

// ============================================================
// INIT / DEINIT
// ============================================================
static void init(void) {
  load_settings();
  recalculate_update_periods();

  // Only initialize wake time tracking if user has $U in any text field
  if (s_any_needs_uptime) {
    init_wake_time();
  }

  // Register for time updates
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  // Register for battery updates
  battery_state_service_subscribe(battery_callback);
  // Register for connection updates
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = bt_callback
  });

  // Register app message inbox
  app_message_register_inbox_received(inbox_received_callback);

  // Open app message
  const int inbox_size = 256;
  const int outbox_size = 8;
  app_message_open(inbox_size, outbox_size);

  s_main_window = window_create();

  // Set handlers to manage the elements inside the window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // Show the window on the watch
  window_stack_push(s_main_window, true);

  // Invalidate all caches to force full initial draw
  invalidate_all_caches();
  s_last_custom_update = -1;
  s_last_bottom_update = -1;

  update_time();
  update_steps();
  update_progress();
  update_weather_layers();
  update_health_subscription();
  battery_callback(battery_state_service_peek());
  bt_callback(connection_service_peek_pebble_app_connection());
}

static void deinit(void) {
  window_destroy(s_main_window);
}

// ============================================================
// MAIN
// ============================================================
int main(void) {
  init();
  app_event_loop();
  deinit();
}
