#pragma once
#include <pebble.h>

// Layers
extern BitmapLayer *s_hud_layer;
extern GBitmap *s_hud_bitmap;
extern BitmapLayer *s_charge_layer;
extern GBitmap *s_charge_bitmap;
extern BitmapLayer *s_low_battery_layer;
extern GBitmap *s_low_battery_bitmap;
extern TextLayer *s_date_layer;
extern TextLayer *s_day_layer;

// Buffers
extern char s_date_buffer[8];
extern char s_day_buffer[32];

// Functions
void load_hud(int x, int y);
void unload_hud(void);
void update_date(struct tm *tick_time);
void update_day_text(struct tm *tick_time, time_t now);
void show_charge_indicator(bool show);
void show_low_battery_indicator(bool show);
