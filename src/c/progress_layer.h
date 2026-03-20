#pragma once
#include <pebble.h>

// Layer and state
extern Layer *s_progress_layer;
extern int s_progress_percent;

// Functions
void load_progress_layer(int x, int y);
void unload_progress_layer(void);
void update_progress(void);
GColor get_progress_bar_color(int percent);
