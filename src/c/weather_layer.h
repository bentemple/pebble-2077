#pragma once
#include <pebble.h>

// Layers
extern TextLayer *s_temperature_layer;
extern TextLayer *s_condition_layer;
#if defined(PBL_PLATFORM_EMERY)
extern TextLayer *s_temp_slash_layer;
extern TextLayer *s_temp_high_layer;
#endif

// Functions
void load_weather_layers(int temperature_y, int condition_y);
void unload_weather_layers(void);
void update_weather_layers(void);
GColor get_temperature_color(int temp_f);
GColor get_condition_color(const char *condition);
int get_temp_text_width(int temp, bool metric);
