#include <pebble.h>
#include "constants.h"
#include "settings.h"
#include "globals.h"
#include "callbacks.h"
#include "time_layer.h"
#include "steps_layer.h"
#include "progress_layer.h"
#include "weather_layer.h"
#include "hud_layer.h"
#include "custom_text.h"

// ============================================================
// WAKE TIME TRACKING
// ============================================================
bool s_was_sleeping = false;
// s_wake_time is defined in custom_text.c

// ============================================================
// TICK HANDLER
// ============================================================
void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();

  // Refresh steps text every 30 minutes
  if (tick_time->tm_min % 30 == 0) {
    update_steps();
  }

  // Progress bar updates based on mode
  if (settings.progress_bar_mode == PROGRESS_MODE_STEPS && tick_time->tm_min % 30 == 0) {
    update_progress();
  }

  // Refresh weather every hour
  if (tick_time->tm_min == 0 && settings.show_weather) {
    // Re-evaluate which high to display (may switch at sunset)
    update_weather_layers();

    DictionaryIterator *it;
    app_message_outbox_begin(&it);
    dict_write_uint8(it, 0, 0);
    app_message_outbox_send();
  }

  if (tick_time->tm_min == 0 && settings.hour_vibe) {
    vibes_double_pulse();
  }
}

// ============================================================
// BATTERY CALLBACK
// ============================================================
void battery_callback(BatteryChargeState state) {
  // Show/hide charging indicator
  show_charge_indicator(state.is_plugged);

  // Update progress bar if in battery mode
  if (settings.progress_bar_mode == PROGRESS_MODE_BATTERY) {
    update_progress();
  }
}

// ============================================================
// BLUETOOTH CALLBACK
// ============================================================
void bt_callback(bool connected) {
  // Replace custom layer with BT layer when disconnected
  layer_set_hidden(text_layer_get_layer(s_bt_layer), connected);
  layer_set_hidden(text_layer_get_layer(s_custom_layer), !connected);

  if (!connected && settings.disconnect_alert) {
    vibes_short_pulse();
  }
}

// ============================================================
// HEALTH HANDLER
// ============================================================
#if defined(PBL_HEALTH)
void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventSignificantUpdate) {
    // Check for sleep -> awake transition
    if (s_needs_sleep_tracking) {
      HealthActivityMask activities = health_service_peek_current_activities();
      bool is_sleeping = activities & HealthActivitySleep;

      if (s_was_sleeping && !is_sleeping) {
        // Just woke up - record wake time
        s_wake_time = time(NULL);

        // Update sleep progress bar on wake
        if (settings.progress_bar_mode == PROGRESS_MODE_SLEEP) {
          update_progress();
        }
      }
      s_was_sleeping = is_sleeping;
    }

    update_steps();
  }
  if (event == HealthEventMovementUpdate) {
    update_steps();
  }
}
#endif

// ============================================================
// UPDATE HEALTH SUBSCRIPTION
// ============================================================
void update_health_subscription(void) {
  #if defined(PBL_HEALTH)
  // Subscribe if showing steps OR tracking sleep
  if (settings.show_steps || s_needs_sleep_tracking) {
    health_service_events_subscribe(health_handler, NULL);
  }
  else {
    health_service_events_unsubscribe();
  }
  #endif
}

// ============================================================
// INIT WAKE TIME
// ============================================================
void init_wake_time(void) {
  #if defined(PBL_HEALTH)
  time_t now = time(NULL);

  // Init current sleep state
  HealthActivityMask activities = health_service_peek_current_activities();
  s_was_sleeping = activities & HealthActivitySleep;

  // Check if cached wake time is still valid
  if (s_wake_time > 0 && s_wake_time <= now && (now - s_wake_time) < 7 * 24 * 3600) {
    return;
  }

  // No valid cache - estimate from health data if awake
  if (!s_was_sleeping) {
    HealthMetric metric = HealthMetricSleepSeconds;
    time_t today_start = time_start_of_today();

    // Look back up to 7 days to find the most recent wake time
    for (int days_ago = 0; days_ago < 7; days_ago++) {
      time_t day_start = today_start - (days_ago * 24 * 3600);
      time_t day_end = day_start + 24 * 3600;
      if (day_end > now) day_end = now;

      HealthServiceAccessibilityMask mask = health_service_metric_accessible(
        metric, day_start, day_end
      );

      if (mask & HealthServiceAccessibilityMaskAvailable) {
        int sleep_seconds = (int)health_service_sum(metric, day_start, day_end);
        if (sleep_seconds > 0) {
          time_t estimated_wake = day_start + sleep_seconds;
          if (estimated_wake <= now) {
            s_wake_time = estimated_wake;
            return;
          }
        }
      }
    }

    // No sleep found in last 7 days - use app start time
    s_wake_time = now;
  }
  #endif
}
