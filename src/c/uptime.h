#pragma once

// Support both Pebble SDK and standard C builds
#ifdef PBL_SDK_3
  // Pebble SDK - includes come from pebble.h
  #include <pebble.h>
#else
  // Standard C build (for testing)
  #include <stdint.h>
  #include <stdbool.h>
  #include <stddef.h>
  #include <time.h>
#endif

// ============================================================
// UPTIME CALCULATION MODULE
// ============================================================
// Calculates "time awake since last real sleep" with nap detection.
//
// Real sleep criteria:
//   - Duration > 2 hours (ALWAYS real sleep, regardless of context)
//   - OR: During night hours (9pm-7am)
//   - OR: Not meeting nap criteria
//
// Nap criteria (does NOT reset uptime):
//   - Duration <= 2 hours
//   - 4-12 hours awake before it (excluding other naps)
//   - NOT during night hours (9pm-7am)
//
// Sleep sessions with < 1 hour gaps are merged into single blocks
// (handles mid-sleep wakeups like tending to kids).
//
// Caching:
//   - Results cached in memory to avoid repeated API calls
//   - Can persist to storage for app restart recovery
//   - Only valid sleep data is stored (no defaults)
// ============================================================

// Thresholds (in seconds)
#define UPTIME_NAP_MAX_DURATION     (2 * 3600)   // Max 2 hours
#define UPTIME_NAP_MIN_AWAKE_BEFORE (2 * 3600)   // Min 2 hours awake before (< 2hr merges)
#define UPTIME_NAP_MAX_AWAKE_BEFORE (12 * 3600)  // Max 12 hours awake before
#define UPTIME_SLEEP_MERGE_GAP      (2 * 3600)   // Gaps < 2hr merge
#define UPTIME_LOOKBACK_HOURS       48           // Look back 48 hours

// Night hours: sleep during these is never a nap
#define UPTIME_NIGHT_START_HOUR     21  // 9pm
#define UPTIME_NIGHT_END_HOUR       7   // 7am

// Maximum sleep blocks we'll process
#define UPTIME_MAX_BLOCKS           16

// Cache validity period (recalculate if older than this)
#define UPTIME_CACHE_MAX_AGE        (5 * 60)  // 5 minutes

// ============================================================
// SLEEP BLOCK
// ============================================================
typedef struct {
  time_t start;
  time_t end;
} UptimeSleepBlock;

// ============================================================
// HEALTH SERVICE ABSTRACTION
// ============================================================
// Callback for iterating sleep sessions
// Returns: true to continue, false to stop
typedef bool (*UptimeSleepIteratorCB)(
  time_t start,
  time_t end,
  void *context
);

// Function to iterate over sleep sessions (abstraction over Pebble API)
// Should call callback for each sleep session in the time range
// direction: true = most recent first (backwards), false = oldest first
typedef void (*UptimeIterateSleepFn)(
  time_t range_start,
  time_t range_end,
  bool backwards,
  UptimeSleepIteratorCB callback,
  void *context
);

// ============================================================
// UPTIME RESULT
// ============================================================
typedef struct {
  time_t last_real_sleep_end;  // When user woke from real sleep
  int total_nap_secs;          // Nap time to subtract from uptime
  bool found_real_sleep;       // True if we found qualifying real sleep
  int blocks_processed;        // Debug: how many blocks we looked at
} UptimeResult;

// ============================================================
// STORAGE ABSTRACTION (for persistence)
// ============================================================

// Key for persistent storage
#define UPTIME_STORAGE_KEY 0x5550  // "UP" in hex

// Persisted data structure
typedef struct {
  uint32_t magic;              // Validation magic number
  time_t last_real_sleep_end;  // Cached wake time
  time_t calculated_at;        // When this was calculated
} UptimePersistedData;

#define UPTIME_MAGIC 0x55505449  // "UPTI"

// Storage function types (abstraction for testing)
typedef int (*UptimeStorageReadFn)(uint32_t key, void *buffer, size_t size);
typedef int (*UptimeStorageWriteFn)(uint32_t key, const void *data, size_t size);

// ============================================================
// CACHE STATE
// ============================================================
typedef struct {
  UptimeResult result;
  time_t calculated_at;
  bool valid;
} UptimeCache;

// ============================================================
// MAIN API
// ============================================================

// Calculate uptime given current time and a sleep iterator function
// This is the core calculation - always recalculates
UptimeResult uptime_calculate(
  time_t now,
  UptimeIterateSleepFn iterate_sleep
);

// Get effective wake time (accounts for naps)
// Returns: last_real_sleep_end + total_nap_secs
// So uptime = now - effective_wake_time
time_t uptime_get_effective_wake_time(const UptimeResult *result);

// ============================================================
// CACHED API (preferred for production use)
// ============================================================

// Initialize uptime system - call once at app start
// Attempts to restore from persistent storage
void uptime_init(
  UptimeStorageReadFn read_fn,
  UptimeStorageWriteFn write_fn
);

// Initialize with explicit time (for testing)
void uptime_init_at(
  UptimeStorageReadFn read_fn,
  UptimeStorageWriteFn write_fn,
  time_t now
);

// Get uptime with caching - avoids recalculation if cache valid
// Returns cached result if fresh, otherwise recalculates
UptimeResult uptime_get_cached(
  time_t now,
  UptimeIterateSleepFn iterate_sleep
);

// Force recalculation (e.g., after detecting wake from sleep)
// Also persists the new result
UptimeResult uptime_recalculate(
  time_t now,
  UptimeIterateSleepFn iterate_sleep
);

// Record a wake event (called when health API detects wake)
// Queries only the most recent sleep block and classifies it
// This is more efficient than full recalculation
void uptime_on_wake_event(
  time_t now,
  UptimeIterateSleepFn iterate_sleep
);

// Direct wake recording (skips classification - use when you KNOW it's real sleep)
// Only use this for initial bootstrap or when cache is empty
void uptime_record_wake(time_t wake_time);

// Version with explicit "now" for testing
void uptime_record_wake_at(time_t wake_time, time_t now);

// Invalidate cache (e.g., when settings change)
void uptime_invalidate_cache(void);

// Check if cache is valid
bool uptime_cache_valid(time_t now);

// ============================================================
// INTERNAL (exposed for testing)
// ============================================================

// Check if a time falls within night hours (9pm-7am)
bool uptime_is_night_hour(time_t t);

// Check if a sleep block qualifies as a nap
// awake_before: seconds of awake time before this block (excluding other naps)
bool uptime_is_nap(const UptimeSleepBlock *block, int awake_before);

// Merge adjacent sleep sessions into consolidated blocks
// Returns number of blocks created
int uptime_merge_sessions_to_blocks(
  const time_t *starts,
  const time_t *ends,
  int session_count,
  UptimeSleepBlock *blocks_out,
  int max_blocks
);
