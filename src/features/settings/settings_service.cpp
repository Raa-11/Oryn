module;
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

module oryn.settings;
import oryn.database;

namespace oryn::settings {
    namespace {
        database::SettingsRecord record(const Preferences &preferences) {
            return {preferences.light_mode,        preferences.reduce_motion,
                    preferences.always_on_top,     preferences.expanded,
                    preferences.remember_position, preferences.window_x,
                    preferences.window_y,          preferences.focus_minutes,
                    preferences.short_minutes,     preferences.long_minutes};
        }

        Preferences preferences(const database::SettingsRecord &value) {
            return {value.light_mode,  value.reduce_motion,     value.always_on_top,
                    value.expanded,    value.remember_position, value.window_x,
                    value.window_y,    value.focus_minutes,     value.short_minutes,
                    value.long_minutes};
        }

        bool valid(const Preferences &value) {
            return value.focus_minutes >= 1 && value.focus_minutes <= 180 &&
                   value.short_minutes >= 1 && value.short_minutes <= 180 &&
                   value.long_minutes >= 1 && value.long_minutes <= 180 &&
                   value.window_x >= -1000000 && value.window_x <= 1000000 &&
                   value.window_y >= -1000000 && value.window_y <= 1000000;
        }

        std::string describe(const database::Error &error) {
            std::string message = "Settings storage: ";
            message += error.operation;
            if (error.code != 0) {
                message += " (code " + std::to_string(error.code) + ")";
            }
            message += ".";
            return message;
        }
    } // namespace

    struct SettingsService::Impl {
        std::filesystem::path path;
        Observer observer;
        std::mutex mutex;
        std::condition_variable changed;
        std::optional<Preferences> pending;
        bool stopping = false;
        std::jthread worker;

        Impl(std::filesystem::path database_path, Observer callback)
            : path(std::move(database_path)), observer(std::move(callback)),
              worker([this] { run(); }) {
        }

        ~Impl() {
            {
                const std::lock_guard lock{mutex};
                stopping = true;
            }
            changed.notify_one();
            worker.join();
        }

        void save(Preferences value) {
            {
                const std::lock_guard lock{mutex};
                if (stopping) {
                    return;
                }
                pending = value;
            }
            changed.notify_one();
        }

        void publish(const Snapshot &snapshot) {
            if (observer) {
                observer(snapshot);
            }
        }

        void run() {
            // Connection, migrations, statements, commits and close all stay on this thread.
            auto opened = database::Database::open(path);
            std::unique_ptr<database::Database> connection;
            Snapshot state;
            if (opened) {
                connection = std::move(*opened);
                auto loaded = connection->load();
                if (loaded) {
                    state.preferences = preferences(*loaded);
                    state.loaded = true;
                } else {
                    state.error = describe(loaded.error());
                    connection.reset();
                }
            } else {
                state.error = describe(opened.error());
            }
            const std::string load_error = state.error;
            bool initial_pending = false;
            {
                const std::lock_guard lock{mutex};
                initial_pending = pending.has_value();
            }
            if (!initial_pending) {
                publish(state);
            }
            while (true) {
                Preferences requested;
                {
                    std::unique_lock lock{mutex};
                    changed.wait(lock, [this] { return stopping || pending.has_value(); });
                    if (!pending) {
                        return;
                    }
                    requested = *pending;
                    pending.reset();
                }
                if (!connection) {
                    state.error = load_error;
                    publish(state);
                    continue;
                }
                if (!valid(requested)) {
                    state.error = "Use timer minutes 1..180 and window coordinates "
                                  "-1000000..1000000. Settings not saved.";
                    publish(state);
                    continue;
                }
                // A saving notification represents the request, not a committed write. Failure
                // restores the previous committed preferences; success has an empty error.
                Snapshot saving{requested, true, true, {}};
                publish(saving);
                auto saved = connection->save(record(requested));
                if (saved) {
                    state.preferences = requested;
                    state.error.clear();
                } else {
                    state.error = describe(saved.error());
                }
                publish(state);
            }
        }
    };

    SettingsService::SettingsService(std::filesystem::path database_path, Observer observer)
        : impl_(std::make_unique<Impl>(std::move(database_path), std::move(observer))) {
    }

    SettingsService::~SettingsService() = default;

    void SettingsService::save(Preferences preferences) {
        impl_->save(preferences);
    }

    void SettingsService::restore_defaults() {
        save(Preferences{});
    }
} // namespace oryn::settings
