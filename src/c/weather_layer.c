#include <pebble.h>
#include "constants.h"
#include "settings.h"
#include "globals.h"
#include "weather_layer.h"
#include "steps_layer.h"

// ============================================================
// LAYERS
// ============================================================
TextLayer *s_temperature_layer;
TextLayer *s_condition_layer;
#if defined(PBL_PLATFORM_EMERY)
TextLayer *s_temp_slash_layer;
TextLayer *s_temp_high_layer;
#endif

// ============================================================
// EMERY COLOR FUNCTIONS
// ============================================================
#if defined(PBL_PLATFORM_EMERY)
GColor get_temperature_color(int temp_f) {
  // O(1) lookup using compile-time LUT with bounds check
  if (temp_f < 0) {
    return (GColor){ .argb = TEMP_COLOR_BELOW_0 };
  }
  if (temp_f > 100) {
    return (GColor){ .argb = TEMP_COLOR_SCORCHING };
  }
  return (GColor){ .argb = s_temp_lut[temp_f] };
}

// Calculate width of temperature string (e.g., "72F" or "-5C")
int get_temp_text_width(int temp, bool metric) {
  int width = 0;
  int abs_temp = temp < 0 ? -temp : temp;

  // Negative sign
  if (temp < 0) {
    width += INFO_CHAR_WIDTH_MINUS + INFO_KERNING;
  }

  // Digits
  if (abs_temp >= 100) {
    width += s_info_digit_widths[abs_temp / 100] + INFO_KERNING;
    abs_temp %= 100;
    width += s_info_digit_widths[abs_temp / 10] + INFO_KERNING;
    width += s_info_digit_widths[abs_temp % 10] + INFO_KERNING;
  } else if (abs_temp >= 10) {
    width += s_info_digit_widths[abs_temp / 10] + INFO_KERNING;
    width += s_info_digit_widths[abs_temp % 10] + INFO_KERNING;
  } else {
    width += s_info_digit_widths[abs_temp] + INFO_KERNING;
  }

  // Unit suffix
  width += metric ? INFO_CHAR_WIDTH_C : INFO_CHAR_WIDTH_F;

  return width;
}

GColor get_condition_color(const char *condition) {
  // Clear/Sunny
  if (strstr(condition, "CLEAR")) {
    return (GColor){ .argb = WEATHER_COLOR_CLEAR };
  }
  // Thunderstorm
  if (strstr(condition, "THNDR") || strstr(condition, "STORM")) {
    return (GColor){ .argb = WEATHER_COLOR_THUNDER };
  }
  // Snow
  if (strstr(condition, "SNOW") || strstr(condition, "SNW")) {
    return (GColor){ .argb = WEATHER_COLOR_SNOW };
  }
  // Freezing precipitation
  if (strstr(condition, "FRZ")) {
    return (GColor){ .argb = WEATHER_COLOR_FREEZING };
  }
  // Rain/Showers/Drizzle
  if (strstr(condition, "RAIN") || strstr(condition, "SHOWER") || strstr(condition, "DRIZZLE")) {
    return (GColor){ .argb = WEATHER_COLOR_RAIN };
  }
  // Fog
  if (strstr(condition, "FOG")) {
    return (GColor){ .argb = WEATHER_COLOR_FOG };
  }
  // Cloudy/Overcast
  if (strstr(condition, "CLOUD") || strstr(condition, "OVERCAST")) {
    return (GColor){ .argb = WEATHER_COLOR_CLOUD };
  }
  // Default
  return color_fg;
}
#endif

// ============================================================
// LOAD WEATHER LAYERS
// ============================================================
void load_weather_layers(int temperature_y, int condition_y) {
  s_condition_layer = text_layer_create(GRect(MARGIN_SIZE, condition_y, INFO_LAYER_WIDTH, TEXT_HEIGHT));
  text_layer_set_background_color(s_condition_layer, GColorClear);
  text_layer_set_text_color(s_condition_layer, color_fg);
  text_layer_set_font(s_condition_layer, s_text_font);

  s_temperature_layer = text_layer_create(GRect(MARGIN_SIZE, temperature_y, INFO_LAYER_WIDTH, TEXT_HEIGHT));
  text_layer_set_background_color(s_temperature_layer, GColorClear);
  text_layer_set_text_color(s_temperature_layer, color_fg);
  text_layer_set_font(s_temperature_layer, s_text_font);

  #if defined(PBL_PLATFORM_EMERY)
  // Split temperature: "72F" "/" "85F" with independent colors
  s_temp_slash_layer = text_layer_create(GRect(MARGIN_SIZE + TEMP_SLASH_OFFSET, temperature_y, 10, TEXT_HEIGHT));
  text_layer_set_background_color(s_temp_slash_layer, GColorClear);
  text_layer_set_text_color(s_temp_slash_layer, color_fg);
  text_layer_set_font(s_temp_slash_layer, s_text_font);
  text_layer_set_text(s_temp_slash_layer, "/");

  s_temp_high_layer = text_layer_create(GRect(MARGIN_SIZE + TEMP_HIGH_OFFSET, temperature_y, 60, TEXT_HEIGHT));
  text_layer_set_background_color(s_temp_high_layer, GColorClear);
  text_layer_set_text_color(s_temp_high_layer, color_fg);
  text_layer_set_font(s_temp_high_layer, s_text_font);
  #endif
}

// ============================================================
// UNLOAD WEATHER LAYERS
// ============================================================
void unload_weather_layers(void) {
  text_layer_destroy(s_temperature_layer);
  text_layer_destroy(s_condition_layer);
  #if defined(PBL_PLATFORM_EMERY)
  text_layer_destroy(s_temp_slash_layer);
  text_layer_destroy(s_temp_high_layer);
  #endif
}

// ============================================================
// UPDATE WEATHER LAYERS
// ============================================================
void update_weather_layers(void) {
  if (settings.show_weather && settings.temperature != -999) {
    static char temperature_buffer[8];
    static char conditions_buffer[32];
    #if defined(PBL_PLATFORM_EMERY)
    static char temp_high_buffer[8];
    #endif

    // Update current temperature display if changed
    bool current_changed = (settings.temperature != s_last_temperature);
    if (current_changed) {
      s_last_temperature = settings.temperature;

      if (settings.weather_use_metric) {
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%dC", settings.temperature);
      } else {
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%dF", s_cached_temp_f);
      }
      text_layer_set_text(s_temperature_layer, temperature_buffer);

      #if defined(PBL_PLATFORM_EMERY)
      // Update current temp color
      switch (settings.temperature_color_mode) {
        case COLOR_MODE_DISABLED:
          s_effective_temp_color = color_fg;
          break;
        case COLOR_MODE_STATIC:
          s_effective_temp_color = settings.temperature_static_color;
          break;
        case COLOR_MODE_DYNAMIC:
        default:
          s_effective_temp_color = get_temperature_color(s_cached_temp_f);
          break;
      }
      text_layer_set_text_color(s_temperature_layer, s_effective_temp_color);

      // Reposition "/" and high temp based on current temp width
      int display_temp = settings.weather_use_metric ? settings.temperature : s_cached_temp_f;
      int current_width = get_temp_text_width(display_temp, settings.weather_use_metric);
      GRect temp_frame = layer_get_frame(text_layer_get_layer(s_temperature_layer));
      int slash_x = temp_frame.origin.x + current_width + 2;
      int high_x = slash_x + INFO_CHAR_WIDTH_SLASH + 2;
      layer_set_frame(text_layer_get_layer(s_temp_slash_layer),
        GRect(slash_x, temp_frame.origin.y, 10, TEXT_HEIGHT));
      layer_set_frame(text_layer_get_layer(s_temp_high_layer),
        GRect(high_x, temp_frame.origin.y, 60, TEXT_HEIGHT));
      #endif

      // Update conditions when current temp changes (they come together)
      strncpy(conditions_buffer, settings.condition, sizeof(conditions_buffer));
      text_layer_set_text(s_condition_layer, conditions_buffer);

      #if defined(PBL_PLATFORM_EMERY)
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
      text_layer_set_text_color(s_condition_layer, s_effective_condition_color);
      #endif
    }

    // Determine which high to display: tomorrow's after sunset, today's before
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int current_hour = t->tm_hour;
    bool use_tomorrow = (current_hour >= settings.sunset_hour);
    int effective_high = use_tomorrow ? settings.temperature_high_tomorrow : settings.temperature_high;
    int effective_high_f = effective_high * 9 / 5 + 32;

    #if defined(PBL_PLATFORM_EMERY)
    // Update high temperature display if changed (independent of current)
    if (effective_high != s_last_temperature_high) {
      s_last_temperature_high = effective_high;

      if (settings.weather_use_metric) {
        snprintf(temp_high_buffer, sizeof(temp_high_buffer), "%dC", effective_high);
      } else {
        snprintf(temp_high_buffer, sizeof(temp_high_buffer), "%dF", effective_high_f);
      }
      text_layer_set_text(s_temp_high_layer, temp_high_buffer);

      // Update high temp color
      switch (settings.temperature_color_mode) {
        case COLOR_MODE_DISABLED:
          s_effective_temp_high_color = color_fg;
          break;
        case COLOR_MODE_STATIC:
          s_effective_temp_high_color = settings.temperature_static_color;
          break;
        case COLOR_MODE_DYNAMIC:
        default:
          s_effective_temp_high_color = get_temperature_color(effective_high_f);
          break;
      }
      text_layer_set_text_color(s_temp_high_layer, s_effective_temp_high_color);
    }
    #else
    // Non-Emery: combined display, update when either changes
    if (current_changed || effective_high != s_last_temperature_high) {
      s_last_temperature_high = effective_high;
      if (settings.weather_use_metric) {
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%d/%dC",
                 settings.temperature, effective_high);
      } else {
        snprintf(temperature_buffer, sizeof(temperature_buffer), "%d/%dF",
                 s_cached_temp_f, effective_high_f);
      }
      text_layer_set_text(s_temperature_layer, temperature_buffer);
    }
    #endif

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
    #if defined(PBL_PLATFORM_EMERY)
    layer_set_hidden(text_layer_get_layer(s_temp_slash_layer), false);
    layer_set_hidden(text_layer_get_layer(s_temp_high_layer), false);
    #endif
  }
  else {
    layer_set_hidden(text_layer_get_layer(s_condition_layer), true);
    layer_set_hidden(text_layer_get_layer(s_temperature_layer), true);
    #if defined(PBL_PLATFORM_EMERY)
    layer_set_hidden(text_layer_get_layer(s_temp_slash_layer), true);
    layer_set_hidden(text_layer_get_layer(s_temp_high_layer), true);
    #endif
  }
}
