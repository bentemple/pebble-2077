#include <pebble.h>
#include "constants.h"
#include "settings.h"
#include "globals.h"
#include "time_layer.h"
#include "hud_layer.h"
#include "custom_text.h"

// ============================================================
// LAYERS
// ============================================================
TextLayer *s_time_layer;
#if defined(PBL_PLATFORM_EMERY)
TextLayer *s_time_hours_layer;
TextLayer *s_time_colon_layer;
TextLayer *s_time_mins_layer;
int s_hours_layer_x = 0;
int s_hours_layer_y = 0;
#endif

// ============================================================
// BUFFERS
// ============================================================
char s_time_buffer[8];
#if defined(PBL_PLATFORM_EMERY)
char s_time_hours_buffer[4];
char s_time_mins_buffer[4];

// Compile-time colon X offsets based on hour digit widths
static const int16_t s_colon_offsets_24h[24] = {
  HOUR_WIDTH(0,0), HOUR_WIDTH(0,1), HOUR_WIDTH(0,2), HOUR_WIDTH(0,3), HOUR_WIDTH(0,4),
  HOUR_WIDTH(0,5), HOUR_WIDTH(0,6), HOUR_WIDTH(0,7), HOUR_WIDTH(0,8), HOUR_WIDTH(0,9),
  HOUR_WIDTH(1,0), HOUR_WIDTH(1,1), HOUR_WIDTH(1,2), HOUR_WIDTH(1,3), HOUR_WIDTH(1,4),
  HOUR_WIDTH(1,5), HOUR_WIDTH(1,6), HOUR_WIDTH(1,7), HOUR_WIDTH(1,8), HOUR_WIDTH(1,9),
  HOUR_WIDTH(2,0), HOUR_WIDTH(2,1), HOUR_WIDTH(2,2), HOUR_WIDTH(2,3)
};
static const int16_t s_colon_offsets_12h[13] = {
  0,  // unused
  HOUR_WIDTH(0,1), HOUR_WIDTH(0,2), HOUR_WIDTH(0,3), HOUR_WIDTH(0,4), HOUR_WIDTH(0,5),
  HOUR_WIDTH(0,6), HOUR_WIDTH(0,7), HOUR_WIDTH(0,8), HOUR_WIDTH(0,9), HOUR_WIDTH(1,0),
  HOUR_WIDTH(1,1), HOUR_WIDTH(1,2)
};
#endif

// ============================================================
// LOAD TIME LAYER
// ============================================================
void load_time_layer(int x, int y) {
  #if defined(PBL_PLATFORM_EMERY)
  // Cache position for hourly repositioning
  s_hours_layer_x = x;
  s_hours_layer_y = y;

  // Pre-calculate positions for colon and minutes layers
  int colon_x = x + HOUR_WIDTH(0,0);
  int colon_y = y + COLON_Y_OFFSET;
  int mins_x_offset = settings.bold_hours ? MINS_BOLD_X_OFFSET : 0;
  int mins_x = colon_x + COLON_WIDTH + mins_x_offset;

  // Split time into hours, colon, minutes for accent coloring
  s_time_hours_layer = text_layer_create(GRect(x, y, INITIAL_TIME_HOURS_WIDTH, TIME_LAYER_HEIGHT));
  text_layer_set_background_color(s_time_hours_layer, GColorClear);
  text_layer_set_text_color(s_time_hours_layer, color_fg);
  text_layer_set_font(s_time_hours_layer, settings.bold_hours ? s_time_font_bold : s_time_font);
  text_layer_set_text_alignment(s_time_hours_layer, GTextAlignmentLeft);

  s_time_colon_layer = text_layer_create(GRect(colon_x, colon_y, COLON_WIDTH, TIME_LAYER_HEIGHT));
  text_layer_set_background_color(s_time_colon_layer, GColorClear);
  text_layer_set_text_color(s_time_colon_layer, color_fg);
  text_layer_set_font(s_time_colon_layer, settings.bold_hours ? s_time_font_regular : s_time_font);
  text_layer_set_text_alignment(s_time_colon_layer, GTextAlignmentLeft);
  text_layer_set_text(s_time_colon_layer, ":");

  s_time_mins_layer = text_layer_create(GRect(mins_x, y, INITIAL_TIME_MINS_WIDTH, TIME_LAYER_HEIGHT));
  text_layer_set_background_color(s_time_mins_layer, GColorClear);
  text_layer_set_text_color(s_time_mins_layer, color_fg);
  text_layer_set_font(s_time_mins_layer, settings.bold_hours ? s_time_font_regular : s_time_font);
  text_layer_set_text_alignment(s_time_mins_layer, GTextAlignmentLeft);
  #else
  s_time_layer = text_layer_create(GRect(x, y, TIME_LAYER_WIDTH, TIME_LAYER_HEIGHT));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, color_fg);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentLeft);
  #endif
}

// ============================================================
// UNLOAD TIME LAYER
// ============================================================
void unload_time_layer(void) {
  #if defined(PBL_PLATFORM_EMERY)
  text_layer_destroy(s_time_hours_layer);
  text_layer_destroy(s_time_colon_layer);
  text_layer_destroy(s_time_mins_layer);
  #else
  text_layer_destroy(s_time_layer);
  #endif
}

// ============================================================
// UPDATE TIME
// ============================================================
void update_time(void) {
  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);

  int current_minute = tick_time->tm_min;
  int current_hour = tick_time->tm_hour;
  int current_day = tick_time->tm_mday;
  bool day_changed = (current_day != s_last_day);

  // Always update main time display every minute
  if (current_minute != s_last_minute) {
    s_last_minute = current_minute;
    strftime(s_time_buffer, sizeof(s_time_buffer), s_time_format, tick_time);
    #if defined(PBL_PLATFORM_EMERY)
    // Split time for accent coloring: direct char copy (faster than strncpy) 
    // Also optionally could easily color hours/minutes separately
    s_time_hours_buffer[0] = s_time_buffer[0];
    s_time_hours_buffer[1] = s_time_buffer[1];
    s_time_hours_buffer[2] = '\0';
    s_time_mins_buffer[0] = s_time_buffer[3];
    s_time_mins_buffer[1] = s_time_buffer[4];
    s_time_mins_buffer[2] = '\0';
    text_layer_set_text(s_time_hours_layer, s_time_hours_buffer);
    text_layer_set_text(s_time_mins_layer, s_time_mins_buffer);

    // Reposition colon/minutes only when hour changes (uses compile-time lookup table)
    if (current_hour != s_last_hour) {
      s_last_hour = current_hour;

      int hour_index = s_is_24h_style ? current_hour : (current_hour % 12 == 0 ? 12 : current_hour % 12);
      int colon_x_offset = s_is_24h_style ? s_colon_offsets_24h[hour_index] : s_colon_offsets_12h[hour_index];
      int colon_x = s_hours_layer_x + colon_x_offset;
      int colon_y = s_hours_layer_y + COLON_Y_OFFSET;
      int mins_x_offset = settings.bold_hours ? MINS_BOLD_X_OFFSET : 0;
      int mins_x = colon_x + COLON_WIDTH + mins_x_offset;

      layer_set_frame(text_layer_get_layer(s_time_colon_layer),
        GRect(colon_x, colon_y, COLON_WIDTH, TIME_LAYER_HEIGHT));
      layer_set_frame(text_layer_get_layer(s_time_mins_layer),
        GRect(mins_x, s_hours_layer_y, INITIAL_TIME_MINS_WIDTH, TIME_LAYER_HEIGHT));
    }
    #else
    text_layer_set_text(s_time_layer, s_time_buffer);
    #endif
  }

  // Update custom text based on its calculated period
  update_custom_text(tick_time, now, current_minute, current_hour, current_day, day_changed);

  // Update bottom text based on its calculated period
  update_bottom_text(tick_time, now, current_minute, current_hour, current_day, day_changed);

  // Update date number
  update_date(tick_time);
}
