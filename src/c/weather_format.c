#include "weather_format.h"

// ============================================================
// UNIT CONVERSION
// ============================================================
int weather_to_display_units(int celsius, bool metric) {
  if (metric) {
    return celsius;
  }
  return celsius * 9 / 5 + 32;
}

static const char *unit_suffix(bool metric) {
  return metric ? "C" : "F";
}

// ============================================================
// DAY SELECTION
// ============================================================
WeatherRange weather_select_range(
  int current_c,
  int low_today_c, int high_today_c,
  int low_tomorrow_c, int high_tomorrow_c,
  int hour, int sunset_hour,
  bool metric
) {
  WeatherRange r = { 0 };

  if (current_c == WEATHER_NO_DATA) {
    return r;  // Nothing to show at all.
  }
  r.valid = true;
  r.current = weather_to_display_units(current_c, metric);

  int effective_sunset = (sunset_hour >= 0) ? sunset_hour : WEATHER_DEFAULT_SUNSET_HOUR;

  // After sunset today's high is history - what matters is the night
  // ahead and tomorrow. Only switch if tomorrow actually has data,
  // otherwise we would render the sentinel as a temperature.
  bool tomorrow_known = (low_tomorrow_c != WEATHER_NO_DATA) ||
                        (high_tomorrow_c != WEATHER_NO_DATA);
  r.is_tomorrow = (hour >= effective_sunset) && tomorrow_known;

  int low_c = r.is_tomorrow ? low_tomorrow_c : low_today_c;
  int high_c = r.is_tomorrow ? high_tomorrow_c : high_today_c;

  r.has_low = (low_c != WEATHER_NO_DATA);
  r.has_high = (high_c != WEATHER_NO_DATA);

  if (r.has_low) {
    r.low = weather_to_display_units(low_c, metric);
  }
  if (r.has_high) {
    r.high = weather_to_display_units(high_c, metric);
  }

  return r;
}

// ============================================================
// SPLIT STRINGS (Emery: one layer per colour)
// ============================================================
void weather_format_current(char *buf, size_t size, const WeatherRange *r, bool metric) {
  if (!buf || size == 0) {
    return;
  }
  if (!r || !r->valid) {
    buf[0] = '\0';
    return;
  }
  snprintf(buf, size, "%d%s", r->current, unit_suffix(metric));
}

void weather_format_tomorrow_prefix(char *buf, size_t size, const WeatherRange *r) {
  if (!buf || size == 0) {
    return;
  }
  // Without a high there is no range for the mark to qualify.
  if (!r || !r->valid || !r->has_high || !r->is_tomorrow) {
    buf[0] = '\0';
    return;
  }
  snprintf(buf, size, "%s", WEATHER_TOMORROW_PREFIX);
}

void weather_format_low(char *buf, size_t size, const WeatherRange *r, bool metric) {
  if (!buf || size == 0) {
    return;
  }
  // Without a high there is no range for a low to belong to.
  if (!r || !r->valid || !r->has_high || !r->has_low) {
    buf[0] = '\0';
    return;
  }
  snprintf(buf, size, "%d%s", r->low, unit_suffix(metric));
}

void weather_format_separator(char *buf, size_t size, const WeatherRange *r) {
  if (!buf || size == 0) {
    return;
  }
  // The separator exists whenever there is a high to separate from -
  // with no low reported this degrades to the legacy "72F/85F" look.
  if (!r || !r->valid || !r->has_high) {
    buf[0] = '\0';
    return;
  }
  snprintf(buf, size, "%s", WEATHER_RANGE_SEPARATOR);
}

void weather_format_high(char *buf, size_t size, const WeatherRange *r, bool metric) {
  if (!buf || size == 0) {
    return;
  }
  if (!r || !r->valid || !r->has_high) {
    buf[0] = '\0';
    return;
  }
  snprintf(buf, size, "%d%s", r->high, unit_suffix(metric));
}

// ============================================================
// COMBINED STRING (single-layer platforms)
// ============================================================
void weather_format_combined(char *buf, size_t size, const WeatherRange *r, bool metric) {
  if (!buf || size == 0) {
    return;
  }
  if (!r || !r->valid) {
    buf[0] = '\0';
    return;
  }

  const char *unit = unit_suffix(metric);

  if (!r->has_high) {
    snprintf(buf, size, "%d%s", r->current, unit);
    return;
  }

  if (r->has_low) {
    // "72F 58\85F" - low first, matching the order the stacked layout
    // draws them in.
    snprintf(buf, size, "%d%s %s%d%s%d%s",
             r->current, unit,
             r->is_tomorrow ? WEATHER_TOMORROW_PREFIX : "",
             r->low, WEATHER_RANGE_SEPARATOR, r->high, unit);
    return;
  }

  // Legacy current/high form. Keep it tight when it refers to today,
  // matching what previous releases displayed.
  if (r->is_tomorrow) {
    snprintf(buf, size, "%d%s %s%s%d%s",
             r->current, unit, WEATHER_TOMORROW_PREFIX,
             WEATHER_RANGE_SEPARATOR, r->high, unit);
  } else {
    snprintf(buf, size, "%d%s%s%d%s",
             r->current, unit, WEATHER_RANGE_SEPARATOR, r->high, unit);
  }
}
