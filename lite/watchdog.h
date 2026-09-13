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

// PlaybackGuard holds g_playback_mutex for one Apple playback call and
// publishes the acquisition to the watchdog, so a hang is attributed to the
// route that caused it instead of to every handler in flight.
class PlaybackGuard {
public:
    explicit PlaybackGuard(const char* route);
    ~PlaybackGuard();
    PlaybackGuard(const PlaybackGuard&) = delete;
    PlaybackGuard& operator=(const PlaybackGuard&) = delete;
private:
    std::unique_lock<std::mutex> lock_;
};

// The mutex both playback call sites serialize on.
extern std::mutex g_playback_mutex;
