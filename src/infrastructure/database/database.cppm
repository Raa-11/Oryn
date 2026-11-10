module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module oryn.database;

export namespace oryn::database {
    enum class ErrorKind {
        filesystem,
        open,
        sqlite,
        unsupported_version,
        invalid_schema,
        invalid_settings,
        invalid_task,
        not_found
    };

    // Operations are fixed labels, never SQL, database contents, or filesystem paths.
    struct Error {
        ErrorKind kind;
        std::string_view operation;
        int code = 0;
    };

    struct SettingsRecord {
        bool light_mode = false;
        bool reduce_motion = false;
        bool always_on_top = true;
        bool expanded = true;
        bool remember_position = false;
        int window_x = 0;
        int window_y = 0;
        int focus_minutes = 25;
        int short_minutes = 5;
        int long_minutes = 15;
        bool operator==(const SettingsRecord &) const = default;
    };

    enum class TaskFilter { today, upcoming, completed };

    struct TaskDraft {
        std::string title;
        std::string notes;
        std::string due_date;
    };

    struct TaskRecord {
        std::int64_t id = 0;
        std::string title;
        std::string notes;
        std::string due_date;
        bool completed = false;
        std::int64_t created_at = 0;
        std::int64_t updated_at = 0;
        bool operator==(const TaskRecord &) const = default;
    };

    // A connection and every statement belong to one calling thread. Opening creates parent
    // directories and migrates synchronously; consumers must call this on their storage worker.
    class Database {
    public:
        static std::expected<std::unique_ptr<Database>, Error>
        open(const std::filesystem::path &path);
        ~Database();
        Database(const Database &) = delete;
        Database &operator=(const Database &) = delete;
        Database(Database &&) = delete;
        Database &operator=(Database &&) = delete;

        std::expected<SettingsRecord, Error> load();
        std::expected<void, Error> save(const SettingsRecord &settings);

        std::expected<std::vector<TaskRecord>, Error> list_tasks(TaskFilter filter,
                                                                 std::string_view today,
                                                                 std::size_t offset,
                                                                 std::size_t limit);
        std::expected<std::int64_t, Error> count_tasks(TaskFilter filter, std::string_view today);
        std::expected<TaskRecord, Error> create_task(const TaskDraft &draft);
        std::expected<TaskRecord, Error> update_task(std::int64_t id, const TaskDraft &draft);
        std::expected<TaskRecord, Error> set_task_completed(std::int64_t id, bool completed);
        std::expected<void, Error> erase_task(std::int64_t id);

    private:
        struct Impl;
        explicit Database(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> impl_;
    };
} // namespace oryn::database
