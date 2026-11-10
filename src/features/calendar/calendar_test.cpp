#include <ostream>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include <sqlite3.h>
import oryn.calendar;
import oryn.database;

using namespace std::chrono_literals;
using namespace oryn::calendar;

static_assert(!std::is_copy_constructible_v<CalendarService>);
static_assert(!std::is_copy_assignable_v<CalendarService>);
static_assert(!std::is_move_constructible_v<CalendarService>);
static_assert(!std::is_move_assignable_v<CalendarService>);

namespace {
    class TemporaryDirectory {
    public:
        TemporaryDirectory() {
            static std::atomic<unsigned> sequence{0};
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (;;) {
                path = std::filesystem::temp_directory_path() /
                       ("oryn-calendar-" + std::to_string(stamp) + "-" +
                        std::to_string(sequence.fetch_add(1)));
                std::error_code error;
                if (std::filesystem::create_directory(path, error)) {
                    break;
                }
                REQUIRE_FALSE(error);
            }
        }

        ~TemporaryDirectory() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }

        std::filesystem::path path;
    };

    class Observations {
    public:
        void receive(Snapshot snapshot) {
            std::unique_lock lock{mutex};
            worker_thread = std::this_thread::get_id();
            const bool block = hold_loaded && snapshot.loaded && !snapshot.busy;
            snapshots.push_back(std::move(snapshot));
            if (block) {
                held = true;
                changed.notify_all();
                changed.wait(lock, [this] { return released; });
                hold_loaded = false;
            }
            changed.notify_all();
        }

        std::size_t size() {
            const std::lock_guard lock{mutex};
            return snapshots.size();
        }

        Snapshot settled(std::size_t after = 0) {
            std::unique_lock lock{mutex};
            const bool found = changed.wait_for(lock, 10s, [&] {
                for (std::size_t index = after; index < snapshots.size(); ++index) {
                    if (!snapshots[index].busy) {
                        return true;
                    }
                }
                return false;
            });
            REQUIRE(found);
            for (std::size_t index = after; index < snapshots.size(); ++index) {
                if (!snapshots[index].busy) {
                    return snapshots[index];
                }
            }
            return {};
        }

        bool await_held() {
            std::unique_lock lock{mutex};
            return changed.wait_for(lock, 10s, [this] { return held; });
        }

        void release() {
            {
                const std::lock_guard lock{mutex};
                released = true;
            }
            changed.notify_all();
        }

        struct ReleaseOnExit {
            Observations &observations;

            ~ReleaseOnExit() {
                observations.release();
            }
        };

        std::mutex mutex;
        std::condition_variable changed;
        std::vector<Snapshot> snapshots;
        std::thread::id worker_thread;
        bool hold_loaded = false;
        bool held = false;
        bool released = false;
    };

    template <class Action> Snapshot perform(Observations &observations, Action action) {
        const auto before = observations.size();
        REQUIRE(action());
        return observations.settled(before);
    }

    CalendarService::Today fixed_today(std::string day = "2026-11-08") {
        return [day = std::move(day)] { return day; };
    }

    class Day {
    public:
        std::string get() {
            const std::lock_guard lock{mutex};
            ++reads;
            if (throws) {
                throw std::runtime_error("private provider details");
            }
            return value;
        }

        void set(std::string day, bool fail = false) {
            const std::lock_guard lock{mutex};
            value = std::move(day);
            throws = fail;
        }

        std::mutex mutex;
        std::string value = "2026-11-08";
        int reads = 0;
        bool throws = false;
    };

    class RawDatabase {
    public:
        explicit RawDatabase(const std::filesystem::path &path) {
            const auto encoded = path.u8string();
            REQUIRE(sqlite3_open(reinterpret_cast<const char *>(encoded.c_str()), &connection) ==
                    SQLITE_OK);
        }

        ~RawDatabase() {
            sqlite3_close(connection);
        }

        void execute(const char *sql) {
            REQUIRE(sqlite3_exec(connection, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
        }

        sqlite3 *connection = nullptr;
    };

    void seed(const std::filesystem::path &path, int count, std::string due = "2026-11-08") {
        auto opened = oryn::database::Database::open(path);
        REQUIRE(opened);
        for (int index = 0; index < count; ++index) {
            REQUIRE((*opened)->create_task({"Task " + std::to_string(index), "", due}));
        }
    }
} // namespace

TEST_CASE("Calendar starts honestly empty, loads off-thread and persists committed task data") {
    TemporaryDirectory directory;
    const auto path = directory.path / "nested" / "settings.sqlite3";
    const auto caller = std::this_thread::get_id();
    TaskId id = 0;
    {
        Observations observations;
        CalendarService service{
            path, [&](Snapshot value) { observations.receive(std::move(value)); }, fixed_today()};
        const auto initial = observations.settled();
        CHECK(initial.loaded);
        CHECK_FALSE(initial.busy);
        CHECK(initial.error.empty());
        CHECK(initial.tasks.empty());
        CHECK(initial.today == "2026-11-08");
        CHECK(initial.today_count == 0);
        CHECK(initial.revision == 0);
        CHECK(initial.change == ChangeKind::none);
        CHECK_FALSE(initial.has_more);
        CHECK(observations.worker_thread != caller);
        auto created = perform(observations, [&] {
            return service.create({"Don't lose 日本語", "Line one\nLine two 'quoted'", ""});
        });
        REQUIRE(created.error.empty());
        REQUIRE(created.tasks.size() == 1);
        id = created.tasks.front().id;
        CHECK(id > 0);
        CHECK(created.changed_id == id);
        CHECK(created.change == ChangeKind::created);
        CHECK(created.revision == 1);
        CHECK(created.today_count == 1);
        CHECK(created.tasks.front().title == "Don't lose 日本語");
        CHECK(created.tasks.front().notes == "Line one\nLine two 'quoted'");
        CHECK(created.tasks.front().due_date.empty());
        const auto refreshed = perform(observations, [&] { return service.refresh(); });
        CHECK(refreshed.revision == 1);
        CHECK(refreshed.tasks == created.tasks);
    }
    Observations restored;
    CalendarService service{path, [&](Snapshot value) { restored.receive(std::move(value)); },
                            fixed_today()};
    const auto snapshot = restored.settled();
    REQUIRE(snapshot.loaded);
    REQUIRE(snapshot.tasks.size() == 1);
    CHECK(snapshot.tasks.front().id == id);
    CHECK(snapshot.tasks.front().title == "Don't lose 日本語");
    CHECK(snapshot.tasks.front().notes == "Line one\nLine two 'quoted'");
    CHECK(snapshot.revision == 0);
}

TEST_CASE("Committed create update complete reopen delete notifications refresh real storage") {
    TemporaryDirectory directory;
    Observations observations;
    const auto path = directory.path / "settings.sqlite3";
    CalendarService service{path, [&](Snapshot value) { observations.receive(std::move(value)); },
                            fixed_today()};
    REQUIRE(observations.settled().loaded);
    auto snapshot = perform(
        observations, [&] { return service.create({"Original", "Original notes", "2026-11-08"}); });
    REQUIRE(snapshot.tasks.size() == 1);
    const auto id = snapshot.tasks.front().id;
    snapshot = perform(
        observations, [&] { return service.update(id, {"Edited", "Edited notes", "2026-11-07"}); });
    REQUIRE(snapshot.tasks.size() == 1);
    CHECK(snapshot.tasks.front().title == "Edited");
    CHECK(snapshot.tasks.front().notes == "Edited notes");
    CHECK(snapshot.tasks.front().due_date == "2026-11-07");
    CHECK(snapshot.revision == 2);
    CHECK(snapshot.change == ChangeKind::updated);
    snapshot = perform(observations, [&] { return service.set_completed(id, true); });
    CHECK(snapshot.tasks.empty());
    CHECK(snapshot.today_count == 0);
    CHECK(snapshot.revision == 3);
    CHECK(snapshot.change == ChangeKind::completed);
    CHECK(snapshot.changed_id == id);
    snapshot = perform(observations, [&] { return service.select_filter(Filter::completed); });
    REQUIRE(snapshot.tasks.size() == 1);
    CHECK(snapshot.tasks.front().completed);
    CHECK(snapshot.revision == 3);
    snapshot =
        perform(observations, [&] { return service.update(id, {"Completed edit", "Saved", ""}); });
    REQUIRE(snapshot.tasks.size() == 1);
    CHECK(snapshot.tasks.front().completed);
    CHECK(snapshot.tasks.front().title == "Completed edit");
    CHECK(snapshot.revision == 4);
    snapshot = perform(observations, [&] { return service.set_completed(id, false); });
    CHECK(snapshot.tasks.empty());
    CHECK(snapshot.today_count == 1);
    CHECK(snapshot.revision == 5);
    snapshot = perform(observations, [&] { return service.select_filter(Filter::today); });
    REQUIRE(snapshot.tasks.size() == 1);
    CHECK_FALSE(snapshot.tasks.front().completed);
    snapshot = perform(observations, [&] { return service.erase(id); });
    CHECK(snapshot.tasks.empty());
    CHECK(snapshot.today_count == 0);
    CHECK(snapshot.changed_id == id);
    CHECK(snapshot.change == ChangeKind::deleted);
    CHECK(snapshot.revision == 6);
    auto opened = oryn::database::Database::open(path);
    REQUIRE(opened);
    auto remaining = (*opened)->count_tasks(oryn::database::TaskFilter::today, "2026-11-08");
    REQUIRE(remaining);
    CHECK(*remaining == 0);
}

TEST_CASE("Failed mutations preserve committed list, revision and change identity") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    Observations observations;
    CalendarService service{path, [&](Snapshot value) { observations.receive(std::move(value)); },
                            fixed_today()};
    REQUIRE(observations.settled().loaded);
    const auto committed = perform(observations, [&] {
        return service.create({"Keep me", "Keep these notes", "2024-02-29"});
    });
    REQUIRE(committed.tasks.size() == 1);
    const auto id = committed.tasks.front().id;
    auto unchanged = [&](const Snapshot &snapshot) {
        CHECK_FALSE(snapshot.error.empty());
        CHECK(snapshot.loaded);
        CHECK_FALSE(snapshot.busy);
        CHECK(snapshot.tasks == committed.tasks);
        CHECK(snapshot.revision == committed.revision);
        CHECK(snapshot.changed_id == committed.changed_id);
        CHECK(snapshot.change == committed.change);
        CHECK(snapshot.today_count == committed.today_count);
    };
    unchanged(perform(observations, [&] { return service.create({"  \t\n", "", ""}); }));
    unchanged(
        perform(observations, [&] { return service.update(id, {"Invalid", "", "2023-02-29"}); }));
    unchanged(
        perform(observations, [&] { return service.create({std::string(241, 'x'), "", ""}); }));
    unchanged(perform(observations,
                      [&] { return service.update(id, {"Keep me", std::string(4097, 'x'), ""}); }));
    unchanged(perform(observations,
                      [&] { return service.create({std::string{"bad\0title", 9}, "", ""}); }));
    unchanged(perform(observations, [&] {
        return service.update(id, {"Keep me", std::string{"bad\0notes", 9}, ""});
    }));
    unchanged(perform(observations, [&] {
        return service.create({std::string(1, static_cast<char>(0xff)), "", ""});
    }));
    unchanged(perform(observations, [&] {
        return service.update(id, {"Keep me", std::string{"\xc0\xaf"}, ""});
    }));
    unchanged(perform(observations, [&] { return service.update(id + 999, {"Missing", "", ""}); }));
    unchanged(perform(observations, [&] { return service.set_completed(id + 999, true); }));
    unchanged(perform(observations, [&] { return service.erase(id + 999); }));
    {
        RawDatabase raw{path};
        raw.execute("CREATE TRIGGER fail_calendar_update BEFORE UPDATE ON tasks "
                    "BEGIN SELECT RAISE(ABORT,'secret task contents'); END");
        raw.execute("CREATE TRIGGER fail_calendar_delete BEFORE DELETE ON tasks "
                    "BEGIN SELECT RAISE(ABORT,'secret task contents'); END");
    }
    auto failed =
        perform(observations, [&] { return service.update(id, {"Do not replace", "", ""}); });
    unchanged(failed);
    CHECK(failed.error.find("secret") == std::string::npos);
    unchanged(perform(observations, [&] { return service.set_completed(id, true); }));
    unchanged(perform(observations, [&] { return service.erase(id); }));
    const auto refreshed = perform(observations, [&] { return service.refresh(); });
    CHECK(refreshed.error.empty());
    CHECK(refreshed.tasks == committed.tasks);
    CHECK(refreshed.revision == 1);
    {
        const std::lock_guard lock{observations.mutex};
        for (const auto &snapshot : observations.snapshots) {
            if (snapshot.busy && snapshot.revision == 1) {
                CHECK(snapshot.tasks == committed.tasks);
            }
        }
    }
}

TEST_CASE("Exact UTF-8 byte limits are accepted without trimming the caller draft") {
    TemporaryDirectory directory;
    Observations observations;
    CalendarService service{directory.path / "settings.sqlite3",
                            [&](Snapshot value) { observations.receive(std::move(value)); },
                            fixed_today()};
    REQUIRE(observations.settled().loaded);
    const std::string title = " " + std::string(238, 'x') + " ";
    const std::string notes(4096, 'n');
    const auto snapshot = perform(observations, [&] { return service.create({title, notes, ""}); });
    REQUIRE(snapshot.error.empty());
    REQUIRE(snapshot.tasks.size() == 1);
    CHECK(snapshot.tasks.front().title == title);
    CHECK(snapshot.tasks.front().notes == notes);
    CHECK(snapshot.revision == 1);
}

TEST_CASE("Gregorian date validation retains drafts and supports complete civil date range") {
    TemporaryDirectory directory;
    Observations observations;
    CalendarService service{directory.path / "settings.sqlite3",
                            [&](Snapshot value) { observations.receive(std::move(value)); },
                            fixed_today("9999-12-31")};
    REQUIRE(observations.settled().loaded);
    for (const std::string invalid : {"0000-01-01", "1900-02-29", "2026-04-31", "2026-13-01",
                                      "2026-00-01", "2026-01-00", "2026-1-01", "2026-11-08Z"}) {
        const auto snapshot = perform(observations, [&] {
            return service.create({"Invalid date", "Preserve this draft at caller", invalid});
        });
        CHECK_FALSE(snapshot.error.empty());
        CHECK(snapshot.tasks.empty());
        CHECK(snapshot.revision == 0);
    }
    for (const std::string valid : {"0001-01-01", "2000-02-29", "2024-02-29", "9999-12-31", ""}) {
        const auto snapshot =
            perform(observations, [&] { return service.create({"Valid date", "", valid}); });
        CHECK(snapshot.error.empty());
    }
    const auto snapshot = perform(observations, [&] { return service.refresh(); });
    REQUIRE(snapshot.tasks.size() == 5);
    CHECK(snapshot.tasks.front().due_date == "0001-01-01");
    CHECK(snapshot.tasks.back().due_date.empty());
    CHECK(snapshot.revision == 5);
}

TEST_CASE("Date providers are validated and recalculated per command without idle polling") {
    TemporaryDirectory directory;
    Day day;
    day.set("2024-02-28");
    Observations observations;
    CalendarService service{directory.path / "settings.sqlite3",
                            [&](Snapshot value) { observations.receive(std::move(value)); },
                            [&] { return day.get(); }};
    CHECK(observations.settled().today == "2024-02-28");
    auto snapshot =
        perform(observations, [&] { return service.create({"Leap day", "", "2024-02-29"}); });
    CHECK(snapshot.tasks.empty());
    CHECK(snapshot.today_count == 0);
    day.set("2024-02-29");
    snapshot = perform(observations, [&] { return service.refresh(); });
    REQUIRE(snapshot.tasks.size() == 1);
    CHECK(snapshot.today == "2024-02-29");
    CHECK(snapshot.today_count == 1);
    CHECK(snapshot.revision == 1);
    const auto committed = snapshot;
    for (const std::string invalid : {"2023-02-29", "0000-01-01", "2026-1-01", ""}) {
        day.set(invalid);
        snapshot = perform(observations, [&] { return service.create({"Never stored", "", ""}); });
        CHECK_FALSE(snapshot.error.empty());
        CHECK(snapshot.tasks == committed.tasks);
        CHECK(snapshot.today == committed.today);
        CHECK(snapshot.revision == committed.revision);
    }
    day.set("2024-03-01", true);
    snapshot = perform(observations, [&] { return service.refresh(); });
    CHECK_FALSE(snapshot.error.empty());
    CHECK(snapshot.error.find("private") == std::string::npos);
    CHECK(snapshot.tasks == committed.tasks);
    day.set("2025-01-01");
    snapshot = perform(observations, [&] { return service.select_filter(Filter::upcoming); });
    CHECK(snapshot.error.empty());
    CHECK(snapshot.today == "2025-01-01");
    CHECK(snapshot.tasks.empty());
    CHECK(snapshot.today_count == 1);
    {
        const std::lock_guard lock{day.mutex};
        // Initial load, create, refresh, four rejected-date commands, throwing refresh, filter.
        CHECK(day.reads == 9);
    }
}

TEST_CASE("Local today matches the system local civil date instead of UTC conversion") {
    const auto before = std::time(nullptr);
    const auto actual = local_today();
    const auto after = std::time(nullptr);
    auto civil = [](std::time_t value) {
        std::tm local{};
#ifdef _WIN32
        REQUIRE(localtime_s(&local, &value) == 0);
#else
        REQUIRE(localtime_r(&value, &local) != nullptr);
#endif
        auto two = [](int part) { return (part < 10 ? "0" : "") + std::to_string(part); };
        return std::to_string(local.tm_year + 1900) + "-" + two(local.tm_mon + 1) + "-" +
               two(local.tm_mday);
    };
    CHECK((actual == civil(before) || actual == civil(after)));
}

TEST_CASE(
    "Today includes overdue and undated tasks, upcoming excludes both, completed ignores dates") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    {
        auto database = oryn::database::Database::open(path);
        REQUIRE(database);
        REQUIRE((*database)->create_task({"Undated", "", ""}));
        REQUIRE((*database)->create_task({"Future", "", "2026-11-09"}));
        REQUIRE((*database)->create_task({"Today first", "", "2026-11-08"}));
        REQUIRE((*database)->create_task({"Overdue", "", "2026-11-07"}));
        REQUIRE((*database)->create_task({"Today second", "", "2026-11-08"}));
        auto complete = (*database)->create_task({"Complete future", "", "9999-12-31"});
        REQUIRE(complete);
        REQUIRE((*database)->set_task_completed(complete->id, true));
    }
    Observations observations;
    CalendarService service{path, [&](Snapshot value) { observations.receive(std::move(value)); },
                            fixed_today()};
    auto snapshot = observations.settled();
    REQUIRE(snapshot.tasks.size() == 4);
    CHECK(snapshot.tasks[0].title == "Overdue");
    CHECK(snapshot.tasks[1].title == "Today first");
    CHECK(snapshot.tasks[2].title == "Today second");
    CHECK(snapshot.tasks[3].title == "Undated");
    CHECK(snapshot.today_count == 4);
    snapshot = perform(observations, [&] { return service.select_filter(Filter::upcoming); });
    REQUIRE(snapshot.tasks.size() == 1);
    CHECK(snapshot.tasks.front().title == "Future");
    CHECK(snapshot.today_count == 4);
    CHECK(snapshot.revision == 0);
    snapshot = perform(observations, [&] { return service.select_filter(Filter::completed); });
    REQUIRE(snapshot.tasks.size() == 1);
    CHECK(snapshot.tasks.front().title == "Complete future");
    CHECK(snapshot.tasks.front().completed);
    CHECK(snapshot.revision == 0);
}

TEST_CASE(
    "Fifty-row pages use lookahead, preserve stable order and clamp after last-row deletion") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    seed(path, 101);
    Observations observations;
    CalendarService service{path, [&](Snapshot value) { observations.receive(std::move(value)); },
                            fixed_today()};
    auto snapshot = observations.settled();
    REQUIRE(snapshot.tasks.size() == 50);
    CHECK(snapshot.page == 0);
    CHECK(snapshot.has_more);
    CHECK(snapshot.today_count == 101);
    CHECK(snapshot.tasks.front().title == "Task 0");
    CHECK(snapshot.tasks.back().title == "Task 49");
    snapshot = perform(observations, [&] { return service.page(1); });
    REQUIRE(snapshot.tasks.size() == 50);
    CHECK(snapshot.page == 1);
    CHECK(snapshot.has_more);
    CHECK(snapshot.tasks.front().title == "Task 50");
    CHECK(snapshot.tasks.back().title == "Task 99");
    snapshot = perform(observations, [&] { return service.page(std::numeric_limits<int>::max()); });
    REQUIRE(snapshot.tasks.size() == 1);
    CHECK(snapshot.page == 2);
    CHECK_FALSE(snapshot.has_more);
    CHECK(snapshot.tasks.front().title == "Task 100");
    const auto id = snapshot.tasks.front().id;
    snapshot = perform(observations, [&] { return service.erase(id); });
    REQUIRE(snapshot.tasks.size() == 50);
    CHECK(snapshot.page == 1);
    CHECK_FALSE(snapshot.has_more);
    CHECK(snapshot.today_count == 100);
    CHECK(snapshot.tasks.front().title == "Task 50");
    CHECK(snapshot.revision == 1);
    const auto committed = snapshot;
    snapshot = perform(observations, [&] { return service.page(-1); });
    CHECK_FALSE(snapshot.error.empty());
    CHECK(snapshot.tasks == committed.tasks);
    CHECK(snapshot.page == committed.page);
    CHECK(snapshot.revision == committed.revision);
    snapshot = perform(observations, [&] { return service.select_filter(Filter::upcoming); });
    CHECK(snapshot.page == 0);
    CHECK(snapshot.tasks.empty());
    CHECK_FALSE(snapshot.has_more);
    snapshot = perform(observations, [&] { return service.select_filter(Filter::today); });
    CHECK(snapshot.page == 0);
    CHECK(snapshot.has_more);
    CHECK(snapshot.revision == 1);
}

TEST_CASE("Exactly fifty tasks have no next page and empty filters clamp to page zero") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    seed(path, 50);
    Observations observations;
    CalendarService service{path, [&](Snapshot value) { observations.receive(std::move(value)); },
                            fixed_today()};
    auto snapshot = observations.settled();
    CHECK(snapshot.tasks.size() == 50);
    CHECK_FALSE(snapshot.has_more);
    snapshot = perform(observations, [&] { return service.page(1); });
    CHECK(snapshot.page == 0);
    CHECK(snapshot.tasks.size() == 50);
    snapshot = perform(observations, [&] { return service.select_filter(Filter::completed); });
    snapshot = perform(observations, [&] { return service.page(123); });
    CHECK(snapshot.page == 0);
    CHECK(snapshot.tasks.empty());
    CHECK(snapshot.loaded);
    CHECK(snapshot.error.empty());
}

TEST_CASE(
    "Initial storage errors stay unloaded and Refresh can recover without rebuilding the service") {
    TemporaryDirectory directory;
    const auto blocked = directory.path / "blocked";
    {
        std::ofstream file{blocked};
        file << "not a directory";
    }
    Observations observations;
    CalendarService service{blocked / "settings.sqlite3",
                            [&](Snapshot value) { observations.receive(std::move(value)); },
                            fixed_today()};
    auto snapshot = observations.settled();
    CHECK_FALSE(snapshot.loaded);
    CHECK_FALSE(snapshot.error.empty());
    CHECK(snapshot.tasks.empty());
    CHECK(snapshot.revision == 0);
    CHECK(snapshot.error.find(blocked.string()) == std::string::npos);
    REQUIRE(std::filesystem::remove(blocked));
    snapshot = perform(observations, [&] { return service.refresh(); });
    CHECK(snapshot.loaded);
    CHECK(snapshot.error.empty());
    CHECK(snapshot.tasks.empty());
}

TEST_CASE(
    "Refresh failures preserve the last committed list and recover without revision changes") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    Observations observations;
    CalendarService service{path, [&](Snapshot value) { observations.receive(std::move(value)); },
                            fixed_today()};
    REQUIRE(observations.settled().loaded);
    const auto committed = perform(observations, [&] { return service.create({"Keep", "", ""}); });
    {
        RawDatabase raw{path};
        raw.execute("ALTER TABLE tasks RENAME TO temporarily_unavailable_tasks");
    }
    auto snapshot = perform(observations, [&] { return service.select_filter(Filter::completed); });
    CHECK_FALSE(snapshot.error.empty());
    CHECK(snapshot.loaded);
    CHECK(snapshot.tasks == committed.tasks);
    CHECK(snapshot.filter == Filter::today);
    CHECK(snapshot.today_count == 1);
    CHECK(snapshot.revision == 1);
    {
        RawDatabase raw{path};
        raw.execute("ALTER TABLE temporarily_unavailable_tasks RENAME TO tasks");
    }
    snapshot = perform(observations, [&] { return service.refresh(); });
    CHECK(snapshot.error.empty());
    CHECK(snapshot.tasks == committed.tasks);
    CHECK(snapshot.revision == 1);
}

TEST_CASE(
    "Bounded FIFO rejects overflow without coalescing and shutdown drains all accepted writes") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    Observations observations;
    observations.hold_loaded = true;
    auto service = std::make_unique<CalendarService>(
        path, [&](Snapshot value) { observations.receive(std::move(value)); }, fixed_today());
    Observations::ReleaseOnExit release{observations};
    REQUIRE(observations.await_held());
    for (int index = 0; index < 64; ++index) {
        REQUIRE(service->create({"FIFO " + std::to_string(index), "", ""}));
    }
    CHECK_FALSE(service->create({"Rejected overflow", "", ""}));
    CHECK_FALSE(service->refresh());
    std::jthread destroyer{[owned = std::move(service)]() mutable { owned.reset(); }};
    observations.release();
    destroyer.join();
    {
        const std::lock_guard lock{observations.mutex};
        std::uint64_t revision = 0;
        for (const auto &snapshot : observations.snapshots) {
            if (!snapshot.busy && snapshot.change == ChangeKind::created) {
                ++revision;
                CHECK(snapshot.revision == revision);
                CHECK(snapshot.changed_id == static_cast<TaskId>(revision));
                CHECK(snapshot.today_count == static_cast<std::int64_t>(revision));
            }
        }
        CHECK(revision == 64);
    }
    Observations restored;
    CalendarService reopened{path, [&](Snapshot value) { restored.receive(std::move(value)); },
                             fixed_today()};
    auto snapshot = restored.settled();
    REQUIRE(snapshot.tasks.size() == 50);
    CHECK(snapshot.today_count == 64);
    CHECK(snapshot.tasks.front().title == "FIFO 0");
    CHECK(snapshot.tasks.back().title == "FIFO 49");
    snapshot = perform(restored, [&] { return reopened.page(1); });
    REQUIRE(snapshot.tasks.size() == 14);
    CHECK(snapshot.tasks.front().title == "FIFO 50");
    CHECK(snapshot.tasks.back().title == "FIFO 63");
}

TEST_CASE("A throwing observer cannot abandon later accepted commands") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    {
        CalendarService service{
            path, [](Snapshot) { throw std::runtime_error("private callback"); }, fixed_today()};
        REQUIRE(service.create({"Persist despite observer", "", ""}));
        REQUIRE(service.create({"And keep order", "", ""}));
    }
    Observations observations;
    CalendarService restored{path, [&](Snapshot value) { observations.receive(std::move(value)); },
                             fixed_today()};
    const auto snapshot = observations.settled();
    REQUIRE(snapshot.tasks.size() == 2);
    CHECK(snapshot.tasks[0].title == "Persist despite observer");
    CHECK(snapshot.tasks[1].title == "And keep order");
}
