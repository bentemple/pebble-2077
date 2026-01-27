#include <pebble.h>
#include <ctype.h>

#define SETTINGS_KEY 1
#define WAKE_CALC_COUNT_KEY 2

const int MARGIN_SIZE = 4;
const int TEXT_HEIGHT = 14;

static Window *s_main_window;

static GFont s_time_font;
static GFont s_date_font;
static GFont s_text_font;

static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_day_layer;
static TextLayer *s_custom_layer;
static TextLayer *s_bt_layer;
static TextLayer *s_step_layer;
static TextLayer *s_temperature_layer;
static TextLayer *s_condition_layer;

static BitmapLayer *s_hud_layer;
static GBitmap *s_hud_bitmap;
static BitmapLayer *s_charge_layer;
static GBitmap *s_charge_bitmap;

static GColor color_fg;
static GColor color_bg;

static int s_sleep_percent;
static Layer *s_progress_layer;

// Wake time tracking
static time_t s_wake_time = 0;
static bool s_was_sleeping = false;
static int s_wake_calc_count = -1;  // -1 = not loaded yet

// Cached state for change detection (reduces unnecessary redraws)
static int s_last_sleep_percent = -1;
static int s_last_day = -1;
static int s_last_minute = -1;
static int s_last_step_count = -1;
static int s_last_temperature = -999;
static bool s_last_steps_visible = false;

// Cached formatted strings (avoid reformatting when unchanged)
static char s_time_buffer[8];
static char s_date_buffer[8];
static char s_day_buffer[32];
static char s_uptime_buffer[16];

typedef struct ClaySettings {
  bool show_steps, show_weather, weather_use_metric, hour_vibe, disconnect_alert;
  int temperature;
  char custom_text[32], condition[32];
} ClaySettings;

static ClaySettings settings;

static void str_to_upper(char *str) {
  char *s = str;
  while (*s) {
    *s = toupper((unsigned char) *s);
    s++;
  }
}

static void update_time() {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  int current_minute = tick_time->tm_min;
  int current_hour = tick_time->tm_hour;
  int current_day = tick_time->tm_mday;

  // Only update time string when minute changes
  if (current_minute != s_last_minute) {
    s_last_minute = current_minute;
    strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
    text_layer_set_text(s_time_layer, s_time_buffer);

    // Update uptime display
    if (s_wake_time > 0) {
      int uptime_secs = time(NULL) - s_wake_time;
      int uptime_hrs = uptime_secs / 3600;
      int uptime_mins = (uptime_secs % 3600) / 60;
      int count = s_wake_calc_count >= 0 ? s_wake_calc_count : 0;
      snprintf(s_uptime_buffer, sizeof(s_uptime_buffer), "UPTIME_%02d:%02d_%d",
               uptime_hrs, uptime_mins, count);
    } else {
      int count = s_wake_calc_count >= 0 ? s_wake_calc_count : 0;
      snprintf(s_uptime_buffer, sizeof(s_uptime_buffer), "UPTIME_--:--_%d", count);
    }
    text_layer_set_text(s_custom_layer, s_uptime_buffer);
  }

  // Only update date strings when day changes (once per day vs 1440 times)
  if (current_day != s_last_day) {
    s_last_day = current_day;
    strftime(s_date_buffer, sizeof(s_date_buffer), "%d", tick_time);
    strftime(s_day_buffer, sizeof(s_day_buffer), "%Y.%m.%d", tick_time);
    text_layer_set_text(s_date_layer, s_date_buffer);
    text_layer_set_text(s_day_layer, s_day_buffer);
  }

}

static void init_wake_time() {
  #if defined(PBL_HEALTH)
  time_t now = time(NULL);

  // Load calc counter from persistence once
  if (s_wake_calc_count < 0) {
    s_wake_calc_count = persist_exists(WAKE_CALC_COUNT_KEY)
      ? persist_read_int(WAKE_CALC_COUNT_KEY)
      : 0;
  }

  // Init current sleep state
  HealthActivityMask activities = health_service_peek_current_activities();
  s_was_sleeping = activities & HealthActivitySleep;

  // Check if cached wake time is still valid (<12 hours old)
  if (s_wake_time > 0 && s_wake_time <= now && (now - s_wake_time) < 12 * 3600) {
    return;  // Valid in-memory cache
  }

  // No valid cache - estimate from health data if awake
  if (!s_was_sleeping) {
    // Increment and persist calc counter
    s_wake_calc_count++;
    persist_write_int(WAKE_CALC_COUNT_KEY, s_wake_calc_count);

    time_t today_start = time_start_of_today();
    HealthMetric metric = HealthMetricSleepSeconds;
    HealthServiceAccessibilityMask mask = health_service_metric_accessible(
      metric, today_start, now
    );

    if (mask & HealthServiceAccessibilityMaskAvailable) {
      int sleep_seconds = (int)health_service_sum_today(metric);
      if (sleep_seconds > 0) {
        // Estimate: woke up after sleeping this many seconds since midnight
        s_wake_time = today_start + sleep_seconds;
      } else {
        // No sleep recorded - use app start time
        s_wake_time = now;
      }
    } else {
      // Health not available - use app start time
      s_wake_time = now;
    }
  }
  // If currently sleeping, s_wake_time stays 0 until we wake up
  #endif
}

static void update_sleep() {
  #if defined(PBL_HEALTH)
  HealthMetric metric = HealthMetricSleepSeconds;
  time_t start = time_start_of_today();
  time_t end = time(NULL);

  HealthServiceAccessibilityMask mask = health_service_metric_accessible(metric, start, end);
  int sleep_seconds = 0;

  if (mask & HealthServiceAccessibilityMaskAvailable) {
    sleep_seconds = (int)health_service_sum_today(metric);
  }

  // 7 hours = 25200 seconds = 100%
  int sleep_percent = (sleep_seconds * 100) / 25200;
  if (sleep_percent > 100) {
    sleep_percent = 100;
  }

  if (sleep_percent != s_last_sleep_percent) {
    s_last_sleep_percent = sleep_percent;
    s_sleep_percent = sleep_percent;
    if (s_progress_layer) {
      layer_mark_dirty(s_progress_layer);
    }
  }
  #endif
}

static void update_steps() {
  #if defined(PBL_HEALTH)
  if (settings.show_steps) {
    HealthMetric metric = HealthMetricStepCount;
    time_t start = time_start_of_today();
    time_t end = time(NULL);

    HealthServiceAccessibilityMask mask = health_service_metric_accessible(metric, start, end);
    int step_count;

    if (mask & HealthServiceAccessibilityMaskAvailable) {
      step_count = (int)health_service_sum_today(metric);
    }
    else {
      step_count = 0;
    }

    // Only update text layer if step count changed
    if (step_count != s_last_step_count) {
      s_last_step_count = step_count;
      static char s_step_buffer[16];
      snprintf(s_step_buffer, sizeof(s_step_buffer), "STEPS: %d", step_count);
      text_layer_set_text(s_step_layer, s_step_buffer);
    }
    layer_set_hidden(text_layer_get_layer(s_step_layer), false);
  }
  else {
    layer_set_hidden(text_layer_get_layer(s_step_layer), true);
  }
  #endif
}

static void update_weather_layers() {
  if (settings.show_weather && settings.temperature) {
    static char temperature_buffer[8];
    static char conditions_buffer[32];

    // Only reformat strings if temperature changed
    if (settings.temperature != s_last_temperature) {
      s_last_temperature = settings.temperature;

      if (settings.weather_use_metric) {
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%dC", settings.temperature);
      }
      else {
        int temp_f = settings.temperature * 1.8 + 32;
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%dF", temp_f);
      }
      snprintf(conditions_buffer, sizeof(conditions_buffer), "%s", settings.condition);

      text_layer_set_text(s_condition_layer, conditions_buffer);
      text_layer_set_text(s_temperature_layer, temperature_buffer);
    }

    // Only recalculate positions if step visibility changed
    bool steps_visible = !layer_get_hidden(text_layer_get_layer(s_step_layer));
    if (steps_visible != s_last_steps_visible) {
      s_last_steps_visible = steps_visible;

      GRect step_frame = layer_get_frame(text_layer_get_layer(s_step_layer));
      int x = step_frame.origin.x;
      if (steps_visible) {
        int condition_y = step_frame.origin.y - TEXT_HEIGHT;
        int temperature_y = condition_y - TEXT_HEIGHT;
        layer_set_frame(text_layer_get_layer(s_condition_layer),
          GRect(x, condition_y, 136, TEXT_HEIGHT)
        );
        layer_set_frame(text_layer_get_layer(s_temperature_layer),
          GRect(x, temperature_y, 136, TEXT_HEIGHT)
        );
      }
      else {
        int temperature_y = step_frame.origin.y - TEXT_HEIGHT;
        layer_set_frame(text_layer_get_layer(s_condition_layer), step_frame);
        layer_set_frame(text_layer_get_layer(s_temperature_layer),
          GRect(x, temperature_y, 136, TEXT_HEIGHT)
        );
      }
    }

    layer_set_hidden(text_layer_get_layer(s_condition_layer), false);
    layer_set_hidden(text_layer_get_layer(s_temperature_layer), false);
  }
  else {
    layer_set_hidden(text_layer_get_layer(s_condition_layer), true);
    layer_set_hidden(text_layer_get_layer(s_temperature_layer), true);
  }
}

static void progress_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int width = (s_sleep_percent * bounds.size.w) / 100;

  // draw the background
  graphics_context_set_fill_color(ctx, color_bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // draw the bar (sleep progress - 7 hours = 100%)
  graphics_context_set_fill_color(ctx, color_fg);
  graphics_fill_rect(ctx, GRect(0, 0, width, bounds.size.h), 0, GCornerNone);
}

static void load_fonts() {
  s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_58));
  s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_24));
  s_text_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ORBITRON_12));
}

static void load_progress_layer(int x, int y) {
  s_progress_layer = layer_create(GRect(x+43, y+13, 96, 8));
  layer_set_update_proc(s_progress_layer, progress_update_proc);
}

static void load_hud_layer(int x, int y) {
  s_hud_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_HUD);
  s_hud_layer = bitmap_layer_create(
    GRect(x, y, 144, 40)
  );
  bitmap_layer_set_bitmap(s_hud_layer, s_hud_bitmap);
  bitmap_layer_set_alignment(s_hud_layer, GAlignCenter);
  bitmap_layer_set_compositing_mode(s_hud_layer, GCompOpSet);
}

static void load_charge_layer(int x, int y) {
  s_charge_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CHARGE);
  s_charge_layer = bitmap_layer_create(
    GRect(x+43, y+13, 5, 8)
  );
  bitmap_layer_set_bitmap(s_charge_layer, s_charge_bitmap);
  bitmap_layer_set_alignment(s_charge_layer, GAlignTopLeft);
  bitmap_layer_set_compositing_mode(s_charge_layer, GCompOpSet);
}

static void load_time_layer(int x, int y) {
  s_time_layer = text_layer_create(
    GRect(x, y, 136, 58)
  );
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, color_fg);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentLeft);
}

static void load_date_layer(int x, int y) {
  s_date_layer = text_layer_create(
    GRect(x+4, y+4, 36, 36)
  );
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, color_fg);
  text_layer_set_font(s_date_layer, s_date_font);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
}

static void load_day_layer(int x, int y) {
  s_day_layer = text_layer_create(
    GRect(x+42, y+24, 97, TEXT_HEIGHT)
  );
  text_layer_set_background_color(s_day_layer, GColorClear);
  text_layer_set_text_color(s_day_layer, color_fg);
  text_layer_set_font(s_day_layer, s_text_font);
  text_layer_set_text_alignment(s_day_layer, GTextAlignmentLeft);
}

// Used for small text layers like weather and steps
static void load_info_layer(TextLayer **layer, int y) {
  *layer = text_layer_create(
    GRect(MARGIN_SIZE, y, 136, TEXT_HEIGHT)
  );
  text_layer_set_background_color(*layer, GColorClear);
  text_layer_set_text_color(*layer, color_fg);
  text_layer_set_font(*layer, s_text_font);
}

static void load_hud(int x, int y) {
  load_progress_layer(x, y);
  load_hud_layer(x, y);
  load_charge_layer(x, y);
  load_date_layer(x, y);
  load_day_layer(x, y);
}

static void main_window_load(Window *window) {
  // get info about the window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  load_fonts();

  // set colors
  color_fg = GColorWhite;
  color_bg = GColorBlack;

  window_set_background_color(window, color_bg);

  int hud_y = bounds.size.h - 44;
  int time_y = hud_y - 60;
  int custom_y = 2;
  int bt_y = custom_y;
  int step_y = time_y + 2;
  int condition_y = step_y - TEXT_HEIGHT;
  int temperature_y = condition_y - TEXT_HEIGHT;

  load_hud(0, hud_y);
  load_time_layer(MARGIN_SIZE, time_y);

  // custom text
  load_info_layer(&s_custom_layer, custom_y);

  // BT
  load_info_layer(&s_bt_layer, bt_y);
  text_layer_set_text(s_bt_layer, "NO_CONNECTION");

  // steps
  load_info_layer(&s_step_layer, step_y);
  // hide step layer initially so it doesn't show on unsupported devices
  layer_set_hidden(text_layer_get_layer(s_step_layer), true);

  // weather
  load_info_layer(&s_condition_layer, condition_y);
  load_info_layer(&s_temperature_layer, temperature_y);

  // add children
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
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_day_layer);
  text_layer_destroy(s_custom_layer);
  text_layer_destroy(s_bt_layer);
  text_layer_destroy(s_step_layer);
  text_layer_destroy(s_temperature_layer);
  text_layer_destroy(s_condition_layer);
  gbitmap_destroy(s_hud_bitmap);
  bitmap_layer_destroy(s_hud_layer);
  gbitmap_destroy(s_charge_bitmap);
  bitmap_layer_destroy(s_charge_layer);
  fonts_unload_custom_font(s_time_font);
  fonts_unload_custom_font(s_date_font);
  fonts_unload_custom_font(s_text_font);
  layer_destroy(s_progress_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();

  // refresh steps every 30 minutes
  if (tick_time->tm_min % 30 == 0) {
    update_steps();
  }

  // refresh sleep and weather every hour
  if (tick_time->tm_min == 0) {
    update_sleep();
  }
  if (tick_time->tm_min == 0 && settings.show_weather) {
    DictionaryIterator *it;
    app_message_outbox_begin(&it);
    dict_write_uint8(it, 0, 0);
    app_message_outbox_send();
  }

  if (tick_time->tm_min == 0 && settings.hour_vibe) {
    vibes_double_pulse();
  }
}

static void battery_callback(BatteryChargeState state) {
  // Show/hide charging indicator
  layer_set_hidden(bitmap_layer_get_layer(s_charge_layer), !state.is_plugged);
}

static void bt_callback(bool connected) {
  // Replace OS layer with BT layer when disconnected
  layer_set_hidden(text_layer_get_layer(s_bt_layer), connected);
  layer_set_hidden(text_layer_get_layer(s_custom_layer), !connected);

  if (!connected) {
    vibes_short_pulse();
  }
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventSignificantUpdate) {
    // Check for sleep → awake transition
    HealthActivityMask activities = health_service_peek_current_activities();
    bool is_sleeping = activities & HealthActivitySleep;

    if (s_was_sleeping && !is_sleeping) {
      // Just woke up - record wake time
      s_wake_time = time(NULL);
    }
    s_was_sleeping = is_sleeping;

    update_steps();
    update_sleep();
  }
  if (event == HealthEventMovementUpdate) {
    update_steps();
  }
}
#endif

static void update_health_subscription() {
  #if defined(PBL_HEALTH)
  if (settings.show_steps) {
    health_service_events_subscribe(health_handler, NULL);
  }
  else {
    health_service_events_unsubscribe();
  }
  #endif
}

static void default_settings() {
  settings.show_steps = true;
  settings.show_weather = true;
  settings.weather_use_metric = false;
  settings.hour_vibe = false;
  settings.disconnect_alert = true;
  settings.temperature = (int)NULL;
  strncpy(settings.custom_text, "PBL_%m%U%j", sizeof(settings.custom_text));
  strncpy(settings.condition, "", sizeof(settings.condition));
}

static void load_settings() {
  default_settings();
  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
}

static void save_settings() {
  persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));

  // Invalidate caches to force refresh after settings change
  s_last_minute = -1;
  s_last_day = -1;
  s_last_sleep_percent = -1;
  s_last_step_count = -1;
  s_last_temperature = -999;

  update_time();
  update_steps();
  update_sleep();
  update_weather_layers();
  update_health_subscription();
}

static void inbox_received_callback(DictionaryIterator *it, void *ctx) {
  Tuple *temperature_t = dict_find(it, MESSAGE_KEY_TEMPERATURE);
  Tuple *conditions_t = dict_find(it, MESSAGE_KEY_CONDITIONS);
  if (temperature_t && conditions_t) {
    settings.temperature = temperature_t->value->int32;
    strncpy(settings.condition, conditions_t->value->cstring, sizeof(settings.condition));
  }
  
  Tuple *show_steps_t = dict_find(it, MESSAGE_KEY_PREF_SHOW_STEPS);
  if (show_steps_t) {
    settings.show_steps = show_steps_t->value->int32 == 1;
  }

  // Force weather always on - ignore config
  // Tuple *show_weather_t = dict_find(it, MESSAGE_KEY_PREF_SHOW_WEATHER);
  // if (show_weather_t) {
  //   settings.show_weather = show_weather_t->value->int32 == 1;
  // }

  Tuple *weather_use_metric_t = dict_find(it, MESSAGE_KEY_PREF_WEATHER_METRIC);
  if (weather_use_metric_t) {
    settings.weather_use_metric = weather_use_metric_t->value->int32 == 1;
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

  save_settings();
}

static void init() {
  load_settings();
  init_wake_time();

  // register for time updates
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  // register for battery updates
  battery_state_service_subscribe(battery_callback);
  // register for connection updates
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = bt_callback
  });

  // register app message inbox and outbox
  app_message_register_inbox_received(inbox_received_callback);

  // open app message
  const int inbox_size = 256;
  const int outbox_size = 16;
  app_message_open(inbox_size, outbox_size);

  s_main_window = window_create();

  // set handlers to manage the elements inside the window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // show the window on the watch
  window_stack_push(s_main_window, true);

  update_time();
  update_steps();
  update_sleep();
  update_weather_layers();
  battery_callback(battery_state_service_peek());
  bt_callback(connection_service_peek_pebble_app_connection());
}

static void deinit() {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}