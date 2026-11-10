module;
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

export module oryn.calendar;

export namespace oryn::calendar {
    using TaskId = std::int64_t;
    enum class Filter { today, upcoming, completed };
    enum class ChangeKind { none, created, updated, completed, deleted };

    struct Draft {
        std::string title;
        std::string notes;
        std::string due_date;
    };

    struct Task {
        TaskId id = 0;
        std::string title;
        std::string notes;
        std::string due_date;
        bool completed = false;
        bool operator==(const Task &) const = default;
    };

    struct Snapshot {
        std::vector<Task> tasks;
        std::string today;
        Filter filter = Filter::today;
        bool loaded = false;
        bool busy = false;
        std::string error;
        int page = 0;
        bool has_more = false;
        std::int64_t today_count = 0;
        std::uint64_t revision = 0;
        TaskId changed_id = 0;
        ChangeKind change = ChangeKind::none;
    };

    // Actual local civil day; throws if the system clock cannot provide a valid date.
    std::string local_today();

    class CalendarService {
    public:
        using Observer = std::function<void(Snapshot)>;
        using Today = std::function<std::string()>;
        // Callbacks execute on the storage worker. They must not destroy this service.
        CalendarService(std::filesystem::path database, Observer observer,
                        Today today = local_today);
        ~CalendarService();
        CalendarService(const CalendarService &) = delete;
        CalendarService &operator=(const CalendarService &) = delete;
        CalendarService(CalendarService &&) = delete;
        CalendarService &operator=(CalendarService &&) = delete;

        // False means the bounded FIFO is full or stopping; no accepted mutation is dropped.
        bool create(Draft draft);
        bool update(TaskId id, Draft draft);
        bool set_completed(TaskId id, bool completed);
        bool erase(TaskId id);
        bool select_filter(Filter filter);
        bool page(int page);
        bool refresh();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace oryn::calendar
