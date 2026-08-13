#pragma once
#include <pebble.h>

// Layers
extern TextLayer *s_condition_layer;

// The temperature line is a single custom-drawn layer on Emery, where
// each segment carries its own colour, and a plain TextLayer elsewhere.
// Callers that just need to add it to a window or move it should use
// weather_temperature_layer() rather than touching either directly.
Layer *weather_temperature_layer(void);

// Functions
void load_weather_layers(int temperature_y, int condition_y);
void unload_weather_layers(void);
void update_weather_layers(void);
void invalidate_weather_render_cache(void);
GColor get_temperature_color(int temp_f);
GColor get_condition_color(const char *condition);

// Layout metrics (Orbitron-SemiBold 17pt, measured by tools/measure_font.py)
int get_temp_digits_width(int temp);
int get_temp_text_width(int temp, bool metric);
int get_tomorrow_prefix_width(void);
int get_temp_small_digits_width(int temp);
int get_temp_small_text_width(int temp, bool metric);
