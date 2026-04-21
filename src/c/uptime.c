#include "uptime.h"

#ifndef PBL_SDK_3
  #include <string.h>
#endif

// ============================================================
// SESSION COLLECTION
// ============================================================
#define MAX_RAW_SESSIONS 32

typedef struct {
  time_t starts[MAX_RAW_SESSIONS];
  time_t ends[MAX_RAW_SESSIONS];
  int count;
} SessionCollector;

static bool collect_session(time_t start, time_t end, void *context) {
  SessionCollector *c = (SessionCollector *)context;
  if (c->count < MAX_RAW_SESSIONS) {
    c->starts[c->count] = start;
    c->ends[c->count] = end;
    c->count++;
  }
  return c->count < MAX_RAW_SESSIONS;  // Stop if full
}

// ============================================================
// NIGHT HOUR CHECK
// ============================================================
bool uptime_is_night_hour(time_t t) {
  struct tm *tm = localtime(&t);
  int hour = tm->tm_hour;

  // Night is 9pm (21) to 7am
  // So night = hour >= 21 OR hour < 7
  return hour >= UPTIME_NIGHT_START_HOUR || hour < UPTIME_NIGHT_END_HOUR;
}

// ============================================================
// NAP DETECTION
// ============================================================
// Debug flag for testing
#ifdef UPTIME_DEBUG
#include <stdio.h>
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

bool uptime_is_nap(const UptimeSleepBlock *block, int awake_before) {
  // Use actual sleep time (not span) so merged blocks with gaps aren't mis-classified
  int duration = block->actual_secs > 0 ? block->actual_secs : (int)(block->end - block->start);

  DEBUG_PRINT("    is_nap check: duration=%d, awake_before=%d\n", duration, awake_before);

  // Too long to be a nap
  if (duration > UPTIME_NAP_MAX_DURATION) {
    DEBUG_PRINT("      -> NOT nap: duration %d > %d\n", duration, UPTIME_NAP_MAX_DURATION);
    return false;
  }

  // Not enough awake time before (just woke from real sleep)
  if (awake_before < UPTIME_NAP_MIN_AWAKE_BEFORE) {
    DEBUG_PRINT("      -> NOT nap: awake_before %d < %d\n", awake_before, UPTIME_NAP_MIN_AWAKE_BEFORE);
    return false;
  }

  // Too much awake time before (this is probably real sleep, not a nap)
  if (awake_before > UPTIME_NAP_MAX_AWAKE_BEFORE) {
    DEBUG_PRINT("      -> NOT nap: awake_before %d > %d\n", awake_before, UPTIME_NAP_MAX_AWAKE_BEFORE);
    return false;
  }

  // Sleep during night hours is never a nap
  // Check if any part of the sleep falls in night hours
  bool start_night = uptime_is_night_hour(block->start);
  bool end_night = uptime_is_night_hour(block->end);
  DEBUG_PRINT("      -> night check: start=%d, end=%d\n", start_night, end_night);

  if (start_night || end_night) {
    DEBUG_PRINT("      -> NOT nap: night hours\n");
    return false;
  }

  DEBUG_PRINT("      -> IS nap\n");
  return true;
}

// ============================================================
// MERGE SESSIONS INTO BLOCKS
// ============================================================
// Sessions are expected in reverse chronological order (most recent first)
// Merges sessions with gaps < UPTIME_SLEEP_MERGE_GAP
int uptime_merge_sessions_to_blocks(
  const time_t *starts,
  const time_t *ends,
  int session_count,
  UptimeSleepBlock *blocks_out,
  int max_blocks
) {
  if (session_count == 0 || max_blocks == 0) {
    return 0;
  }

  int block_count = 0;

  // Start with first session
  time_t block_start = starts[0];
  time_t block_end = ends[0];
  int block_actual_secs = (int)(ends[0] - starts[0]);

  for (int i = 1; i <= session_count; i++) {
    bool should_finalize = false;

    if (i < session_count) {
      // Gap between current block start (more recent) and this session's end (older)
      int gap = block_start - ends[i];

      if (gap <= UPTIME_SLEEP_MERGE_GAP) {
        // Merge: extend block backwards, accumulate only actual sleep (not the gap)
        block_start = starts[i];
        block_actual_secs += (int)(ends[i] - starts[i]);
      } else {
        should_finalize = true;
      }
    } else {
      // End of sessions - finalize last block
      should_finalize = true;
    }

    if (should_finalize && block_count < max_blocks) {
      blocks_out[block_count].start = block_start;
      blocks_out[block_count].end = block_end;
      blocks_out[block_count].actual_secs = block_actual_secs;
      block_count++;

      // Start new block if there are more sessions
      if (i < session_count) {
        block_start = starts[i];
        block_end = ends[i];
        block_actual_secs = (int)(ends[i] - starts[i]);
      }
    }
  }

  return block_count;
}

// ============================================================
// MAIN CALCULATION
// ============================================================
UptimeResult uptime_calculate(
  time_t now,
  UptimeIterateSleepFn iterate_sleep
) {
  UptimeResult result = {
    .last_real_sleep_end = 0,
    .last_real_sleep_secs = 0,
    .total_nap_secs = 0,
    .found_real_sleep = false,
    .blocks_processed = 0
  };

  if (!iterate_sleep) {
    return result;
  }

  // Collect raw sessions
  SessionCollector collector = { .count = 0 };
  time_t lookback_start = now - (UPTIME_LOOKBACK_HOURS * 3600);

  iterate_sleep(
    lookback_start,
    now,
    true,  // backwards (most recent first)
    collect_session,
    &collector
  );

  if (collector.count == 0) {
    return result;
  }

  // Merge into blocks
  UptimeSleepBlock blocks[UPTIME_MAX_BLOCKS];
  int block_count = uptime_merge_sessions_to_blocks(
    collector.starts,
    collector.ends,
    collector.count,
    blocks,
    UPTIME_MAX_BLOCKS
  );

  result.blocks_processed = block_count;

  if (block_count == 0) {
    return result;
  }

  // Process blocks from oldest to newest to calculate awake_before correctly
  // awake_before = time from previous sleep end to this sleep start
  //
  // We iterate backwards through the array since blocks are stored most-recent-first

  // First pass: identify naps vs real sleep (oldest to newest)
  // We need to know what's a nap before calculating effective uptime
  bool is_nap_block[UPTIME_MAX_BLOCKS] = {false};
  time_t prev_sleep_end = 0;

  for (int i = block_count - 1; i >= 0; i--) {
    UptimeSleepBlock *block = &blocks[i];
    int awake_before;

    if (prev_sleep_end > 0) {
      awake_before = block->start - prev_sleep_end;
    } else {
      // Oldest block - no previous sleep known, assume lots of awake time
      // (so it's likely real sleep unless it's tiny)
      awake_before = UPTIME_NAP_MAX_AWAKE_BEFORE + 1;  // Force "not a nap"
    }

    is_nap_block[i] = uptime_is_nap(block, awake_before);

    // Update prev_sleep_end only for real sleep (naps don't count)
    if (!is_nap_block[i]) {
      prev_sleep_end = block->end;
    }
  }

  // Second pass: find most recent real sleep and sum naps after it
  for (int i = 0; i < block_count; i++) {
    UptimeSleepBlock *block = &blocks[i];

    if (is_nap_block[i]) {
      result.total_nap_secs += (block->end - block->start);
    } else {
      // This is real sleep - we're done
      result.last_real_sleep_end = block->end;
      // Use actual_secs (not span) so fragmented-but-merged blocks report true sleep time
      result.last_real_sleep_secs = block->actual_secs > 0 ? block->actual_secs : (int)(block->end - block->start);
      result.found_real_sleep = true;
      break;
    }
  }

  return result;
}

// ============================================================
// EFFECTIVE WAKE TIME
// ============================================================
time_t uptime_get_effective_wake_time(const UptimeResult *result) {
  if (!result->found_real_sleep) {
    return 0;
  }
  // Adding nap_secs moves wake time forward, reducing displayed uptime
  return result->last_real_sleep_end + result->total_nap_secs;
}

// ============================================================
// CACHING AND PERSISTENCE
// ============================================================

// Static cache state
static UptimeCache s_cache = { .valid = false };

// Storage function pointers (set during init)
static UptimeStorageReadFn s_storage_read = NULL;
static UptimeStorageWriteFn s_storage_write = NULL;

// Persist current cache to storage
static void persist_cache(void) {
  if (!s_storage_write || !s_cache.valid || !s_cache.result.found_real_sleep) {
    return;  // Only persist valid real sleep data
  }

  UptimePersistedData data = {
    .magic = UPTIME_MAGIC,
    .last_real_sleep_end = s_cache.result.last_real_sleep_end,
    .calculated_at = s_cache.calculated_at
  };

  s_storage_write(UPTIME_STORAGE_KEY, &data, sizeof(data));
}

// Restore cache from storage (called with a reference time for validation)
static bool restore_cache_at(time_t reference_time) {
  if (!s_storage_read) {
    return false;
  }

  UptimePersistedData data;
  int bytes_read = s_storage_read(UPTIME_STORAGE_KEY, &data, sizeof(data));

  if (bytes_read != sizeof(data)) {
    return false;
  }

  if (data.magic != UPTIME_MAGIC) {
    return false;
  }

  // Validate the stored data is reasonable
  if (data.last_real_sleep_end <= 0 ||
      data.last_real_sleep_end > reference_time) {
    return false;
  }

  // Check if not too old (7 days max)
  if ((reference_time - data.last_real_sleep_end) > 7 * 24 * 3600) {
    return false;
  }

  // Restore to cache
  // Set calculated_at to reference_time - we trust the stored wake time is valid
  s_cache.result.last_real_sleep_end = data.last_real_sleep_end;
  s_cache.result.last_real_sleep_secs = 0;  // Needs recalculation
  s_cache.result.total_nap_secs = 0;  // Naps need recalculation
  s_cache.result.found_real_sleep = true;
  s_cache.result.blocks_processed = 0;
  s_cache.calculated_at = reference_time;  // Treat as just verified
  s_cache.valid = true;

  return true;
}

// Wrapper that uses actual time (for production)
static bool restore_cache(void) {
  return restore_cache_at(time(NULL));
}

void uptime_init_at(
  UptimeStorageReadFn read_fn,
  UptimeStorageWriteFn write_fn,
  time_t now
) {
  s_storage_read = read_fn;
  s_storage_write = write_fn;
  s_cache.valid = false;

  // Try to restore from persistent storage
  restore_cache_at(now);
}

void uptime_init(
  UptimeStorageReadFn read_fn,
  UptimeStorageWriteFn write_fn
) {
  uptime_init_at(read_fn, write_fn, time(NULL));
}

bool uptime_cache_valid(time_t now) {
  if (!s_cache.valid) {
    return false;
  }

  // Check if cache is too old
  if ((now - s_cache.calculated_at) > UPTIME_CACHE_MAX_AGE) {
    return false;
  }

  // Check if stored wake time is still reasonable
  if (s_cache.result.last_real_sleep_end > now) {
    s_cache.valid = false;
    return false;
  }

  return true;
}

UptimeResult uptime_get_cached(
  time_t now,
  UptimeIterateSleepFn iterate_sleep
) {
  if (uptime_cache_valid(now)) {
    return s_cache.result;
  }

  // Cache invalid - recalculate
  return uptime_recalculate(now, iterate_sleep);
}

UptimeResult uptime_recalculate(
  time_t now,
  UptimeIterateSleepFn iterate_sleep
) {
  UptimeResult result = uptime_calculate(now, iterate_sleep);

  // Update cache
  s_cache.result = result;
  s_cache.calculated_at = now;
  s_cache.valid = result.found_real_sleep;

  // Persist if we found real sleep
  if (result.found_real_sleep) {
    persist_cache();
  }

  return result;
}

void uptime_record_wake(time_t wake_time) {
  // Direct update without API call - used when health event fires
  // wake_time is when the user woke, but we record "now" as calculation time
  // Note: sleep duration unknown in this path, will be 0 until recalculated
  time_t now = time(NULL);
  s_cache.result.last_real_sleep_end = wake_time;
  s_cache.result.last_real_sleep_secs = 0;  // Unknown - needs recalculation
  s_cache.result.total_nap_secs = 0;
  s_cache.result.found_real_sleep = true;
  s_cache.result.blocks_processed = 0;
  s_cache.calculated_at = now;
  s_cache.valid = true;

  persist_cache();
}

// Version with explicit time for testing
void uptime_record_wake_at(time_t wake_time, time_t now) {
  s_cache.result.last_real_sleep_end = wake_time;
  s_cache.result.last_real_sleep_secs = 0;  // Unknown - needs recalculation
  s_cache.result.total_nap_secs = 0;
  s_cache.result.found_real_sleep = true;
  s_cache.result.blocks_processed = 0;
  s_cache.calculated_at = now;
  s_cache.valid = true;

  persist_cache();
}

void uptime_invalidate_cache(void) {
  s_cache.valid = false;
}

// ============================================================
// WAKE EVENT HANDLING
// ============================================================
// Context for capturing the most recent sleep block
typedef struct {
  time_t start;
  time_t end;
  bool found;
} RecentSleepCapture;

static bool capture_recent_sleep(time_t start, time_t end, void *context) {
  RecentSleepCapture *capture = (RecentSleepCapture *)context;
  capture->start = start;
  capture->end = end;
  capture->found = true;
  return false;  // Stop after first (most recent) session
}

void uptime_on_wake_event(
  time_t now,
  UptimeIterateSleepFn iterate_sleep
) {
  if (!iterate_sleep) {
    return;
  }

  // Query only recent sleep (last 4 hours should be plenty)
  time_t lookback_start = now - (4 * 3600);
  RecentSleepCapture capture = { .found = false };

  iterate_sleep(
    lookback_start,
    now,
    true,  // backwards (most recent first)
    capture_recent_sleep,
    &capture
  );

  if (!capture.found) {
    // No sleep found - nothing to do
    return;
  }

  // Create a block for classification (single session, so actual_secs == span)
  UptimeSleepBlock block = {
    .start = capture.start,
    .end = capture.end,
    .actual_secs = (int)(capture.end - capture.start)
  };

  // Calculate awake_before: time from last real sleep end to this sleep start
  int awake_before;
  if (s_cache.valid && s_cache.result.found_real_sleep) {
    // We have a known real sleep end - calculate awake time excluding naps
    time_t effective_wake = uptime_get_effective_wake_time(&s_cache.result);
    awake_before = block.start - effective_wake;
  } else {
    // No prior cache - treat as too much awake time (so it's real sleep)
    awake_before = UPTIME_NAP_MAX_AWAKE_BEFORE + 1;
  }

  // Classify the sleep
  bool is_nap = uptime_is_nap(&block, awake_before);

  if (is_nap) {
    // It's a nap - add to nap total, keep existing wake time
    int nap_duration = block.end - block.start;
    s_cache.result.total_nap_secs += nap_duration;
    s_cache.calculated_at = now;
    // Don't update last_real_sleep_end or last_real_sleep_secs - they stay the same
  } else {
    // It's real sleep - update wake time, reset naps
    s_cache.result.last_real_sleep_end = block.end;
    s_cache.result.last_real_sleep_secs = (int)(block.end - block.start);
    s_cache.result.total_nap_secs = 0;
    s_cache.result.found_real_sleep = true;
    s_cache.calculated_at = now;
    s_cache.valid = true;
  }

  persist_cache();
}
