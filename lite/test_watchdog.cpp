// Host test for the wrapper watchdog decision — see test-watchdog.sh, which
// splices the real watchdog_decide() out of lite/lite_main.cpp in place of the
// marker below. Kept dependency-free (no httplib, no cJSON) so it runs on the
// build host in a second.
#include <cstdio>
#include <string>

// EXTRACTED_SOURCE_HERE

static int failures = 0;

static const char* name_of(WatchdogAction a) {
    switch (a) {
    case WD_IDLE: return "WD_IDLE";
    case WD_OK: return "WD_OK";
    case WD_EXIT_HOLDER: return "WD_EXIT_HOLDER";
    case WD_EXIT_WEDGED: return "WD_EXIT_WEDGED";
    }
    return "?";
}

static void check(const char* what, WatchdogAction got, WatchdogAction want) {
    if (got == want) {
        printf("  ok   %s -> %s\n", what, name_of(got));
        return;
    }
    printf("  FAIL %s -> %s, want %s\n", what, name_of(got), name_of(want));
    failures++;
}

int main() {
    const long long STALL = 30000;
    const long long WEDGED = 300000;
    const long long NOW = 1000000000LL;

    printf("watchdog_decide: holder stall %lld ms, wedged %lld ms\n", STALL, WEDGED);

    // Idle: nothing in flight, no holder.
    check("idle", watchdog_decide(NOW, 0, 0, 0, STALL, WEDGED), WD_IDLE);

    // THE REGRESSION: one handler holds the lock for 1s, three more have been
    // queueing for 31s behind it. The old rule saw the 31s-old handler and
    // force-exited, killing the other rips. The holder is healthy -> keep going.
    check("3 waiters 31s behind a 1s-old holder",
          watchdog_decide(NOW, NOW - 1000, 4, NOW - 31000, STALL, WEDGED), WD_OK);
    check("7 waiters 5min behind a 2s-old holder (full thread pool)",
          watchdog_decide(NOW, NOW - 2000, 8, NOW - 300000, STALL, WEDGED), WD_OK);

    // The holder really is stuck: just over the threshold exits, with the
    // timestamp that identifies it.
    check("holder stuck 29.9s", watchdog_decide(NOW, NOW - 29900, 3, NOW - 29900, STALL, WEDGED), WD_OK);
    check("holder stuck 30.1s", watchdog_decide(NOW, NOW - 30100, 3, NOW - 30100, STALL, WEDGED), WD_EXIT_HOLDER);
    check("holder stuck 96s", watchdog_decide(NOW, NOW - 96000, 1, NOW - 96000, STALL, WEDGED), WD_EXIT_HOLDER);

    // No holder at all: ordinary waiters still never exit, however long the
    // queue is — the lock is free, so nobody is blocking anyone.
    check("no holder, 2 handlers 40s in flight",
          watchdog_decide(NOW, 0, 2, NOW - 40000, STALL, WEDGED), WD_OK);
    check("no holder, 1 handler 4min in flight",
          watchdog_decide(NOW, 0, 1, NOW - 240000, STALL, WEDGED), WD_OK);

    // ...but a handler wedged outside the playback path (lock free, way past
    // any real request) still restarts the process.
    check("no holder, handler wedged 5m1s",
          watchdog_decide(NOW, 0, 1, NOW - 301000, STALL, WEDGED), WD_EXIT_WEDGED);

    // A holder timestamp of 0 must never be read as "held since epoch".
    check("zero holder stamp with a stale handler stamp",
          watchdog_decide(NOW, 0, 1, NOW - 301000, STALL, WEDGED), WD_EXIT_WEDGED);

    // Thresholds are arguments, not constants: an operator override moves the
    // boundary (LITE_STALL_MS / LITE_WEDGED_MS).
    check("holder 45s with stall override 60s",
          watchdog_decide(NOW, NOW - 45000, 2, NOW - 45000, 60000, WEDGED), WD_OK);
    check("holder 45s with stall override 10s",
          watchdog_decide(NOW, NOW - 45000, 2, NOW - 45000, 10000, WEDGED), WD_EXIT_HOLDER);

    if (failures) {
        printf("FAILED: %d case(s)\n", failures);
        return 1;
    }
    printf("all watchdog_decide cases passed\n");
    return 0;
}
