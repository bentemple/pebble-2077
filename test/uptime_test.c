/*
 * Uptime Calculation Unit Tests
 * Compile: gcc -I../src/c -o uptime_test uptime_test.c ../src/c/uptime.c && ./uptime_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "uptime.h"

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

// ============================================================
// TIME HELPERS
// ============================================================
static time_t make_time(int day_offset, int hour, int minute) {
  struct tm base_tm = {
    .tm_year = 124, .tm_mon = 0, .tm_mday = 15,
    .tm_hour = 0, .tm_min = 0, .tm_sec = 0, .tm_isdst = -1
  };
  time_t base = mktime(&base_tm);
  return base + (day_offset * 86400) + (hour * 3600) + (minute * 60);
}

#define TODAY(h, m) make_time(0, h, m)
#define YESTERDAY(h, m) make_time(-1, h, m)
#define TWO_DAYS_AGO(h, m) make_time(-2, h, m)

// ============================================================
// MOCK SLEEP ITERATOR
// ============================================================
typedef struct {
  time_t starts[32];
  time_t ends[32];
  int count;
} MockSleepData;

static MockSleepData *g_mock_data = NULL;

static void mock_iterate_sleep(
  time_t range_start, time_t range_end, bool backwards,
  UptimeSleepIteratorCB callback, void *context
) {
  if (!g_mock_data) return;
  if (backwards) {
    for (int i = 0; i < g_mock_data->count; i++) {
      if (g_mock_data->ends[i] >= range_start && g_mock_data->starts[i] <= range_end) {
        if (!callback(g_mock_data->starts[i], g_mock_data->ends[i], context)) break;
      }
    }
  } else {
    for (int i = g_mock_data->count - 1; i >= 0; i--) {
      if (g_mock_data->ends[i] >= range_start && g_mock_data->starts[i] <= range_end) {
        if (!callback(g_mock_data->starts[i], g_mock_data->ends[i], context)) break;
      }
    }
  }
}

static void add_sleep(MockSleepData *data, time_t start, time_t end) {
  if (data->count < 32) {
    data->starts[data->count] = start;
    data->ends[data->count] = end;
    data->count++;
  }
}

// Helper to get uptime in minutes
static int get_uptime_mins(time_t now, UptimeResult *result) {
  if (!result->found_real_sleep) return -1;
  time_t effective = uptime_get_effective_wake_time(result);
  return (now - effective) / 60;
}

// ============================================================
// TEST 1: Simple overnight sleep
// ============================================================
/*
 * YESTERDAY 23:00 ════════════════ TODAY 07:00 ──── 07:30^ ──── 14:00^
 *           8hr sleep                          30m        7hr
 *
 * At 07:30^: uptime = 0:30
 * At 14:00^: uptime = 7:00
 */
TEST(simple_overnight_sleep) {
  MockSleepData data = {0};
  add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));
  g_mock_data = &data;

  UptimeResult r1 = uptime_calculate(TODAY(7, 30), mock_iterate_sleep);
  ASSERT_TRUE(r1.found_real_sleep);
  ASSERT_EQ(get_uptime_mins(TODAY(7, 30), &r1), 30);

  UptimeResult r2 = uptime_calculate(TODAY(14, 0), mock_iterate_sleep);
  ASSERT_EQ(get_uptime_mins(TODAY(14, 0), &r2), 7 * 60);
}

// ============================================================
// TEST 2: Afternoon nap
// ============================================================
/*
 * YESTERDAY 23:00 ════════ TODAY 07:00 ──── 13:00 ══ 15:00 ── 15:30^ ── 17:00^
 *           8hr sleep            6hr         2hr nap    30m       2hr
 *
 * At 15:30^: uptime = 6:30 (8.5hr - 2hr nap)
 * At 17:00^: uptime = 8:00 (10hr - 2hr nap)
 */
TEST(afternoon_nap) {
  MockSleepData data = {0};
  add_sleep(&data, TODAY(13, 0), TODAY(15, 0));
  add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));
  g_mock_data = &data;

  UptimeResult r1 = uptime_calculate(TODAY(15, 30), mock_iterate_sleep);
  ASSERT_TRUE(r1.found_real_sleep);
  ASSERT_EQ(r1.total_nap_secs, 2 * 3600);
  ASSERT_EQ(get_uptime_mins(TODAY(15, 30), &r1), 6 * 60 + 30);

  UptimeResult r2 = uptime_calculate(TODAY(17, 0), mock_iterate_sleep);
  ASSERT_EQ(get_uptime_mins(TODAY(17, 0), &r2), 8 * 60);
}

// ============================================================
// TEST 3: Bad night sleep + nap + next sleep
// ============================================================
/*
 * 2DAYS_AGO 22:00 ══ 23:30 ──── YESTERDAY 09:30 ═ 10:30 ──── 18:00 ════════ TODAY 02:00 ── 08:00^
 *              1.5hr real         10hr         1hr nap   7.5hr    8hr real sleep     6hr
 *
 * At 08:00^: uptime = 6:00
 */
TEST(bad_night_then_nap_then_sleep) {
  MockSleepData data = {0};
  add_sleep(&data, YESTERDAY(18, 0), TODAY(2, 0));
  add_sleep(&data, YESTERDAY(9, 30), YESTERDAY(10, 30));
  add_sleep(&data, TWO_DAYS_AGO(22, 0), TWO_DAYS_AGO(23, 30));
  g_mock_data = &data;

  UptimeResult r = uptime_calculate(TODAY(8, 0), mock_iterate_sleep);
  ASSERT_TRUE(r.found_real_sleep);
  ASSERT_EQ(r.last_real_sleep_end, TODAY(2, 0));
  ASSERT_EQ(get_uptime_mins(TODAY(8, 0), &r), 6 * 60);
}

// ============================================================
// TEST 4: Sleep with kid wakeup (merged sessions)
// ============================================================
/*
 * YESTERDAY 23:00 ════ 02:00 | 02:30^ | 02:30 ════ 06:00 ── 06:30^ ── 14:00^
 *              3hr      30m   check     3.5hr            30m       8hr
 *                      gap   mid-wake  [merges with earlier block]
 *
 * At 02:30^ (mid-wake): uptime = 0:30 (only sees first 3hr block)
 * At 06:30^: uptime = 0:30 (merged 7hr block ends at 06:00)
 * At 14:00^: uptime = 8:00
 */
TEST(sleep_with_kid_wakeup) {
  MockSleepData data = {0};
  add_sleep(&data, TODAY(2, 30), TODAY(6, 0));
  add_sleep(&data, YESTERDAY(23, 0), TODAY(2, 0));
  g_mock_data = &data;

  // Mid-wake check (only first block visible conceptually)
  UptimeResult r1 = uptime_calculate(TODAY(2, 30), mock_iterate_sleep);
  ASSERT_TRUE(r1.found_real_sleep);

  // After full sleep
  UptimeResult r2 = uptime_calculate(TODAY(6, 30), mock_iterate_sleep);
  ASSERT_EQ(r2.last_real_sleep_end, TODAY(6, 0));
  ASSERT_EQ(get_uptime_mins(TODAY(6, 30), &r2), 30);

  UptimeResult r3 = uptime_calculate(TODAY(14, 0), mock_iterate_sleep);
  ASSERT_EQ(get_uptime_mins(TODAY(14, 0), &r3), 8 * 60);
}

// ============================================================
// TEST 5: Night shift worker (sleeps during day)
// ============================================================
/*
 * YESTERDAY 06:00 ════════════ 14:00 ────────────── TODAY 06:00 ── 06:30^ ── 10:00^
 *              8hr day sleep         16hr awake              30m       4hr
 *
 * At 06:30^: uptime = 16:30
 * At 10:00^: uptime = 20:00
 */
TEST(night_shift_worker) {
  MockSleepData data = {0};
  add_sleep(&data, YESTERDAY(6, 0), YESTERDAY(14, 0));
  g_mock_data = &data;

  UptimeResult r1 = uptime_calculate(TODAY(6, 30), mock_iterate_sleep);
  ASSERT_TRUE(r1.found_real_sleep);
  ASSERT_EQ(get_uptime_mins(TODAY(6, 30), &r1), 16 * 60 + 30);

  UptimeResult r2 = uptime_calculate(TODAY(10, 0), mock_iterate_sleep);
  ASSERT_EQ(get_uptime_mins(TODAY(10, 0), &r2), 20 * 60);
}

// ============================================================
// TEST 6: Evening "nap" is night sleep
// ============================================================
/*
 * YESTERDAY 06:00 ══ 06:30 ────────────── 21:30 ═ 22:30 ── 23:00^ ── TODAY 10:00^
 *           8hr prior          15hr          1hr         30m          11.5hr
 *                                         (night=real)
 *
 * At 23:00^: uptime = 0:30
 * At 10:00^ (next day): uptime = 11:30
 */
TEST(evening_nap_is_night_sleep) {
  MockSleepData data = {0};
  add_sleep(&data, YESTERDAY(21, 30), YESTERDAY(22, 30));
  g_mock_data = &data;

  UptimeResult r1 = uptime_calculate(YESTERDAY(23, 0), mock_iterate_sleep);
  ASSERT_TRUE(r1.found_real_sleep);
  ASSERT_EQ(get_uptime_mins(YESTERDAY(23, 0), &r1), 30);

  UptimeResult r2 = uptime_calculate(TODAY(10, 0), mock_iterate_sleep);
  ASSERT_EQ(get_uptime_mins(TODAY(10, 0), &r2), 11 * 60 + 30);
}

// ============================================================
// TEST 7: Multiple naps in a day
// ============================================================
/*
 * YESTERDAY 22:00 ════════ TODAY 06:00 ──── 11:00 ═ 12:00 ──── 17:00 ═ 18:00 ── 18:30^ ── 20:00^
 *              8hr sleep          5hr       1hr nap    5hr      1hr nap   30m       2hr
 *
 * At 18:30^: uptime = 10:30 (12.5hr - 2hr naps)
 * At 20:00^: uptime = 12:00 (14hr - 2hr naps)
 */
TEST(multiple_naps) {
  MockSleepData data = {0};
  add_sleep(&data, TODAY(17, 0), TODAY(18, 0));
  add_sleep(&data, TODAY(11, 0), TODAY(12, 0));
  add_sleep(&data, YESTERDAY(22, 0), TODAY(6, 0));
  g_mock_data = &data;

  UptimeResult r1 = uptime_calculate(TODAY(18, 30), mock_iterate_sleep);
  ASSERT_TRUE(r1.found_real_sleep);
  ASSERT_EQ(r1.total_nap_secs, 2 * 3600);
  ASSERT_EQ(get_uptime_mins(TODAY(18, 30), &r1), 10 * 60 + 30);

  UptimeResult r2 = uptime_calculate(TODAY(20, 0), mock_iterate_sleep);
  ASSERT_EQ(get_uptime_mins(TODAY(20, 0), &r2), 12 * 60);
}

// ============================================================
// TEST 7b: Early morning sleep is nap (not reset)
// ============================================================
/*
 * YESTERDAY 22:00 ════════ TODAY 06:00 ── 09:00 ═ 10:00 ── 10:30^ ── 15:00 ═ 16:00 ── 16:30^ ── 18:00^
 *              8hr sleep          3hr       1hr nap   30m       5hr      1hr nap   30m       2hr
 *
 * At 10:30^: uptime = 3:30 (4.5hr - 1hr nap)
 * At 16:30^: uptime = 8:30 (10.5hr - 2hr naps)
 * At 18:00^: uptime = 10:00 (12hr - 2hr naps)
 */
TEST(early_morning_nap) {
  MockSleepData data = {0};
  add_sleep(&data, TODAY(15, 0), TODAY(16, 0));
  add_sleep(&data, TODAY(9, 0), TODAY(10, 0));
  add_sleep(&data, YESTERDAY(22, 0), TODAY(6, 0));
  g_mock_data = &data;

  UptimeResult r1 = uptime_calculate(TODAY(10, 30), mock_iterate_sleep);
  ASSERT_TRUE(r1.found_real_sleep);
  ASSERT_EQ(r1.total_nap_secs, 1 * 3600);
  ASSERT_EQ(get_uptime_mins(TODAY(10, 30), &r1), 3 * 60 + 30);

  UptimeResult r2 = uptime_calculate(TODAY(16, 30), mock_iterate_sleep);
  ASSERT_EQ(r2.total_nap_secs, 2 * 3600);
  ASSERT_EQ(get_uptime_mins(TODAY(16, 30), &r2), 8 * 60 + 30);

  UptimeResult r3 = uptime_calculate(TODAY(18, 0), mock_iterate_sleep);
  ASSERT_EQ(get_uptime_mins(TODAY(18, 0), &r3), 10 * 60);
}

// ============================================================
// TEST 7c: Fragmented night sleep (all merges)
// ============================================================
/*
 * 23:00 ═ 00:00 ── 01:30 ═ 02:30 ── 04:00 ═ 05:00 ── 06:30 ═ 07:30 ── 08:00^
 *   1hr     1.5hr    1hr     1.5hr    1hr     1.5hr    1hr       30m
 *
 * All gaps < 2hr → merges into one 8.5hr block (23:00-07:30)
 * At 08:00^: uptime = 0:30
 */
TEST(fragmented_night_sleep) {
  MockSleepData data = {0};
  add_sleep(&data, TODAY(6, 30), TODAY(7, 30));
  add_sleep(&data, TODAY(4, 0), TODAY(5, 0));
  add_sleep(&data, TODAY(1, 30), TODAY(2, 30));
  add_sleep(&data, YESTERDAY(23, 0), TODAY(0, 0));
  g_mock_data = &data;

  UptimeResult r = uptime_calculate(TODAY(8, 0), mock_iterate_sleep);
  ASSERT_TRUE(r.found_real_sleep);
  ASSERT_EQ(r.last_real_sleep_end, TODAY(7, 30));
  ASSERT_EQ(r.total_nap_secs, 0);
  ASSERT_EQ(get_uptime_mins(TODAY(8, 0), &r), 30);
}

// ============================================================
// TEST 8: No sleep data
// ============================================================
/*
 * [No sleep sessions]
 * At 14:00^: found_real_sleep = false
 */
TEST(no_sleep_data) {
  MockSleepData data = {0};
  g_mock_data = &data;

  UptimeResult r = uptime_calculate(TODAY(14, 0), mock_iterate_sleep);
  ASSERT_FALSE(r.found_real_sleep);
}

// ============================================================
// TEST 9: Night hour detection
// ============================================================
TEST(night_hour_detection) {
  ASSERT_TRUE(uptime_is_night_hour(TODAY(21, 0)));   // 9pm
  ASSERT_TRUE(uptime_is_night_hour(TODAY(22, 0)));   // 10pm
  ASSERT_TRUE(uptime_is_night_hour(TODAY(0, 0)));    // midnight
  ASSERT_TRUE(uptime_is_night_hour(TODAY(6, 59)));   // 6:59am
  ASSERT_FALSE(uptime_is_night_hour(TODAY(7, 0)));   // 7am
  ASSERT_FALSE(uptime_is_night_hour(TODAY(12, 0)));  // noon
  ASSERT_FALSE(uptime_is_night_hour(TODAY(20, 59))); // 8:59pm
}

// ============================================================
// TEST 10: Nap excluded from awake calculation
// ============================================================
/*
 * YESTERDAY 22:00 ════════ TODAY 06:00 ──── 12:00 ═ 13:00 ──── 18:00 ═ 19:00 ── 21:00^
 *              8hr sleep          6hr       1hr nap    5hr      1hr nap    2hr
 *
 * Nap 1: awake before = 6hr ✓
 * Nap 2: awake before = 5hr (nap 1 excluded) ✓
 * At 21:00^: uptime = 13:00 (15hr - 2hr naps)
 */
TEST(nap_exclusion_from_awake) {
  MockSleepData data = {0};
  add_sleep(&data, TODAY(18, 0), TODAY(19, 0));
  add_sleep(&data, TODAY(12, 0), TODAY(13, 0));
  add_sleep(&data, YESTERDAY(22, 0), TODAY(6, 0));
  g_mock_data = &data;

  UptimeResult r = uptime_calculate(TODAY(21, 0), mock_iterate_sleep);
  ASSERT_TRUE(r.found_real_sleep);
  ASSERT_EQ(r.total_nap_secs, 2 * 3600);
  ASSERT_EQ(get_uptime_mins(TODAY(21, 0), &r), 13 * 60);
}

// ============================================================
// TEST 11: Broken night sleep with intermediate checks
// ============================================================
/*
 * YESTERDAY 08:00 wake ──────────── 22:00 ══ 00:00 ── 00:30^ ── 01:00 ══ 02:00 ── 02:30^ ── 03:00 ══ 08:00 ── 08:30^
 *                    14hr awake      2hr    30m       1hr     1hr    30m       1hr    5hr    30m
 *                                  sleep  check 1   awake   sleep  check 2  awake  sleep  check 3
 *
 * At 00:30^: uptime = 0:30 (only see 22:00-00:00 block so far)
 * At 02:30^: uptime = 0:30 (22:00-00:00 + 01:00-02:00 merge into one block)
 * At 08:30^: uptime = 0:30 (all blocks merge, ending at 08:00)
 */
TEST(broken_night_sleep_intermediate_checks) {
  // Check 1: Only first sleep block exists at 00:30
  {
    MockSleepData data = {0};
    add_sleep(&data, YESTERDAY(22, 0), TODAY(0, 0));  // 10pm-midnight
    g_mock_data = &data;

    UptimeResult r = uptime_calculate(TODAY(0, 30), mock_iterate_sleep);
    ASSERT_TRUE(r.found_real_sleep);
    ASSERT_EQ(r.last_real_sleep_end, TODAY(0, 0));
    ASSERT_EQ(get_uptime_mins(TODAY(0, 30), &r), 30);
  }

  // Check 2: Two sleep blocks at 02:30 (should merge)
  {
    MockSleepData data = {0};
    add_sleep(&data, TODAY(1, 0), TODAY(2, 0));       // 1am-2am
    add_sleep(&data, YESTERDAY(22, 0), TODAY(0, 0));  // 10pm-midnight
    g_mock_data = &data;

    UptimeResult r = uptime_calculate(TODAY(2, 30), mock_iterate_sleep);
    ASSERT_TRUE(r.found_real_sleep);
    ASSERT_EQ(r.last_real_sleep_end, TODAY(2, 0));  // Merged block ends at 2am
    ASSERT_EQ(get_uptime_mins(TODAY(2, 30), &r), 30);
  }

  // Check 3: All sleep blocks at 08:30 (all merge)
  {
    MockSleepData data = {0};
    add_sleep(&data, TODAY(3, 0), TODAY(8, 0));       // 3am-8am
    add_sleep(&data, TODAY(1, 0), TODAY(2, 0));       // 1am-2am
    add_sleep(&data, YESTERDAY(22, 0), TODAY(0, 0));  // 10pm-midnight
    g_mock_data = &data;

    UptimeResult r = uptime_calculate(TODAY(8, 30), mock_iterate_sleep);
    ASSERT_TRUE(r.found_real_sleep);
    ASSERT_EQ(r.last_real_sleep_end, TODAY(8, 0));  // Merged block ends at 8am
    ASSERT_EQ(r.total_nap_secs, 0);  // No naps, all merged
    ASSERT_EQ(get_uptime_mins(TODAY(8, 30), &r), 30);
  }
}

// ============================================================
// CACHING TESTS
// ============================================================
static uint8_t mock_storage[256];
static bool mock_storage_written = false;

static int mock_storage_read(uint32_t key, void *buffer, size_t size) {
  if (!mock_storage_written || key != UPTIME_STORAGE_KEY) return 0;
  memcpy(buffer, mock_storage, size);
  return (int)size;
}

static int mock_storage_write(uint32_t key, const void *data, size_t size) {
  if (key != UPTIME_STORAGE_KEY || size > sizeof(mock_storage)) return 0;
  memcpy(mock_storage, data, size);
  mock_storage_written = true;
  return (int)size;
}

TEST(cache_hit) {
  mock_storage_written = false;
  time_t now = TODAY(14, 0);
  uptime_init_at(mock_storage_read, mock_storage_write, now);
  uptime_invalidate_cache();

  MockSleepData data = {0};
  add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));
  g_mock_data = &data;

  UptimeResult r1 = uptime_get_cached(now, mock_iterate_sleep);
  ASSERT_TRUE(r1.found_real_sleep);

  g_mock_data = NULL;
  UptimeResult r2 = uptime_get_cached(now, mock_iterate_sleep);
  ASSERT_EQ(r2.last_real_sleep_end, r1.last_real_sleep_end);
}

TEST(cache_invalidation) {
  mock_storage_written = false;
  time_t now = TODAY(14, 0);
  uptime_init_at(mock_storage_read, mock_storage_write, now);

  MockSleepData data = {0};
  add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));
  g_mock_data = &data;

  uptime_get_cached(now, mock_iterate_sleep);
  ASSERT_TRUE(uptime_cache_valid(now));

  uptime_invalidate_cache();
  ASSERT_FALSE(uptime_cache_valid(now));
}

TEST(cache_expiry) {
  mock_storage_written = false;
  time_t now = TODAY(14, 0);
  uptime_init_at(mock_storage_read, mock_storage_write, now);

  MockSleepData data = {0};
  add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));
  g_mock_data = &data;

  uptime_get_cached(now, mock_iterate_sleep);
  ASSERT_TRUE(uptime_cache_valid(now + 4 * 60));
  ASSERT_FALSE(uptime_cache_valid(now + 6 * 60));
}

TEST(persistence) {
  mock_storage_written = false;
  time_t now = TODAY(14, 0);
  uptime_init_at(mock_storage_read, mock_storage_write, now);

  MockSleepData data = {0};
  add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));
  g_mock_data = &data;

  UptimeResult r1 = uptime_recalculate(now, mock_iterate_sleep);
  ASSERT_TRUE(mock_storage_written);
  time_t saved = r1.last_real_sleep_end;

  uptime_invalidate_cache();
  g_mock_data = NULL;
  uptime_init_at(mock_storage_read, mock_storage_write, now);

  ASSERT_TRUE(uptime_cache_valid(now));
  UptimeResult r2 = uptime_get_cached(now, mock_iterate_sleep);
  ASSERT_EQ(r2.last_real_sleep_end, saved);
}

TEST(record_wake_direct) {
  mock_storage_written = false;
  time_t now = TODAY(14, 0);
  uptime_init_at(mock_storage_read, mock_storage_write, now);
  uptime_invalidate_cache();

  uptime_record_wake_at(TODAY(6, 30), now);

  ASSERT_TRUE(uptime_cache_valid(now));
  UptimeResult r = uptime_get_cached(now, NULL);
  ASSERT_EQ(r.last_real_sleep_end, TODAY(6, 30));
}

TEST(no_persist_invalid) {
  mock_storage_written = false;
  time_t now = TODAY(14, 0);
  uptime_init_at(mock_storage_read, mock_storage_write, now);
  uptime_invalidate_cache();

  MockSleepData data = {0};
  g_mock_data = &data;

  uptime_recalculate(now, mock_iterate_sleep);
  ASSERT_FALSE(mock_storage_written);
}

// ============================================================
// WAKE EVENT TESTS
// ============================================================

/*
 * Wake from real sleep - should update last_real_sleep_end
 * YESTERDAY 23:00 ════════════ TODAY 07:00 ── 07:30^ (wake event fires)
 *              8hr sleep
 */
TEST(wake_event_from_real_sleep) {
  mock_storage_written = false;
  uptime_init_at(mock_storage_read, mock_storage_write, TODAY(7, 30));
  uptime_invalidate_cache();

  MockSleepData data = {0};
  add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));
  g_mock_data = &data;

  // Simulate wake event at 07:30
  uptime_on_wake_event(TODAY(7, 30), mock_iterate_sleep);

  // Verify cache was updated with real sleep end
  ASSERT_TRUE(uptime_cache_valid(TODAY(7, 30)));
  UptimeResult r = uptime_get_cached(TODAY(7, 30), mock_iterate_sleep);
  ASSERT_TRUE(r.found_real_sleep);
  ASSERT_EQ(r.last_real_sleep_end, TODAY(7, 0));
  ASSERT_EQ(r.total_nap_secs, 0);
}

/*
 * Wake from nap - should add to nap total, keep existing wake time
 * YESTERDAY 23:00 ════════ TODAY 07:00 ──── 14:00 ═ 15:00 ── 15:30^ (wake event fires)
 *              8hr sleep          7hr        1hr nap
 */
TEST(wake_event_from_nap) {
  mock_storage_written = false;
  uptime_init_at(mock_storage_read, mock_storage_write, TODAY(7, 30));
  uptime_invalidate_cache();

  // First, establish real sleep baseline
  {
    MockSleepData data = {0};
    add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));
    g_mock_data = &data;
    uptime_recalculate(TODAY(7, 30), mock_iterate_sleep);
  }

  // Now simulate waking from a nap at 15:30
  {
    MockSleepData data = {0};
    add_sleep(&data, TODAY(14, 0), TODAY(15, 0));  // 1hr nap
    g_mock_data = &data;
    uptime_on_wake_event(TODAY(15, 30), mock_iterate_sleep);
  }

  // Verify nap was added, not treated as real sleep
  ASSERT_TRUE(uptime_cache_valid(TODAY(15, 30)));
  UptimeResult r = uptime_get_cached(TODAY(15, 30), mock_iterate_sleep);
  ASSERT_TRUE(r.found_real_sleep);
  ASSERT_EQ(r.last_real_sleep_end, TODAY(7, 0));  // Still original wake time
  ASSERT_EQ(r.total_nap_secs, 1 * 3600);  // 1hr nap recorded
  ASSERT_EQ(get_uptime_mins(TODAY(15, 30), &r), 7 * 60 + 30);  // 8.5hr - 1hr nap
}

/*
 * Multiple naps via wake events
 * YESTERDAY 23:00 ═══ TODAY 07:00 ── 11:00 ═ 12:00 ── 17:00 ═ 18:00 ── 18:30^
 *              8hr          4hr      1hr nap    5hr     1hr nap
 */
TEST(wake_event_multiple_naps) {
  mock_storage_written = false;
  uptime_init_at(mock_storage_read, mock_storage_write, TODAY(7, 30));
  uptime_invalidate_cache();

  // Establish real sleep baseline
  {
    MockSleepData data = {0};
    add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));
    g_mock_data = &data;
    uptime_recalculate(TODAY(7, 30), mock_iterate_sleep);
  }

  // First nap wake event at 12:30
  {
    MockSleepData data = {0};
    add_sleep(&data, TODAY(11, 0), TODAY(12, 0));
    g_mock_data = &data;
    uptime_on_wake_event(TODAY(12, 30), mock_iterate_sleep);
  }

  // Second nap wake event at 18:30
  {
    MockSleepData data = {0};
    add_sleep(&data, TODAY(17, 0), TODAY(18, 0));
    g_mock_data = &data;
    uptime_on_wake_event(TODAY(18, 30), mock_iterate_sleep);
  }

  // Both naps should be accumulated
  UptimeResult r = uptime_get_cached(TODAY(18, 30), mock_iterate_sleep);
  ASSERT_EQ(r.last_real_sleep_end, TODAY(7, 0));
  ASSERT_EQ(r.total_nap_secs, 2 * 3600);  // 2hr total naps
  ASSERT_EQ(get_uptime_mins(TODAY(18, 30), &r), 9 * 60 + 30);  // 11.5hr - 2hr naps
}

/*
 * Night sleep via wake event - should be treated as real sleep, not nap
 * Yesterday had real sleep, now new night sleep
 * TWO_DAYS_AGO 23:00 ═══ YESTERDAY 07:00 ───────────── 23:00 ═══ TODAY 07:00 ── 07:30^
 *                 8hr             16hr                   8hr
 */
TEST(wake_event_night_sleep_resets) {
  mock_storage_written = false;
  uptime_init_at(mock_storage_read, mock_storage_write, YESTERDAY(7, 30));
  uptime_invalidate_cache();

  // Establish first real sleep baseline
  {
    MockSleepData data = {0};
    add_sleep(&data, TWO_DAYS_AGO(23, 0), YESTERDAY(7, 0));
    g_mock_data = &data;
    uptime_recalculate(YESTERDAY(7, 30), mock_iterate_sleep);
  }

  // Wake from new night sleep
  {
    MockSleepData data = {0};
    add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));
    g_mock_data = &data;
    uptime_on_wake_event(TODAY(7, 30), mock_iterate_sleep);
  }

  // Should reset to new wake time, not add as nap
  UptimeResult r = uptime_get_cached(TODAY(7, 30), mock_iterate_sleep);
  ASSERT_EQ(r.last_real_sleep_end, TODAY(7, 0));
  ASSERT_EQ(r.total_nap_secs, 0);  // Reset, not accumulated
  ASSERT_EQ(get_uptime_mins(TODAY(7, 30), &r), 30);
}

// ============================================================
// APP RESTART TESTS
// ============================================================

/*
 * App closed during sleep, reopened after wake - should catch missed wake
 *
 * Timeline:
 * YESTERDAY 14:00 - App running, wake_time = YESTERDAY 07:00 (from overnight sleep)
 * YESTERDAY 14:01 - App closed (user switches watchface)
 * YESTERDAY 23:00 - User falls asleep
 * TODAY 07:00     - User wakes up (no app running = no wake event!)
 * TODAY 07:30     - App reopens
 *
 * Expected: App should recalculate and find TODAY 07:00 as wake time
 */
TEST(app_restart_catches_missed_wake) {
  mock_storage_written = false;

  // Step 1: Simulate previous app session at YESTERDAY 14:00
  // App had calculated wake_time = YESTERDAY 07:00
  {
    uptime_init_at(mock_storage_read, mock_storage_write, YESTERDAY(14, 0));
    uptime_invalidate_cache();

    MockSleepData data = {0};
    add_sleep(&data, TWO_DAYS_AGO(23, 0), YESTERDAY(7, 0));  // Sleep from 2 days ago
    g_mock_data = &data;

    UptimeResult r = uptime_recalculate(YESTERDAY(14, 0), mock_iterate_sleep);
    ASSERT_EQ(r.last_real_sleep_end, YESTERDAY(7, 0));
  }

  // Verify it was persisted
  ASSERT_TRUE(mock_storage_written);

  // Step 2: App closed, user slept, woke up, app reopens at TODAY 07:30
  // Simulate fresh app start - restore from storage
  {
    uptime_init_at(mock_storage_read, mock_storage_write, TODAY(7, 30));

    // Sleep data now includes last night's sleep
    MockSleepData data = {0};
    add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));         // Last night
    add_sleep(&data, TWO_DAYS_AGO(23, 0), YESTERDAY(7, 0));  // Night before
    g_mock_data = &data;

    // App would call uptime_recalculate on start (not get_cached)
    UptimeResult r = uptime_recalculate(TODAY(7, 30), mock_iterate_sleep);

    // Should find the NEW wake time, not the stale cached one
    ASSERT_TRUE(r.found_real_sleep);
    ASSERT_EQ(r.last_real_sleep_end, TODAY(7, 0));  // NEW wake time!
    ASSERT_EQ(get_uptime_mins(TODAY(7, 30), &r), 30);
  }
}

/*
 * App closed, user took a nap, app reopens - should count nap correctly
 *
 * Timeline:
 * TODAY 07:30     - App running, wake_time = TODAY 07:00
 * TODAY 08:00     - App closed
 * TODAY 14:00     - User naps
 * TODAY 15:00     - User wakes from nap (no app = no event)
 * TODAY 16:00     - App reopens
 *
 * Expected: Should find wake_time = 07:00, nap_secs = 1hr, uptime = 8hr
 */
TEST(app_restart_with_nap_since_last_run) {
  mock_storage_written = false;

  // Step 1: App session at TODAY 07:30
  {
    uptime_init_at(mock_storage_read, mock_storage_write, TODAY(7, 30));
    uptime_invalidate_cache();

    MockSleepData data = {0};
    add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));
    g_mock_data = &data;

    UptimeResult r = uptime_recalculate(TODAY(7, 30), mock_iterate_sleep);
    ASSERT_EQ(r.last_real_sleep_end, TODAY(7, 0));
    ASSERT_EQ(r.total_nap_secs, 0);
  }

  // Step 2: App reopens at TODAY 16:00, user took a nap in between
  {
    uptime_init_at(mock_storage_read, mock_storage_write, TODAY(16, 0));

    // Sleep data now includes the nap
    MockSleepData data = {0};
    add_sleep(&data, TODAY(14, 0), TODAY(15, 0));            // Nap
    add_sleep(&data, YESTERDAY(23, 0), TODAY(7, 0));         // Last night
    g_mock_data = &data;

    // App recalculates on start
    UptimeResult r = uptime_recalculate(TODAY(16, 0), mock_iterate_sleep);

    // Should still have 07:00 as wake, but with 1hr nap
    ASSERT_TRUE(r.found_real_sleep);
    ASSERT_EQ(r.last_real_sleep_end, TODAY(7, 0));
    ASSERT_EQ(r.total_nap_secs, 1 * 3600);  // 1hr nap
    ASSERT_EQ(get_uptime_mins(TODAY(16, 0), &r), 8 * 60);  // 9hr - 1hr nap
  }
}

// ============================================================
// MAIN
// ============================================================
int main(void) {
  printf("==============================================\n");
  printf("       UPTIME CALCULATION UNIT TESTS\n");
  printf("==============================================\n");

  printf("\n--- CALCULATION TESTS ---\n");
  RUN_TEST(simple_overnight_sleep);
  RUN_TEST(afternoon_nap);
  RUN_TEST(bad_night_then_nap_then_sleep);
  RUN_TEST(sleep_with_kid_wakeup);
  RUN_TEST(night_shift_worker);
  RUN_TEST(evening_nap_is_night_sleep);
  RUN_TEST(multiple_naps);
  RUN_TEST(early_morning_nap);
  RUN_TEST(fragmented_night_sleep);
  RUN_TEST(no_sleep_data);
  RUN_TEST(night_hour_detection);
  RUN_TEST(nap_exclusion_from_awake);
  RUN_TEST(broken_night_sleep_intermediate_checks);

  printf("\n--- CACHING TESTS ---\n");
  RUN_TEST(cache_hit);
  RUN_TEST(cache_invalidation);
  RUN_TEST(cache_expiry);
  RUN_TEST(persistence);
  RUN_TEST(record_wake_direct);
  RUN_TEST(no_persist_invalid);

  printf("\n--- WAKE EVENT TESTS ---\n");
  RUN_TEST(wake_event_from_real_sleep);
  RUN_TEST(wake_event_from_nap);
  RUN_TEST(wake_event_multiple_naps);
  RUN_TEST(wake_event_night_sleep_resets);

  printf("\n--- APP RESTART TESTS ---\n");
  RUN_TEST(app_restart_catches_missed_wake);
  RUN_TEST(app_restart_with_nap_since_last_run);

  printf("\n==============================================\n");
  printf("  Results: %d/%d tests passed\n", tests_passed, tests_run);
  printf("==============================================\n");

  return (tests_passed == tests_run) ? 0 : 1;
}
