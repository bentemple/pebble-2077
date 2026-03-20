#pragma once
#include <pebble.h>
#include "constants.h"

// Layers
extern TextLayer *s_custom_layer;
extern TextLayer *s_bt_layer;

// Buffers
extern char s_custom_buffer[32];

// Update periods
extern UpdatePeriod s_custom_text_period;
extern UpdatePeriod s_bottom_text_period;
extern int s_last_custom_update;
extern int s_last_bottom_update;

// Functions
void load_custom_text_layers(int custom_y, int bt_y);
void unload_custom_text_layers(void);
UpdatePeriod analyze_format_period(const char *fmt);
void recalculate_update_periods(void);
void replace_uptime(char *buf, size_t buf_size, time_t now);
bool should_update_field(int current_minute, int current_hour, int current_day,
                         UpdatePeriod period, int *last_update);
void update_custom_text(struct tm *tick_time, time_t now, int current_minute,
                        int current_hour, int current_day, bool day_changed);
void update_bottom_text(struct tm *tick_time, time_t now, int current_minute,
                        int current_hour, int current_day, bool day_changed);
