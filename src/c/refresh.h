#pragma once

// Support both Pebble SDK and standard C builds
#ifdef PBL_SDK_3
  #include <pebble.h>
#else
  #include <stdint.h>
  #include <stdbool.h>
  #include <stddef.h>
  #include <time.h>
#endif

// ============================================================
// REFRESH SCHEDULING MODULE
// ============================================================
// Pure decision logic for "when should we ask for fresh data?".
//
// Why this exists:
//   The watchface cannot force the firmware to recompute anything.
//   Sleep totals are recalculated by a kernel cron job once a minute,
//   and weather arrives only when our PebbleKit JS counterpart answers
//   an AppMessage. Both delivery paths are lossy:
//
//   - HealthEventSleepUpdate stops firing once the sleep total stops
//     changing, so an event-driven watchface goes permanently stale.
//   - A watchface's pkjs is torn down and restarted whenever the user
//     opens any other app; in-flight outbox messages are dropped with
//     no callback, and inbound replies addressed to a non-foreground
//     app are NACKed and discarded.
//
//   So instead of trusting events, we poll on a schedule and retry on
//   failure. This module holds the "is it time yet?" decisions so they
//   can be unit tested on the host without a watch.
// ============================================================

// ============================================================
// WEATHER REFRESH
// ============================================================

// Ask for fresh weather this long after the last successful update.
#define WEATHER_REFRESH_INTERVAL   (60 * 60)   // 1 hour

// Never send two requests closer together than this, even on retry.
#define WEATHER_MIN_REQUEST_GAP    60          // 1 minute

// Retry backoff after a failed send: 60s, 120s, 240s, 480s, capped.
#define WEATHER_RETRY_BASE_DELAY   60
#define WEATHER_RETRY_MAX_DELAY    (15 * 60)   // 15 minutes

// Treat stored weather older than this as worth refetching at app start.
#define WEATHER_STALE_ON_LAUNCH    (30 * 60)   // 30 minutes

typedef struct {
  time_t last_success;       // When weather data last arrived (0 = never)
  time_t last_attempt;       // When we last sent a request (0 = never)
  int consecutive_failures;  // Failed sends since the last success
} WeatherRefreshState;

// Backoff delay in seconds for the Nth consecutive failure (N >= 1).
int weather_retry_delay(int consecutive_failures);

// Is a request due on timing alone, ignoring connectivity?
//
// Split out so callers can run this cheap arithmetic check every minute
// and only pay for the connection syscall once a request is actually
// due - which is roughly once an hour.
bool weather_is_due(const WeatherRefreshState *state, time_t now);

// Should we send a weather request right now?
bool weather_should_request(const WeatherRefreshState *state, time_t now, bool connected);

// Should we send a weather request because the watchface just launched?
// Uses a shorter staleness threshold than the periodic check: a launch
// is a cheap, natural moment to top up, and the watchface is relaunched
// every time the user backs out of another app.
bool weather_should_request_on_launch(const WeatherRefreshState *state, time_t now, bool connected);

// Timing-only half of the launch check, for the same reason as above.
bool weather_is_due_on_launch(const WeatherRefreshState *state, time_t now);

// Record that we just sent a request.
void weather_on_attempt(WeatherRefreshState *state, time_t now);

// Record that weather data arrived.
void weather_on_success(WeatherRefreshState *state, time_t now);

// Record that a send failed (outbox_failed, or outbox_begin refused).
void weather_on_failure(WeatherRefreshState *state, time_t now);

// ============================================================
// STEP COUNT REFRESH
// ============================================================

// Step count is read from the health service no more often than this.
// MovementUpdate events fire every few seconds while the user walks,
// and each read costs two health syscalls to produce a number that is
// only rendered once a minute anyway.
#define STEPS_REFRESH_INTERVAL 60

// Has enough time passed to re-read the step count?
bool steps_should_refresh(time_t last_fetch, time_t now);

// ============================================================
// SLEEP REFRESH
// ============================================================

// How often to poll the health service for the current sleep state.
#define SLEEP_POLL_INTERVAL        60          // Every minute tick

// Steps accumulated while we still believe the user is asleep that
// mean "the sleep algorithm has not caught up yet — recheck anyway".
#define SLEEP_WAKE_STEP_THRESHOLD  100

// Minimum spacing between forced recalculations.
#define SLEEP_FORCE_RECALC_GAP     (5 * 60)    // 5 minutes

// Backstop: recalculate this often even when nothing looks wrong, so
// retroactive corrections by the sleep algorithm are picked up.
#define SLEEP_IDLE_RECALC_INTERVAL (30 * 60)   // 30 minutes

typedef struct {
  time_t last_poll;          // Last time we peeked the health service
  time_t last_recalc;        // Last full uptime recalculation
  bool believed_sleeping;    // What we currently think the user is doing
  int steps_at_sleep_start;  // Step count when we started believing asleep
} SleepRefreshState;

// Should we peek the health service for the current sleep state?
bool sleep_should_poll(const SleepRefreshState *state, time_t now);

// Given a fresh health reading, should we force a full recalculation?
bool sleep_should_force_recalc(const SleepRefreshState *state, time_t now,
                               bool health_says_sleeping, int current_steps);

// Record the result of a poll, updating the believed state.
void sleep_on_poll(SleepRefreshState *state, time_t now,
                   bool health_says_sleeping, int current_steps);

// Record that a full recalculation just happened.
void sleep_on_recalc(SleepRefreshState *state, time_t now);
