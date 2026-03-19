#include <pebble.h>
#include <ctype.h>

#define SETTINGS_KEY 1

// Platform-specific dimensions
#if defined(PBL_PLATFORM_EMERY)
  // Pebble Time 2: 200x228 (larger time font)
  #define SCREEN_WIDTH 200
  #define SCREEN_HEIGHT 228
  #define MARGIN_SIZE 7
  #define HUD_WIDTH (SCREEN_WIDTH - MARGIN_SIZE * 2)
  #define HUD_HEIGHT 40
  #define TEXT_HEIGHT 14
  #define TIME_LAYER_WIDTH (SCREEN_WIDTH - MARGIN_SIZE * 2)
  #define TIME_LAYER_HEIGHT 76
  #define DATE_LAYER_SIZE 36
  #define PROGRESS_BAR_OFFSET_X 43
  #define PROGRESS_BAR_OFFSET_Y 13
  #define PROGRESS_BAR_WIDTH (HUD_WIDTH - PROGRESS_BAR_OFFSET_X)
  #define PROGRESS_BAR_HEIGHT 8
  #define DAY_LAYER_OFFSET_X 43
  #define DAY_LAYER_OFFSET_Y (HUD_HEIGHT - TEXT_HEIGHT)
  #define DAY_LAYER_WIDTH (HUD_WIDTH - DAY_LAYER_OFFSET_X)
  #define INFO_LAYER_WIDTH (SCREEN_WIDTH - MARGIN_SIZE * 2)
#else
  // Original Pebble/Time/2: 144x168
  #define SCREEN_WIDTH 144
  #define SCREEN_HEIGHT 168
  #define HUD_WIDTH 144
  #define HUD_HEIGHT 40
  #define TIME_FONT_HEIGHT 58
  #define DATE_FONT_HEIGHT 24
  #define TEXT_FONT_HEIGHT 12
  #define MARGIN_SIZE 4
  #define TEXT_HEIGHT 14
  #define TIME_LAYER_WIDTH 136
  #define TIME_LAYER_HEIGHT 58
  #define DATE_LAYER_SIZE 36
  #define PROGRESS_BAR_WIDTH 96
  #define PROGRESS_BAR_HEIGHT 8
  #define PROGRESS_BAR_OFFSET_X 43
  #define PROGRESS_BAR_OFFSET_Y 13
  #define DAY_LAYER_WIDTH 97
  #define DAY_LAYER_OFFSET_X 42
  #define DAY_LAYER_OFFSET_Y 24
  #define INFO_LAYER_WIDTH (SCREEN_WIDTH - MARGIN_SIZE * 2)
#endif

const int DEFAULT_STEP_GOAL = 10000;
const int DEFAULT_SLEEP_GOAL_MINS = 420;  // 7 hours

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

static int s_progress_percent;
static Layer *s_progress_layer;

// Wake time tracking
static time_t s_wake_time = 0;
static bool s_was_sleeping = false;

// Update periods for format strings
typedef enum {
  UPDATE_PERIOD_MINUTE = 1,
  UPDATE_PERIOD_HOUR = 60,
  UPDATE_PERIOD_DAY = 1440
} UpdatePeriod;

// Progress bar display modes
typedef enum {
  PROGRESS_MODE_BATTERY = 0,
  PROGRESS_MODE_STEPS = 1,
  PROGRESS_MODE_SLEEP = 2
} ProgressBarMode;

// Cached update periods (calculated from format strings)
static UpdatePeriod s_custom_text_period = UPDATE_PERIOD_DAY;
static UpdatePeriod s_bottom_text_period = UPDATE_PERIOD_DAY;
static int s_last_custom_update = -1;  // minute of day when last updated
static int s_last_bottom_update = -1;

// Cached flags to avoid repeated strstr() calls
static bool s_custom_needs_uptime = false;
static bool s_bottom_needs_uptime = false;
static bool s_any_needs_uptime = false;      // OR of both above
static bool s_needs_sleep_tracking = false;  // uptime OR sleep progress mode

// Cached settings that rarely change
static bool s_is_24h_style = true;
static int s_cached_temp_f = 0;  // Pre-calculated Fahrenheit

// Cached state for change detection (reduces unnecessary redraws)
static int s_last_progress_percent = -1;
static int s_last_day = -1;
static int s_last_minute = -1;
static int s_last_hour = -1;
static int s_last_step_count = -1;
static int s_last_temperature = -999;
static bool s_last_steps_visible = false;

// Cached formatted strings (avoid reformatting when unchanged)
static char s_time_buffer[8];
static char s_date_buffer[8];
static char s_day_buffer[32];
static char s_custom_buffer[32];

typedef struct ClaySettings {
  bool show_steps, show_weather, weather_use_metric, skip_location, hour_vibe, disconnect_alert;
  int temperature;
  int progress_bar_mode;  // 0=battery, 1=steps, 2=sleep
  int step_goal;
  int sleep_goal_mins;
  char custom_text[32], bottom_text[32], condition[32];
} ClaySettings;

static ClaySettings settings;

static void str_to_upper(char *str) {
  char *s = str;
  while (*s) {
    *s = toupper((unsigned char) *s);
    s++;
  }
}

// Analyze format string to determine minimum update period
// Returns UPDATE_PERIOD_MINUTE, UPDATE_PERIOD_HOUR, or UPDATE_PERIOD_DAY
static UpdatePeriod analyze_format_period(const char *fmt) {
  UpdatePeriod period = UPDATE_PERIOD_DAY;

  // Check for $U (uptime) - needs minute updates
  if (strstr(fmt, "$U") != NULL) {
    return UPDATE_PERIOD_MINUTE;
  }

  // Scan for strftime codes
  const char *p = fmt;
  while (*p) {
    if (*p == '%' && *(p + 1)) {
      char code = *(p + 1);
      switch (code) {
        // Minute-level codes
        case 'M':  // minute
        case 'S':  // second
        case 'T':  // %H:%M:%S
        case 'R':  // %H:%M
        case 'r':  // 12-hour with seconds
        case 'X':  // locale time
        case 'c':  // locale date and time
          return UPDATE_PERIOD_MINUTE;

        // Hour-level codes
        case 'H':  // hour 24
        case 'I':  // hour 12
        case 'k':  // hour 24 space-padded
        case 'l':  // hour 12 space-padded
        case 'p':  // AM/PM
        case 'P':  // am/pm
          if (period > UPDATE_PERIOD_HOUR) {
            period = UPDATE_PERIOD_HOUR;
          }
          break;

        // Day-level codes (default) - no action needed
        // %d, %e, %j, %m, %b, %B, %y, %Y, %U, %W, %a, %A, %u, %w, %G, %V, etc.
      }
      p += 2;  // Skip the format code
    } else {
      p++;
    }
  }

  return period;
}

// Recalculate update periods and cached flags for all format fields
static void recalculate_update_periods() {
  s_custom_text_period = analyze_format_period(settings.custom_text);
  s_bottom_text_period = analyze_format_period(settings.bottom_text);

  // Cache uptime flags (avoids strstr on every tick/event)
  s_custom_needs_uptime = strstr(settings.custom_text, "$U") != NULL;
  s_bottom_needs_uptime = strstr(settings.bottom_text, "$U") != NULL;
  s_any_needs_uptime = s_custom_needs_uptime || s_bottom_needs_uptime;
  s_needs_sleep_tracking = s_any_needs_uptime ||
                           settings.progress_bar_mode == PROGRESS_MODE_SLEEP;

  // Cache 24h style (rarely changes, checked every minute otherwise)
  s_is_24h_style = clock_is_24h_style();

  // Reset last update times to force immediate refresh
  s_last_custom_update = -1;
  s_last_bottom_update = -1;
}

// Replace $U with uptime string in buffer
// Pass current time to avoid redundant time() calls
static void replace_uptime(char *buf, size_t buf_size, time_t now) {
  char *pos = strstr(buf, "$U");
  if (!pos) return;

  // Calculate uptime string
  char uptime_str[16];

  if (s_wake_time > 0) {
    int uptime_secs = now - s_wake_time;
    int uptime_hrs = uptime_secs / 3600;
    int uptime_mins = (uptime_secs % 3600) / 60;
    snprintf(uptime_str, sizeof(uptime_str), "%02d:%02d", uptime_hrs, uptime_mins);
  } else {
    snprintf(uptime_str, sizeof(uptime_str), "--:--");
  }

  // Build new string: before $U + uptime + after $U
  size_t prefix_len = pos - buf;
  size_t uptime_len = strlen(uptime_str);
  char *after = pos + 2;  // Skip "$U"
  size_t after_len = strlen(after);

  if (prefix_len + uptime_len + after_len < buf_size) {
    memmove(pos + uptime_len, after, after_len + 1);  // +1 for null terminator
    memcpy(pos, uptime_str, uptime_len);
  }
}

// Check if field needs update based on its period
static bool should_update_field(int current_minute, int current_hour, int current_day,
                                 UpdatePeriod period, int *last_update) {
  int minute_of_day = current_hour * 60 + current_minute;

  switch (period) {
    case UPDATE_PERIOD_MINUTE:
      if (*last_update != minute_of_day) {
        *last_update = minute_of_day;
        return true;
      }
      break;

    case UPDATE_PERIOD_HOUR:
      // Update on hour boundary or if never updated
      if (*last_update < 0 || (minute_of_day / 60) != (*last_update / 60)) {
        *last_update = minute_of_day;
        return true;
      }
      break;

    case UPDATE_PERIOD_DAY:
    default:
      // Update on day change (detected via s_last_day) or if never updated
      if (*last_update < 0) {
        *last_update = minute_of_day;
        return true;
      }
      // Day change is handled separately
      break;
  }
  return false;
}

static void update_time() {
  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);

  int current_minute = tick_time->tm_min;
  int current_hour = tick_time->tm_hour;
  int current_day = tick_time->tm_mday;
  bool day_changed = (current_day != s_last_day);

  // Always update main time display every minute
  if (current_minute != s_last_minute) {
    s_last_minute = current_minute;
    strftime(s_time_buffer, sizeof(s_time_buffer), s_is_24h_style ? "%H:%M" : "%I:%M", tick_time);
    text_layer_set_text(s_time_layer, s_time_buffer);
  }

  // Update custom text based on its calculated period
  if (day_changed ||
      should_update_field(current_minute, current_hour, current_day,
                          s_custom_text_period, &s_last_custom_update)) {
    strftime(s_custom_buffer, sizeof(s_custom_buffer), settings.custom_text, tick_time);

    if (s_custom_needs_uptime) {  // Use cached flag
      replace_uptime(s_custom_buffer, sizeof(s_custom_buffer), now);
    }

    str_to_upper(s_custom_buffer);
    text_layer_set_text(s_custom_layer, s_custom_buffer);
  }

  // Update bottom text based on its calculated period
  if (day_changed ||
      should_update_field(current_minute, current_hour, current_day,
                          s_bottom_text_period, &s_last_bottom_update)) {
    strftime(s_day_buffer, sizeof(s_day_buffer), settings.bottom_text, tick_time);

    if (s_bottom_needs_uptime) {  // Use cached flag
      replace_uptime(s_day_buffer, sizeof(s_day_buffer), now);
    }

    text_layer_set_text(s_day_layer, s_day_buffer);
  }

  // Update date number only when day changes
  if (day_changed) {
    s_last_day = current_day;
    strftime(s_date_buffer, sizeof(s_date_buffer), "%d", tick_time);
    text_layer_set_text(s_date_layer, s_date_buffer);
  }
}

static void init_wake_time() {
  #if defined(PBL_HEALTH)
  time_t now = time(NULL);

  // Init current sleep state
  HealthActivityMask activities = health_service_peek_current_activities();
  s_was_sleeping = activities & HealthActivitySleep;

  // Check if cached wake time is still valid (<12 hours old)
  if (s_wake_time > 0 && s_wake_time <= now && (now - s_wake_time) < 12 * 3600) {
    return;  // Valid in-memory cache
  }

  // No valid cache - estimate from health data if awake
  if (!s_was_sleeping) {
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

static void update_progress() {
  int new_percent = 0;

  switch (settings.progress_bar_mode) {
    case PROGRESS_MODE_BATTERY:
      new_percent = battery_state_service_peek().charge_percent;
      break;

    case PROGRESS_MODE_STEPS:
      #if defined(PBL_HEALTH)
      {
        // Use cached step count from update_steps() - no refetch needed
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

// Fetch and cache step count (used by both text display and progress bar)
// NOTE: update_steps() must be called before update_progress() when in PROGRESS_MODE_STEPS
static void fetch_step_count() {
  #if defined(PBL_HEALTH)
  HealthMetric metric = HealthMetricStepCount;
  time_t start = time_start_of_today();
  time_t end = time(NULL);

  HealthServiceAccessibilityMask mask = health_service_metric_accessible(metric, start, end);

  if (mask & HealthServiceAccessibilityMaskAvailable) {
    s_last_step_count = (int)health_service_sum_today(metric);
  } else {
    s_last_step_count = 0;
  }
  #endif
}

static void update_steps() {
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

static void update_weather_layers() {
  if (settings.show_weather && settings.temperature) {
    static char temperature_buffer[8];
    static char conditions_buffer[32];

    // Only reformat strings if temperature changed
    if (settings.temperature != s_last_temperature) {
      s_last_temperature = settings.temperature;

      if (settings.weather_use_metric) {
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%dC", settings.temperature);
      } else {
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%dF", s_cached_temp_f);
      }
      strncpy(conditions_buffer, settings.condition, sizeof(conditions_buffer));

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
          GRect(x, condition_y, INFO_LAYER_WIDTH, TEXT_HEIGHT)
        );
        layer_set_frame(text_layer_get_layer(s_temperature_layer),
          GRect(x, temperature_y, INFO_LAYER_WIDTH, TEXT_HEIGHT)
        );
      }
      else {
        int temperature_y = step_frame.origin.y - TEXT_HEIGHT;
        layer_set_frame(text_layer_get_layer(s_condition_layer), step_frame);
        layer_set_frame(text_layer_get_layer(s_temperature_layer),
          GRect(x, temperature_y, INFO_LAYER_WIDTH, TEXT_HEIGHT)
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
  int width = (s_progress_percent * bounds.size.w) / 100;

  // draw the background
  graphics_context_set_fill_color(ctx, color_bg);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // draw the progress bar
  graphics_context_set_fill_color(ctx, color_fg);
  graphics_fill_rect(ctx, GRect(0, 0, width, bounds.size.h), 0, GCornerNone);
}

static void load_fonts() {
  #if defined(PBL_PLATFORM_EMERY)
    s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_76));
  #else
    s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_58));
  #endif
  s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_RAJDHANI_24));
  s_text_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ORBITRON_12));
}

static void load_progress_layer(int x, int y) {
  s_progress_layer = layer_create(GRect(x+PROGRESS_BAR_OFFSET_X, y+PROGRESS_BAR_OFFSET_Y, PROGRESS_BAR_WIDTH, PROGRESS_BAR_HEIGHT));
  layer_set_update_proc(s_progress_layer, progress_update_proc);
}

static void load_hud_layer(int x, int y) {
  s_hud_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_HUD);
  s_hud_layer = bitmap_layer_create(
    GRect(x, y, HUD_WIDTH, HUD_HEIGHT)
  );
  bitmap_layer_set_bitmap(s_hud_layer, s_hud_bitmap);
  bitmap_layer_set_alignment(s_hud_layer, GAlignCenter);
  bitmap_layer_set_compositing_mode(s_hud_layer, GCompOpSet);
}

static void load_charge_layer(int x, int y) {
  s_charge_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CHARGE);
  s_charge_layer = bitmap_layer_create(
    GRect(x+PROGRESS_BAR_OFFSET_X, y+PROGRESS_BAR_OFFSET_Y, 5, 8)
  );
  bitmap_layer_set_bitmap(s_charge_layer, s_charge_bitmap);
  bitmap_layer_set_alignment(s_charge_layer, GAlignTopLeft);
  bitmap_layer_set_compositing_mode(s_charge_layer, GCompOpSet);
}

static void load_time_layer(int x, int y) {
  s_time_layer = text_layer_create(
    GRect(x, y, TIME_LAYER_WIDTH, TIME_LAYER_HEIGHT)
  );
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, color_fg);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentLeft);
}

static void load_date_layer(int x, int y) {
  s_date_layer = text_layer_create(
    GRect(x+4, y+4, DATE_LAYER_SIZE, DATE_LAYER_SIZE)
  );
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, color_fg);
  text_layer_set_font(s_date_layer, s_date_font);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
}

static void load_day_layer(int x, int y) {
  s_day_layer = text_layer_create(
    GRect(x+DAY_LAYER_OFFSET_X, y+DAY_LAYER_OFFSET_Y, DAY_LAYER_WIDTH, TEXT_HEIGHT)
  );
  text_layer_set_background_color(s_day_layer, GColorClear);
  text_layer_set_text_color(s_day_layer, color_fg);
  text_layer_set_font(s_day_layer, s_text_font);
  text_layer_set_text_alignment(s_day_layer, GTextAlignmentLeft);
}

// Used for small text layers like weather and steps
static void load_info_layer(TextLayer **layer, int y) {
  *layer = text_layer_create(
    GRect(MARGIN_SIZE, y, INFO_LAYER_WIDTH, TEXT_HEIGHT)
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

  // Use platform-specific HUD dimensions
  int hud_y = bounds.size.h - HUD_HEIGHT - MARGIN_SIZE - 3;
  int time_y = hud_y - TIME_LAYER_HEIGHT - 2;
  int custom_y = MARGIN_SIZE;
  int bt_y = custom_y;
  int step_y = time_y + 2;
  int condition_y = step_y - TEXT_HEIGHT;
  int temperature_y = condition_y - TEXT_HEIGHT;

  load_hud(MARGIN_SIZE, hud_y);
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

  // Refresh steps text every 30 minutes
  if (tick_time->tm_min % 30 == 0) {
    update_steps();
  }

  // Progress bar updates based on mode:
  // - Battery: updated via battery_callback (event-driven)
  // - Steps: every 30 minutes
  // - Sleep: updated via health_handler on wake events
  if (settings.progress_bar_mode == PROGRESS_MODE_STEPS && tick_time->tm_min % 30 == 0) {
    update_progress();
  }

  // Refresh weather every hour
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

  // Update progress bar if in battery mode
  if (settings.progress_bar_mode == PROGRESS_MODE_BATTERY) {
    update_progress();
  }
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
    // Check for sleep → awake transition (using cached flag)
    if (s_needs_sleep_tracking) {
      HealthActivityMask activities = health_service_peek_current_activities();
      bool is_sleeping = activities & HealthActivitySleep;

      if (s_was_sleeping && !is_sleeping) {
        // Just woke up - record wake time
        s_wake_time = time(NULL);

        // Update sleep progress bar on wake
        if (settings.progress_bar_mode == PROGRESS_MODE_SLEEP) {
          update_progress();
        }
      }
      s_was_sleeping = is_sleeping;
    }

    update_steps();
  }
  if (event == HealthEventMovementUpdate) {
    update_steps();
  }
}
#endif

static void update_health_subscription() {
  #if defined(PBL_HEALTH)
  // Subscribe if showing steps OR tracking sleep (for uptime/progress bar)
  if (settings.show_steps || s_needs_sleep_tracking) {
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
  settings.skip_location = true;  // Default to skip (for Gadgetbridge users)
  settings.hour_vibe = false;
  settings.disconnect_alert = true;
  settings.temperature = (int)NULL;
  settings.progress_bar_mode = PROGRESS_MODE_SLEEP;
  settings.step_goal = DEFAULT_STEP_GOAL;
  settings.sleep_goal_mins = DEFAULT_SLEEP_GOAL_MINS;
  strncpy(settings.custom_text, "PBL_%m%U%j", sizeof(settings.custom_text));
  strncpy(settings.bottom_text, "%Y.%m.%d", sizeof(settings.bottom_text));
  strncpy(settings.condition, "", sizeof(settings.condition));
}

static void load_settings() {
  default_settings();
  persist_read_data(SETTINGS_KEY, &settings, sizeof(settings));
  s_cached_temp_f = settings.temperature * 9 / 5 + 32;  // Cache Fahrenheit
  recalculate_update_periods();
}

static void save_settings() {
  persist_write_data(SETTINGS_KEY, &settings, sizeof(settings));

  // Recalculate update periods for format strings
  recalculate_update_periods();

  // Invalidate caches to force refresh after settings change
  s_last_minute = -1;
  s_last_day = -1;
  s_last_progress_percent = -1;
  s_last_step_count = -1;
  s_last_temperature = -999;

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

static void inbox_received_callback(DictionaryIterator *it, void *ctx) {
  Tuple *temperature_t = dict_find(it, MESSAGE_KEY_TEMPERATURE);
  Tuple *conditions_t = dict_find(it, MESSAGE_KEY_CONDITIONS);
  if (temperature_t && conditions_t) {
    settings.temperature = temperature_t->value->int32;
    s_cached_temp_f = settings.temperature * 9 / 5 + 32;  // Pre-calculate Fahrenheit
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

  save_settings();
}

static void init() {
  load_settings();

  // Only initialize wake time tracking if user has $U in any text field
  if (s_any_needs_uptime) {
    init_wake_time();
  }

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
  const int outbox_size = 8;
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
  update_progress();
  update_weather_layers();
  update_health_subscription();
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
