/*
 * Refresh Scheduling Unit Tests
 * Compile: gcc -Isrc/c -o test/refresh_test.o test/refresh_test.c src/c/refresh.c && ./test/refresh_test.o
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "refresh.h"

// ============================================================
// TEST FRAMEWORK
// ============================================================
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
  printf("\n--- %s ---\n", #name); \
  test_##name(); \
  tests_run++; \
  tests_passed++; \
  printf("PASS\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
  if ((a) != (b)) { \
    printf("FAIL: %s:%d: %s == %ld, expected %ld\n", \
           __FILE__, __LINE__, #a, (long)(a), (long)(b)); \
    exit(1); \
  } \
} while(0)

#define ASSERT_TRUE(x) ASSERT_EQ(!!(x), 1)
#define ASSERT_FALSE(x) ASSERT_EQ(!!(x), 0)

// Arbitrary fixed "now" so tests never depend on the wall clock.
#define T0 ((time_t)1700000000)

// ============================================================
// WEATHER: BOOTSTRAP
// ============================================================
TEST(weather_requests_immediately_when_never_fetched) {
  // A brand new install has no weather at all - ask right away.
  WeatherRefreshState st = { .last_success = 0, .last_attempt = 0, .consecutive_failures = 0 };
  ASSERT_TRUE(weather_should_request(&st, T0, true));
}

TEST(weather_waits_when_disconnected) {
  // No phone connection means the outbox send is guaranteed to fail.
  // Don't burn a retry slot on it.
  WeatherRefreshState st = { .last_success = 0, .last_attempt = 0, .consecutive_failures = 0 };
  ASSERT_FALSE(weather_should_request(&st, T0, false));
}

// ============================================================
// WEATHER: HOURLY CADENCE
// ============================================================
TEST(weather_holds_off_while_data_is_fresh) {
  WeatherRefreshState st = { .last_success = T0, .last_attempt = T0, .consecutive_failures = 0 };
  // 59 minutes later - still fresh.
  ASSERT_FALSE(weather_should_request(&st, T0 + 59 * 60, true));
}

TEST(weather_refreshes_after_an_hour) {
  WeatherRefreshState st = { .last_success = T0, .last_attempt = T0, .consecutive_failures = 0 };
  ASSERT_TRUE(weather_should_request(&st, T0 + WEATHER_REFRESH_INTERVAL, true));
}

TEST(weather_refreshes_when_the_hourly_tick_was_missed) {
  // This is the real-world bug: the old code only fired at tm_min == 0.
  // If the watchface was not running at the top of the hour (user was in
  // another app), the refresh was skipped entirely and nothing retried.
  // A time-based check must still fire once the watchface comes back,
  // however late that is.
  WeatherRefreshState st = { .last_success = T0, .last_attempt = T0, .consecutive_failures = 0 };
  ASSERT_TRUE(weather_should_request(&st, T0 + 3 * 60 * 60 + 37 * 60, true));
}

// ============================================================
// WEATHER: RETRY / BACKOFF
// ============================================================
TEST(weather_retry_delay_backs_off_exponentially) {
  ASSERT_EQ(weather_retry_delay(1), 60);
  ASSERT_EQ(weather_retry_delay(2), 120);
  ASSERT_EQ(weather_retry_delay(3), 240);
  ASSERT_EQ(weather_retry_delay(4), 480);
}

TEST(weather_retry_delay_is_capped) {
  ASSERT_EQ(weather_retry_delay(5), WEATHER_RETRY_MAX_DELAY);
  ASSERT_EQ(weather_retry_delay(20), WEATHER_RETRY_MAX_DELAY);
  // Guard against a nonsense input shifting by a huge amount.
  ASSERT_EQ(weather_retry_delay(0), WEATHER_RETRY_BASE_DELAY);
  ASSERT_EQ(weather_retry_delay(-1), WEATHER_RETRY_BASE_DELAY);
}

TEST(weather_retries_after_a_failure_without_waiting_an_hour) {
  // A dropped request must not leave us blind until the next hour.
  WeatherRefreshState st = { .last_success = T0, .last_attempt = T0 + 10, .consecutive_failures = 1 };
  ASSERT_FALSE(weather_should_request(&st, T0 + 10 + 59, true));
  ASSERT_TRUE(weather_should_request(&st, T0 + 10 + 60, true));
}

TEST(weather_respects_growing_backoff) {
  WeatherRefreshState st = { .last_success = T0, .last_attempt = T0 + 10, .consecutive_failures = 3 };
  ASSERT_FALSE(weather_should_request(&st, T0 + 10 + 239, true));
  ASSERT_TRUE(weather_should_request(&st, T0 + 10 + 240, true));
}

TEST(weather_success_clears_the_failure_streak) {
  WeatherRefreshState st = { .last_success = 0, .last_attempt = T0, .consecutive_failures = 4 };
  weather_on_success(&st, T0 + 5);
  ASSERT_EQ(st.consecutive_failures, 0);
  ASSERT_EQ(st.last_success, T0 + 5);
  // Back to the normal hourly cadence.
  ASSERT_FALSE(weather_should_request(&st, T0 + 5 + 60, true));
  ASSERT_TRUE(weather_should_request(&st, T0 + 5 + WEATHER_REFRESH_INTERVAL, true));
}

TEST(weather_failure_increments_the_streak) {
  WeatherRefreshState st = { .last_success = T0, .last_attempt = T0, .consecutive_failures = 0 };
  weather_on_failure(&st, T0 + 1);
  ASSERT_EQ(st.consecutive_failures, 1);
  weather_on_failure(&st, T0 + 2);
  ASSERT_EQ(st.consecutive_failures, 2);
}

TEST(weather_never_spams_faster_than_the_minimum_gap) {
  // Even with a never-fetched bootstrap state, two ticks in a row must
  // not produce two sends.
  WeatherRefreshState st = { .last_success = 0, .last_attempt = 0, .consecutive_failures = 0 };
  ASSERT_TRUE(weather_should_request(&st, T0, true));
  weather_on_attempt(&st, T0);
  ASSERT_FALSE(weather_should_request(&st, T0 + 1, true));
  ASSERT_FALSE(weather_should_request(&st, T0 + WEATHER_MIN_REQUEST_GAP - 1, true));
  ASSERT_TRUE(weather_should_request(&st, T0 + WEATHER_MIN_REQUEST_GAP, true));
}

TEST(weather_attempt_does_not_count_as_success) {
  // Sending is not receiving. An unanswered request must still be
  // considered stale so the retry path takes over.
  WeatherRefreshState st = { .last_success = 0, .last_attempt = 0, .consecutive_failures = 0 };
  weather_on_attempt(&st, T0);
  ASSERT_EQ(st.last_success, 0);
}

TEST(weather_recovers_from_a_clock_jump_backwards) {
  // Timezone change or a manual clock set can move "now" behind the
  // stored timestamps. That must not wedge the scheduler forever.
  WeatherRefreshState st = { .last_success = T0 + 10000, .last_attempt = T0 + 10000, .consecutive_failures = 0 };
  ASSERT_TRUE(weather_should_request(&st, T0, true));
}

// ============================================================
// WEATHER: DUE-ONLY PREDICATES
// ============================================================
// These exist so the caller can skip the connection syscall on the
// ~59 of every 60 ticks where no request is due.
TEST(weather_is_due_ignores_connectivity) {
  WeatherRefreshState st = { .last_success = 0, .last_attempt = 0, .consecutive_failures = 0 };
  ASSERT_TRUE(weather_is_due(&st, T0));
}

TEST(weather_is_due_matches_should_request_when_connected) {
  // The split must not change behaviour, only the order of the checks.
  WeatherRefreshState states[] = {
    { .last_success = 0,  .last_attempt = 0,       .consecutive_failures = 0 },
    { .last_success = T0, .last_attempt = T0,      .consecutive_failures = 0 },
    { .last_success = T0, .last_attempt = T0 + 10, .consecutive_failures = 1 },
    { .last_success = T0, .last_attempt = T0 + 10, .consecutive_failures = 3 },
  };
  int offsets[] = { 0, 30, 59, 60, 61, 120, 240, 3599, 3600, 7200 };

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 10; j++) {
      time_t now = T0 + offsets[j];
      ASSERT_EQ(weather_is_due(&states[i], now),
                weather_should_request(&states[i], now, true));
      // Never send while disconnected, whatever the timing says.
      ASSERT_FALSE(weather_should_request(&states[i], now, false));
    }
  }
}

TEST(weather_is_due_on_launch_matches_should_request_on_launch) {
  WeatherRefreshState st = { .last_success = T0, .last_attempt = T0, .consecutive_failures = 0 };
  int offsets[] = { 0, 30, 60, 1799, 1800, 3600 };
  for (int j = 0; j < 6; j++) {
    time_t now = T0 + offsets[j];
    ASSERT_EQ(weather_is_due_on_launch(&st, now),
              weather_should_request_on_launch(&st, now, true));
    ASSERT_FALSE(weather_should_request_on_launch(&st, now, false));
  }
}

// ============================================================
// WEATHER: LAUNCH
// ============================================================
TEST(weather_launch_fetches_when_data_is_stale) {
  // The watchface is torn down every time the user opens another app,
  // so launch is a frequent and cheap opportunity to top up.
  WeatherRefreshState st = { .last_success = T0, .last_attempt = T0, .consecutive_failures = 0 };
  ASSERT_TRUE(weather_should_request_on_launch(&st, T0 + WEATHER_STALE_ON_LAUNCH, true));
}

TEST(weather_launch_skips_when_data_is_recent) {
  // Opening and closing the Sleep app five times must not fire five
  // weather requests.
  WeatherRefreshState st = { .last_success = T0, .last_attempt = T0, .consecutive_failures = 0 };
  ASSERT_FALSE(weather_should_request_on_launch(&st, T0 + 60, true));
}

TEST(weather_launch_fetches_when_never_fetched) {
  WeatherRefreshState st = { .last_success = 0, .last_attempt = 0, .consecutive_failures = 0 };
  ASSERT_TRUE(weather_should_request_on_launch(&st, T0, true));
}

TEST(weather_launch_respects_disconnection) {
  WeatherRefreshState st = { .last_success = 0, .last_attempt = 0, .consecutive_failures = 0 };
  ASSERT_FALSE(weather_should_request_on_launch(&st, T0, false));
}

TEST(weather_launch_respects_the_minimum_gap) {
  // Guards against a relaunch loop hammering the phone.
  WeatherRefreshState st = { .last_success = 0, .last_attempt = T0, .consecutive_failures = 0 };
  ASSERT_FALSE(weather_should_request_on_launch(&st, T0 + 30, true));
  ASSERT_TRUE(weather_should_request_on_launch(&st, T0 + WEATHER_MIN_REQUEST_GAP, true));
}

// ============================================================
// STEP COUNT REFRESH
// ============================================================
TEST(steps_refresh_on_the_first_read) {
  ASSERT_TRUE(steps_should_refresh(0, T0));
}

TEST(steps_refresh_at_most_once_a_minute) {
  // MovementUpdate fires every few seconds while walking; each read is
  // two health syscalls for a number rendered once a minute.
  ASSERT_FALSE(steps_should_refresh(T0, T0 + 1));
  ASSERT_FALSE(steps_should_refresh(T0, T0 + 59));
  ASSERT_TRUE(steps_should_refresh(T0, T0 + 60));
}

TEST(steps_refresh_recovers_from_a_clock_jump_backwards) {
  ASSERT_TRUE(steps_should_refresh(T0 + 10000, T0));
}

// ============================================================
// SLEEP: POLLING
// ============================================================
TEST(sleep_polls_on_the_first_tick) {
  SleepRefreshState st = { 0 };
  ASSERT_TRUE(sleep_should_poll(&st, T0));
}

TEST(sleep_polls_once_a_minute) {
  SleepRefreshState st = { .last_poll = T0 };
  ASSERT_FALSE(sleep_should_poll(&st, T0 + 59));
  ASSERT_TRUE(sleep_should_poll(&st, T0 + 60));
}

TEST(sleep_poll_recovers_from_a_clock_jump_backwards) {
  SleepRefreshState st = { .last_poll = T0 + 10000 };
  ASSERT_TRUE(sleep_should_poll(&st, T0));
}

// ============================================================
// SLEEP: THE WAKE EDGE
// ============================================================
TEST(sleep_recalculates_on_the_wake_edge) {
  // Believed asleep, health now says awake -> the classic transition.
  SleepRefreshState st = {
    .last_poll = T0, .last_recalc = T0, .believed_sleeping = true, .steps_at_sleep_start = 0
  };
  ASSERT_TRUE(sleep_should_force_recalc(&st, T0 + 60, false, 0));
}

TEST(sleep_wake_edge_ignores_the_recalc_gap) {
  // The wake edge is the single most important event we can observe.
  // It must never be suppressed by rate limiting.
  SleepRefreshState st = {
    .last_poll = T0, .last_recalc = T0, .believed_sleeping = true, .steps_at_sleep_start = 0
  };
  ASSERT_TRUE(sleep_should_force_recalc(&st, T0 + 1, false, 0));
}

// ============================================================
// SLEEP: THE STEP-COUNT BACKSTOP (the user's reported symptom)
// ============================================================
TEST(sleep_recalculates_when_steps_climb_while_believed_asleep) {
  // "I know the watchface is checking because I have >100 steps, but the
  // sleep value doesn't update." The Kraepelin algorithm can hold the
  // sleep session open for ~19 minutes after the user actually wakes, so
  // peek_current_activities still reports sleeping. Walking around is a
  // far better wake signal than the algorithm's own opinion.
  SleepRefreshState st = {
    .last_poll = T0, .last_recalc = T0, .believed_sleeping = true, .steps_at_sleep_start = 500
  };
  // Health still insists we are asleep, but we have taken 150 steps.
  ASSERT_TRUE(sleep_should_force_recalc(&st, T0 + SLEEP_FORCE_RECALC_GAP, true, 650));
}

TEST(sleep_ignores_a_few_restless_steps) {
  // Rolling over in bed should not be mistaken for getting up.
  SleepRefreshState st = {
    .last_poll = T0, .last_recalc = T0, .believed_sleeping = true, .steps_at_sleep_start = 500
  };
  ASSERT_FALSE(sleep_should_force_recalc(&st, T0 + SLEEP_FORCE_RECALC_GAP, true, 500 + 99));
}

TEST(sleep_step_backstop_is_rate_limited) {
  // Once we have recalculated, don't hammer the health API every tick
  // just because the step delta is still above the threshold.
  SleepRefreshState st = {
    .last_poll = T0, .last_recalc = T0, .believed_sleeping = true, .steps_at_sleep_start = 500
  };
  ASSERT_FALSE(sleep_should_force_recalc(&st, T0 + SLEEP_FORCE_RECALC_GAP - 1, true, 900));
  ASSERT_TRUE(sleep_should_force_recalc(&st, T0 + SLEEP_FORCE_RECALC_GAP, true, 900));
}

TEST(sleep_step_backstop_handles_a_step_counter_reset) {
  // Step count resets to 0 at midnight. A negative delta must not be
  // read as "hundreds of steps" via unsigned wraparound, nor wedge the
  // backstop permanently.
  SleepRefreshState st = {
    .last_poll = T0, .last_recalc = T0, .believed_sleeping = true, .steps_at_sleep_start = 8000
  };
  ASSERT_FALSE(sleep_should_force_recalc(&st, T0 + SLEEP_FORCE_RECALC_GAP, true, 12));
}

// ============================================================
// SLEEP: IDLE BACKSTOP
// ============================================================
TEST(sleep_recalculates_periodically_while_awake) {
  // The sleep algorithm retroactively adjusts and can even delete
  // sessions. A slow backstop keeps the display honest without the
  // user having to open the Sleep app.
  SleepRefreshState st = {
    .last_poll = T0, .last_recalc = T0, .believed_sleeping = false, .steps_at_sleep_start = 0
  };
  ASSERT_FALSE(sleep_should_force_recalc(&st, T0 + SLEEP_IDLE_RECALC_INTERVAL - 1, false, 0));
  ASSERT_TRUE(sleep_should_force_recalc(&st, T0 + SLEEP_IDLE_RECALC_INTERVAL, false, 0));
}

TEST(sleep_does_not_recalculate_constantly_while_asleep) {
  // Believed asleep, health agrees, no meaningful steps: leave it alone.
  SleepRefreshState st = {
    .last_poll = T0, .last_recalc = T0, .believed_sleeping = true, .steps_at_sleep_start = 100
  };
  ASSERT_FALSE(sleep_should_force_recalc(&st, T0 + 60, true, 100));
}

// ============================================================
// SLEEP: STATE TRANSITIONS
// ============================================================
TEST(sleep_on_poll_records_the_step_baseline_when_falling_asleep) {
  SleepRefreshState st = { .last_poll = 0, .last_recalc = 0, .believed_sleeping = false };
  sleep_on_poll(&st, T0, true, 4200);
  ASSERT_TRUE(st.believed_sleeping);
  ASSERT_EQ(st.steps_at_sleep_start, 4200);
  ASSERT_EQ(st.last_poll, T0);
}

TEST(sleep_on_poll_keeps_the_baseline_while_still_asleep) {
  // The baseline must be captured once at sleep onset. Re-baselining on
  // every poll would make the step delta always ~0 and silently disable
  // the entire backstop.
  SleepRefreshState st = { .last_poll = 0, .last_recalc = 0, .believed_sleeping = false };
  sleep_on_poll(&st, T0, true, 4200);
  sleep_on_poll(&st, T0 + 60, true, 4250);
  sleep_on_poll(&st, T0 + 120, true, 4310);
  ASSERT_EQ(st.steps_at_sleep_start, 4200);
}

TEST(sleep_on_poll_clears_the_belief_on_waking) {
  SleepRefreshState st = { .last_poll = 0, .last_recalc = 0, .believed_sleeping = false };
  sleep_on_poll(&st, T0, true, 4200);
  sleep_on_poll(&st, T0 + 60, false, 4400);
  ASSERT_FALSE(st.believed_sleeping);
}

TEST(sleep_on_recalc_stamps_the_time) {
  SleepRefreshState st = { 0 };
  sleep_on_recalc(&st, T0);
  ASSERT_EQ(st.last_recalc, T0);
}

TEST(sleep_full_wake_sequence) {
  // End-to-end: asleep -> algorithm lags -> steps give it away ->
  // recalc -> algorithm catches up -> no further forced recalcs.
  SleepRefreshState st = { 0 };

  sleep_on_poll(&st, T0, true, 0);           // Falls asleep at step 0
  sleep_on_recalc(&st, T0);
  ASSERT_TRUE(st.believed_sleeping);

  // 6 minutes later: awake and walking, but health has not caught up.
  time_t t = T0 + 6 * 60;
  ASSERT_TRUE(sleep_should_force_recalc(&st, t, true, 180));
  sleep_on_recalc(&st, t);

  // Immediately after, rate limiting holds.
  ASSERT_FALSE(sleep_should_force_recalc(&st, t + 60, true, 200));

  // Health finally agrees we are awake - the edge still fires.
  ASSERT_TRUE(sleep_should_force_recalc(&st, t + 60, false, 200));
  sleep_on_poll(&st, t + 60, false, 200);
  sleep_on_recalc(&st, t + 60);

  // Now settled: nothing until the idle backstop.
  ASSERT_FALSE(sleep_should_force_recalc(&st, t + 120, false, 400));
}

// ============================================================
// MAIN
// ============================================================
int main(void) {
  printf("==============================================\n");
  printf("      REFRESH SCHEDULING UNIT TESTS\n");
  printf("==============================================\n");

  printf("\n--- WEATHER BOOTSTRAP ---\n");
  RUN_TEST(weather_requests_immediately_when_never_fetched);
  RUN_TEST(weather_waits_when_disconnected);

  printf("\n--- WEATHER CADENCE ---\n");
  RUN_TEST(weather_holds_off_while_data_is_fresh);
  RUN_TEST(weather_refreshes_after_an_hour);
  RUN_TEST(weather_refreshes_when_the_hourly_tick_was_missed);

  printf("\n--- WEATHER RETRY ---\n");
  RUN_TEST(weather_retry_delay_backs_off_exponentially);
  RUN_TEST(weather_retry_delay_is_capped);
  RUN_TEST(weather_retries_after_a_failure_without_waiting_an_hour);
  RUN_TEST(weather_respects_growing_backoff);
  RUN_TEST(weather_success_clears_the_failure_streak);
  RUN_TEST(weather_failure_increments_the_streak);
  RUN_TEST(weather_never_spams_faster_than_the_minimum_gap);
  RUN_TEST(weather_attempt_does_not_count_as_success);
  RUN_TEST(weather_recovers_from_a_clock_jump_backwards);

  printf("\n--- WEATHER DUE-ONLY PREDICATES ---\n");
  RUN_TEST(weather_is_due_ignores_connectivity);
  RUN_TEST(weather_is_due_matches_should_request_when_connected);
  RUN_TEST(weather_is_due_on_launch_matches_should_request_on_launch);

  printf("\n--- WEATHER LAUNCH ---\n");
  RUN_TEST(weather_launch_fetches_when_data_is_stale);
  RUN_TEST(weather_launch_skips_when_data_is_recent);
  RUN_TEST(weather_launch_fetches_when_never_fetched);
  RUN_TEST(weather_launch_respects_disconnection);
  RUN_TEST(weather_launch_respects_the_minimum_gap);

  printf("\n--- STEP COUNT REFRESH ---\n");
  RUN_TEST(steps_refresh_on_the_first_read);
  RUN_TEST(steps_refresh_at_most_once_a_minute);
  RUN_TEST(steps_refresh_recovers_from_a_clock_jump_backwards);

  printf("\n--- SLEEP POLLING ---\n");
  RUN_TEST(sleep_polls_on_the_first_tick);
  RUN_TEST(sleep_polls_once_a_minute);
  RUN_TEST(sleep_poll_recovers_from_a_clock_jump_backwards);

  printf("\n--- SLEEP WAKE EDGE ---\n");
  RUN_TEST(sleep_recalculates_on_the_wake_edge);
  RUN_TEST(sleep_wake_edge_ignores_the_recalc_gap);

  printf("\n--- SLEEP STEP BACKSTOP ---\n");
  RUN_TEST(sleep_recalculates_when_steps_climb_while_believed_asleep);
  RUN_TEST(sleep_ignores_a_few_restless_steps);
  RUN_TEST(sleep_step_backstop_is_rate_limited);
  RUN_TEST(sleep_step_backstop_handles_a_step_counter_reset);

  printf("\n--- SLEEP IDLE BACKSTOP ---\n");
  RUN_TEST(sleep_recalculates_periodically_while_awake);
  RUN_TEST(sleep_does_not_recalculate_constantly_while_asleep);

  printf("\n--- SLEEP STATE TRANSITIONS ---\n");
  RUN_TEST(sleep_on_poll_records_the_step_baseline_when_falling_asleep);
  RUN_TEST(sleep_on_poll_keeps_the_baseline_while_still_asleep);
  RUN_TEST(sleep_on_poll_clears_the_belief_on_waking);
  RUN_TEST(sleep_on_recalc_stamps_the_time);
  RUN_TEST(sleep_full_wake_sequence);

  printf("\n==============================================\n");
  printf("  Results: %d/%d tests passed\n", tests_passed, tests_run);
  printf("==============================================\n");

  return (tests_passed == tests_run) ? 0 : 1;
}
