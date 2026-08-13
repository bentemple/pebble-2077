#include "refresh.h"

// ============================================================
// TIME HELPER
// ============================================================
// Has `seconds` elapsed since `since`?
//
// Two deliberate special cases:
//   - since == 0 means "never happened", which always counts as elapsed.
//   - A negative delta means the clock moved backwards (timezone change,
//     manual clock set, NTP correction). Treating that as "elapsed"
//     keeps the scheduler from wedging until the clock catches up.
static bool elapsed_at_least(time_t now, time_t since, int seconds) {
  if (since == 0) {
    return true;
  }
  long delta = (long)(now - since);
  if (delta < 0) {
    return true;
  }
  return delta >= (long)seconds;
}

// ============================================================
// WEATHER REFRESH
// ============================================================
int weather_retry_delay(int consecutive_failures) {
  if (consecutive_failures < 1) {
    return WEATHER_RETRY_BASE_DELAY;
  }
  // Clamp the shift before doing it so a runaway counter cannot overflow.
  if (consecutive_failures > 8) {
    return WEATHER_RETRY_MAX_DELAY;
  }
  int delay = WEATHER_RETRY_BASE_DELAY << (consecutive_failures - 1);
  return delay > WEATHER_RETRY_MAX_DELAY ? WEATHER_RETRY_MAX_DELAY : delay;
}

bool weather_is_due(const WeatherRefreshState *state, time_t now) {
  if (!state) {
    return false;
  }

  // Hard floor on request rate, applies to every path below.
  if (!elapsed_at_least(now, state->last_attempt, WEATHER_MIN_REQUEST_GAP)) {
    return false;
  }

  // Recovering from a dropped send: back off rather than wait a full hour.
  if (state->consecutive_failures > 0) {
    return elapsed_at_least(now, state->last_attempt,
                            weather_retry_delay(state->consecutive_failures));
  }

  // Normal cadence, measured from the last time data actually arrived -
  // not from the last time we asked.
  return elapsed_at_least(now, state->last_success, WEATHER_REFRESH_INTERVAL);
}

bool weather_should_request(const WeatherRefreshState *state, time_t now, bool connected) {
  // Without a phone there is nobody to answer the request.
  return connected && weather_is_due(state, now);
}

bool weather_is_due_on_launch(const WeatherRefreshState *state, time_t now) {
  if (!state) {
    return false;
  }
  if (!elapsed_at_least(now, state->last_attempt, WEATHER_MIN_REQUEST_GAP)) {
    return false;
  }
  return elapsed_at_least(now, state->last_success, WEATHER_STALE_ON_LAUNCH);
}

bool weather_should_request_on_launch(const WeatherRefreshState *state, time_t now, bool connected) {
  return connected && weather_is_due_on_launch(state, now);
}

void weather_on_attempt(WeatherRefreshState *state, time_t now) {
  if (!state) {
    return;
  }
  state->last_attempt = now;
}

void weather_on_success(WeatherRefreshState *state, time_t now) {
  if (!state) {
    return;
  }
  state->last_success = now;
  state->consecutive_failures = 0;
}

void weather_on_failure(WeatherRefreshState *state, time_t now) {
  if (!state) {
    return;
  }
  state->last_attempt = now;
  // Saturate rather than wrap; the delay is capped anyway.
  if (state->consecutive_failures < 1000) {
    state->consecutive_failures++;
  }
}

// ============================================================
// STEP COUNT REFRESH
// ============================================================
bool steps_should_refresh(time_t last_fetch, time_t now) {
  return elapsed_at_least(now, last_fetch, STEPS_REFRESH_INTERVAL);
}

// ============================================================
// SLEEP REFRESH
// ============================================================
bool sleep_should_poll(const SleepRefreshState *state, time_t now) {
  if (!state) {
    return false;
  }
  return elapsed_at_least(now, state->last_poll, SLEEP_POLL_INTERVAL);
}

bool sleep_should_force_recalc(const SleepRefreshState *state, time_t now,
                               bool health_says_sleeping, int current_steps) {
  if (!state) {
    return false;
  }

  if (state->believed_sleeping) {
    // The wake edge is the strongest signal we get. Never rate limit it.
    if (!health_says_sleeping) {
      return true;
    }

    // Health still claims we are asleep. The sleep algorithm holds a
    // session open for up to ~19 minutes after the user actually wakes,
    // so a rising step count is the better evidence. Signed arithmetic
    // here is load-bearing: the step counter resets to 0 at midnight and
    // the resulting negative delta must not read as a large positive one.
    int steps_since_sleep = current_steps - state->steps_at_sleep_start;
    if (steps_since_sleep >= SLEEP_WAKE_STEP_THRESHOLD) {
      return elapsed_at_least(now, state->last_recalc, SLEEP_FORCE_RECALC_GAP);
    }

    return false;
  }

  // Awake: slow backstop so retroactive corrections by the sleep
  // algorithm show up without the user opening the Sleep app.
  return elapsed_at_least(now, state->last_recalc, SLEEP_IDLE_RECALC_INTERVAL);
}

void sleep_on_poll(SleepRefreshState *state, time_t now,
                   bool health_says_sleeping, int current_steps) {
  if (!state) {
    return;
  }

  // Capture the step baseline once, at sleep onset. Re-baselining on
  // every poll would drive the delta to zero and silently disable the
  // step backstop entirely.
  if (health_says_sleeping && !state->believed_sleeping) {
    state->steps_at_sleep_start = current_steps;
  }

  state->believed_sleeping = health_says_sleeping;
  state->last_poll = now;
}

void sleep_on_recalc(SleepRefreshState *state, time_t now) {
  if (!state) {
    return;
  }
  state->last_recalc = now;
}
