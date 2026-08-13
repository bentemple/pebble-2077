/*
 * Weather Display Formatting Unit Tests
 * Compile: gcc -Isrc/c -o test/weather_format_test.o test/weather_format_test.c src/c/weather_format.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "weather_format.h"

// ============================================================
// TEST FRAMEWORK
// ============================================================
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
  printf("\n--- %s ---\n", #name); \
  test_##name(); \
  tests_run++; \
  tests_passed++; \
  printf("PASS\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
  if ((a) != (b)) { \
    printf("FAIL: %s:%d: %s == %ld, expected %ld\n", \
           __FILE__, __LINE__, #a, (long)(a), (long)(b)); \
    exit(1); \
  } \
} while(0)

#define ASSERT_STR(actual, expected) do { \
  if (strcmp((actual), (expected)) != 0) { \
    printf("FAIL: %s:%d: got \"%s\", expected \"%s\"\n", \
           __FILE__, __LINE__, (actual), (expected)); \
    exit(1); \
  } \
} while(0)

#define ASSERT_TRUE(x) ASSERT_EQ(!!(x), 1)
#define ASSERT_FALSE(x) ASSERT_EQ(!!(x), 0)

// Celsius fixtures chosen so the Fahrenheit conversions are tidy:
//   0C -> 32F, 10C -> 50F, 20C -> 68F, 30C -> 86F, -10C -> 14F
#define SUNSET 19

// ============================================================
// UNIT CONVERSION
// ============================================================
TEST(converts_celsius_to_fahrenheit) {
  ASSERT_EQ(weather_to_display_units(0, false), 32);
  ASSERT_EQ(weather_to_display_units(100, false), 212);
  ASSERT_EQ(weather_to_display_units(-10, false), 14);
}

TEST(metric_passes_celsius_through) {
  ASSERT_EQ(weather_to_display_units(20, true), 20);
  ASSERT_EQ(weather_to_display_units(-10, true), -10);
}

// ============================================================
// DAY SELECTION
// ============================================================
TEST(uses_todays_range_before_sunset) {
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 12, SUNSET, true);
  ASSERT_TRUE(r.valid);
  ASSERT_TRUE(r.has_low);
  ASSERT_TRUE(r.has_high);
  ASSERT_FALSE(r.is_tomorrow);
  ASSERT_EQ(r.low, 10);
  ASSERT_EQ(r.high, 30);
}

TEST(uses_tomorrows_range_after_sunset) {
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 20, SUNSET, true);
  ASSERT_TRUE(r.is_tomorrow);
  ASSERT_EQ(r.low, 0);
  ASSERT_EQ(r.high, 25);
}

TEST(switches_exactly_at_the_sunset_hour) {
  WeatherRange before = weather_select_range(20, 10, 30, 0, 25, SUNSET - 1, SUNSET, true);
  ASSERT_FALSE(before.is_tomorrow);

  WeatherRange at = weather_select_range(20, 10, 30, 0, 25, SUNSET, SUNSET, true);
  ASSERT_TRUE(at.is_tomorrow);
}

TEST(falls_back_to_default_sunset_when_unknown) {
  // A negative sunset hour means the phone never reported one.
  WeatherRange before = weather_select_range(
    20, 10, 30, 0, 25, WEATHER_DEFAULT_SUNSET_HOUR - 1, -1, true);
  ASSERT_FALSE(before.is_tomorrow);

  WeatherRange after = weather_select_range(
    20, 10, 30, 0, 25, WEATHER_DEFAULT_SUNSET_HOUR, -1, true);
  ASSERT_TRUE(after.is_tomorrow);
}

TEST(stays_on_today_after_sunset_when_tomorrow_is_missing) {
  // Never show a sentinel as a temperature.
  WeatherRange r = weather_select_range(
    20, 10, 30, WEATHER_NO_DATA, WEATHER_NO_DATA, 22, SUNSET, true);
  ASSERT_FALSE(r.is_tomorrow);
  ASSERT_EQ(r.low, 10);
  ASSERT_EQ(r.high, 30);
}

TEST(uses_tomorrow_high_even_if_tomorrow_low_is_missing) {
  // Partial data is still better than yesterday's numbers.
  WeatherRange r = weather_select_range(
    20, 10, 30, WEATHER_NO_DATA, 25, 22, SUNSET, true);
  ASSERT_TRUE(r.is_tomorrow);
  ASSERT_FALSE(r.has_low);
  ASSERT_TRUE(r.has_high);
  ASSERT_EQ(r.high, 25);
}

// ============================================================
// SENTINEL HANDLING
// ============================================================
TEST(no_current_temperature_is_invalid) {
  WeatherRange r = weather_select_range(
    WEATHER_NO_DATA, 10, 30, 0, 25, 12, SUNSET, true);
  ASSERT_FALSE(r.valid);
}

TEST(missing_low_is_reported_not_rendered_as_sentinel) {
  WeatherRange r = weather_select_range(
    20, WEATHER_NO_DATA, 30, 0, 25, 12, SUNSET, true);
  ASSERT_TRUE(r.valid);
  ASSERT_FALSE(r.has_low);
  ASSERT_TRUE(r.has_high);
}

TEST(missing_high_is_reported) {
  WeatherRange r = weather_select_range(
    20, 10, WEATHER_NO_DATA, 0, 25, 12, SUNSET, true);
  ASSERT_TRUE(r.valid);
  ASSERT_FALSE(r.has_high);
}

// ============================================================
// COMBINED STRING
// ============================================================
TEST(combined_shows_current_low_and_high) {
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 12, SUNSET, true);
  char buf[24];
  weather_format_combined(buf, sizeof(buf), &r, true);
  ASSERT_STR(buf, "20C 10\\30C");
}

TEST(combined_in_fahrenheit) {
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 12, SUNSET, false);
  char buf[24];
  weather_format_combined(buf, sizeof(buf), &r, false);
  ASSERT_STR(buf, "68F 50\\86F");
}

TEST(combined_marks_tomorrow_with_a_leading_prefix) {
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 20, SUNSET, true);
  char buf[24];
  weather_format_combined(buf, sizeof(buf), &r, true);
  ASSERT_STR(buf, "20C T_0\\25C");
}

TEST(combined_has_no_mark_for_today) {
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 12, SUNSET, true);
  char buf[24];
  weather_format_combined(buf, sizeof(buf), &r, true);
  ASSERT_TRUE(strstr(buf, WEATHER_TOMORROW_PREFIX) == NULL);
}

TEST(combined_falls_back_to_legacy_form_without_a_low) {
  // An older phone payload carries only the high.
  WeatherRange r = weather_select_range(
    20, WEATHER_NO_DATA, 30, WEATHER_NO_DATA, 25, 12, SUNSET, true);
  char buf[24];
  weather_format_combined(buf, sizeof(buf), &r, true);
  ASSERT_STR(buf, "20C\\30C");
}

TEST(combined_shows_current_only_without_a_range) {
  WeatherRange r = weather_select_range(
    20, WEATHER_NO_DATA, WEATHER_NO_DATA,
    WEATHER_NO_DATA, WEATHER_NO_DATA, 12, SUNSET, true);
  char buf[24];
  weather_format_combined(buf, sizeof(buf), &r, true);
  ASSERT_STR(buf, "20C");
}

TEST(combined_is_empty_when_invalid) {
  WeatherRange r = weather_select_range(
    WEATHER_NO_DATA, 10, 30, 0, 25, 12, SUNSET, true);
  char buf[24];
  weather_format_combined(buf, sizeof(buf), &r, true);
  ASSERT_STR(buf, "");
}

TEST(combined_handles_negative_temperatures) {
  WeatherRange r = weather_select_range(-10, -20, 0, 5, 10, 12, SUNSET, true);
  char buf[24];
  weather_format_combined(buf, sizeof(buf), &r, true);
  ASSERT_STR(buf, "-10C -20\\0C");
}

TEST(combined_never_overflows_a_small_buffer) {
  // Worst realistic case: negative three-digit Fahrenheit, tomorrow.
  WeatherRange r = weather_select_range(-50, -60, -40, -60, -40, 22, SUNSET, false);
  char buf[8];
  weather_format_combined(buf, sizeof(buf), &r, false);
  ASSERT_TRUE(strlen(buf) < sizeof(buf));
}

// ============================================================
// SPLIT STRINGS (Emery renders these as separate coloured layers)
// ============================================================
TEST(split_pieces_for_today) {
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 12, SUNSET, true);
  char cur[8], mark[8], low[8], sep[4], high[8];
  weather_format_current(cur, sizeof(cur), &r, true);
  weather_format_tomorrow_prefix(mark, sizeof(mark), &r);
  weather_format_low(low, sizeof(low), &r, true);
  weather_format_separator(sep, sizeof(sep), &r);
  weather_format_high(high, sizeof(high), &r, true);
  ASSERT_STR(cur, "20C");
  ASSERT_STR(mark, "");
  ASSERT_STR(low, "10C");
  ASSERT_STR(sep, WEATHER_RANGE_SEPARATOR);
  ASSERT_STR(high, "30C");
}

TEST(split_pieces_for_tomorrow_isolate_the_mark) {
  // The mark must be its own piece so it can stay neutral while the
  // low and high each take their own temperature colour.
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 20, SUNSET, true);
  char cur[8], mark[8], low[8], sep[4], high[8];
  weather_format_current(cur, sizeof(cur), &r, true);
  weather_format_tomorrow_prefix(mark, sizeof(mark), &r);
  weather_format_low(low, sizeof(low), &r, true);
  weather_format_separator(sep, sizeof(sep), &r);
  weather_format_high(high, sizeof(high), &r, true);
  ASSERT_STR(cur, "20C");
  ASSERT_STR(mark, "T_");
  ASSERT_STR(low, "0C");
  ASSERT_STR(sep, WEATHER_RANGE_SEPARATOR);
  ASSERT_STR(high, "25C");
}

TEST(split_low_never_contains_the_mark) {
  // Regression guard: if the mark leaked into the low string it would
  // be tinted by the low temperature colour.
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 20, SUNSET, true);
  char low[8];
  weather_format_low(low, sizeof(low), &r, true);
  ASSERT_TRUE(strstr(low, WEATHER_TOMORROW_PREFIX) == NULL);
}

TEST(separator_is_a_backslash) {
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 12, SUNSET, true);
  char sep[4];
  weather_format_separator(sep, sizeof(sep), &r);
  ASSERT_EQ(sep[0], '\\');
  ASSERT_EQ(sep[1], '\0');
}

TEST(split_low_is_empty_but_separator_remains_without_a_low) {
  // Degrades to the legacy "20C/30C" look.
  WeatherRange r = weather_select_range(
    20, WEATHER_NO_DATA, 30, WEATHER_NO_DATA, 25, 12, SUNSET, true);
  char low[8], sep[4];
  weather_format_low(low, sizeof(low), &r, true);
  weather_format_separator(sep, sizeof(sep), &r);
  ASSERT_STR(low, "");
  ASSERT_STR(sep, WEATHER_RANGE_SEPARATOR);
}

TEST(split_separator_is_never_a_temperature) {
  // Regression guard: the separator must stay its own piece so it can
  // be drawn neutral rather than tinted by an adjacent temperature.
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 12, SUNSET, true);
  char sep[4];
  weather_format_separator(sep, sizeof(sep), &r);
  ASSERT_STR(sep, WEATHER_RANGE_SEPARATOR);
}

TEST(split_pieces_are_empty_without_a_high) {
  WeatherRange r = weather_select_range(
    20, WEATHER_NO_DATA, WEATHER_NO_DATA,
    WEATHER_NO_DATA, WEATHER_NO_DATA, 12, SUNSET, true);
  char mark[8], low[8], sep[4], high[8];
  weather_format_tomorrow_prefix(mark, sizeof(mark), &r);
  weather_format_low(low, sizeof(low), &r, true);
  weather_format_separator(sep, sizeof(sep), &r);
  weather_format_high(high, sizeof(high), &r, true);
  ASSERT_STR(mark, "");
  ASSERT_STR(low, "");
  ASSERT_STR(sep, "");
  ASSERT_STR(high, "");
}

TEST(split_pieces_are_empty_when_invalid) {
  WeatherRange r = weather_select_range(
    WEATHER_NO_DATA, 10, 30, 0, 25, 12, SUNSET, true);
  char cur[8], mark[8], low[8], sep[4], high[8];
  weather_format_current(cur, sizeof(cur), &r, true);
  weather_format_tomorrow_prefix(mark, sizeof(mark), &r);
  weather_format_low(low, sizeof(low), &r, true);
  weather_format_separator(sep, sizeof(sep), &r);
  weather_format_high(high, sizeof(high), &r, true);
  ASSERT_STR(cur, "");
  ASSERT_STR(mark, "");
  ASSERT_STR(low, "");
  ASSERT_STR(sep, "");
  ASSERT_STR(high, "");
}

TEST(split_high_and_low_each_carry_a_unit) {
  // The stacked layout suffixes both rather than sharing one unit.
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 12, SUNSET, false);
  char high[8], low[8];
  weather_format_high(high, sizeof(high), &r, false);
  weather_format_low(low, sizeof(low), &r, false);
  ASSERT_STR(high, "86F");
  ASSERT_STR(low, "50F");
}

TEST(split_units_follow_metric_setting) {
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 12, SUNSET, true);
  char high[8], low[8];
  weather_format_high(high, sizeof(high), &r, true);
  weather_format_low(low, sizeof(low), &r, true);
  ASSERT_STR(high, "30C");
  ASSERT_STR(low, "10C");
}

TEST(split_and_combined_agree_on_order_and_values) {
  // The two rendering paths must not drift apart. They differ only in
  // where the unit sits: the stacked form suffixes both temperatures,
  // the combined form carries one trailing unit.
  WeatherRange r = weather_select_range(20, 10, 30, 0, 25, 20, SUNSET, false);
  char cur[8], mark[8], high[8], sep[4], low[8], combined[48];
  weather_format_current(cur, sizeof(cur), &r, false);
  weather_format_tomorrow_prefix(mark, sizeof(mark), &r);
  weather_format_high(high, sizeof(high), &r, false);
  weather_format_separator(sep, sizeof(sep), &r);
  weather_format_low(low, sizeof(low), &r, false);
  weather_format_combined(combined, sizeof(combined), &r, false);

  ASSERT_STR(cur, "68F");
  ASSERT_STR(mark, "T_");
  ASSERT_STR(high, "77F");
  ASSERT_STR(sep, WEATHER_RANGE_SEPARATOR);
  ASSERT_STR(low, "32F");
  // High before low in both.
  ASSERT_STR(combined, "68F T_32\\77F");
}

// ============================================================
// MAIN
// ============================================================
int main(void) {
  printf("==============================================\n");
  printf("     WEATHER FORMATTING UNIT TESTS\n");
  printf("==============================================\n");

  printf("\n--- UNIT CONVERSION ---\n");
  RUN_TEST(converts_celsius_to_fahrenheit);
  RUN_TEST(metric_passes_celsius_through);

  printf("\n--- DAY SELECTION ---\n");
  RUN_TEST(uses_todays_range_before_sunset);
  RUN_TEST(uses_tomorrows_range_after_sunset);
  RUN_TEST(switches_exactly_at_the_sunset_hour);
  RUN_TEST(falls_back_to_default_sunset_when_unknown);
  RUN_TEST(stays_on_today_after_sunset_when_tomorrow_is_missing);
  RUN_TEST(uses_tomorrow_high_even_if_tomorrow_low_is_missing);

  printf("\n--- SENTINEL HANDLING ---\n");
  RUN_TEST(no_current_temperature_is_invalid);
  RUN_TEST(missing_low_is_reported_not_rendered_as_sentinel);
  RUN_TEST(missing_high_is_reported);

  printf("\n--- COMBINED STRING ---\n");
  RUN_TEST(combined_shows_current_low_and_high);
  RUN_TEST(combined_in_fahrenheit);
  RUN_TEST(combined_marks_tomorrow_with_a_leading_prefix);
  RUN_TEST(combined_has_no_mark_for_today);
  RUN_TEST(combined_falls_back_to_legacy_form_without_a_low);
  RUN_TEST(combined_shows_current_only_without_a_range);
  RUN_TEST(combined_is_empty_when_invalid);
  RUN_TEST(combined_handles_negative_temperatures);
  RUN_TEST(combined_never_overflows_a_small_buffer);

  printf("\n--- SPLIT STRINGS ---\n");
  RUN_TEST(split_pieces_for_today);
  RUN_TEST(split_pieces_for_tomorrow_isolate_the_mark);
  RUN_TEST(split_low_never_contains_the_mark);
  RUN_TEST(separator_is_a_backslash);
  RUN_TEST(split_low_is_empty_but_separator_remains_without_a_low);
  RUN_TEST(split_separator_is_never_a_temperature);
  RUN_TEST(split_pieces_are_empty_without_a_high);
  RUN_TEST(split_pieces_are_empty_when_invalid);
  RUN_TEST(split_high_and_low_each_carry_a_unit);
  RUN_TEST(split_units_follow_metric_setting);
  RUN_TEST(split_and_combined_agree_on_order_and_values);

  printf("\n==============================================\n");
  printf("  Results: %d/%d tests passed\n", tests_passed, tests_run);
  printf("==============================================\n");

  return (tests_passed == tests_run) ? 0 : 1;
}
