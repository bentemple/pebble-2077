#pragma once
#include <pebble.h>

// ============================================================
// DEMO MODE
// ============================================================
// Set to 1 to enable demo mode with hard-coded values:
// - Progress bar: 85%
// - Current temp: 61F
// - High temp: 85F
// - Show low battery indicator: ON
#define DEMO_MODE 0

// ============================================================
// SETTINGS KEY
// ============================================================
#define SETTINGS_KEY 1

// ============================================================
// PLATFORM-SPECIFIC DIMENSIONS
// ============================================================
#if defined(PBL_PLATFORM_EMERY)
  // Pebble Time 2: 200x228 (larger time font)
  #define SCREEN_WIDTH 200
  #define SCREEN_HEIGHT 228
  #define MARGIN_SIZE 7
  #define TIME_X_OFFSET 0  // Less margin for time display
  #define HUD_WIDTH (SCREEN_WIDTH - MARGIN_SIZE * 2)
  #define HUD_HEIGHT 40
  #define TEXT_HEIGHT 20
  #define TIME_LAYER_WIDTH (SCREEN_WIDTH - MARGIN_SIZE * 2)
  #define TIME_LAYER_HEIGHT 86

  // Digit widths for Rajdhani-Medium 86pt (measured by tools/measure_font.py)
  #define DIGIT_WIDTH_0 45
  #define DIGIT_WIDTH_1 28
  #define DIGIT_WIDTH_2 41
  #define DIGIT_WIDTH_3 43
  #define DIGIT_WIDTH_4 45
  #define DIGIT_WIDTH_5 42
  #define DIGIT_WIDTH_6 43
  #define DIGIT_WIDTH_7 34
  #define DIGIT_WIDTH_8 45
  #define DIGIT_WIDTH_9 43
  #define COLON_WIDTH 20
  #define KERNING_ADJUST -4

  // Compile-time hour width calculation
  #define HOUR_WIDTH(tens, ones) (DIGIT_WIDTH_##tens + DIGIT_WIDTH_##ones + KERNING_ADJUST)

  // Split time layer positions for accent coloring (HH : MM)
  #define INITIAL_TIME_HOURS_WIDTH 100
  #define INITIAL_TIME_MINS_WIDTH 100
  #define MINS_BOLD_X_OFFSET -8  // Move minutes closer to colon when using thinner regular font
  #define COLON_Y_OFFSET -6       // Colon sits slightly higher than time digits
  #define DATE_X_OFFSET 5
  #define DATE_Y_OFFSET 4
  #define DATE_LAYER_SIZE 36
  #define PROGRESS_BAR_OFFSET_X 43
  #define PROGRESS_BAR_OFFSET_Y 13
  #define PROGRESS_BAR_WIDTH (HUD_WIDTH - PROGRESS_BAR_OFFSET_X)
  #define PROGRESS_BAR_HEIGHT 8
  #define DAY_LAYER_OFFSET_X 43
  #define DAY_LAYER_OFFSET_Y (HUD_HEIGHT - TEXT_HEIGHT + 3)
  #define DAY_LAYER_WIDTH (HUD_WIDTH - DAY_LAYER_OFFSET_X)
  #define INFO_LAYER_WIDTH (SCREEN_WIDTH - MARGIN_SIZE * 2)

  // Orbitron-SemiBold 17pt character widths (measured by tools/measure_font.py)
  #define INFO_CHAR_WIDTH_F 12
  #define INFO_CHAR_WIDTH_C 14
  #define INFO_CHAR_WIDTH_SLASH 9
  #define INFO_CHAR_WIDTH_MINUS 9
  #define INFO_KERNING 1
  // Initial offsets for temperature layers (repositioned dynamically)
  #define TEMP_SLASH_OFFSET 40
  #define TEMP_HIGH_OFFSET 50

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
  #define DATE_X_OFFSET 5
  #define DATE_Y_OFFSET 4
  #define INFO_LAYER_WIDTH (SCREEN_WIDTH - MARGIN_SIZE * 2)
#endif

// ============================================================
// DEFAULT VALUES
// ============================================================
#define DEFAULT_STEP_GOAL 10000
#define DEFAULT_SLEEP_GOAL_MINS 420  // 7 hours

// ============================================================
// ENUMS
// ============================================================

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

// Color mode: 0=disabled, 1=static, 2=dynamic
typedef enum {
  COLOR_MODE_DISABLED = 0,
  COLOR_MODE_STATIC = 1,
  COLOR_MODE_DYNAMIC = 2
} ColorMode;

// ============================================================
// EMERY-ONLY: DYNAMIC COLOR CONFIGURATION
// ============================================================
#if defined(PBL_PLATFORM_EMERY)

// Orbitron-SemiBold 17pt digit widths (measured by tools/measure_font.py)
static const int s_info_digit_widths[10] = {
  13,  // 0
  6,   // 1
  13,  // 2
  13,  // 3
  11,  // 4
  13,  // 5
  13,  // 6
  10,  // 7
  13,  // 8
  13   // 9
};

// Progress bar color definitions
#define PROGRESS_COLOR_0    GColorDarkCandyAppleRedARGB8  // Critical
#define PROGRESS_COLOR_10   GColorRedARGB8                // Very low
#define PROGRESS_COLOR_20   GColorFollyARGB8              // Low
#define PROGRESS_COLOR_30   GColorOrangeARGB8             // Below average
#define PROGRESS_COLOR_40   GColorChromeYellowARGB8       // Approaching average
#define PROGRESS_COLOR_50   GColorYellowARGB8             // Average
#define PROGRESS_COLOR_60   GColorSpringBudARGB8          // Above average
#define PROGRESS_COLOR_70   GColorGreenARGB8              // Good
#define PROGRESS_COLOR_80   GColorBrightGreenARGB8        // Great
#define PROGRESS_COLOR_90   GColorBrightGreenARGB8        // Excellent
#define PROGRESS_COLOR_100  GColorVividCeruleanARGB8      // 100% celebration

// Compile-time LUT for progress bar colors (0-100, O(1) lookup)
static const uint8_t s_progress_lut[101] = {
  [0 ... 9]   = PROGRESS_COLOR_0,
  [10 ... 19] = PROGRESS_COLOR_10,
  [20 ... 29] = PROGRESS_COLOR_20,
  [30 ... 39] = PROGRESS_COLOR_30,
  [40 ... 49] = PROGRESS_COLOR_40,
  [50 ... 59] = PROGRESS_COLOR_50,
  [60 ... 69] = PROGRESS_COLOR_60,
  [70 ... 79] = PROGRESS_COLOR_70,
  [80 ... 89] = PROGRESS_COLOR_80,
  [90 ... 99] = PROGRESS_COLOR_90,
  [100]       = PROGRESS_COLOR_100,
};
_Static_assert(sizeof(s_progress_lut) == 101, "Progress LUT must cover 0-100");

// Temperature color definitions (Fahrenheit)
// Green = comfortable, Orange/Red = hot, Blue = cold
#define TEMP_COLOR_BELOW_0   GColorWhiteARGB8             // Freezing
#define TEMP_COLOR_FRIGID    GColorWhiteARGB8             // 0-19F
#define TEMP_COLOR_VERY_COLD GColorCelesteARGB8           // 20-31F
#define TEMP_COLOR_FREEZING  GColorCyanARGB8              // 32-39F
#define TEMP_COLOR_COLD      GColorPictonBlueARGB8        // 40-44F
#define TEMP_COLOR_CHILLY    GColorCyanARGB8              // 45-49F
#define TEMP_COLOR_COOL      GColorYellowARGB8            // 50-54F (transition)
#define TEMP_COLOR_SLIGHT    GColorSpringBudARGB8         // 55-59F (yellow-green)
#define TEMP_COLOR_COMFORT   GColorGreenARGB8             // 60-64F
#define TEMP_COLOR_IDEAL     GColorGreenARGB8             // 65-69F
#define TEMP_COLOR_PERFECT   GColorBrightGreenARGB8       // 70-74F
#define TEMP_COLOR_WARM      GColorChromeYellowARGB8      // 75-79F
#define TEMP_COLOR_HOT       GColorOrangeARGB8            // 80-89F
#define TEMP_COLOR_VERY_HOT  GColorRedARGB8               // 90-99F
#define TEMP_COLOR_SCORCHING GColorDarkCandyAppleRedARGB8 // 100F+

// Compile-time LUT for temperature colors (0-100F, O(1) lookup)
static const uint8_t s_temp_lut[101] = {
  [0 ... 19]  = TEMP_COLOR_FRIGID,    // Frigid
  [20 ... 31] = TEMP_COLOR_VERY_COLD, // Very cold
  [32 ... 39] = TEMP_COLOR_FREEZING,  // Near freezing
  [40 ... 44] = TEMP_COLOR_COLD,      // Cold
  [45 ... 49] = TEMP_COLOR_CHILLY,    // Chilly
  [50 ... 54] = TEMP_COLOR_COOL,      // Transition (yellow)
  [55 ... 59] = TEMP_COLOR_SLIGHT,    // Slightly cool (yellow-green)
  [60 ... 64] = TEMP_COLOR_COMFORT,   // Comfortable (green)
  [65 ... 69] = TEMP_COLOR_IDEAL,     // Ideal (green)
  [70 ... 74] = TEMP_COLOR_PERFECT,   // Perfect (bright green)
  [75 ... 79] = TEMP_COLOR_WARM,      // Getting warm (yellow-orange)
  [80 ... 89] = TEMP_COLOR_HOT,       // Hot (orange)
  [90 ... 99] = TEMP_COLOR_VERY_HOT,  // Very hot (red)
  [100]       = TEMP_COLOR_SCORCHING, // Scorching (dark red)
};
_Static_assert(sizeof(s_temp_lut) == 101, "Temperature LUT must cover 0-100");

// Weather condition colors
#define WEATHER_COLOR_CLEAR     GColorYellowARGB8
#define WEATHER_COLOR_THUNDER   GColorPurpleARGB8
#define WEATHER_COLOR_SNOW      GColorWhiteARGB8
#define WEATHER_COLOR_FREEZING  GColorCyanARGB8
#define WEATHER_COLOR_RAIN      GColorPictonBlueARGB8
#define WEATHER_COLOR_FOG       GColorDarkGrayARGB8
#define WEATHER_COLOR_CLOUD     GColorLightGrayARGB8

#endif // PBL_PLATFORM_EMERY
