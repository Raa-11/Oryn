module;
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

export module oryn.settings;

export namespace oryn::settings {
    struct Preferences {
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
        bool operator==(const Preferences &) const = default;
    };

    struct Snapshot {
        // With saving=true, preferences is the validated request; otherwise it is the
        // committed value (safe defaults if load failed). An error never implies success.
        Preferences preferences;
        // True only when the initial load produced a usable persistent store.
        bool loaded = false;
        bool saving = false;
        std::string error;
    };

    class SettingsService {
    public:
        using Observer = std::function<void(Snapshot)>;
        // No filesystem work occurs in construction. Observer runs on the storage thread;
        // it must not throw or destroy this service. Dispatch UI updates onto its event loop.
        explicit SettingsService(std::filesystem::path database_path, Observer observer);
        // Stops accepting commands, drains the latest accepted save, closes SQLite and joins.
        ~SettingsService();
        SettingsService(const SettingsService &) = delete;
        SettingsService &operator=(const SettingsService &) = delete;
        SettingsService(SettingsService &&) = delete;
        SettingsService &operator=(SettingsService &&) = delete;

        // Thread-safe, nonblocking except for a short queue mutex. A single latest slot
        // replaces older unstarted saves. Load completes before any save is processed;
        // pending edits replace the initial loaded-preferences notification.
        void save(Preferences preferences);
        void restore_defaults();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace oryn::settings
