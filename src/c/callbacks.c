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
#include "refresh.h"

// ============================================================
// WAKE TIME TRACKING
// ============================================================
bool s_was_sleeping = false;
// s_wake_time is defined in custom_text.c

// ============================================================
// REFRESH STATE
// ============================================================
// Neither weather nor sleep can be pulled on demand, so both are driven
// from the minute tick. See refresh.h for the full reasoning.
static WeatherRefreshState s_weather_refresh = { 0 };

#if defined(PBL_HEALTH)
static SleepRefreshState s_sleep_refresh = { 0 };

// ============================================================
// PEBBLE STORAGE WRAPPERS FOR UPTIME MODULE
// ============================================================
static int pebble_storage_read(uint32_t key, void *buffer, size_t size) {
  return persist_read_data(key, buffer, size);
}

static int pebble_storage_write(uint32_t key, const void *data, size_t size) {
  return persist_write_data(key, (const uint8_t *)data, size);
}
#endif

// ============================================================
// WEATHER REFRESH
// ============================================================
// The watchface process is destroyed whenever the user opens any other
// app, taking its PebbleKit JS counterpart with it. The whole refresh
// state has to survive that, not just the last success:
//
// last_attempt and consecutive_failures are what enforce the minimum
// request gap and the retry backoff. If they reset to zero on every
// relaunch, a user whose weather never succeeds (no phone app, denied
// location, bad network) sends a fresh request every time they glance
// at the watch, because "never attempted" reads as "due now".
typedef struct {
  uint32_t magic;
  int32_t last_success;
  int32_t last_attempt;
  int32_t consecutive_failures;
} WeatherPersistedData;

#define WEATHER_PERSIST_MAGIC 0x57544831  // "WTH1"

static void load_weather_refresh_state(void) {
  WeatherPersistedData data;
  int bytes_read = persist_read_data(WEATHER_STORAGE_KEY, &data, sizeof(data));

  if (bytes_read != sizeof(data) || data.magic != WEATHER_PERSIST_MAGIC) {
    return;  // Absent or unrecognised - start clean.
  }

  s_weather_refresh.last_success = (time_t)data.last_success;
  s_weather_refresh.last_attempt = (time_t)data.last_attempt;
  s_weather_refresh.consecutive_failures = (int)data.consecutive_failures;
}

static void save_weather_refresh_state(void) {
  WeatherPersistedData data = {
    .magic = WEATHER_PERSIST_MAGIC,
    .last_success = (int32_t)s_weather_refresh.last_success,
    .last_attempt = (int32_t)s_weather_refresh.last_attempt,
    .consecutive_failures = (int32_t)s_weather_refresh.consecutive_failures
  };
  persist_write_data(WEATHER_STORAGE_KEY, &data, sizeof(data));
}

// Send the weather request. Every failure path is recorded so the
// scheduler can retry with backoff instead of silently going blind
// until the next hour, which is what the old code did.
static void send_weather_request(time_t now) {
  DictionaryIterator *it = NULL;

  AppMessageResult begin_result = app_message_outbox_begin(&it);
  if (begin_result != APP_MSG_OK || it == NULL) {
    weather_on_failure(&s_weather_refresh, now);
    save_weather_refresh_state();
    return;
  }

  // Payload is ignored by the JS side - any inbound message is treated
  // as "please fetch weather". Kept byte-identical to the previous
  // implementation so the phone side needs no changes.
  dict_write_uint8(it, 0, 0);

  AppMessageResult send_result = app_message_outbox_send();
  if (send_result != APP_MSG_OK) {
    weather_on_failure(&s_weather_refresh, now);
    save_weather_refresh_state();
    return;
  }

  weather_on_attempt(&s_weather_refresh, now);
  // Persist the attempt, not just successes - this is what stops a
  // relaunch from bypassing the minimum request gap.
  save_weather_refresh_state();
}

void maybe_request_weather(time_t now) {
  if (!settings.show_weather) {
    return;
  }
  bool connected = connection_service_peek_pebble_app_connection();
  if (!weather_should_request(&s_weather_refresh, now, connected)) {
    return;
  }
  send_weather_request(now);
}

void request_weather_on_launch(void) {
  if (!settings.show_weather) {
    return;
  }
  time_t now = time(NULL);
  bool connected = connection_service_peek_pebble_app_connection();
  if (!weather_should_request_on_launch(&s_weather_refresh, now, connected)) {
    return;
  }
  send_weather_request(now);
}

void weather_notify_success(void) {
  weather_on_success(&s_weather_refresh, time(NULL));
  save_weather_refresh_state();
}

void weather_notify_failure(void) {
  weather_on_failure(&s_weather_refresh, time(NULL));
  save_weather_refresh_state();
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
void pebble_iterate_sleep(
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

// ============================================================
// SLEEP STATE REFRESH
// ============================================================
// Polling, not events.
//
// HealthEventSleepUpdate is only posted when the sleep total actually
// changes, so it stops firing entirely once the night's total settles -
// which is exactly when a freshly launched watchface needs it. The
// kernel recomputes sleep every minute regardless, and reads of the
// metric are uncached, so a minute-tick poll always sees the freshest
// value the firmware has. Opening the Sleep app never refreshed
// anything; it just restarted the watchface, which re-ran init.
static void refresh_sleep_state(time_t now, bool force) {
  if (!s_needs_sleep_tracking) {
    return;
  }
  if (!force && !sleep_should_poll(&s_sleep_refresh, now)) {
    return;
  }

  HealthActivityMask activities = health_service_peek_current_activities();
  bool is_sleeping = (activities & HealthActivitySleep) != 0;

  // The step count doubles as a wake signal, so make sure it is current
  // before the decision rather than after.
  fetch_step_count();

  // Decide before recording the poll - the wake edge is defined against
  // the previous belief, which sleep_on_poll is about to overwrite.
  bool should_recalc = sleep_should_force_recalc(&s_sleep_refresh, now,
                                                 is_sleeping, s_last_step_count);

  sleep_on_poll(&s_sleep_refresh, now, is_sleeping, s_last_step_count);
  s_was_sleeping = is_sleeping;

  if (!should_recalc) {
    return;
  }

  UptimeResult result = uptime_recalculate(now, pebble_iterate_sleep);
  sleep_on_recalc(&s_sleep_refresh, now);

  // Only move the wake time when we actually know one. A failed lookup
  // means the firmware purged the sessions, not that the user never
  // slept - uptime_recalculate already preserves the previous value.
  if (result.found_real_sleep) {
    s_wake_time = uptime_get_effective_wake_time(&result);
  }

  if (settings.progress_bar_mode == PROGRESS_MODE_SLEEP) {
    update_progress();
  }
}
#endif

// ============================================================
// TICK HANDLER
// ============================================================
void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  time_t now = time(NULL);

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

  #if defined(PBL_HEALTH)
  // Poll sleep every minute. This is the only thing that keeps sleep and
  // uptime current without the user opening the Sleep app.
  refresh_sleep_state(now, false);
  #endif

  // Weather is scheduled on elapsed time, not on tm_min == 0. The old
  // top-of-the-hour check was skipped entirely whenever the watchface
  // was not the foreground app at that exact minute, which left weather
  // stale for hours with no retry.
  maybe_request_weather(now);

  // Re-evaluate which high to display (may switch at sunset)
  if (tick_time->tm_min == 0 && settings.show_weather) {
    update_weather_layers();
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

  // Reconnecting is the first moment a queued weather request can
  // actually succeed, so take it.
  if (connected) {
    maybe_request_weather(time(NULL));
  }
}

// ============================================================
// HEALTH HANDLER
// ============================================================
#if defined(PBL_HEALTH)
void health_handler(HealthEventType event, void *context) {
  time_t now = time(NULL);

  if (event == HealthEventSignificantUpdate ||
      event == HealthEventSleepUpdate ||
      event == HealthEventMovementUpdate) {
    update_steps();
  }

  // Events are a bonus, not the mechanism - they let us react faster
  // than the next minute tick when they do happen to fire.
  //
  // Only the two sleep-relevant events bypass the poll interval.
  // MovementUpdate fires every few seconds while the user is walking,
  // so it goes through the normal rate-limited path.
  bool sleep_relevant = (event == HealthEventSignificantUpdate ||
                         event == HealthEventSleepUpdate);
  refresh_sleep_state(now, sleep_relevant);
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
  // inbox_received_callback runs on every inbound AppMessage, including
  // plain weather replies. Without this guard each one would trigger a
  // full health-session scan and a flash write for no reason.
  static bool s_initialized = false;
  if (s_initialized) {
    return;
  }
  s_initialized = true;

  time_t now = time(NULL);

  // Init current sleep state
  HealthActivityMask activities = health_service_peek_current_activities();
  s_was_sleeping = (activities & HealthActivitySleep) != 0;

  // Initialize uptime module with Pebble storage. This restores the last
  // known wake time from flash, which is what carries the value across
  // the firmware's midnight session purge.
  uptime_init(pebble_storage_read, pebble_storage_write);

  // Seed the poll state so the first tick compares against a real
  // baseline instead of reporting a spurious wake edge.
  fetch_step_count();
  sleep_on_poll(&s_sleep_refresh, now, s_was_sleeping, s_last_step_count);

  // If currently sleeping, don't calculate yet
  if (s_was_sleeping) {
    return;
  }

  // Force recalculation on app start to catch any missed wake events
  // (e.g., app was closed during sleep and reopened after waking)
  UptimeResult result = uptime_recalculate(now, pebble_iterate_sleep);
  sleep_on_recalc(&s_sleep_refresh, now);

  if (result.found_real_sleep) {
    s_wake_time = uptime_get_effective_wake_time(&result);
  } else if (s_wake_time == 0) {
    // Genuinely nothing known - fall back to app start so the display
    // shows something rather than --:--.
    s_wake_time = now;
  }
  #endif
}

// ============================================================
// INIT REFRESH STATE
// ============================================================
void init_refresh_state(void) {
  load_weather_refresh_state();
}
