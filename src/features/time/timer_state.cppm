module;
#include <algorithm>
#include <chrono>
#include <optional>
#include <string>

export module oryn.time.state;
export import oryn.time.pomodoro;

export namespace oryn::time {
    enum class Status { idle, running, paused, finished };

    struct Snapshot {
        PomodoroConfig config;
        Status status = Status::idle;
        bool pomodoro = false;
        Phase phase = Phase::focus;
        int focus_session = 1;
        int remaining_seconds = 15 * 60;
        int total_seconds = 15 * 60;
        unsigned completions = 0;
        bool completed = false;
        std::string error;
    };

    // All timestamps are injected: tests advance time without sleeping.
    class TimerState {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        using Duration = Clock::duration;
        static constexpr auto max_duration = std::chrono::seconds{24 * 60 * 60 - 1};

        bool select_mode(bool pomodoro, TimePoint now) {
            update(now);
            if (pomodoro_ == pomodoro) {
                return false;
            }
            pomodoro_ = pomodoro;
            return true;
        }

        bool configure(PomodoroConfig config, TimePoint) {
            const auto active = [](const Session &session) {
                return session.status == Status::running || session.status == Status::paused;
            };
            if (!config.valid() || active(selected()) || active(pomodoro_session_)) {
                return false;
            }
            cycle_.config = config;
            cycle_.reset();
            pomodoro_session_ = Session{.remaining = cycle_.duration()};
            return true;
        }

        bool start_countdown(std::chrono::seconds duration, TimePoint now) {
            if (duration <= std::chrono::seconds::zero() || duration > max_duration) {
                return false;
            }
            pomodoro_ = false;
            countdown_ = duration;
            reset(now);
            return start(now);
        }

        bool start(TimePoint now) {
            auto &session = selected();
            if (session.status == Status::running) {
                return false;
            }
            if (session.status != Status::paused) {
                session.remaining = total();
            }
            session.deadline = now + session.remaining;
            session.completed = false;
            session.status = Status::running;
            return true;
        }

        bool update(TimePoint now) {
            const bool countdown_completed = update_session(countdown_session_, false, now);
            const bool pomodoro_completed = update_session(pomodoro_session_, true, now);
            return countdown_completed || pomodoro_completed;
        }

        bool pause(TimePoint now) {
            update(now);
            auto &session = selected();
            if (session.status != Status::running) {
                return false;
            }
            session.status = Status::paused;
            return true;
        }

        void reset(TimePoint) {
            auto &session = selected();
            session.status = Status::idle;
            session.remaining = total();
            session.completed = false;
        }

        void cancel(TimePoint now) {
            if (pomodoro_) {
                cycle_.reset();
            }
            reset(now);
        }

        bool running() const {
            return selected().status == Status::running;
        }

        std::optional<TimePoint> next_deadline() const {
            std::optional<TimePoint> next;
            if (countdown_session_.status == Status::running) {
                next = countdown_session_.deadline;
            }
            if (pomodoro_session_.status == Status::running) {
                next =
                    next ? std::min(*next, pomodoro_session_.deadline) : pomodoro_session_.deadline;
            }
            return next;
        }

        Snapshot snapshot(TimePoint now) {
            update(now);
            Snapshot result;
            result.config = cycle_.config;
            result.status = selected().status;
            result.pomodoro = pomodoro_;
            result.phase = cycle_.phase;
            result.focus_session = cycle_.phase == Phase::focus
                                       ? cycle_.completed_focus % 4 + 1
                                       : (std::max(1, cycle_.completed_focus) - 1) % 4 + 1;
            result.remaining_seconds = static_cast<int>(
                std::chrono::ceil<std::chrono::seconds>(selected().remaining).count());
            result.total_seconds = static_cast<int>(total().count());
            result.completions = completions_;
            result.completed = selected().completed;
            return result;
        }

    private:
        struct Session {
            Status status = Status::idle;
            Duration remaining{};
            TimePoint deadline{};
            bool completed = false;
        };

        Session &selected() {
            return pomodoro_ ? pomodoro_session_ : countdown_session_;
        }

        const Session &selected() const {
            return pomodoro_ ? pomodoro_session_ : countdown_session_;
        }

        bool update_session(Session &session, bool pomodoro, TimePoint now) {
            if (session.status != Status::running) {
                return false;
            }
            if (now < session.deadline) {
                session.remaining = session.deadline - now;
                return false;
            }
            ++completions_;
            session.completed = true;
            if (pomodoro) {
                cycle_.advance();
                session.remaining = cycle_.duration();
                // Each phase still starts explicitly, even when its tab was hidden.
                session.status = Status::idle;
            } else {
                session.remaining = Duration::zero();
                session.status = Status::finished;
            }
            return true;
        }

        std::chrono::seconds total() const {
            return pomodoro_ ? cycle_.duration() : countdown_;
        }

        PomodoroCycle cycle_;
        bool pomodoro_ = false;
        std::chrono::seconds countdown_{15 * 60};
        Session countdown_session_{.remaining = countdown_};
        Session pomodoro_session_{.remaining = cycle_.duration()};
        unsigned completions_ = 0;
    };
} // namespace oryn::time
