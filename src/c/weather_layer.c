#include <pebble.h>
#include "constants.h"
#include "settings.h"
#include "globals.h"
#include "weather_layer.h"
#include "steps_layer.h"
#include "weather_format.h"

// ============================================================
// LAYERS
// ============================================================
TextLayer *s_condition_layer;

#if defined(PBL_PLATFORM_EMERY)
// One custom layer draws the whole temperature line. The Pebble SDK has
// no attributed-text API, so multi-coloured text is either N TextLayers
// or one draw proc that sets the colour between segments. At five
// segments the draw proc wins: one layer, one place that positions
// things, and no per-layer frame bookkeeping.
static Layer *s_temp_layer;
#else
static TextLayer *s_temp_text_layer;
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

// Width of a temperature's sign and digits, without the unit suffix.
// Split out of get_temp_text_width so the low can be measured on its
// own - it renders as "58/" with no C/F of its own.
int get_temp_digits_width(int temp) {
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

  return width;
}

// Calculate width of temperature string (e.g., "72F" or "-5C")
int get_temp_text_width(int temp, bool metric) {
  // Unit suffix
  return get_temp_digits_width(temp) + (metric ? INFO_CHAR_WIDTH_C : INFO_CHAR_WIDTH_F);
}

// Same, in the smaller cut used for the high/low pair.
int get_temp_small_digits_width(int temp) {
  int width = 0;
  int abs_temp = temp < 0 ? -temp : temp;

  if (temp < 0) {
    width += INFO_SMALL_CHAR_WIDTH_MINUS + INFO_KERNING;
  }

  if (abs_temp >= 100) {
    width += s_info_small_digit_widths[abs_temp / 100] + INFO_KERNING;
    abs_temp %= 100;
    width += s_info_small_digit_widths[abs_temp / 10] + INFO_KERNING;
    width += s_info_small_digit_widths[abs_temp % 10] + INFO_KERNING;
  } else if (abs_temp >= 10) {
    width += s_info_small_digit_widths[abs_temp / 10] + INFO_KERNING;
    width += s_info_small_digit_widths[abs_temp % 10] + INFO_KERNING;
  } else {
    width += s_info_small_digit_widths[abs_temp] + INFO_KERNING;
  }

  return width;
}

// Small-font width including the unit suffix. The high and low each
// carry their own C/F at this size.
int get_temp_small_text_width(int temp, bool metric) {
  return get_temp_small_digits_width(temp) +
         (metric ? INFO_SMALL_CHAR_WIDTH_C : INFO_SMALL_CHAR_WIDTH_F);
}

// Width of the "T_" tomorrow marker.
int get_tomorrow_prefix_width(void) {
  return INFO_CHAR_WIDTH_T + INFO_KERNING + INFO_CHAR_WIDTH_UNDERSCORE;
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
// CURRENT RANGE
// ============================================================
// Resolve settings into a display-ready range. Which day is shown and
// how each piece reads is decided in weather_format, which is unit
// tested on the host; this is only the glue that supplies the clock.
static WeatherRange current_range(bool metric) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  WeatherRange r = weather_select_range(
    settings.temperature,
    settings.temperature_low, settings.temperature_high,
    settings.temperature_low_tomorrow, settings.temperature_high_tomorrow,
    t->tm_hour, settings.sunset_hour,
    metric
  );

  #if FORCE_TOMORROW_PREFIX
  // Debug only. Flipped after selection, so the displayed temperatures
  // are still whichever day the real rule picked - only the marker is
  // forced on. See constants.h.
  if (r.valid && r.has_high) {
    r.is_tomorrow = true;
  }
  #endif

  return r;
}

#if defined(PBL_PLATFORM_EMERY)
// ============================================================
// TEMPERATURE RENDER PLAN
// ============================================================
// The draw proc must stay free of computation. Marking any layer dirty
// re-renders the whole window layer tree, so this proc runs at least
// once a minute when the clock ticks - doing localtime(), snprintf()
// and colour lookups in there would repeat that work every minute for
// data that only changes hourly.
//
// So the segments are resolved once, whenever the displayed value
// actually changes, and the proc just blits them.
#define TEMP_MAX_SEGMENTS 6

typedef struct {
  char text[8];
  GColor color;
  int16_t x;
  int16_t y;      // Offset from the top of the line
  bool small;     // Draw in the smaller info font
} TempSegment;

static TempSegment s_segments[TEMP_MAX_SEGMENTS];
static int s_segment_count = 0;

// Resolve a temperature colour honouring the user's colour mode.
static GColor temp_color_for(int temp_f) {
  switch (settings.temperature_color_mode) {
    case COLOR_MODE_DISABLED:
      return color_fg;
    case COLOR_MODE_STATIC:
      return settings.temperature_static_color;
    case COLOR_MODE_DYNAMIC:
    default:
      return get_temperature_color(temp_f);
  }
}

static void add_segment(const char *text, GColor color, int x, int y, bool small) {
  if (!text || text[0] == '\0' || s_segment_count >= TEMP_MAX_SEGMENTS) {
    return;
  }
  TempSegment *seg = &s_segments[s_segment_count];
  strncpy(seg->text, text, sizeof(seg->text) - 1);
  seg->text[sizeof(seg->text) - 1] = '\0';
  seg->color = color;
  seg->x = (int16_t)x;
  seg->y = (int16_t)y;
  seg->small = small;
  s_segment_count++;
}

// Lay out the line left to right. Widths come from the measured
// Orbitron metrics in constants.h; the 2px gaps are the original
// spacing between the current temperature and the range.
static void rebuild_temp_segments(const WeatherRange *r, bool metric) {
  s_segment_count = 0;

  if (!r || !r->valid) {
    return;
  }

  // Only the dynamic colour mode needs Fahrenheit values to index the
  // LUT with. In the other two modes the colour is fixed, so skip the
  // second conversion pass entirely.
  WeatherRange f_range;
  if (settings.temperature_color_mode == COLOR_MODE_DYNAMIC) {
    f_range = metric ? current_range(false) : *r;
  } else {
    f_range = *r;  // Values unused; colours are fixed.
  }

  const WeatherRange range = *r;
  char buf[8];
  int x = 0;

  // Current temperature, full size, carrying its own unit.
  weather_format_current(buf, sizeof(buf), &range, metric);
  add_segment(buf, temp_color_for(f_range.current), x, 0, false);
  x += get_temp_text_width(range.current, metric) + TEMP_RANGE_GAP;

  if (!range.has_high) {
    return;  // Nothing else to show.
  }

  // Tomorrow marker - not a temperature, so it stays neutral.
  if (range.is_tomorrow) {
    add_segment(WEATHER_TOMORROW_PREFIX, color_fg, x, 0, false);
    x += get_tomorrow_prefix_width();
  }

  // The range is set as a fraction: low first but dropped, high second
  // and raised, a full-size separator between them. High sits on top
  // because that is what the numbers mean. Each carries its own unit.
  if (range.has_low) {
    weather_format_low(buf, sizeof(buf), &range, metric);
    add_segment(buf, temp_color_for(f_range.low), x, TEMP_SMALL_LOW_Y, true);
    x += get_temp_small_text_width(range.low, metric) + TEMP_FRACTION_GAP;
  }

  weather_format_separator(buf, sizeof(buf), &range);
  add_segment(buf, color_fg, x, 0, false);
  x += INFO_CHAR_WIDTH_SLASH + TEMP_FRACTION_GAP;

  weather_format_high(buf, sizeof(buf), &range, metric);
  add_segment(buf, temp_color_for(f_range.high), x, TEMP_SMALL_HIGH_Y, true);
}

// ============================================================
// TEMPERATURE DRAW PROC
// ============================================================
// Pure blit - no allocation, no formatting, no time lookups.
static void temperature_update_proc(Layer *layer, GContext *ctx) {
  int height = layer_get_bounds(layer).size.h;

  for (int i = 0; i < s_segment_count; i++) {
    const TempSegment *seg = &s_segments[i];
    graphics_context_set_text_color(ctx, seg->color);
    graphics_draw_text(ctx, seg->text,
                       seg->small ? s_text_font_small : s_text_font,
                       GRect(seg->x, seg->y, INFO_LAYER_WIDTH - seg->x, height - seg->y),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}
#endif

// ============================================================
// LAYER ACCESSOR
// ============================================================
Layer *weather_temperature_layer(void) {
  #if defined(PBL_PLATFORM_EMERY)
  return s_temp_layer;
  #else
  return text_layer_get_layer(s_temp_text_layer);
  #endif
}

// ============================================================
// LOAD WEATHER LAYERS
// ============================================================
void load_weather_layers(int temperature_y, int condition_y) {
  s_condition_layer = text_layer_create(GRect(MARGIN_SIZE, condition_y, INFO_LAYER_WIDTH, TEXT_HEIGHT));
  text_layer_set_background_color(s_condition_layer, GColorClear);
  text_layer_set_text_color(s_condition_layer, color_fg);
  text_layer_set_font(s_condition_layer, s_text_font);

  #if defined(PBL_PLATFORM_EMERY)
  s_temp_layer = layer_create(GRect(MARGIN_SIZE, temperature_y, INFO_LAYER_WIDTH, TEXT_HEIGHT));
  layer_set_update_proc(s_temp_layer, temperature_update_proc);
  #else
  s_temp_text_layer = text_layer_create(GRect(MARGIN_SIZE, temperature_y, INFO_LAYER_WIDTH, TEXT_HEIGHT));
  text_layer_set_background_color(s_temp_text_layer, GColorClear);
  text_layer_set_text_color(s_temp_text_layer, color_fg);
  text_layer_set_font(s_temp_text_layer, s_text_font);
  #endif
}

// ============================================================
// UNLOAD WEATHER LAYERS
// ============================================================
void unload_weather_layers(void) {
  text_layer_destroy(s_condition_layer);
  #if defined(PBL_PLATFORM_EMERY)
  layer_destroy(s_temp_layer);
  #else
  text_layer_destroy(s_temp_text_layer);
  #endif
}

// ============================================================
// RENDER CACHE
// ============================================================
// Rendering is gated on comparing freshly formatted strings against
// what is on screen. That covers every input at once - current, low,
// high, unit and the today/tomorrow switch - instead of tracking a
// separate "last value" int per field and forgetting one.
static char s_rendered_condition[32];
static char s_rendered_temps[24];

void invalidate_weather_render_cache(void) {
  s_rendered_condition[0] = '\0';
  s_rendered_temps[0] = '\0';
}

// ============================================================
// UPDATE WEATHER LAYERS
// ============================================================
void update_weather_layers(void) {
  #if DEMO_MODE
  // Hard-code demo weather: 61F current, 48F low / 85F high, clear skies
  settings.show_weather = true;
  settings.temperature = 16;          // ~61F
  settings.temperature_low = 9;       // ~48F
  settings.temperature_high = 29;     // ~85F
  settings.weather_use_metric = false;
  strncpy(settings.condition, "CLEAR", sizeof(settings.condition));
  #endif

  Layer *temp_layer = weather_temperature_layer();

  if (settings.show_weather && settings.temperature != WEATHER_NO_DATA) {
    bool metric = settings.weather_use_metric;
    WeatherRange range = current_range(metric);

    // Conditions arrive alongside the current temperature.
    if (strcmp(s_rendered_condition, settings.condition) != 0) {
      strncpy(s_rendered_condition, settings.condition, sizeof(s_rendered_condition) - 1);
      s_rendered_condition[sizeof(s_rendered_condition) - 1] = '\0';
      text_layer_set_text(s_condition_layer, s_rendered_condition);

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

    // The combined string is the single source of truth for "has
    // anything visible changed", on both the drawn and text paths.
    // Everything downstream of this check runs only when it fires, so
    // the per-minute redraw costs nothing but the blit itself.
    char temps[sizeof(s_rendered_temps)];
    weather_format_combined(temps, sizeof(temps), &range, metric);

    if (strcmp(s_rendered_temps, temps) != 0) {
      strcpy(s_rendered_temps, temps);
      #if defined(PBL_PLATFORM_EMERY)
      rebuild_temp_segments(&range, metric);
      layer_mark_dirty(temp_layer);
      #else
      text_layer_set_text(s_temp_text_layer, s_rendered_temps);
      #endif
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
        layer_set_frame(temp_layer,
          GRect(x, temperature_y, INFO_LAYER_WIDTH, TEXT_HEIGHT)
        );
      }
      else {
        int temperature_y = step_frame.origin.y - TEXT_HEIGHT;
        layer_set_frame(text_layer_get_layer(s_condition_layer), step_frame);
        layer_set_frame(temp_layer,
          GRect(x, temperature_y, INFO_LAYER_WIDTH, TEXT_HEIGHT)
        );
      }
    }

    layer_set_hidden(text_layer_get_layer(s_condition_layer), false);
    layer_set_hidden(temp_layer, false);
  }
  else {
    layer_set_hidden(text_layer_get_layer(s_condition_layer), true);
    layer_set_hidden(temp_layer, true);
  }
}
