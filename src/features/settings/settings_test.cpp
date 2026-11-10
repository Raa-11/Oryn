#include <ostream>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include <sqlite3.h>
import oryn.settings;
import oryn.database;

using namespace std::chrono_literals;
using namespace oryn::settings;

namespace {
    class TemporaryDirectory {
    public:
        TemporaryDirectory() {
            static std::atomic<unsigned> sequence{0};
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (;;) {
                path = std::filesystem::temp_directory_path() /
                       ("oryn-settings-" + std::to_string(stamp) + "-" +
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

    class Observations {
    public:
        void receive(Snapshot snapshot) {
            std::unique_lock lock{mutex};
            worker_thread = std::this_thread::get_id();
            const bool block = hold_initial || (hold_saving && snapshot.saving);
            snapshots.push_back(std::move(snapshot));
            if (block) {
                held = true;
                changed.notify_all();
                changed.wait(lock, [this] { return released; });
                hold_initial = false;
                hold_saving = false;
            }
            changed.notify_all();
        }

        bool await(const std::function<bool(const std::vector<Snapshot> &)> &predicate) {
            std::unique_lock lock{mutex};
            return changed.wait_for(lock, 5s, [&] { return predicate(snapshots); });
        }

        bool await_held() {
            std::unique_lock lock{mutex};
            return changed.wait_for(lock, 5s, [this] { return held; });
        }

        void release() {
            {
                const std::lock_guard lock{mutex};
                released = true;
            }
            changed.notify_all();
        }

        // If a REQUIRE aborts before explicit release, do not strand the worker at a gate.
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
        bool hold_initial = false;
        bool hold_saving = false;
        bool held = false;
        bool released = false;
    };

    bool loaded(const std::vector<Snapshot> &snapshots) {
        return !snapshots.empty() && snapshots.back().loaded && !snapshots.back().saving;
    }

    bool committed(const std::vector<Snapshot> &snapshots, const Preferences &expected) {
        return loaded(snapshots) && snapshots.back().error.empty() &&
               snapshots.back().preferences == expected;
    }
} // namespace

TEST_CASE("Settings persist across service construction and callbacks stay off caller thread") {
    TemporaryDirectory directory;
    const auto path = directory.path / "nested" / "settings.sqlite3";
    const auto caller = std::this_thread::get_id();
    const Preferences requested{true, true, false, false, true, -1920, -200, 45, 8, 24};
    {
        Observations observations;
        SettingsService service{
            path, [&](Snapshot snapshot) { observations.receive(std::move(snapshot)); }};
        REQUIRE(observations.await(loaded));
        {
            const std::lock_guard lock{observations.mutex};
            CHECK(observations.worker_thread != caller);
            CHECK(observations.snapshots.back().preferences == Preferences{});
        }
        service.save(requested);
        REQUIRE(
            observations.await([&](const auto &values) { return committed(values, requested); }));
    }
    Observations restored;
    SettingsService service{path,
                            [&](Snapshot snapshot) { restored.receive(std::move(snapshot)); }};
    REQUIRE(restored.await([&](const auto &values) { return committed(values, requested); }));
}

TEST_CASE("Latest save is bounded and drained on destruction without sleeps") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    Observations observations;
    observations.hold_initial = true;
    auto service = std::make_unique<SettingsService>(
        path, [&](Snapshot snapshot) { observations.receive(std::move(snapshot)); });
    Observations::ReleaseOnExit release{observations};
    REQUIRE(observations.await_held());
    Preferences latest;
    for (int index = 1; index <= 1000; ++index) {
        latest.window_x = -index;
        latest.focus_minutes = index % 180 + 1;
        latest.light_mode = index % 2 != 0;
        service->save(latest);
    }
    // The worker is held in its initial callback; exactly one fixed latest slot is pending.
    std::jthread destroyer{[owned = std::move(service)]() mutable { owned.reset(); }};
    observations.release();
    destroyer.join();
    int saves = 0;
    for (const auto &snapshot : observations.snapshots) {
        saves += snapshot.saving ? 1 : 0;
    }
    CHECK(saves == 1);
    REQUIRE_FALSE(observations.snapshots.empty());
    CHECK(observations.snapshots.back().preferences == latest);
    CHECK(observations.snapshots.back().error.empty());
    auto database = oryn::database::Database::open(path);
    REQUIRE(database.has_value());
    auto persisted = (*database)->load();
    REQUIRE(persisted.has_value());
    CHECK(persisted->window_x == latest.window_x);
    CHECK(persisted->focus_minutes == latest.focus_minutes);
}

TEST_CASE("Destruction drains an in-flight save and the latest replacement") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    Observations observations;
    observations.hold_saving = true;
    auto service = std::make_unique<SettingsService>(
        path, [&](Snapshot snapshot) { observations.receive(std::move(snapshot)); });
    Observations::ReleaseOnExit release{observations};
    REQUIRE(observations.await(loaded));
    Preferences first;
    first.focus_minutes = 35;
    service->save(first);
    REQUIRE(observations.await_held());
    Preferences latest = first;
    latest.light_mode = true;
    latest.focus_minutes = 60;
    service->save(latest);
    latest.focus_minutes = 90;
    service->save(latest);
    std::jthread destroyer{[owned = std::move(service)]() mutable { owned.reset(); }};
    observations.release();
    destroyer.join();
    int saves = 0;
    for (const auto &snapshot : observations.snapshots) {
        saves += snapshot.saving ? 1 : 0;
    }
    CHECK(saves == 2);
    CHECK(observations.snapshots.back().preferences == latest);
    auto database = oryn::database::Database::open(path);
    REQUIRE(database.has_value());
    CHECK((*database)->load().value().focus_minutes == 90);
}

TEST_CASE("Save accepted before initial load wins over the persisted startup values") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    {
        auto database = oryn::database::Database::open(path);
        REQUIRE(database.has_value());
    }
    RawDatabase lock{path};
    lock.execute("BEGIN IMMEDIATE;");
    Observations observations;
    SettingsService service{path,
                            [&](Snapshot snapshot) { observations.receive(std::move(snapshot)); }};
    Preferences requested;
    requested.light_mode = true;
    requested.window_x = -1280;
    service.save(requested);
    lock.execute("ROLLBACK;");
    REQUIRE(observations.await([&](const auto &values) { return committed(values, requested); }));
    const std::lock_guard guard{observations.mutex};
    REQUIRE_FALSE(observations.snapshots.empty());
    CHECK(observations.snapshots.front().preferences == requested);
    CHECK(observations.snapshots.front().saving);
}

TEST_CASE("Restore defaults is persisted and invalid preferences report failure") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    Observations observations;
    SettingsService service{path,
                            [&](Snapshot snapshot) { observations.receive(std::move(snapshot)); }};
    REQUIRE(observations.await(loaded));
    Preferences changed;
    changed.light_mode = true;
    changed.long_minutes = 40;
    service.save(changed);
    REQUIRE(observations.await([&](const auto &values) { return committed(values, changed); }));
    Preferences invalid = changed;
    invalid.focus_minutes = 0;
    service.save(invalid);
    REQUIRE(observations.await([](const auto &values) { return !values.back().error.empty(); }));
    {
        const std::lock_guard lock{observations.mutex};
        CHECK(observations.snapshots.back().preferences == changed);
        CHECK_FALSE(observations.snapshots.back().saving);
    }
    invalid = changed;
    invalid.window_y = 1000001;
    service.save(invalid);
    service.restore_defaults();
    REQUIRE(
        observations.await([](const auto &values) { return committed(values, Preferences{}); }));
    auto database = oryn::database::Database::open(path);
    REQUIRE(database.has_value());
    CHECK((*database)->load().value() == oryn::database::SettingsRecord{});
}

TEST_CASE("Failed save publishes an error and preserves committed preferences without SQL values") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    Observations observations;
    SettingsService service{path,
                            [&](Snapshot snapshot) { observations.receive(std::move(snapshot)); }};
    REQUIRE(observations.await(loaded));
    {
        RawDatabase raw{path};
        raw.execute("CREATE TRIGGER reject_save BEFORE UPDATE ON settings "
                    "BEGIN SELECT RAISE(ABORT,'sensitive-canary'); END;");
    }
    Preferences requested;
    requested.light_mode = true;
    service.save(requested);
    REQUIRE(observations.await([](const auto &values) { return !values.back().error.empty(); }));
    {
        const std::lock_guard lock{observations.mutex};
        const auto &snapshot = observations.snapshots.back();
        CHECK(snapshot.loaded);
        CHECK_FALSE(snapshot.saving);
        CHECK(snapshot.preferences == Preferences{});
        CHECK(snapshot.error.find("sensitive-canary") == std::string::npos);
        CHECK(snapshot.error.find("write settings preferences") != std::string::npos);
    }
    auto database = oryn::database::Database::open(path);
    REQUIRE(database.has_value());
    CHECK((*database)->load().value() == oryn::database::SettingsRecord{});
}

TEST_CASE("Failed initial open publishes safe defaults and never replaces a corrupt file") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    const std::string original = "not-sqlite-preserve-user-file";
    {
        std::ofstream file{path, std::ios::binary};
        file << original;
    }
    Observations observations;
    {
        SettingsService service{
            path, [&](Snapshot snapshot) { observations.receive(std::move(snapshot)); }};
        REQUIRE(observations.await([](const auto &values) { return !values.empty(); }));
        {
            const std::lock_guard lock{observations.mutex};
            CHECK_FALSE(observations.snapshots.back().loaded);
            CHECK_FALSE(observations.snapshots.back().error.empty());
            CHECK(observations.snapshots.back().preferences == Preferences{});
        }
        Preferences requested;
        requested.light_mode = true;
        service.save(requested);
        REQUIRE(observations.await([](const auto &values) { return values.size() >= 2; }));
    }
    CHECK_FALSE(observations.snapshots.back().loaded);
    CHECK_FALSE(observations.snapshots.back().error.empty());
    std::ifstream file{path, std::ios::binary};
    const std::string after{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    CHECK(after == original);
}

TEST_CASE("Future schema failure is asynchronous and cannot be overwritten by restore defaults") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    {
        RawDatabase raw{path};
        raw.execute("PRAGMA user_version=99;");
    }
    Observations observations;
    {
        SettingsService service{
            path, [&](Snapshot snapshot) { observations.receive(std::move(snapshot)); }};
        REQUIRE(observations.await([](const auto &values) { return !values.empty(); }));
        service.restore_defaults();
    }
    CHECK_FALSE(observations.snapshots.back().loaded);
    CHECK(observations.snapshots.back().error.find("newer Oryn version") != std::string::npos);
    auto database = oryn::database::Database::open(path);
    REQUIRE_FALSE(database.has_value());
    CHECK(database.error().kind == oryn::database::ErrorKind::unsupported_version);
}
