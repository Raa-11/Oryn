#include <ostream>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <doctest/doctest.h>
#include <sqlite3.h>
import oryn.database;

using namespace oryn::database;

namespace {
    class TemporaryDirectory {
    public:
        TemporaryDirectory() {
            static std::atomic<unsigned> sequence{0};
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            for (;;) {
                path = std::filesystem::temp_directory_path() /
                       ("oryn-database-" + std::to_string(stamp) + "-" +
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

        int scalar(const char *sql) {
            sqlite3_stmt *raw = nullptr;
            REQUIRE(sqlite3_prepare_v2(connection, sql, -1, &raw, nullptr) == SQLITE_OK);
            const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement{
                raw, sqlite3_finalize};
            REQUIRE(sqlite3_step(raw) == SQLITE_ROW);
            return sqlite3_column_int(raw, 0);
        }

        std::string text(const char *sql) {
            sqlite3_stmt *raw = nullptr;
            REQUIRE(sqlite3_prepare_v2(connection, sql, -1, &raw, nullptr) == SQLITE_OK);
            const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement{
                raw, sqlite3_finalize};
            REQUIRE(sqlite3_step(raw) == SQLITE_ROW);
            return reinterpret_cast<const char *>(sqlite3_column_text(raw, 0));
        }

        sqlite3 *connection = nullptr;
    };

    void create_version_one(RawDatabase &raw) {
        raw.execute("CREATE TABLE settings ("
                    "id INTEGER NOT NULL PRIMARY KEY CHECK(id=1),"
                    "light_mode INTEGER NOT NULL CHECK(light_mode IN (0,1)),"
                    "reduce_motion INTEGER NOT NULL CHECK(reduce_motion IN (0,1)),"
                    "always_on_top INTEGER NOT NULL CHECK(always_on_top IN (0,1)),"
                    "expanded INTEGER NOT NULL CHECK(expanded IN (0,1)),"
                    "remember_position INTEGER NOT NULL CHECK(remember_position IN (0,1)),"
                    "window_x INTEGER NOT NULL CHECK(window_x BETWEEN -1000000 AND 1000000),"
                    "window_y INTEGER NOT NULL CHECK(window_y BETWEEN -1000000 AND 1000000),"
                    "focus_minutes INTEGER NOT NULL CHECK(focus_minutes BETWEEN 1 AND 180),"
                    "short_minutes INTEGER NOT NULL CHECK(short_minutes BETWEEN 1 AND 180),"
                    "long_minutes INTEGER NOT NULL CHECK(long_minutes BETWEEN 1 AND 180)"
                    ") STRICT;"
                    "INSERT INTO settings VALUES(1,1,1,0,0,1,-27,45,90,12,30);"
                    "PRAGMA user_version=1;");
    }
} // namespace

TEST_CASE("Empty database migrates once to validated singleton settings in WAL") {
    TemporaryDirectory directory;
    const auto path = directory.path / "nested" / "settings.sqlite3";
    {
        auto opened = Database::open(path);
        REQUIRE(opened.has_value());
        auto loaded = (*opened)->load();
        REQUIRE(loaded.has_value());
        CHECK(*loaded == SettingsRecord{});
    }
    RawDatabase raw{path};
    CHECK(raw.scalar("PRAGMA user_version") == 2);
    CHECK(raw.text("PRAGMA journal_mode") == "wal");
    CHECK(raw.scalar("SELECT count(*) FROM settings") == 1);
    CHECK(raw.scalar("SELECT id FROM settings") == 1);
    CHECK(raw.scalar("SELECT strict FROM pragma_table_list WHERE name='settings'") == 1);
    CHECK(sqlite3_exec(raw.connection, "UPDATE settings SET focus_minutes=0", nullptr, nullptr,
                       nullptr) == SQLITE_CONSTRAINT);
    CHECK(sqlite3_exec(raw.connection, "UPDATE settings SET light_mode=2", nullptr, nullptr,
                       nullptr) == SQLITE_CONSTRAINT);
    CHECK(sqlite3_exec(raw.connection, "UPDATE settings SET window_x=-1000001", nullptr, nullptr,
                       nullptr) == SQLITE_CONSTRAINT);
    CHECK(sqlite3_exec(raw.connection, "UPDATE settings SET short_minutes='invalid'", nullptr,
                       nullptr, nullptr) == SQLITE_CONSTRAINT);
    auto reopened = Database::open(path);
    REQUIRE(reopened.has_value());
    CHECK((*reopened)->load().value() == SettingsRecord{});
}

TEST_CASE("Every preference roundtrips and invalid saves preserve the committed row") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    SettingsRecord requested{true, true, false, false, true, -1000000, 1000000, 180, 1, 60};
    {
        auto opened = Database::open(path);
        REQUIRE(opened.has_value());
        REQUIRE((*opened)->save(requested).has_value());
        auto invalid = requested;
        invalid.long_minutes = 181;
        auto rejected = (*opened)->save(invalid);
        REQUIRE_FALSE(rejected.has_value());
        CHECK(rejected.error().kind == ErrorKind::invalid_settings);
        invalid = requested;
        invalid.window_x = -1000001;
        CHECK_FALSE((*opened)->save(invalid).has_value());
        CHECK((*opened)->load().value() == requested);
    }
    auto reopened = Database::open(path);
    REQUIRE(reopened.has_value());
    CHECK((*reopened)->load().value() == requested);
}

TEST_CASE("Failed migration preserves schema version and preexisting user contents") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    {
        RawDatabase raw{path};
        raw.execute("CREATE TABLE settings(sentinel TEXT);"
                    "INSERT INTO settings VALUES('do-not-destroy'); PRAGMA user_version=0;");
    }
    auto opened = Database::open(path);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().kind == ErrorKind::sqlite);
    RawDatabase raw{path};
    CHECK(raw.scalar("PRAGMA user_version") == 0);
    CHECK(raw.text("SELECT sentinel FROM settings") == "do-not-destroy");
    CHECK(raw.scalar("SELECT count(*) FROM sqlite_schema WHERE type='table'") == 1);
}

TEST_CASE("Future schema is refused without changing its version journal or contents") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    {
        RawDatabase raw{path};
        raw.execute("CREATE TABLE future_data(value TEXT);"
                    "INSERT INTO future_data VALUES('future-owner'); PRAGMA user_version=99;");
        CHECK(raw.text("PRAGMA journal_mode") == "delete");
    }
    auto opened = Database::open(path);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().kind == ErrorKind::unsupported_version);
    RawDatabase raw{path};
    CHECK(raw.scalar("PRAGMA user_version") == 99);
    CHECK(raw.text("PRAGMA journal_mode") == "delete");
    CHECK(raw.text("SELECT value FROM future_data") == "future-owner");
    CHECK(raw.scalar("SELECT count(*) FROM sqlite_schema WHERE name='settings'") == 0);
}

TEST_CASE("Corrupt files and invalid persisted rows fail without resetting data") {
    TemporaryDirectory directory;
    const auto corrupt = directory.path / "corrupt.sqlite3";
    const std::string original = "not a sqlite database: preserve this content";
    {
        std::ofstream file{corrupt, std::ios::binary};
        file << original;
    }
    auto opened = Database::open(corrupt);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().kind == ErrorKind::sqlite);
    std::ifstream file{corrupt, std::ios::binary};
    const std::string after{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    CHECK(after == original);

    const auto invalid = directory.path / "invalid.sqlite3";
    {
        auto valid = Database::open(invalid);
        REQUIRE(valid.has_value());
    }
    {
        RawDatabase raw{invalid};
        raw.execute("PRAGMA ignore_check_constraints=ON; UPDATE settings SET reduce_motion=2;");
    }
    auto invalid_database = Database::open(invalid);
    REQUIRE(invalid_database.has_value());
    auto rejected = (*invalid_database)->load();
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().kind == ErrorKind::invalid_settings);
    RawDatabase raw{invalid};
    CHECK(raw.scalar("SELECT reduce_motion FROM settings") == 2);
}

TEST_CASE("Missing row and wrong version-one schema are not silently recreated") {
    TemporaryDirectory directory;
    const auto missing = directory.path / "missing.sqlite3";
    {
        auto opened = Database::open(missing);
        REQUIRE(opened.has_value());
    }
    {
        RawDatabase raw{missing};
        raw.execute("DELETE FROM settings;");
    }
    auto missing_database = Database::open(missing);
    REQUIRE(missing_database.has_value());
    auto missing_result = (*missing_database)->load();
    REQUIRE_FALSE(missing_result.has_value());
    CHECK(missing_result.error().kind == ErrorKind::invalid_settings);

    const auto wrong = directory.path / "wrong.sqlite3";
    {
        RawDatabase raw{wrong};
        raw.execute("CREATE TABLE settings(id INTEGER PRIMARY KEY); PRAGMA user_version=1;");
    }
    auto wrong_result = Database::open(wrong);
    REQUIRE_FALSE(wrong_result.has_value());
    CHECK(wrong_result.error().kind == ErrorKind::invalid_schema);
}

TEST_CASE("Unusable database parent returns a typed filesystem failure") {
    TemporaryDirectory directory;
    const auto parent = directory.path / "ordinary-file";
    {
        std::ofstream file{parent};
        file << "preserve";
    }
    auto opened = Database::open(parent / "settings.sqlite3");
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().kind == ErrorKind::filesystem);
    CHECK(opened.error().code != 0);
}

TEST_CASE("Version one migrates to task storage without changing any preference") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    {
        RawDatabase raw{path};
        create_version_one(raw);
    }
    const SettingsRecord expected{true, true, false, false, true, -27, 45, 90, 12, 30};
    {
        auto opened = Database::open(path);
        REQUIRE(opened.has_value());
        CHECK((*opened)->load().value() == expected);
        CHECK((*opened)->count_tasks(TaskFilter::today, "2026-11-08").value() == 0);
        REQUIRE((*opened)->create_task({"Preserved", "", "2026-11-08"}).has_value());
    }
    RawDatabase raw{path};
    CHECK(raw.scalar("PRAGMA user_version") == 2);
    CHECK(raw.scalar("SELECT strict FROM pragma_table_list WHERE name='tasks'") == 1);
    CHECK(raw.scalar(
              "SELECT count(*) FROM sqlite_schema WHERE type='index' AND tbl_name='tasks'") == 2);
    auto reopened = Database::open(path);
    REQUIRE(reopened.has_value());
    CHECK((*reopened)->load().value() == expected);
    CHECK((*reopened)->count_tasks(TaskFilter::today, "2026-11-08").value() == 1);
}

TEST_CASE("All pending migrations roll back together when task migration fails") {
    TemporaryDirectory directory;
    for (const bool version_one : {false, true}) {
        const auto path = directory.path / (version_one ? "v1.sqlite3" : "v0.sqlite3");
        {
            RawDatabase raw{path};
            if (version_one) {
                create_version_one(raw);
            }
            raw.execute("CREATE TABLE tasks(sentinel TEXT);"
                        "INSERT INTO tasks VALUES('keep task owner');");
        }
        auto opened = Database::open(path);
        REQUIRE_FALSE(opened.has_value());
        CHECK(opened.error().kind == ErrorKind::sqlite);
        RawDatabase raw{path};
        CHECK(raw.scalar("PRAGMA user_version") == (version_one ? 1 : 0));
        CHECK(raw.text("SELECT sentinel FROM tasks") == "keep task owner");
        CHECK(raw.scalar("SELECT count(*) FROM sqlite_schema WHERE name='settings'") ==
              (version_one ? 1 : 0));
        if (version_one) {
            CHECK(raw.scalar("SELECT focus_minutes FROM settings") == 90);
            CHECK(raw.scalar("SELECT window_x FROM settings") == -27);
        }
    }
}

TEST_CASE("Version two requires a real strict task schema without repairing foreign contents") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    {
        RawDatabase raw{path};
        create_version_one(raw);
        raw.execute("CREATE TABLE tasks(sentinel TEXT);"
                    "INSERT INTO tasks VALUES('foreign contents'); PRAGMA user_version=2;");
    }
    auto opened = Database::open(path);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().kind == ErrorKind::invalid_schema);
    RawDatabase raw{path};
    CHECK(raw.scalar("PRAGMA user_version") == 2);
    CHECK(raw.text("SELECT sentinel FROM tasks") == "foreign contents");
    CHECK(raw.scalar("SELECT focus_minutes FROM settings") == 90);
}

TEST_CASE("Task CRUD preserves quotes Unicode notes status and UTC millisecond timestamps") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    TaskRecord committed;
    {
        auto opened = Database::open(path);
        REQUIRE(opened.has_value());
        auto &database = **opened;
        const auto before = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
        auto created = database.create_task({"O'Brien — 日本語'); DROP TABLE tasks;--",
                                             "line one\n\"quoted\" — café", "2024-02-29"});
        REQUIRE(created.has_value());
        const auto after = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        CHECK(created->id > 0);
        CHECK_FALSE(created->completed);
        CHECK(created->created_at >= before);
        CHECK(created->created_at <= after);
        CHECK(created->updated_at == created->created_at);
        auto completed = database.set_task_completed(created->id, true);
        REQUIRE(completed.has_value());
        CHECK(completed->completed);
        CHECK(completed->title == created->title);
        CHECK(completed->notes == created->notes);
        CHECK(completed->due_date == created->due_date);
        CHECK(completed->created_at == created->created_at);
        CHECK(completed->updated_at >= created->updated_at);
        auto edited = database.update_task(created->id, {"Revised 'title'", "", ""});
        REQUIRE(edited.has_value());
        CHECK(edited->completed);
        CHECK(edited->created_at == created->created_at);
        CHECK(edited->updated_at >= completed->updated_at);
        CHECK(edited->title == "Revised 'title'");
        CHECK(edited->notes.empty());
        CHECK(edited->due_date.empty());
        auto reopened = database.set_task_completed(created->id, false);
        REQUIRE(reopened.has_value());
        CHECK_FALSE(reopened->completed);
        committed = *reopened;
        CHECK(database.load().value() == SettingsRecord{});
    }
    auto opened = Database::open(path);
    REQUIRE(opened.has_value());
    auto &database = **opened;
    const auto tasks = database.list_tasks(TaskFilter::today, "2026-11-08", 0, 51);
    REQUIRE(tasks.has_value());
    REQUIRE(tasks->size() == 1);
    CHECK(tasks->front() == committed);
    REQUIRE(database.erase_task(committed.id).has_value());
    CHECK(database.count_tasks(TaskFilter::today, "2026-11-08").value() == 0);
    auto replacement = database.create_task({"New identity", "", ""});
    REQUIRE(replacement.has_value());
    CHECK(replacement->id > committed.id);
}

TEST_CASE("Task filters include overdue and unscheduled work with stable date then id ordering") {
    TemporaryDirectory directory;
    auto opened = Database::open(directory.path / "settings.sqlite3");
    REQUIRE(opened.has_value());
    auto &database = **opened;
    const auto undated = database.create_task({"Undated", "", ""}).value();
    const auto future = database.create_task({"Future", "", "2026-11-09"}).value();
    const auto today_a = database.create_task({"Today A", "", "2026-11-08"}).value();
    const auto overdue = database.create_task({"Overdue", "", "2026-11-07"}).value();
    const auto today_b = database.create_task({"Today B", "", "2026-11-08"}).value();
    const auto later = database.create_task({"Later", "", "2027-01-01"}).value();
    auto today = database.list_tasks(TaskFilter::today, "2026-11-08", 0, 51).value();
    REQUIRE(today.size() == 4);
    CHECK(today[0].id == overdue.id);
    CHECK(today[1].id == today_a.id);
    CHECK(today[2].id == today_b.id);
    CHECK(today[3].id == undated.id);
    auto upcoming = database.list_tasks(TaskFilter::upcoming, "2026-11-08", 0, 51).value();
    REQUIRE(upcoming.size() == 2);
    CHECK(upcoming[0].id == future.id);
    CHECK(upcoming[1].id == later.id);
    auto page = database.list_tasks(TaskFilter::today, "2026-11-08", 1, 2).value();
    REQUIRE(page.size() == 2);
    CHECK(page[0].id == today_a.id);
    CHECK(page[1].id == today_b.id);
    CHECK(database.list_tasks(TaskFilter::today, "2026-11-08", 4, 1).value().empty());
    CHECK(database.count_tasks(TaskFilter::today, "2026-11-08").value() == 4);
    CHECK(database.count_tasks(TaskFilter::upcoming, "2026-11-08").value() == 2);
    CHECK(database.count_tasks(TaskFilter::completed, "2026-11-08").value() == 0);
    CHECK(database.count_tasks(TaskFilter::today, "2026-11-09").value() == 5);
    REQUIRE(database.set_task_completed(undated.id, true).has_value());
    REQUIRE(database.set_task_completed(later.id, true).has_value());
    REQUIRE(database.set_task_completed(today_b.id, true).has_value());
    REQUIRE(database.set_task_completed(today_a.id, true).has_value());
    auto completed = database.list_tasks(TaskFilter::completed, "2026-11-08", 0, 51).value();
    REQUIRE(completed.size() == 4);
    CHECK(completed[0].id == today_a.id);
    CHECK(completed[1].id == today_b.id);
    CHECK(completed[2].id == later.id);
    CHECK(completed[3].id == undated.id);
    CHECK(database.count_tasks(TaskFilter::today, "2026-11-08").value() == 1);
    CHECK(database.count_tasks(TaskFilter::upcoming, "2026-11-08").value() == 1);
    CHECK(database.count_tasks(TaskFilter::completed, "1900-01-01").value() == 4);
}

TEST_CASE("Task inputs require real canonical Gregorian dates and bounded pages") {
    TemporaryDirectory directory;
    auto opened = Database::open(directory.path / "settings.sqlite3");
    REQUIRE(opened.has_value());
    auto &database = **opened;
    for (const auto date : {"", "0001-01-01", "2000-02-29", "2024-02-29", "9999-12-31"}) {
        REQUIRE(database.create_task({"Valid", "", date}).has_value());
    }
    for (const auto date : {"0000-01-01", "1900-02-29", "2100-02-29", "2025-02-29", "2026-04-31",
                            "2026-00-01", "2026-13-01", "2026-01-00", "2026-01-32", "2026-1-01",
                            "26-01-01", "2026-01-01 ", "2026-01-01T00:00:00Z", "2026-aa-01"}) {
        auto rejected = database.create_task({"Invalid date", "", date});
        REQUIRE_FALSE(rejected.has_value());
        CHECK(rejected.error().kind == ErrorKind::invalid_task);
    }
    for (const auto title : {"", " ", "\t\r\n\f\v"}) {
        auto rejected = database.create_task({title, "notes", ""});
        REQUIRE_FALSE(rejected.has_value());
        CHECK(rejected.error().kind == ErrorKind::invalid_task);
    }
    auto existing = database.create_task({"Keep original", "keep notes", "2026-11-08"}).value();
    CHECK(database.update_task(existing.id, {"", "", ""}).error().kind == ErrorKind::invalid_task);
    CHECK(database.update_task(existing.id, {"Title", "", "2026-02-30"}).error().kind ==
          ErrorKind::invalid_task);
    CHECK(database.list_tasks(TaskFilter::today, "2026-11-08", 0, 0).error().kind ==
          ErrorKind::invalid_task);
    CHECK(database.list_tasks(TaskFilter::today, "2026-11-08", 0, 52).error().kind ==
          ErrorKind::invalid_task);
    CHECK(database.list_tasks(TaskFilter::today, "2026-11-08", 0, 51).has_value());
    CHECK(database.list_tasks(TaskFilter::today, "2026-02-30", 0, 1).error().kind ==
          ErrorKind::invalid_task);
    CHECK(database.count_tasks(TaskFilter::completed, "").error().kind == ErrorKind::invalid_task);
    CHECK(database.count_tasks(static_cast<TaskFilter>(99), "2026-11-08").error().kind ==
          ErrorKind::invalid_task);
    CHECK(database.list_tasks(static_cast<TaskFilter>(99), "2026-11-08", 0, 1).error().kind ==
          ErrorKind::invalid_task);
    if (std::numeric_limits<std::size_t>::max() >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        CHECK(database
                  .list_tasks(TaskFilter::today, "2026-11-08",
                              std::numeric_limits<std::size_t>::max(), 1)
                  .error()
                  .kind == ErrorKind::invalid_task);
    }
    CHECK(database.update_task(existing.id, {existing.title, existing.notes, existing.due_date})
              .value()
              .title == existing.title);
}

TEST_CASE("Missing task mutations return typed errors and leave other rows intact") {
    TemporaryDirectory directory;
    auto opened = Database::open(directory.path / "settings.sqlite3");
    REQUIRE(opened.has_value());
    auto &database = **opened;
    const auto existing = database.create_task({"Original", "notes", ""}).value();
    const auto missing = existing.id + 1;
    CHECK(database.update_task(missing, {"No row", "", ""}).error().kind == ErrorKind::not_found);
    CHECK(database.set_task_completed(missing, true).error().kind == ErrorKind::not_found);
    CHECK(database.erase_task(missing).error().kind == ErrorKind::not_found);
    for (const auto id : {0, -1}) {
        CHECK(database.update_task(id, {"No row", "", ""}).error().kind == ErrorKind::invalid_task);
        CHECK(database.set_task_completed(id, true).error().kind == ErrorKind::invalid_task);
        CHECK(database.erase_task(id).error().kind == ErrorKind::invalid_task);
    }
    CHECK(database.list_tasks(TaskFilter::today, "2026-11-08", 0, 51).value().front() == existing);
    REQUIRE(database.erase_task(existing.id).has_value());
    CHECK(database.erase_task(existing.id).error().kind == ErrorKind::not_found);
}

TEST_CASE("Task mutation failures roll back both the task and trigger side effects") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    auto opened = Database::open(path);
    REQUIRE(opened.has_value());
    auto &database = **opened;
    RawDatabase raw{path};
    raw.execute(
        "CREATE TRIGGER reject_create AFTER INSERT ON tasks BEGIN "
        "UPDATE settings SET focus_minutes=99; SELECT RAISE(FAIL,'injected failure'); END;");
    auto failed = database.create_task({"Must roll back", "", ""});
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().kind == ErrorKind::sqlite);
    CHECK(database.count_tasks(TaskFilter::today, "2026-11-08").value() == 0);
    CHECK(database.load().value() == SettingsRecord{});
    raw.execute("DROP TRIGGER reject_create;");
    const auto existing =
        database.create_task({"Original", "original notes", "2026-11-08"}).value();
    raw.execute(
        "CREATE TRIGGER reject_update AFTER UPDATE ON tasks BEGIN "
        "UPDATE settings SET focus_minutes=99; SELECT RAISE(FAIL,'injected failure'); END;");
    CHECK(database.update_task(existing.id, {"Changed", "", ""}).error().kind == ErrorKind::sqlite);
    CHECK(database.set_task_completed(existing.id, true).error().kind == ErrorKind::sqlite);
    CHECK(database.list_tasks(TaskFilter::today, "2026-11-08", 0, 51).value().front() == existing);
    CHECK(database.load().value() == SettingsRecord{});
    raw.execute(
        "DROP TRIGGER reject_update;"
        "CREATE TRIGGER reject_delete AFTER DELETE ON tasks BEGIN "
        "UPDATE settings SET focus_minutes=99; SELECT RAISE(FAIL,'injected failure'); END;");
    CHECK(database.erase_task(existing.id).error().kind == ErrorKind::sqlite);
    CHECK(database.list_tasks(TaskFilter::today, "2026-11-08", 0, 51).value().front() == existing);
    CHECK(database.load().value() == SettingsRecord{});
    raw.execute("DROP TRIGGER reject_delete;");
    REQUIRE(database.set_task_completed(existing.id, true).has_value());
    REQUIRE(database.erase_task(existing.id).has_value());
}

TEST_CASE("Task schema constraints and row validation reject corrupt data without reset") {
    TemporaryDirectory directory;
    const auto path = directory.path / "settings.sqlite3";
    auto opened = Database::open(path);
    REQUIRE(opened.has_value());
    auto &database = **opened;
    REQUIRE(database.create_task({"Valid", "", "2026-11-08"}).has_value());
    RawDatabase raw{path};
    for (const auto sql :
         {"UPDATE tasks SET title='  '", "UPDATE tasks SET due_date='2026-02-30'",
          "UPDATE tasks SET due_date='1900-02-29'", "UPDATE tasks SET completed=2",
          "UPDATE tasks SET notes=NULL", "UPDATE tasks SET updated_at=created_at-1"}) {
        CHECK(sqlite3_exec(raw.connection, sql, nullptr, nullptr, nullptr) == SQLITE_CONSTRAINT);
    }
    raw.execute("PRAGMA ignore_check_constraints=ON;");
    for (const auto sql :
         {"UPDATE tasks SET due_date='2026-02-30'", "UPDATE tasks SET title='  '",
          "UPDATE tasks SET title=CAST(X'80' AS TEXT)",
          "UPDATE tasks SET notes=CAST(X'EDA080' AS TEXT)", "UPDATE tasks SET notes=char(0)",
          "UPDATE tasks SET title=replace(hex(zeroblob(121)),'0','t')",
          "UPDATE tasks SET notes=hex(zeroblob(2049))", "UPDATE tasks SET created_at=-1",
          "UPDATE tasks SET updated_at=created_at-1"}) {
        raw.execute(sql);
        auto rejected = database.list_tasks(TaskFilter::today, "2026-11-08", 0, 51);
        REQUIRE_FALSE(rejected.has_value());
        CHECK(rejected.error().kind == ErrorKind::invalid_task);
        raw.execute("UPDATE tasks SET title='Valid',notes='',due_date='2026-11-08',"
                    "created_at=0,updated_at=0;");
    }
    raw.execute("UPDATE tasks SET completed=2;");
    CHECK(database.update_task(1, {"Repair must not silently happen", "", ""}).error().kind ==
          ErrorKind::invalid_task);
    CHECK(raw.scalar("SELECT completed FROM tasks") == 2);
    raw.execute("UPDATE tasks SET completed=0;");
    CHECK(database.load().value() == SettingsRecord{});
}

TEST_CASE("Task text limits count UTF8 bytes and reject NUL and malformed encoding") {
    TemporaryDirectory directory;
    auto opened = Database::open(directory.path / "settings.sqlite3");
    REQUIRE(opened.has_value());
    auto &database = **opened;
    const TaskDraft boundary{std::string(240, 't'), std::string(4096, 'n'), ""};
    const auto existing = database.create_task(boundary);
    REQUIRE(existing.has_value());
    CHECK(existing->title == boundary.title);
    CHECK(existing->notes == boundary.notes);
    std::string unicode_boundary;
    for (int index = 0; index < 60; ++index) {
        unicode_boundary += "\xf0\x9f\x8c\x8d";
    }
    REQUIRE(unicode_boundary.size() == 240);
    REQUIRE(database.create_task({unicode_boundary, "", ""}).has_value());
    CHECK(database.create_task({"  Keep surrounding space  ", "", ""}).value().title ==
          "  Keep surrounding space  ");
    for (const auto &draft :
         {TaskDraft{std::string(241, 't'), "", ""}, TaskDraft{"Title", std::string(4097, 'n'), ""},
          TaskDraft{unicode_boundary + "x", "", ""},
          TaskDraft{std::string{"title\0suffix", 12}, "", ""},
          TaskDraft{"Title", std::string{"note\0suffix", 11}, ""}}) {
        auto rejected = database.create_task(draft);
        REQUIRE_FALSE(rejected.has_value());
        CHECK(rejected.error().kind == ErrorKind::invalid_task);
        CHECK(database.update_task(existing->id, draft).error().kind == ErrorKind::invalid_task);
    }
    for (const auto &malformed : {std::string{"\x80"}, std::string{"\xc0\xaf"}, std::string{"\xc2"},
                                  std::string{"\xe0\x80\xaf"}, std::string{"\xed\xa0\x80"},
                                  std::string{"\xf0\x80\x80\xaf"}, std::string{"\xf4\x90\x80\x80"},
                                  std::string{"\xf5\x80\x80\x80"}, std::string{"\xc2\x20"}}) {
        CHECK(database.create_task({malformed, "", ""}).error().kind == ErrorKind::invalid_task);
        CHECK(database.create_task({"Title", malformed, ""}).error().kind ==
              ErrorKind::invalid_task);
    }
    auto unchanged = database.list_tasks(TaskFilter::today, "2026-11-08", 0, 51).value();
    REQUIRE(unchanged.size() == 3);
    CHECK(unchanged.front() == *existing);
}

TEST_CASE("Task pages use a fifty row page plus one lookahead without overlap or gaps") {
    TemporaryDirectory directory;
    auto opened = Database::open(directory.path / "settings.sqlite3");
    REQUIRE(opened.has_value());
    auto &database = **opened;
    std::int64_t first_id = 0;
    for (int index = 0; index < 55; ++index) {
        auto created = database.create_task({"Task " + std::to_string(index), "", "2026-11-08"});
        REQUIRE(created.has_value());
        if (index == 0) {
            first_id = created->id;
        }
    }
    auto first = database.list_tasks(TaskFilter::today, "2026-11-08", 0, 51).value();
    auto next = database.list_tasks(TaskFilter::today, "2026-11-08", 50, 51).value();
    REQUIRE(first.size() == 51);
    REQUIRE(next.size() == 5);
    for (std::size_t index = 0; index < first.size(); ++index) {
        CHECK(first[index].id == first_id + static_cast<std::int64_t>(index));
    }
    CHECK(next.front() == first.back());
    CHECK(next.back().id == first_id + 54);
    CHECK(database.count_tasks(TaskFilter::today, "2026-11-08").value() == 55);
}
