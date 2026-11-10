module;
#include <chrono>

export module oryn.time.pomodoro;

export namespace oryn::time {
    enum class Phase { focus, short_break, long_break };

    struct PomodoroConfig {
        std::chrono::seconds focus{25 * 60};
        std::chrono::seconds short_break{5 * 60};
        std::chrono::seconds long_break{15 * 60};

        bool valid() const {
            using namespace std::chrono;
            return focus >= 1s && focus <= 180min && short_break >= 1s && short_break <= 180min &&
                   long_break >= 1s && long_break <= 180min;
        }
    };

    class PomodoroCycle {
    public:
        PomodoroConfig config;
        Phase phase = Phase::focus;
        int completed_focus = 0;

        std::chrono::seconds duration() const {
            if (phase == Phase::short_break) {
                return config.short_break;
            }
            if (phase == Phase::long_break) {
                return config.long_break;
            }
            return config.focus;
        }

        void advance() {
            if (phase == Phase::focus) {
                ++completed_focus;
                phase = completed_focus % 4 == 0 ? Phase::long_break : Phase::short_break;
            } else {
                phase = Phase::focus;
            }
        }

        void reset() {
            phase = Phase::focus;
            completed_focus = 0;
        }
    };
} // namespace oryn::time
