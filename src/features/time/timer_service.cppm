module;
#include <functional>
#include <memory>
#include <string>

export module oryn.time;
export import oryn.time.state;

export namespace oryn::time {
    std::string format_remaining(int seconds);

    // Commands are posted to one Asio owner thread. Destruction cancels, drains and joins;
    // if shutdown posting or cancellation fails, it stops and joins without draining.
    class TimerService {
    public:
        using Observer = std::function<void(Snapshot)>;
        // The observer runs on the owner thread; it must not throw or destroy this service.
        explicit TimerService(Observer observer);
        ~TimerService();
        TimerService(const TimerService &) = delete;
        TimerService &operator=(const TimerService &) = delete;
        TimerService(TimerService &&) = delete;
        TimerService &operator=(TimerService &&) = delete;
        void countdown(int minutes, int seconds);
        void select_mode(bool pomodoro);
        void configure(int focus_minutes, int short_minutes, int long_minutes);
        void start();
        void toggle();
        void reset();
        void cancel();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace oryn::time
