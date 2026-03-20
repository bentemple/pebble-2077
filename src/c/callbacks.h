#pragma once
#include <pebble.h>

// Wake time tracking
extern time_t s_wake_time;
extern bool s_was_sleeping;

// Functions
void tick_handler(struct tm *tick_time, TimeUnits units_changed);
void battery_callback(BatteryChargeState state);
void bt_callback(bool connected);
void health_handler(HealthEventType event, void *context);
void update_health_subscription(void);
void init_wake_time(void);
