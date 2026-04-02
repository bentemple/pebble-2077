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
#include "uptime.h"

// ============================================================
// WAKE TIME TRACKING
// ============================================================
bool s_was_sleeping = false;
// s_wake_time is defined in custom_text.c

// ============================================================
// PEBBLE STORAGE WRAPPERS FOR UPTIME MODULE
// ============================================================
static int pebble_storage_read(uint32_t key, void *buffer, size_t size) {
  return persist_read_data(key, buffer, size);
}

static int pebble_storage_write(uint32_t key, const void *data, size_t size) {
  return persist_write_data(key, (const uint8_t *)data, size);
}

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

  // Update battery progress every minute while charging
  if (s_is_charging) {
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
  bool was_charging = s_is_charging;
  s_is_charging = state.is_plugged;

  // Invalidate progress cache when charging state changes so the bar
  // immediately reflects the correct value (battery % vs user mode)
  if (s_is_charging != was_charging) {
    s_last_progress_percent = -1;
  }

  // Show/hide charging indicator
  show_charge_indicator(state.is_plugged);

  // Show low battery icon when below 10% and not charging
  #if DEMO_MODE
  show_low_battery_indicator(true);
  #else
  show_low_battery_indicator(!state.is_plugged && state.charge_percent < 10);
  #endif

  // Update progress bar (respects charging override in update_progress)
  update_progress();
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
// PEBBLE HEALTH ITERATOR WRAPPER FOR UPTIME MODULE
// ============================================================
#if defined(PBL_HEALTH)

// Context for bridging Pebble callback to uptime callback
typedef struct {
  UptimeSleepIteratorCB callback;
  void *user_context;
} PebbleIteratorBridge;

// Pebble callback that bridges to uptime callback
static bool pebble_sleep_callback(HealthActivity activity, time_t time_start,
                                  time_t time_end, void *context) {
  if (activity != HealthActivitySleep && activity != HealthActivityRestfulSleep) {
    return true;  // Skip non-sleep activities
  }

  PebbleIteratorBridge *bridge = (PebbleIteratorBridge *)context;
  return bridge->callback(time_start, time_end, bridge->user_context);
}

// Uptime iterator function that uses Pebble health API
static void pebble_iterate_sleep(
  time_t range_start,
  time_t range_end,
  bool backwards,
  UptimeSleepIteratorCB callback,
  void *context
) {
  HealthServiceAccessibilityMask mask = health_service_any_activity_accessible(
    HealthActivitySleep, range_start, range_end
  );

  if (!(mask & HealthServiceAccessibilityMaskAvailable)) {
    return;
  }

  PebbleIteratorBridge bridge = {
    .callback = callback,
    .user_context = context
  };

  health_service_activities_iterate(
    HealthActivitySleep,
    range_start,
    range_end,
    backwards ? HealthIterationDirectionPast : HealthIterationDirectionFuture,
    pebble_sleep_callback,
    &bridge
  );
}
#endif

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
        // Just woke up - classify the sleep and update cache
        time_t now = time(NULL);
        uptime_on_wake_event(now, pebble_iterate_sleep);

        // Get the effective wake time (accounts for naps)
        UptimeResult result = uptime_get_cached(now, pebble_iterate_sleep);
        if (result.found_real_sleep) {
          s_wake_time = uptime_get_effective_wake_time(&result);
        } else {
          s_wake_time = now;
        }

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

  // Initialize uptime module with Pebble storage
  uptime_init(pebble_storage_read, pebble_storage_write);

  // If currently sleeping, don't calculate yet
  if (s_was_sleeping) {
    return;
  }

  // Force recalculation on app start to catch any missed wake events
  // (e.g., app was closed during sleep and reopened after waking)
  UptimeResult result = uptime_recalculate(now, pebble_iterate_sleep);

  if (result.found_real_sleep) {
    s_wake_time = uptime_get_effective_wake_time(&result);
  } else {
    // No real sleep found - use app start time
    s_wake_time = now;
  }
  #endif
}
