#include <pebble.h>
#include "constants.h"
#include "settings.h"
#include "globals.h"
#include "hud_layer.h"

// ============================================================
// LAYERS
// ============================================================
BitmapLayer *s_hud_layer;
GBitmap *s_hud_bitmap;
BitmapLayer *s_charge_layer;
GBitmap *s_charge_bitmap;
TextLayer *s_date_layer;
TextLayer *s_day_layer;

// ============================================================
// BUFFERS
// ============================================================
char s_date_buffer[8];
char s_day_buffer[32];

// ============================================================
// INTERNAL LOADERS
// ============================================================
static void load_hud_layer(int x, int y) {
  s_hud_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_HUD);
  s_hud_layer = bitmap_layer_create(GRect(x, y, HUD_WIDTH, HUD_HEIGHT));
  bitmap_layer_set_bitmap(s_hud_layer, s_hud_bitmap);
  bitmap_layer_set_alignment(s_hud_layer, GAlignCenter);
  bitmap_layer_set_compositing_mode(s_hud_layer, GCompOpSet);
}

static void load_charge_layer(int x, int y) {
  s_charge_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CHARGE);
  s_charge_layer = bitmap_layer_create(GRect(x + PROGRESS_BAR_OFFSET_X, y + PROGRESS_BAR_OFFSET_Y, 5, 8));
  bitmap_layer_set_bitmap(s_charge_layer, s_charge_bitmap);
  bitmap_layer_set_alignment(s_charge_layer, GAlignTopLeft);
  bitmap_layer_set_compositing_mode(s_charge_layer, GCompOpSet);
}

static void load_date_layer(int x, int y) {
  int date_x = x + DATE_X_OFFSET;
  int date_y = y + DATE_Y_OFFSET;
  s_date_layer = text_layer_create(GRect(date_x, date_y, DATE_LAYER_SIZE, DATE_LAYER_SIZE));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, color_fg);
  text_layer_set_font(s_date_layer, s_date_font);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
}

static void load_day_layer(int x, int y) {
  s_day_layer = text_layer_create(GRect(x + DAY_LAYER_OFFSET_X, y + DAY_LAYER_OFFSET_Y, DAY_LAYER_WIDTH, TEXT_HEIGHT));
  text_layer_set_background_color(s_day_layer, GColorClear);
  text_layer_set_text_color(s_day_layer, color_fg);
  text_layer_set_font(s_day_layer, s_text_font);
  text_layer_set_text_alignment(s_day_layer, GTextAlignmentLeft);
}

// ============================================================
// PUBLIC FUNCTIONS
// ============================================================
void load_hud(int x, int y) {
  load_hud_layer(x, y);
  load_charge_layer(x, y);
  load_date_layer(x, y);
  load_day_layer(x, y);
}

void unload_hud(void) {
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_day_layer);
  gbitmap_destroy(s_hud_bitmap);
  bitmap_layer_destroy(s_hud_layer);
  gbitmap_destroy(s_charge_bitmap);
  bitmap_layer_destroy(s_charge_layer);
}

void update_date(struct tm *tick_time) {
  int current_day = tick_time->tm_mday;
  if (current_day != s_last_day) {
    s_last_day = current_day;
    strftime(s_date_buffer, sizeof(s_date_buffer), "%d", tick_time);
    text_layer_set_text(s_date_layer, s_date_buffer);
  }
}

void show_charge_indicator(bool show) {
  layer_set_hidden(bitmap_layer_get_layer(s_charge_layer), !show);
}
