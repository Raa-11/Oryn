module;

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <entt/entt.hpp>

module oryn.time;

namespace oryn::time {
    std::string format_remaining(int seconds) {
        seconds = std::max(0, seconds);
        auto two = [](int value) { return (value < 10 ? "0" : "") + std::to_string(value); };
        if (seconds >= 3600) {
            return two(seconds / 3600) + ":" + two(seconds / 60 % 60) + ":" + two(seconds % 60);
        }
        return two(seconds / 60) + ":" + two(seconds % 60);
    }

    struct TimeChanged {
        Snapshot state;
    };

    struct TimerStarted {};

    struct TimerPaused {};

    struct TimerCompleted {
        unsigned count;
    };

    struct PomodoroPhaseChanged {
        Phase phase;
    };

    struct TimerService::Impl {
        boost::asio::io_context io;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work{
            io.get_executor()};
        boost::asio::steady_timer wake{io};
        TimerState state;
        Observer observer;
        entt::dispatcher events; // Owned and triggered exclusively on the I/O thread.
        Snapshot previous;
        std::jthread worker;

        explicit Impl(Observer callback) : observer(std::move(callback)) {
            events.sink<TimeChanged>().connect<&Impl::publish>(*this);
            // Finish all potentially throwing setup before a worker can own the work guard.
            command([](TimerState &, auto) { return true; });
            worker = std::jthread([this] { io.run(); });
        }

        ~Impl() {
            try {
                boost::asio::post(io, [this]() noexcept {
                    observer = {};
                    work.reset();
                    try {
                        wake.cancel();
                    } catch (...) {
                        // The supported Asio cancel API throws on failure.
                        io.stop();
                    }
                });
            } catch (...) {
                // If posting shutdown fails, stop rather than throw from destruction.
                // Only this exceptional path abandons queued commands; joining still
                // keeps all captured references alive until the worker has exited.
                io.stop();
            }
            // Normally io.run drains queued commands and timer cancellation before joining.
            worker.join();
        }

        void publish(const TimeChanged &event) {
            if (observer) {
                observer(event.state);
            }
        }

        void notify(std::string error = {}) {
            auto current = state.snapshot(TimerState::Clock::now());
            current.error = std::move(error);
            if (current.status == Status::running && previous.status != Status::running) {
                events.trigger(TimerStarted{});
            }
            if (current.status == Status::paused && previous.status != Status::paused) {
                events.trigger(TimerPaused{});
            }
            if (current.completions != previous.completions) {
                events.trigger(TimerCompleted{current.completions});
            }
            if (current.phase != previous.phase) {
                events.trigger(PomodoroPhaseChanged{current.phase});
            }
            previous = current;
            events.trigger(TimeChanged{std::move(current)});
        }

        void schedule() {
            const auto deadline = state.next_deadline();
            if (!deadline) {
                return;
            }
            const auto now = TimerState::Clock::now();
            wake.expires_at(std::min(*deadline, now + std::chrono::seconds{1}));
            wake.async_wait([this](boost::system::error_code error) {
                if (error) {
                    return;
                }
                notify();
                schedule();
            });
        }

        template <class Command> void command(Command command, std::string invalid = {}) {
            boost::asio::post(io,
                              [this, command = std::move(command), invalid = std::move(invalid)] {
                                  wake.cancel();
                                  const bool valid = command(state, TimerState::Clock::now());
                                  notify(valid ? "" : invalid);
                                  schedule();
                              });
        }
    };

    TimerService::TimerService(Observer observer)
        : impl_(std::make_unique<Impl>(std::move(observer))) {
    }

    TimerService::~TimerService() = default;

    void TimerService::countdown(int minutes, int seconds) {
        impl_->command(
            [minutes, seconds](TimerState &state, auto now) {
                if (minutes < 0 || minutes > 1439 || seconds < 0 || seconds > 59) {
                    return false;
                }
                return state.start_countdown(std::chrono::seconds{minutes * 60 + seconds}, now);
            },
            "Enter a duration from 00:01 to 23:59:59.");
    }

    void TimerService::select_mode(bool pomodoro) {
        impl_->command([pomodoro](TimerState &state, auto now) {
            state.select_mode(pomodoro, now);
            return true;
        });
    }

    void TimerService::configure(int focus, int short_break, int long_break) {
        impl_->command(
            [=](TimerState &state, auto now) {
                if (focus < 1 || focus > 180 || short_break < 1 || short_break > 180 ||
                    long_break < 1 || long_break > 180) {
                    return false;
                }
                return state.configure({std::chrono::minutes{focus},
                                        std::chrono::minutes{short_break},
                                        std::chrono::minutes{long_break}},
                                       now);
            },
            "Durations must be 1–180 minutes; reset before editing.");
    }

    void TimerService::start() {
        impl_->command([](TimerState &state, auto now) {
            state.start(now);
            return true;
        });
    }

    void TimerService::toggle() {
        impl_->command([](TimerState &state, auto now) {
            if (state.running()) {
                state.pause(now);
            } else {
                state.start(now);
            }
            return true;
        });
    }

    void TimerService::reset() {
        impl_->command([](TimerState &state, auto now) {
            state.reset(now);
            return true;
        });
    }

    void TimerService::cancel() {
        impl_->command([](TimerState &state, auto now) {
            state.cancel(now);
            return true;
        });
    }
} // namespace oryn::time
