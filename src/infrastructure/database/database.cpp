module;
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <sqlite3.h>

module oryn.database;

namespace oryn::database {
    namespace {
        constexpr int current_schema_version = 2;

        struct ConnectionDeleter {
            void operator()(sqlite3 *connection) const noexcept {
                sqlite3_close_v2(connection);
            }
        };

        struct StatementDeleter {
            void operator()(sqlite3_stmt *statement) const noexcept {
                sqlite3_finalize(statement);
            }
        };

        using Connection = std::unique_ptr<sqlite3, ConnectionDeleter>;
        using Statement = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

        Error sqlite_error(sqlite3 *connection, std::string_view operation) {
            return {ErrorKind::sqlite, operation, sqlite3_extended_errcode(connection)};
        }

        std::expected<Statement, Error> prepare(sqlite3 *connection, const char *sql,
                                                std::string_view operation) {
            sqlite3_stmt *raw = nullptr;
            const int code = sqlite3_prepare_v2(connection, sql, -1, &raw, nullptr);
            Statement statement{raw};
            if (code != SQLITE_OK) {
                return std::unexpected(sqlite_error(connection, operation));
            }
            return std::move(statement);
        }

        std::expected<void, Error> execute(sqlite3 *connection, const char *sql,
                                           std::string_view operation) {
            if (sqlite3_exec(connection, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
                return std::unexpected(sqlite_error(connection, operation));
            }
            return {};
        }

        // Construct only after BEGIN succeeds; rollback never masks the original failure.
        class Transaction {
        public:
            explicit Transaction(sqlite3 *connection) : connection_(connection) {
            }

            ~Transaction() {
                if (!committed_) {
                    sqlite3_exec(connection_, "ROLLBACK", nullptr, nullptr, nullptr);
                }
            }

            Transaction(const Transaction &) = delete;
            Transaction &operator=(const Transaction &) = delete;
            Transaction(Transaction &&) = delete;
            Transaction &operator=(Transaction &&) = delete;

            std::expected<void, Error> commit() {
                auto result = execute(connection_, "COMMIT", "commit database transaction");
                committed_ = result.has_value();
                return result;
            }

        private:
            sqlite3 *connection_;
            bool committed_ = false;
        };

        bool valid(const SettingsRecord &settings) {
            return settings.focus_minutes >= 1 && settings.focus_minutes <= 180 &&
                   settings.short_minutes >= 1 && settings.short_minutes <= 180 &&
                   settings.long_minutes >= 1 && settings.long_minutes <= 180 &&
                   settings.window_x >= -1000000 && settings.window_x <= 1000000 &&
                   settings.window_y >= -1000000 && settings.window_y <= 1000000;
        }

        bool valid_date(std::string_view value) {
            if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
                return false;
            }
            int parts[3]{};
            int part = 0;
            for (std::size_t index = 0; index < value.size(); ++index) {
                if (index == 4 || index == 7) {
                    ++part;
                } else {
                    if (value[index] < '0' || value[index] > '9') {
                        return false;
                    }
                    parts[part] = parts[part] * 10 + value[index] - '0';
                }
            }
            return parts[0] >= 1 &&
                   std::chrono::year_month_day{std::chrono::year{parts[0]},
                                               std::chrono::month{static_cast<unsigned>(parts[1])},
                                               std::chrono::day{static_cast<unsigned>(parts[2])}}
                       .ok();
        }

        bool valid_utf8(std::string_view value) {
            std::size_t index = 0;
            while (index < value.size()) {
                const auto lead = static_cast<unsigned char>(value[index++]);
                if (lead < 0x80) {
                    if (lead == 0) {
                        return false;
                    }
                    continue;
                }
                unsigned codepoint = 0;
                unsigned minimum = 0;
                std::size_t remaining = 0;
                if (lead >= 0xc2 && lead <= 0xdf) {
                    codepoint = lead & 0x1f;
                    minimum = 0x80;
                    remaining = 1;
                } else if (lead >= 0xe0 && lead <= 0xef) {
                    codepoint = lead & 0x0f;
                    minimum = 0x800;
                    remaining = 2;
                } else if (lead >= 0xf0 && lead <= 0xf4) {
                    codepoint = lead & 0x07;
                    minimum = 0x10000;
                    remaining = 3;
                } else {
                    return false;
                }
                if (remaining > value.size() - index) {
                    return false;
                }
                for (std::size_t count = 0; count < remaining; ++count) {
                    const auto continuation = static_cast<unsigned char>(value[index++]);
                    if ((continuation & 0xc0) != 0x80) {
                        return false;
                    }
                    codepoint = (codepoint << 6) | (continuation & 0x3f);
                }
                if (codepoint < minimum || codepoint > 0x10ffff ||
                    (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
                    return false;
                }
            }
            return true;
        }

        bool valid_task(std::string_view title, std::string_view notes, std::string_view due_date) {
            return title.size() <= 240 && notes.size() <= 4096 &&
                   title.find_first_not_of(" \t\n\r\f\v") != std::string_view::npos &&
                   valid_utf8(title) && valid_utf8(notes) &&
                   (due_date.empty() || valid_date(due_date));
        }

        bool valid_query(TaskFilter filter, std::string_view today) {
            return (filter == TaskFilter::today || filter == TaskFilter::upcoming ||
                    filter == TaskFilter::completed) &&
                   valid_date(today);
        }

        std::int64_t timestamp() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        std::expected<int, Error> version(sqlite3 *connection) {
            auto statement = prepare(connection, "PRAGMA user_version", "read schema version");
            if (!statement) {
                return std::unexpected(statement.error());
            }
            if (sqlite3_step(statement->get()) != SQLITE_ROW) {
                return std::unexpected(sqlite_error(connection, "read schema version"));
            }
            return sqlite3_column_int(statement->get(), 0);
        }

        constexpr std::array<std::string_view, 11> columns{"id",
                                                           "light_mode",
                                                           "reduce_motion",
                                                           "always_on_top",
                                                           "expanded",
                                                           "remember_position",
                                                           "window_x",
                                                           "window_y",
                                                           "focus_minutes",
                                                           "short_minutes",
                                                           "long_minutes"};

        std::string_view text(sqlite3_stmt *statement, int column) {
            const auto *value = sqlite3_column_text(statement, column);
            if (value == nullptr) {
                return {};
            }
            return {reinterpret_cast<const char *>(value),
                    static_cast<std::size_t>(sqlite3_column_bytes(statement, column))};
        }

        std::expected<void, Error> validate_schema(sqlite3 *connection) {
            auto table =
                prepare(connection, "PRAGMA table_list('settings')", "validate settings table");
            if (!table) {
                return std::unexpected(table.error());
            }
            if (sqlite3_step(table->get()) != SQLITE_ROW || text(table->get(), 2) != "table" ||
                sqlite3_column_int(table->get(), 3) != static_cast<int>(columns.size()) ||
                sqlite3_column_int(table->get(), 5) != 1 ||
                sqlite3_step(table->get()) != SQLITE_DONE) {
                return std::unexpected(Error{ErrorKind::invalid_schema, "validate settings table"});
            }
            auto statement =
                prepare(connection, "PRAGMA table_xinfo('settings')", "validate settings columns");
            if (!statement) {
                return std::unexpected(statement.error());
            }
            for (std::size_t index = 0; index < columns.size(); ++index) {
                if (sqlite3_step(statement->get()) != SQLITE_ROW ||
                    text(statement->get(), 1) != columns[index] ||
                    text(statement->get(), 2) != "INTEGER" ||
                    sqlite3_column_int(statement->get(), 3) != 1 ||
                    sqlite3_column_int(statement->get(), 5) != (index == 0 ? 1 : 0) ||
                    sqlite3_column_int(statement->get(), 6) != 0) {
                    return std::unexpected(
                        Error{ErrorKind::invalid_schema, "validate settings columns"});
                }
            }
            if (sqlite3_step(statement->get()) != SQLITE_DONE) {
                return std::unexpected(
                    Error{ErrorKind::invalid_schema, "validate settings columns"});
            }
            return {};
        }

        std::expected<void, Error> validate_tasks_schema(sqlite3 *connection) {
            constexpr std::array<std::string_view, 7> names{
                "id", "title", "notes", "due_date", "completed", "created_at", "updated_at"};
            auto table = prepare(connection, "PRAGMA table_list('tasks')", "validate tasks table");
            if (!table) {
                return std::unexpected(table.error());
            }
            if (sqlite3_step(table->get()) != SQLITE_ROW || text(table->get(), 2) != "table" ||
                sqlite3_column_int(table->get(), 3) != static_cast<int>(names.size()) ||
                sqlite3_column_int(table->get(), 5) != 1 ||
                sqlite3_step(table->get()) != SQLITE_DONE) {
                return std::unexpected(Error{ErrorKind::invalid_schema, "validate tasks table"});
            }
            auto columns_statement =
                prepare(connection, "PRAGMA table_xinfo('tasks')", "validate tasks columns");
            if (!columns_statement) {
                return std::unexpected(columns_statement.error());
            }
            for (std::size_t index = 0; index < names.size(); ++index) {
                const auto type = index >= 1 && index <= 3 ? "TEXT" : "INTEGER";
                if (sqlite3_step(columns_statement->get()) != SQLITE_ROW ||
                    text(columns_statement->get(), 1) != names[index] ||
                    text(columns_statement->get(), 2) != type ||
                    sqlite3_column_int(columns_statement->get(), 3) != 1 ||
                    sqlite3_column_int(columns_statement->get(), 5) != (index == 0 ? 1 : 0) ||
                    sqlite3_column_int(columns_statement->get(), 6) != 0) {
                    return std::unexpected(
                        Error{ErrorKind::invalid_schema, "validate tasks columns"});
                }
            }
            if (sqlite3_step(columns_statement->get()) != SQLITE_DONE) {
                return std::unexpected(Error{ErrorKind::invalid_schema, "validate tasks columns"});
            }
            return {};
        }

        std::expected<void, Error> bind_text(sqlite3 *connection, sqlite3_stmt *statement,
                                             int index, std::string_view value) {
            if (sqlite3_bind_text64(statement, index, value.empty() ? "" : value.data(),
                                    static_cast<sqlite3_uint64>(value.size()), SQLITE_STATIC,
                                    SQLITE_UTF8) != SQLITE_OK) {
                return std::unexpected(sqlite_error(connection, "bind task text"));
            }
            return {};
        }

        std::expected<void, Error> bind_integer(sqlite3 *connection, sqlite3_stmt *statement,
                                                int index, std::int64_t value) {
            if (sqlite3_bind_int64(statement, index, value) != SQLITE_OK) {
                return std::unexpected(sqlite_error(connection, "bind task number"));
            }
            return {};
        }

        std::expected<void, Error> bind_draft(sqlite3 *connection, sqlite3_stmt *statement,
                                              const TaskDraft &draft) {
            const std::array<std::string_view, 3> values{draft.title, draft.notes, draft.due_date};
            for (int index = 0; index < static_cast<int>(values.size()); ++index) {
                auto result = bind_text(connection, statement, index + 1, values[index]);
                if (!result) {
                    return result;
                }
            }
            return {};
        }

        std::expected<TaskRecord, Error> read_task(sqlite3_stmt *statement) {
            for (int index = 0; index < 7; ++index) {
                const int type = index >= 1 && index <= 3 ? SQLITE_TEXT : SQLITE_INTEGER;
                if (sqlite3_column_type(statement, index) != type) {
                    return std::unexpected(
                        Error{ErrorKind::invalid_task, "repair invalid task row"});
                }
            }
            const auto id = sqlite3_column_int64(statement, 0);
            const auto title = text(statement, 1);
            const auto due_date = text(statement, 3);
            const auto completed = sqlite3_column_int64(statement, 4);
            const auto created_at = sqlite3_column_int64(statement, 5);
            const auto updated_at = sqlite3_column_int64(statement, 6);
            if (id <= 0 || !valid_task(title, text(statement, 2), due_date) ||
                (completed != 0 && completed != 1) || created_at < 0 || updated_at < created_at) {
                return std::unexpected(Error{ErrorKind::invalid_task, "repair invalid task row"});
            }
            return TaskRecord{id,
                              std::string{title},
                              std::string{text(statement, 2)},
                              std::string{due_date},
                              completed != 0,
                              created_at,
                              updated_at};
        }

        std::expected<TaskRecord, Error> find_task(sqlite3 *connection, std::int64_t id) {
            auto statement =
                prepare(connection,
                        "SELECT id,title,notes,due_date,completed,created_at,updated_at FROM tasks "
                        "WHERE id=?",
                        "find task");
            if (!statement) {
                return std::unexpected(statement.error());
            }
            auto bound = bind_integer(connection, statement->get(), 1, id);
            if (!bound) {
                return std::unexpected(bound.error());
            }
            const int code = sqlite3_step(statement->get());
            if (code == SQLITE_DONE) {
                return std::unexpected(Error{ErrorKind::not_found, "find existing task"});
            }
            if (code != SQLITE_ROW) {
                return std::unexpected(sqlite_error(connection, "read task"));
            }
            auto record = read_task(statement->get());
            if (!record) {
                return record;
            }
            if (sqlite3_step(statement->get()) != SQLITE_DONE) {
                return std::unexpected(sqlite_error(connection, "finish reading task"));
            }
            return record;
        }

        std::expected<void, Error> write_task(sqlite3 *connection, sqlite3_stmt *statement) {
            if (sqlite3_step(statement) != SQLITE_DONE) {
                return std::unexpected(sqlite_error(connection, "write task"));
            }
            if (sqlite3_changes(connection) != 1) {
                return std::unexpected(Error{ErrorKind::not_found, "write existing task"});
            }
            return {};
        }

        std::expected<void, Error> bind_filter(sqlite3 *connection, sqlite3_stmt *statement,
                                               TaskFilter filter, std::string_view today) {
            auto bound = bind_integer(connection, statement, 1, static_cast<int>(filter));
            if (!bound) {
                return bound;
            }
            return bind_text(connection, statement, 2, today);
        }

        std::expected<void, Error> initialize(sqlite3 *connection) {
            auto initial_version = version(connection);
            if (!initial_version) {
                return std::unexpected(initial_version.error());
            }
            // Refuse future databases before changing their persistent journal mode.
            if (*initial_version < 0 || *initial_version > current_schema_version) {
                return std::unexpected(Error{ErrorKind::unsupported_version,
                                             "open newer database with a newer Oryn version"});
            }
            auto wal = prepare(connection, "PRAGMA journal_mode=WAL", "enable WAL journal");
            if (!wal) {
                return std::unexpected(wal.error());
            }
            if (sqlite3_step(wal->get()) != SQLITE_ROW || text(wal->get(), 0) != "wal" ||
                sqlite3_step(wal->get()) != SQLITE_DONE) {
                return std::unexpected(sqlite_error(connection, "enable WAL journal"));
            }
            auto begun = execute(connection, "BEGIN IMMEDIATE", "begin schema transaction");
            if (!begun) {
                return begun;
            }
            Transaction transaction{connection};
            // Recheck under the write lock so concurrent first launches cannot race migration.
            auto current_version = version(connection);
            if (!current_version) {
                return std::unexpected(current_version.error());
            }
            if (*current_version < 0 || *current_version > current_schema_version) {
                return std::unexpected(Error{ErrorKind::unsupported_version,
                                             "open newer database with a newer Oryn version"});
            }
            if (*current_version == 0) {
                auto migration = execute(
                    connection,
                    "CREATE TABLE settings ("
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
                    "INSERT INTO settings VALUES(1,0,0,1,1,0,0,0,25,5,15);"
                    "PRAGMA user_version=1;",
                    "migrate settings schema");
                if (!migration) {
                    return migration;
                }
            }
            auto schema = validate_schema(connection);
            if (!schema) {
                return schema;
            }
            if (*current_version <= 1) {
                auto migration = execute(
                    connection,
                    "CREATE TABLE tasks ("
                    "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT CHECK(id>0),"
                    "title TEXT NOT NULL CHECK(length(CAST(title AS BLOB)) BETWEEN 1 AND 240 "
                    "AND instr(title,char(0))=0 "
                    "AND length(trim(title,char(9,10,11,12,13,32)))>0),"
                    "notes TEXT NOT NULL CHECK(length(CAST(notes AS BLOB))<=4096 "
                    "AND instr(notes,char(0))=0),"
                    "due_date TEXT NOT NULL CHECK(due_date='' OR ("
                    "length(due_date)=10 AND "
                    "due_date GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]' AND "
                    "CAST(substr(due_date,1,4) AS INTEGER) BETWEEN 1 AND 9999 AND "
                    "CAST(substr(due_date,6,2) AS INTEGER) BETWEEN 1 AND 12 AND "
                    "CAST(substr(due_date,9,2) AS INTEGER) BETWEEN 1 AND "
                    "CASE CAST(substr(due_date,6,2) AS INTEGER) "
                    "WHEN 2 THEN 28+(CAST(substr(due_date,1,4) AS INTEGER)%4=0 AND "
                    "(CAST(substr(due_date,1,4) AS INTEGER)%100!=0 OR "
                    "CAST(substr(due_date,1,4) AS INTEGER)%400=0)) "
                    "WHEN 4 THEN 30 WHEN 6 THEN 30 WHEN 9 THEN 30 WHEN 11 THEN 30 ELSE 31 END)),"
                    "completed INTEGER NOT NULL CHECK(completed IN (0,1)),"
                    "created_at INTEGER NOT NULL CHECK(created_at>=0),"
                    "updated_at INTEGER NOT NULL CHECK(updated_at>=created_at)"
                    ") STRICT;"
                    "CREATE INDEX tasks_filter_order ON tasks(completed,(due_date=''),due_date,id);"
                    "CREATE INDEX tasks_filter_date ON tasks(completed,due_date,id);"
                    "PRAGMA user_version=2;",
                    "migrate tasks schema");
                if (!migration) {
                    return migration;
                }
            }
            auto tasks_schema = validate_tasks_schema(connection);
            if (!tasks_schema) {
                return tasks_schema;
            }
            return transaction.commit();
        }
    } // namespace

    struct Database::Impl {
        Connection connection;

        explicit Impl(Connection value) : connection(std::move(value)) {
        }
    };

    Database::Database(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {
    }

    Database::~Database() = default;

    std::expected<std::unique_ptr<Database>, Error>
    Database::open(const std::filesystem::path &path) {
        if (path.empty() || path.filename().empty()) {
            return std::unexpected(Error{ErrorKind::filesystem, "choose a database file path"});
        }
        std::error_code error;
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) {
                return std::unexpected(Error{ErrorKind::filesystem,
                                             "create application data directory", error.value()});
            }
        }
        const auto encoded = path.u8string();
        sqlite3 *raw = nullptr;
        const int code = sqlite3_open_v2(
            reinterpret_cast<const char *>(encoded.c_str()), &raw,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
        Connection connection{raw};
        if (code != SQLITE_OK) {
            return std::unexpected(Error{ErrorKind::open, "open settings database", code});
        }
        sqlite3_extended_result_codes(raw, 1);
        sqlite3_busy_timeout(raw, 1000);
        auto initialized = initialize(raw);
        if (!initialized) {
            return std::unexpected(initialized.error());
        }
        auto database =
            std::unique_ptr<Database>(new Database(std::make_unique<Impl>(std::move(connection))));
        return std::move(database);
    }

    std::expected<SettingsRecord, Error> Database::load() {
        auto *connection = impl_->connection.get();
        auto statement = prepare(
            connection,
            "SELECT id,light_mode,reduce_motion,always_on_top,expanded,remember_position,"
            "window_x,window_y,focus_minutes,short_minutes,long_minutes FROM settings LIMIT 2",
            "read settings");
        if (!statement) {
            return std::unexpected(statement.error());
        }
        const int code = sqlite3_step(statement->get());
        if (code != SQLITE_ROW) {
            if (code != SQLITE_DONE) {
                return std::unexpected(sqlite_error(connection, "read settings"));
            }
            return std::unexpected(
                Error{ErrorKind::invalid_settings, "repair missing settings row"});
        }
        std::array<int, 11> values{};
        for (int index = 0; index < static_cast<int>(values.size()); ++index) {
            const auto value = sqlite3_column_int64(statement->get(), index);
            if (sqlite3_column_type(statement->get(), index) != SQLITE_INTEGER ||
                value < std::numeric_limits<std::int32_t>::min() ||
                value > std::numeric_limits<std::int32_t>::max() || (index == 0 && value != 1) ||
                (index >= 1 && index <= 5 && value != 0 && value != 1)) {
                return std::unexpected(
                    Error{ErrorKind::invalid_settings, "repair invalid settings row"});
            }
            values[index] = static_cast<int>(value);
        }
        SettingsRecord settings{values[1] != 0, values[2] != 0, values[3] != 0, values[4] != 0,
                                values[5] != 0, values[6],      values[7],      values[8],
                                values[9],      values[10]};
        if (!valid(settings)) {
            return std::unexpected(
                Error{ErrorKind::invalid_settings, "repair invalid persisted preferences"});
        }
        if (sqlite3_step(statement->get()) != SQLITE_DONE) {
            return std::unexpected(
                Error{ErrorKind::invalid_settings, "repair duplicate settings rows"});
        }
        return settings;
    }

    std::expected<void, Error> Database::save(const SettingsRecord &settings) {
        if (!valid(settings)) {
            return std::unexpected(
                Error{ErrorKind::invalid_settings,
                      "use timer minutes 1..180 and coordinates -1000000..1000000"});
        }
        auto *connection = impl_->connection.get();
        auto begun = execute(connection, "BEGIN IMMEDIATE", "begin settings save");
        if (!begun) {
            return begun;
        }
        Transaction transaction{connection};
        auto existing = load();
        if (!existing) {
            return std::unexpected(existing.error());
        }
        auto statement =
            prepare(connection,
                    "UPDATE settings SET light_mode=?,reduce_motion=?,always_on_top=?,expanded=?,"
                    "remember_position=?,window_x=?,window_y=?,focus_minutes=?,short_minutes=?,"
                    "long_minutes=? WHERE id=1",
                    "prepare settings save");
        if (!statement) {
            return std::unexpected(statement.error());
        }
        const std::array<int, 10> values{settings.light_mode,        settings.reduce_motion,
                                         settings.always_on_top,     settings.expanded,
                                         settings.remember_position, settings.window_x,
                                         settings.window_y,          settings.focus_minutes,
                                         settings.short_minutes,     settings.long_minutes};
        for (int index = 0; index < static_cast<int>(values.size()); ++index) {
            const int code = sqlite3_bind_int(statement->get(), index + 1, values[index]);
            if (code != SQLITE_OK) {
                return std::unexpected(sqlite_error(connection, "bind settings preferences"));
            }
        }
        if (sqlite3_step(statement->get()) != SQLITE_DONE) {
            return std::unexpected(sqlite_error(connection, "write settings preferences"));
        }
        if (sqlite3_changes(connection) != 1) {
            return std::unexpected(
                Error{ErrorKind::invalid_settings, "repair missing settings row"});
        }
        return transaction.commit();
    }

    std::expected<std::vector<TaskRecord>, Error> Database::list_tasks(TaskFilter filter,
                                                                       std::string_view today,
                                                                       std::size_t offset,
                                                                       std::size_t limit) {
        if (!valid_query(filter, today) || limit == 0 || limit > 51 ||
            offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return std::unexpected(Error{ErrorKind::invalid_task, "use valid task page and date"});
        }
        auto *connection = impl_->connection.get();
        auto statement =
            prepare(connection,
                    "SELECT id,title,notes,due_date,completed,created_at,updated_at FROM tasks "
                    "WHERE (?1=0 AND completed=0 AND (due_date='' OR due_date<=?2)) "
                    "OR (?1=1 AND completed=0 AND due_date>?2) OR (?1=2 AND completed=1) "
                    "ORDER BY (due_date=''),due_date,id LIMIT ?3 OFFSET ?4",
                    "list tasks");
        if (!statement) {
            return std::unexpected(statement.error());
        }
        auto bound = bind_filter(connection, statement->get(), filter, today);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        bound = bind_integer(connection, statement->get(), 3, static_cast<std::int64_t>(limit));
        if (!bound) {
            return std::unexpected(bound.error());
        }
        bound = bind_integer(connection, statement->get(), 4, static_cast<std::int64_t>(offset));
        if (!bound) {
            return std::unexpected(bound.error());
        }
        std::vector<TaskRecord> tasks;
        tasks.reserve(limit);
        for (;;) {
            const int code = sqlite3_step(statement->get());
            if (code == SQLITE_DONE) {
                return tasks;
            }
            if (code != SQLITE_ROW) {
                return std::unexpected(sqlite_error(connection, "list tasks"));
            }
            auto task = read_task(statement->get());
            if (!task) {
                return std::unexpected(task.error());
            }
            tasks.push_back(std::move(*task));
        }
    }

    std::expected<std::int64_t, Error> Database::count_tasks(TaskFilter filter,
                                                             std::string_view today) {
        if (!valid_query(filter, today)) {
            return std::unexpected(
                Error{ErrorKind::invalid_task, "use valid task filter and date"});
        }
        auto *connection = impl_->connection.get();
        auto statement =
            prepare(connection,
                    "SELECT count(*) FROM tasks "
                    "WHERE (?1=0 AND completed=0 AND (due_date='' OR due_date<=?2)) "
                    "OR (?1=1 AND completed=0 AND due_date>?2) OR (?1=2 AND completed=1)",
                    "count tasks");
        if (!statement) {
            return std::unexpected(statement.error());
        }
        auto bound = bind_filter(connection, statement->get(), filter, today);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        if (sqlite3_step(statement->get()) != SQLITE_ROW) {
            return std::unexpected(sqlite_error(connection, "count tasks"));
        }
        const auto count = sqlite3_column_int64(statement->get(), 0);
        if (sqlite3_column_type(statement->get(), 0) != SQLITE_INTEGER || count < 0) {
            return std::unexpected(Error{ErrorKind::invalid_task, "read task count"});
        }
        if (sqlite3_step(statement->get()) != SQLITE_DONE) {
            return std::unexpected(sqlite_error(connection, "finish task count"));
        }
        return count;
    }

    std::expected<TaskRecord, Error> Database::create_task(const TaskDraft &draft) {
        const auto now = timestamp();
        if (!valid_task(draft.title, draft.notes, draft.due_date) || now < 0) {
            return std::unexpected(Error{ErrorKind::invalid_task, "use task title and civil date"});
        }
        auto *connection = impl_->connection.get();
        auto begun = execute(connection, "BEGIN IMMEDIATE", "begin task creation");
        if (!begun) {
            return std::unexpected(begun.error());
        }
        Transaction transaction{connection};
        auto statement =
            prepare(connection,
                    "INSERT INTO tasks(title,notes,due_date,completed,created_at,updated_at) "
                    "VALUES(?1,?2,?3,0,?4,?4)",
                    "prepare task creation");
        if (!statement) {
            return std::unexpected(statement.error());
        }
        auto bound = bind_draft(connection, statement->get(), draft);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        bound = bind_integer(connection, statement->get(), 4, now);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        auto written = write_task(connection, statement->get());
        if (!written) {
            return std::unexpected(written.error());
        }
        auto result = find_task(connection, sqlite3_last_insert_rowid(connection));
        if (!result) {
            return result;
        }
        auto committed = transaction.commit();
        if (!committed) {
            return std::unexpected(committed.error());
        }
        return result;
    }

    std::expected<TaskRecord, Error> Database::update_task(std::int64_t id,
                                                           const TaskDraft &draft) {
        if (id <= 0 || !valid_task(draft.title, draft.notes, draft.due_date)) {
            return std::unexpected(
                Error{ErrorKind::invalid_task, "use task id title and civil date"});
        }
        auto *connection = impl_->connection.get();
        auto begun = execute(connection, "BEGIN IMMEDIATE", "begin task update");
        if (!begun) {
            return std::unexpected(begun.error());
        }
        Transaction transaction{connection};
        auto existing = find_task(connection, id);
        if (!existing) {
            return existing;
        }
        auto statement = prepare(
            connection, "UPDATE tasks SET title=?1,notes=?2,due_date=?3,updated_at=?4 WHERE id=?5",
            "prepare task update");
        if (!statement) {
            return std::unexpected(statement.error());
        }
        auto bound = bind_draft(connection, statement->get(), draft);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        bound = bind_integer(connection, statement->get(), 4,
                             std::max(timestamp(), existing->updated_at));
        if (!bound) {
            return std::unexpected(bound.error());
        }
        bound = bind_integer(connection, statement->get(), 5, id);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        auto written = write_task(connection, statement->get());
        if (!written) {
            return std::unexpected(written.error());
        }
        auto result = find_task(connection, id);
        if (!result) {
            return result;
        }
        auto committed = transaction.commit();
        if (!committed) {
            return std::unexpected(committed.error());
        }
        return result;
    }

    std::expected<TaskRecord, Error> Database::set_task_completed(std::int64_t id, bool completed) {
        if (id <= 0) {
            return std::unexpected(Error{ErrorKind::invalid_task, "use positive task id"});
        }
        auto *connection = impl_->connection.get();
        auto begun = execute(connection, "BEGIN IMMEDIATE", "begin task completion");
        if (!begun) {
            return std::unexpected(begun.error());
        }
        Transaction transaction{connection};
        auto existing = find_task(connection, id);
        if (!existing) {
            return existing;
        }
        auto statement =
            prepare(connection, "UPDATE tasks SET completed=?1,updated_at=?2 WHERE id=?3",
                    "prepare task completion");
        if (!statement) {
            return std::unexpected(statement.error());
        }
        auto bound = bind_integer(connection, statement->get(), 1, completed);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        bound = bind_integer(connection, statement->get(), 2,
                             std::max(timestamp(), existing->updated_at));
        if (!bound) {
            return std::unexpected(bound.error());
        }
        bound = bind_integer(connection, statement->get(), 3, id);
        if (!bound) {
            return std::unexpected(bound.error());
        }
        auto written = write_task(connection, statement->get());
        if (!written) {
            return std::unexpected(written.error());
        }
        auto result = find_task(connection, id);
        if (!result) {
            return result;
        }
        auto committed = transaction.commit();
        if (!committed) {
            return std::unexpected(committed.error());
        }
        return result;
    }

    std::expected<void, Error> Database::erase_task(std::int64_t id) {
        if (id <= 0) {
            return std::unexpected(Error{ErrorKind::invalid_task, "use positive task id"});
        }
        auto *connection = impl_->connection.get();
        auto begun = execute(connection, "BEGIN IMMEDIATE", "begin task deletion");
        if (!begun) {
            return begun;
        }
        Transaction transaction{connection};
        auto existing = find_task(connection, id);
        if (!existing) {
            return std::unexpected(existing.error());
        }
        auto statement =
            prepare(connection, "DELETE FROM tasks WHERE id=?", "prepare task deletion");
        if (!statement) {
            return std::unexpected(statement.error());
        }
        auto bound = bind_integer(connection, statement->get(), 1, id);
        if (!bound) {
            return bound;
        }
        auto written = write_task(connection, statement->get());
        if (!written) {
            return written;
        }
        return transaction.commit();
    }
} // namespace oryn::database
