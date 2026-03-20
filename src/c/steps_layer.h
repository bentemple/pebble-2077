#pragma once
#include <pebble.h>

// Layer
extern TextLayer *s_step_layer;

// Functions
void load_step_layer(int y);
void unload_step_layer(void);
void fetch_step_count(void);
void update_steps(void);
