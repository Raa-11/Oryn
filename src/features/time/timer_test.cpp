#include <ostream>
#include <chrono>
#include <condition_variable>
#include <doctest/doctest.h>
#include <mutex>
#include <utility>
#include <vector>
import oryn.time;

using namespace std::chrono_literals;
using namespace oryn::time;
const TimerState::TimePoint epoch{};

TEST_CASE("Countdown uses deadlines, rounds up and never goes negative") {
    TimerState timer;
    CHECK_FALSE(timer.start_countdown(0s, epoch));
    CHECK_FALSE(timer.start_countdown(-1s, epoch));
    CHECK_FALSE(timer.start_countdown(24h, epoch));
    REQUIRE(timer.start_countdown(10s, epoch));
    CHECK(timer.snapshot(epoch + 1ms).remaining_seconds == 10);
    CHECK(timer.snapshot(epoch + 3500ms).remaining_seconds == 7);
    CHECK(timer.snapshot(epoch + 9999ms).status == Status::running);
    auto done = timer.snapshot(epoch + 10s);
    CHECK(done.status == Status::finished);
    CHECK(done.remaining_seconds == 0);
    CHECK(done.completions == 1);
    CHECK(timer.snapshot(epoch + 10h).completions == 1);
}

TEST_CASE("Pause/resume preserves subsecond time, reset and cancel do not expire") {
    TimerState timer;
    timer.start_countdown(10s, epoch);
    REQUIRE(timer.pause(epoch + 2500ms));
    CHECK(timer.snapshot(epoch + 1h).remaining_seconds == 8);
    REQUIRE(timer.start(epoch + 1h));
    CHECK(timer.snapshot(epoch + 1h + 7499ms).status == Status::running);
    CHECK(timer.snapshot(epoch + 1h + 7500ms).status == Status::finished);
    timer.reset(epoch + 2h);
    CHECK(timer.snapshot(epoch + 3h).remaining_seconds == 10);
    CHECK(timer.snapshot(epoch + 3h).status == Status::idle);
    CHECK_FALSE(timer.snapshot(epoch + 3h).completed);
    timer.start(epoch + 3h);
    timer.cancel(epoch + 3h + 1s);
    CHECK(timer.snapshot(epoch + 4h).status == Status::idle);
    CHECK(timer.snapshot(epoch + 4h).completions == 1);
}

TEST_CASE("Pause after expiry cannot resurrect a completed session") {
    TimerState timer;
    timer.start_countdown(1s, epoch);
    CHECK_FALSE(timer.pause(epoch + 5s));
    CHECK(timer.snapshot(epoch + 5s).status == Status::finished);
}

TEST_CASE("Sleep or a delayed wake reconciles directly from the monotonic deadline") {
    TimerState timer;
    timer.start_countdown(5min, epoch);
    CHECK(timer.snapshot(epoch + 8h).status == Status::finished);
    CHECK(timer.snapshot(epoch + 8h).completions == 1);
}

TEST_CASE("Pomodoro alternates phases and uses a long break every fourth focus") {
    TimerState timer;
    REQUIRE(timer.configure({2s, 1s, 3s}, epoch));
    timer.select_mode(true, epoch);
    auto now = epoch;
    for (int session = 1; session <= 4; ++session) {
        CHECK(timer.snapshot(now).focus_session == session);
        timer.start(now);
        now += 2s;
        auto next = timer.snapshot(now);
        CHECK(next.status == Status::idle);
        CHECK(next.phase == (session == 4 ? Phase::long_break : Phase::short_break));
        CHECK(next.remaining_seconds == (session == 4 ? 3 : 1));
        CHECK(next.focus_session == session);
        CHECK(next.completed);
        CHECK(next.completions == static_cast<unsigned>(session * 2 - 1));
        timer.start(now);
        CHECK_FALSE(timer.snapshot(now).completed);
        now += session == 4 ? 3s : 1s;
        CHECK(timer.snapshot(now).phase == Phase::focus);
    }
    CHECK(timer.snapshot(now).focus_session == 1);
    CHECK(timer.snapshot(now).completions == 8);
    timer.cancel(now);
    CHECK(timer.snapshot(now).focus_session == 1);
}

TEST_CASE("Pomodoro resume does not auto-run missed phases") {
    TimerState timer;
    timer.select_mode(true, epoch);
    timer.start(epoch);
    auto after_sleep = timer.snapshot(epoch + 24h);
    CHECK(after_sleep.phase == Phase::short_break);
    CHECK(after_sleep.status == Status::idle);
    CHECK(after_sleep.remaining_seconds == 5 * 60);
    CHECK(after_sleep.completions == 1);
}

TEST_CASE("Switching timer tabs preserves running Pomodoro and its deadline") {
    TimerState timer;
    REQUIRE(timer.select_mode(true, epoch));
    REQUIRE(timer.start(epoch));
    REQUIRE(timer.select_mode(false, epoch + 6s));
    CHECK(timer.snapshot(epoch + 30s).status == Status::idle);
    REQUIRE(timer.next_deadline().has_value());
    CHECK(*timer.next_deadline() == epoch + 25min);
    REQUIRE(timer.select_mode(true, epoch + 60s));
    const auto restored = timer.snapshot(epoch + 60s);
    CHECK(restored.status == Status::running);
    CHECK(restored.remaining_seconds == 24 * 60);
    CHECK(restored.phase == Phase::focus);
    CHECK(restored.focus_session == 1);
    CHECK_FALSE(restored.completed);
}

TEST_CASE("Both modes run independently and hidden expiry advances only one Pomodoro phase") {
    TimerState timer;
    REQUIRE(timer.configure({2s, 1s, 3s}, epoch));
    REQUIRE(timer.select_mode(true, epoch));
    REQUIRE(timer.start(epoch));
    REQUIRE(timer.start_countdown(10s, epoch + 1s));
    REQUIRE(timer.next_deadline().has_value());
    CHECK(*timer.next_deadline() == epoch + 2s);
    const auto standard = timer.snapshot(epoch + 5s);
    CHECK(standard.status == Status::running);
    CHECK(standard.remaining_seconds == 6);
    CHECK(standard.completions == 1);
    CHECK(*timer.next_deadline() == epoch + 11s);
    REQUIRE(timer.select_mode(true, epoch + 5s));
    const auto pomodoro = timer.snapshot(epoch + 5s);
    CHECK(pomodoro.status == Status::idle);
    CHECK(pomodoro.phase == Phase::short_break);
    CHECK(pomodoro.remaining_seconds == 1);
    CHECK(pomodoro.completed);
    REQUIRE(timer.select_mode(false, epoch + 12s));
    CHECK(timer.snapshot(epoch + 12s).status == Status::finished);
    CHECK(timer.snapshot(epoch + 12s).completions == 2);
    CHECK_FALSE(timer.next_deadline().has_value());
}

TEST_CASE("Paused time and focus-session progress survive navigation and other-mode cancellation") {
    TimerState timer;
    REQUIRE(timer.configure({4s, 1s, 3s}, epoch));
    timer.select_mode(true, epoch);
    REQUIRE(timer.start(epoch));
    CHECK(timer.snapshot(epoch + 4s).phase == Phase::short_break);
    REQUIRE(timer.start(epoch + 4s));
    CHECK(timer.snapshot(epoch + 5s).focus_session == 2);
    REQUIRE(timer.start(epoch + 5s));
    REQUIRE(timer.pause(epoch + 6500ms));
    timer.select_mode(false, epoch + 7s);
    REQUIRE(timer.start_countdown(20s, epoch + 7s));
    timer.reset(epoch + 8s);
    timer.cancel(epoch + 9s);
    timer.select_mode(true, epoch + 1h);
    const auto restored = timer.snapshot(epoch + 1h);
    CHECK(restored.status == Status::paused);
    CHECK(restored.remaining_seconds == 3);
    CHECK(restored.focus_session == 2);
    CHECK(restored.phase == Phase::focus);
    REQUIRE(timer.start(epoch + 1h));
    CHECK(timer.snapshot(epoch + 1h + 2499ms).status == Status::running);
    CHECK(timer.snapshot(epoch + 1h + 2500ms).phase == Phase::short_break);
}

TEST_CASE("Pomodoro reset and cancel leave the running Standard timer untouched") {
    TimerState timer;
    REQUIRE(timer.start_countdown(30s, epoch));
    timer.select_mode(true, epoch + 1s);
    REQUIRE(timer.start(epoch + 1s));
    timer.reset(epoch + 2s);
    timer.cancel(epoch + 3s);
    timer.select_mode(false, epoch + 4s);
    CHECK(timer.snapshot(epoch + 4s).status == Status::running);
    CHECK(timer.snapshot(epoch + 4s).remaining_seconds == 26);
}

TEST_CASE("Configuration rejects invalid durations and active Pomodoro even on the other tab") {
    TimerState timer;
    CHECK_FALSE(timer.configure({0s, 1s, 1s}, epoch));
    REQUIRE(timer.start_countdown(2s, epoch));
    CHECK_FALSE(timer.configure({1s, 1s, 1s}, epoch));
    timer.select_mode(true, epoch);
    REQUIRE(timer.start(epoch));
    timer.select_mode(false, epoch + 3s);
    CHECK_FALSE(timer.configure({1s, 1s, 1s}, epoch + 3s));
}

TEST_CASE("Remaining time formatting supports sub-minute, hour and zero values") {
    CHECK(format_remaining(-1) == "00:00");
    CHECK(format_remaining(1) == "00:01");
    CHECK(format_remaining(90) == "01:30");
    CHECK(format_remaining(3600) == "01:00:00");
    CHECK(format_remaining(86399) == "23:59:59");
}

struct Inbox {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<Snapshot> samples;

    void push(Snapshot state) {
        std::lock_guard lock(mutex);
        if (samples.size() == 128) {
            samples.erase(samples.begin());
        }
        samples.push_back(std::move(state));
        changed.notify_all();
    }

    template <class Predicate> bool wait(Predicate predicate) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 5s, [&] {
            for (const auto &state : samples) {
                if (predicate(state)) {
                    return true;
                }
            }
            return false;
        });
    }
};

TEST_CASE("Asio delivers expiry and validation errors without UI polling") {
    Inbox inbox;
    TimerService service([&](Snapshot state) { inbox.push(std::move(state)); });
    service.countdown(0, 0);
    REQUIRE(inbox.wait([](auto &state) { return !state.error.empty(); }));
    service.countdown(0, 1);
    REQUIRE(inbox.wait([](auto &state) { return state.status == Status::running; }));
    REQUIRE(inbox.wait(
        [](auto &state) { return state.status == Status::finished && state.completions == 1; }));
}

TEST_CASE("Asio keeps scheduling a timer while the other mode is selected and idle") {
    Inbox inbox;
    TimerService service([&](Snapshot state) { inbox.push(std::move(state)); });
    service.countdown(0, 1);
    service.select_mode(true);
    REQUIRE(inbox.wait([](const auto &state) {
        return state.pomodoro && state.status == Status::idle && state.completions == 1;
    }));
    service.select_mode(false);
    REQUIRE(inbox.wait([](const auto &state) {
        return !state.pomodoro && state.status == Status::finished &&
               state.remaining_seconds == 0 && state.completed;
    }));
}

TEST_CASE("Destruction drains accepted commands in FIFO order") {
    Inbox inbox;
    {
        TimerService service([&](Snapshot state) { inbox.push(std::move(state)); });
        service.configure(1, 2, 3);
        service.select_mode(true);
        service.configure(2, 3, 4);
        service.configure(0, 3, 4);
        service.select_mode(false);
    }
    // These commands never start a timer, so the initial snapshot and one per
    // command are the only possible notifications. Joining makes inspection safe.
    REQUIRE(inbox.samples.size() == 6);
    CHECK_FALSE(inbox.samples[0].pomodoro);
    CHECK(inbox.samples[0].config.focus == 25min);
    CHECK_FALSE(inbox.samples[1].pomodoro);
    CHECK(inbox.samples[1].config.focus == 1min);
    CHECK(inbox.samples[2].pomodoro);
    CHECK(inbox.samples[2].remaining_seconds == 60);
    CHECK(inbox.samples[3].pomodoro);
    CHECK(inbox.samples[3].config.focus == 2min);
    CHECK(inbox.samples[3].remaining_seconds == 120);
    CHECK(inbox.samples[3].error.empty());
    CHECK(inbox.samples[4].config.focus == 2min);
    CHECK_FALSE(inbox.samples[4].error.empty());
    CHECK_FALSE(inbox.samples[5].pomodoro);
    CHECK(inbox.samples[5].config.focus == 2min);
    CHECK(inbox.samples[5].error.empty());
}

TEST_CASE("100 queued starts resets cancels and service shutdown drain safely") {
    Inbox inbox;
    {
        TimerService service([&](Snapshot state) { inbox.push(std::move(state)); });
        for (int i = 0; i < 100; ++i) {
            service.countdown(0, 30);
            service.toggle();
            service.reset();
            service.cancel();
        }
        service.select_mode(true);
        REQUIRE(
            inbox.wait([](auto &state) { return state.pomodoro && state.status == Status::idle; }));
        service.start(); // Destruction must cancel the pending async wait, not wait 25 minutes.
    }
    for (int i = 0; i < 8; ++i) {
        TimerService service([](Snapshot) {});
        service.countdown(1, 0);
    }
}
