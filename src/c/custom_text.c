#include <pebble.h>
#include <string.h>
#include "constants.h"
#include "settings.h"
#include "globals.h"
#include "custom_text.h"
#include "hud_layer.h"
#include "utils.h"

// ============================================================
// LAYERS
// ============================================================
TextLayer *s_custom_layer;
TextLayer *s_bt_layer;

// ============================================================
// BUFFERS
// ============================================================
char s_custom_buffer[32];

// ============================================================
// UPDATE PERIODS
// ============================================================
UpdatePeriod s_custom_text_period = UPDATE_PERIOD_DAY;
UpdatePeriod s_bottom_text_period = UPDATE_PERIOD_DAY;
int s_last_custom_update = -1;
int s_last_bottom_update = -1;

// ============================================================
// LOAD CUSTOM TEXT LAYERS
// ============================================================
void load_custom_text_layers(int custom_y, int bt_y) {
  s_custom_layer = text_layer_create(GRect(MARGIN_SIZE, custom_y, INFO_LAYER_WIDTH, TEXT_HEIGHT));
  text_layer_set_background_color(s_custom_layer, GColorClear);
  text_layer_set_text_color(s_custom_layer, color_fg);
  text_layer_set_font(s_custom_layer, s_text_font);

  s_bt_layer = text_layer_create(GRect(MARGIN_SIZE, bt_y, INFO_LAYER_WIDTH, TEXT_HEIGHT));
  text_layer_set_background_color(s_bt_layer, GColorClear);
  text_layer_set_text_color(s_bt_layer, color_fg);
  text_layer_set_font(s_bt_layer, s_text_font);
  text_layer_set_text(s_bt_layer, "NO_CONNECTION");
}

// ============================================================
// UNLOAD CUSTOM TEXT LAYERS
// ============================================================
void unload_custom_text_layers(void) {
  text_layer_destroy(s_custom_layer);
  text_layer_destroy(s_bt_layer);
}

// ============================================================
// ANALYZE FORMAT PERIOD
// ============================================================
UpdatePeriod analyze_format_period(const char *fmt) {
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
      }
      p += 2;  // Skip the format code
    } else {
      p++;
    }
  }

  return period;
}

// ============================================================
// RECALCULATE UPDATE PERIODS
// ============================================================
void recalculate_update_periods(void) {
  s_custom_text_period = analyze_format_period(settings.custom_text);
  s_bottom_text_period = analyze_format_period(settings.bottom_text);

  // Cache uptime flags
  s_custom_needs_uptime = strstr(settings.custom_text, "$U") != NULL;
  s_bottom_needs_uptime = strstr(settings.bottom_text, "$U") != NULL;
  s_any_needs_uptime = s_custom_needs_uptime || s_bottom_needs_uptime;
  s_needs_sleep_tracking = s_any_needs_uptime ||
                           settings.progress_bar_mode == PROGRESS_MODE_SLEEP;

  // Cache 24h style
  s_is_24h_style = clock_is_24h_style();

  // Reset last update times to force immediate refresh
  s_last_custom_update = -1;
  s_last_bottom_update = -1;
}

// ============================================================
// REPLACE UPTIME
// ============================================================
// Wake time tracking (defined here, used by callbacks)
time_t s_wake_time = 0;

void replace_uptime(char *buf, size_t buf_size, time_t now) {
  char *pos = strstr(buf, "$U");
  if (!pos) return;

  // Calculate uptime string
  char uptime_str[16];

  if (s_wake_time > 0 && s_wake_time <= now) {
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
    memmove(pos + uptime_len, after, after_len + 1);
    memcpy(pos, uptime_str, uptime_len);
  }
}

// ============================================================
// SHOULD UPDATE FIELD
// ============================================================
bool should_update_field(int current_minute, int current_hour, int current_day,
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
      if (*last_update < 0 || (minute_of_day / 60) != (*last_update / 60)) {
        *last_update = minute_of_day;
        return true;
      }
      break;

    case UPDATE_PERIOD_DAY:
    default:
      if (*last_update < 0) {
        *last_update = minute_of_day;
        return true;
      }
      break;
  }
  return false;
}

// ============================================================
// UPDATE CUSTOM TEXT (TOP)
// ============================================================
void update_custom_text(struct tm *tick_time, time_t now, int current_minute,
                        int current_hour, int current_day, bool day_changed) {
  if (day_changed ||
      should_update_field(current_minute, current_hour, current_day,
                          s_custom_text_period, &s_last_custom_update)) {
    strftime(s_custom_buffer, sizeof(s_custom_buffer), settings.custom_text, tick_time);

    if (s_custom_needs_uptime) {
      replace_uptime(s_custom_buffer, sizeof(s_custom_buffer), now);
    }

    str_to_upper(s_custom_buffer);
    text_layer_set_text(s_custom_layer, s_custom_buffer);
  }
}

// ============================================================
// UPDATE BOTTOM TEXT
// ============================================================
void update_bottom_text(struct tm *tick_time, time_t now, int current_minute,
                        int current_hour, int current_day, bool day_changed) {
  if (day_changed ||
      should_update_field(current_minute, current_hour, current_day,
                          s_bottom_text_period, &s_last_bottom_update)) {
    strftime(s_day_buffer, sizeof(s_day_buffer), settings.bottom_text, tick_time);

    if (s_bottom_needs_uptime) {
      replace_uptime(s_day_buffer, sizeof(s_day_buffer), now);
    }

    text_layer_set_text(s_day_layer, s_day_buffer);
  }
}
