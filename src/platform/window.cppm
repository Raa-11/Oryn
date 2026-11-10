module;
#include <cstdint>
#include <expected>
#include <optional>
#include <slint.h>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#endif
export module oryn.platform.window;

export namespace oryn::platform {
    struct WindowError {
        // Static operation label; zero means no documented native error detail is available.
        std::string_view operation;
        std::uint32_t system_code;
    };

    using WindowResult = std::expected<void, WindowError>;

    struct WindowPosition {
        int x;
        int y;
    };

    // Physical monitor coordinates can be negative; use wide arithmetic at signed boundaries.
    [[nodiscard]] constexpr int clamped_window_coordinate(int position, int near_edge, int far_edge,
                                                          int extent) noexcept {
        const auto available = static_cast<std::int64_t>(far_edge) - near_edge;
        const auto size = extent > 0 ? static_cast<std::int64_t>(extent) : 0;
        if (available <= size || position < near_edge) {
            return near_edge;
        }
        const auto maximum = static_cast<std::int64_t>(far_edge) - size;
        return position > maximum ? static_cast<int>(maximum) : position;
    }

    class TrayIcon {
    public:
        TrayIcon() = default;
        ~TrayIcon() noexcept;

        TrayIcon(const TrayIcon &) = delete;
        TrayIcon &operator=(const TrayIcon &) = delete;
        TrayIcon(TrayIcon &&) = delete;
        TrayIcon &operator=(TrayIcon &&) = delete;

        [[nodiscard]] WindowResult attach(slint::Window &window);

    private:
#ifdef _WIN32
        static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                            UINT_PTR subclass_id, DWORD_PTR owner);

        [[nodiscard]] WindowResult register_icon() noexcept;
        [[nodiscard]] WindowResult set_taskbar_style(bool tray_only) noexcept;
        void hide_panel() noexcept;
        void show_panel() noexcept;
        void show_menu(LPARAM position) noexcept;
        void release() noexcept;

        HWND window_ = nullptr;         // Borrowed; the UI thread owns its lifetime.
        slint::Window *view_ = nullptr; // Borrowed; outlives this owner.
        HICON icon_ = nullptr;
        HMENU menu_ = nullptr;
        LONG_PTR original_ex_style_ = 0;
        UINT taskbar_created_ = 0;
        bool subclass_installed_ = false;
        bool registered_ = false;
#endif
    };

    [[nodiscard]] constexpr int centered_window_x(int left, int right, int width) noexcept {
        return left + (right - left - width) / 2;
    }

#ifdef _WIN32
    [[nodiscard]] UINT window_dpi(HWND hwnd) noexcept;

    // Clip the native window to the same Bezier silhouette as the Slint Path.
    [[nodiscard]] WindowResult shape_window(HWND hwnd, double expansion,
                                            double panel_design_height) noexcept;
#endif

    [[nodiscard]] WindowResult place_window(slint::Window &window, float logical_width);
    [[nodiscard]] WindowResult apply_window_preferences(slint::Window &window, bool always_on_top,
                                                        std::optional<WindowPosition> position);
    [[nodiscard]] std::optional<WindowPosition> window_position(slint::Window &window) noexcept;
    [[nodiscard]] WindowResult initialize_window(slint::Window &window, double expansion,
                                                 double panel_design_height);
    [[nodiscard]] WindowResult update_window_shape(slint::Window &window, double expansion,
                                                   double panel_design_height);
} // namespace oryn::platform
