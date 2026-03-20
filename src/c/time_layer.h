#pragma once
#include <pebble.h>
#include "constants.h"

// Layers
extern TextLayer *s_time_layer;
#if defined(PBL_PLATFORM_EMERY)
extern TextLayer *s_time_hours_layer;
extern TextLayer *s_time_colon_layer;
extern TextLayer *s_time_mins_layer;
extern int s_hours_layer_x;
extern int s_hours_layer_y;
#endif

// Buffers
extern char s_time_buffer[8];
#if defined(PBL_PLATFORM_EMERY)
extern char s_time_hours_buffer[4];
extern char s_time_mins_buffer[4];
#endif

// Functions
void load_time_layer(int x, int y);
void unload_time_layer(void);
void update_time(void);
