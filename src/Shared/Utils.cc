#include <Shared/Utils.hh>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <print>
#include <queue>
#include <thread>

using namespace pr;

#ifdef PRESCRIPTIVISM_ENABLE_SANITISERS
extern "C" const char* __asan_default_options() {
    // Disable memory leak detection since some of the libraries we
    // use like to allocate global state, which is never freed.
    return "detect_leaks=0";
}
#endif

// Run the logger on a separate thread since printing to the console
// might be slow depending on what console we’re printing to (it can
// take ~100ms in my IDE).
std::mutex LoggerMutex;
std::condition_variable LoggerCV;
std::queue<std::pair<chr::system_clock::time_point, std::string>> LoggerQueue;
std::once_flag LoggerInitialised;
std::atomic_bool Enabled = true;
std::jthread LoggerThread([](std::stop_token tok) {
    std::atexit([] {
        LoggerThread.request_stop();
        LoggerQueue.emplace();
        LoggerCV.notify_one();
    });

    for (;;) {
        std::unique_lock lock(LoggerMutex);
        LoggerCV.wait(lock, [&] { return not LoggerQueue.empty(); });
        if (tok.stop_requested()) return;
        auto [time, msg] = std::move(LoggerQueue.front());
        LoggerQueue.pop();
        lock.unlock();
        if (not Enabled) continue;

        std::tm now_tm;
        std::time_t now_c = chr::system_clock::to_time_t(time);
        ::localtime_r(&now_c, &now_tm);
        std::print(
            stderr,
            "\033[33m[{:02}:{:02}:{:02}]\033[m {}",
            now_tm.tm_hour,
            now_tm.tm_min,
            now_tm.tm_sec,
            msg
        );
    }
});

void pr::LogImpl(std::string msg) {
    if (not msg.ends_with('\n')) msg += '\n';
    chr::system_clock::time_point now = chr::system_clock::now();
    std::unique_lock _(LoggerMutex);
    LoggerQueue.emplace(now, std::move(msg));
    LoggerCV.notify_one();
}

SilenceLog::SilenceLog() {
    Enabled = false;
}

SilenceLog::~SilenceLog() {
    Enabled = true;
}
