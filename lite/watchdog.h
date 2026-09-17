#pragma once
// Watchdog hand-off between the playback call sites (playback.cpp), the guard
// implementation (bridge.cpp) and the watchdog thread (lite_main.cpp).
//
// Kept in its own header, NOT in internal.h: internal.h declares the bridge's
// globals (including a legacy `extern char g_base_dir[256]`) and lite_main.cpp
// has its own unrelated `std::string g_base_dir`, so including internal.h there
// makes lite_main.cpp fail to compile. This header has no such baggage.
#include <atomic>
#include <chrono>
#include <mutex>

// g_playback_holder_since_ms is a steady-clock millisecond stamp written when a
// handler ACQUIRES g_playback_mutex and cleared when it releases; 0 = free.
// g_playback_holder_route names what the holder is doing ("m3u8"/"key") and is
// diagnostics only.
//
// WHY this exists: the watchdog used to judge the OLDEST IN-FLIGHT handler,
// which includes handlers merely queued behind the mutex. One slow Apple call
// therefore made every waiter look stuck and force-exited the process, killing
// the other rips' in-flight downloads with it. Only the holder can be stuck
// inside an uninterruptible Apple ABI call; the waiters are victims.
extern std::atomic<long long> g_playback_holder_since_ms;
extern std::atomic<const char*> g_playback_holder_route;

// A handler that cannot take the playback lock within this budget gives up and
// answers 503 + Retry-After instead of queueing. See PlaybackGuard.
long long playback_wait_max_ms();

// PlaybackGuard holds g_playback_mutex for one Apple playback call and
// publishes the acquisition to the watchdog, so a hang is attributed to the
// route that caused it instead of to every handler in flight.
//
// BOUNDED: the wait for the lock is capped (LITE_PLAYBACK_WAIT_MS, default 75s
// — above every legitimate hold measured, below the watchdog's 90s). A waiter
// that exceeds it does NOT acquire, and the caller answers 503 + Retry-After.
// Unbounded queueing was half of the 2026-09-16 outage: one stale-CKC /key held
// the lock for 90s while 15 handlers parked behind it, and the watchdog then
// force-exited the process, killing every in-flight rip.
class PlaybackGuard {
public:
    explicit PlaybackGuard(const char* route);
    ~PlaybackGuard();
    PlaybackGuard(const PlaybackGuard&) = delete;
    PlaybackGuard& operator=(const PlaybackGuard&) = delete;
    // False = the budget expired (or a CKC refresh owns the lock, or the
    // process is draining): the caller must NOT make an Apple call and should
    // answer a retryable 503 rather than a hard failure.
    bool acquired() const { return acquired_; }
private:
    // Deferred: the mutex is associated at construction but NOT locked, so the
    // bounded wait below can time out. A default-constructed unique_lock has no
    // associated mutex at all, and try_lock_for on one throws.
    std::unique_lock<std::timed_mutex> lock_;
    bool acquired_ = false;
};

// The mutex both playback call sites serialize on. Timed, so a waiter can give
// up instead of parking forever.
extern std::timed_mutex g_playback_mutex;

// ---- CKC refresh state -----------------------------------------------------
// A stale Fairplay CKC (-42786) makes every /key throw until
// refresh_decrypt_ctx() rebuilds the lease and the FootHill contexts. That
// refresh is a heavy Apple re-init (measured ~60s during the 2026-09-16
// episode) which runs UNDER the playback lock, because it resets contexts a
// concurrent derive would be using. Two things follow from that, and both are
// what these atomics exist for:
//
//   · the watchdog must not read "90s inside a refresh" as a hung holder — it
//     used to force-exit mid-recovery, and that is what killed the rips;
//   · new playback work must fail fast (503) rather than queue a minute behind
//     the refresh.
extern std::atomic<bool> g_ckc_refreshing;
extern std::atomic<long long> g_ckc_refresh_started_ms; // 0 = not refreshing
extern std::atomic<long long> g_ckc_refresh_count;      // completed refreshes
extern std::atomic<long long> g_ckc_refresh_last_ms;    // duration of the last
extern std::atomic<long long> g_ckc_failures;           // CKC exceptions seen
extern std::atomic<long long> g_ckc_last_ok_ms;         // last successful derive

// Set once on SIGTERM/SIGINT so waiters stop queueing immediately: httplib's
// stop() waits for in-flight handlers, and handlers parked on the playback lock
// for up to a minute are what made the supervisor escalate to SIGKILL.
extern std::atomic<bool> g_shutting_down;
