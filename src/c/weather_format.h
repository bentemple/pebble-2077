#pragma once

// Support both Pebble SDK and standard C builds
#ifdef PBL_SDK_3
  #include <pebble.h>
#else
  #include <stdio.h>
  #include <stdint.h>
  #include <stdbool.h>
  #include <stddef.h>
#endif

// ============================================================
// WEATHER DISPLAY FORMATTING
// ============================================================
// Decides which day's low/high to show and renders the strings.
//
// Kept free of Pebble APIs so the day-selection rule and the string
// building can be unit tested on the host - the parts most likely to
// go wrong are the sentinel handling and the sunset boundary, neither
// of which is convenient to exercise on a watch.
//
// Display forms:
//   72F 58\85F      low and high for today
//   72F T[58\85F]   low and high for tomorrow (past sunset)
//   72F/85F        high only, no low available (legacy phone payload)
//   72F            no range data at all
// ============================================================

// Sentinel used throughout settings for "no data yet".
#define WEATHER_NO_DATA (-999)

// Fallback when the phone has not reported a sunset hour.
#define WEATHER_DEFAULT_SUNSET_HOUR 19

// Wraps the range when it belongs to tomorrow rather than today.
// Both halves are empty for today's range.
//
// The pieces are also exposed individually because the drawn layout
// sets the brackets in a larger font than the T - they have to be
// separate segments to carry separate fonts.
#define WEATHER_TOMORROW_MARK  "T"
#define WEATHER_BRACKET_OPEN   "["
#define WEATHER_BRACKET_CLOSE  "]"
#define WEATHER_TOMORROW_PREFIX WEATHER_TOMORROW_MARK WEATHER_BRACKET_OPEN
#define WEATHER_TOMORROW_SUFFIX WEATHER_BRACKET_CLOSE

// Separates the low from the high.
#define WEATHER_RANGE_SEPARATOR "\\"

typedef struct {
  bool valid;        // Is there any weather to show?
  bool has_low;      // Low temperature available
  bool has_high;     // High temperature available
  bool is_tomorrow;  // Range belongs to tomorrow - show the mark
  int current;       // All three already converted to display units
  int low;
  int high;
} WeatherRange;

// Choose today's or tomorrow's range and convert to display units.
// All temperature inputs are Celsius, as stored in settings.
WeatherRange weather_select_range(
  int current_c,
  int low_today_c, int high_today_c,
  int low_tomorrow_c, int high_tomorrow_c,
  int hour, int sunset_hour,
  bool metric
);

// Celsius to display units (identity when metric).
int weather_to_display_units(int celsius, bool metric);

// Emery renders each piece as its own layer so current, low and high
// can each carry their own temperature colour, with the tomorrow mark
// left neutral. Other platforms render one combined string.

// "72F" - coloured by the current temperature.
void weather_format_current(char *buf, size_t size, const WeatherRange *r, bool metric);

// "T[" when the range is tomorrow's, otherwise empty. Not a
// temperature, so it stays the foreground colour.
void weather_format_tomorrow_prefix(char *buf, size_t size, const WeatherRange *r);

// "]" closing the wrapper, otherwise empty.
void weather_format_tomorrow_suffix(char *buf, size_t size, const WeatherRange *r);

// "58F" - coloured by the low temperature. Empty when no low is known.
// Carries its own unit: the stacked layout suffixes the high and the
// low separately rather than sharing one trailing unit.
void weather_format_low(char *buf, size_t size, const WeatherRange *r, bool metric);

// The separator between low and high. Its own piece so it stays neutral
// instead of being tinted by whichever temperature it adjoins.
void weather_format_separator(char *buf, size_t size, const WeatherRange *r);

// "85F" - coloured by the high temperature. Empty when no high is known.
void weather_format_high(char *buf, size_t size, const WeatherRange *r, bool metric);

// "72F T[58\85F]" - the whole line in one string.
void weather_format_combined(char *buf, size_t size, const WeatherRange *r, bool metric);
