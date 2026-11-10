module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <slint.h>
#include <type_traits>
#include <utility>
#ifdef _WIN32
#include <cwchar>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#endif

module oryn.platform.window;

namespace oryn::platform {
#ifdef _WIN32
    namespace {
        constexpr UINT tray_callback_message = WM_APP + 0x4f1;
        constexpr UINT tray_icon_id = 1;
        constexpr UINT_PTR tray_subclass_id = 2;
        constexpr UINT tray_show_command = 1;
        constexpr UINT tray_exit_command = 2;
        constexpr UINT_PTR preferences_subclass_id = 3;

        LRESULT CALLBACK preferences_window_proc(HWND hwnd, UINT message, WPARAM wparam,
                                                 LPARAM lparam, UINT_PTR subclass_id,
                                                 DWORD_PTR topmost) {
            if (message == WM_NCDESTROY) {
                RemoveWindowSubclass(hwnd, preferences_window_proc, subclass_id);
                return DefSubclassProc(hwnd, message, wparam, lparam);
            }
            const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
            if (message == WM_WINDOWPOSCHANGING) {
                auto &position = *reinterpret_cast<WINDOWPOS *>(lparam);
                if ((position.flags & SWP_NOZORDER) == 0) {
                    // Winit can reapply its cached level on show. The saved preference wins.
                    position.hwndInsertAfter = topmost ? HWND_TOPMOST : HWND_NOTOPMOST;
                }
            } else if (message == WM_STYLECHANGING && wparam == static_cast<WPARAM>(GWL_EXSTYLE)) {
                auto &style = *reinterpret_cast<STYLESTRUCT *>(lparam);
                style.styleNew =
                    topmost ? style.styleNew | WS_EX_TOPMOST : style.styleNew & ~WS_EX_TOPMOST;
            }
            return result;
        }

        [[nodiscard]] std::expected<HICON, WindowError> create_tray_icon(HWND window) noexcept {
            // Slint's embedded window icon comes from the official Oryn logo.
            // Copy the borrowed handle so the tray owns its lifetime independently.
            HICON window_icon = nullptr;
            constexpr std::array sizes{ICON_SMALL2, ICON_SMALL, ICON_BIG};
            for (const auto size : sizes) {
                window_icon = reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, size, 0));
                if (window_icon) {
                    break;
                }
            }
            if (!window_icon) {
                return std::unexpected(WindowError{"WM_GETICON(Oryn)", 0});
            }
            const auto icon = CopyIcon(window_icon);
            if (!icon) {
                return std::unexpected(WindowError{"CopyIcon", GetLastError()});
            }
            return icon;
        }

        [[nodiscard]] NOTIFYICONDATAW tray_data(HWND window, HICON icon) noexcept {
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = window;
            data.uID = tray_icon_id;
            data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
            data.uCallbackMessage = tray_callback_message;
            data.hIcon = icon;
            constexpr wchar_t tooltip[] = L"Oryn";
            std::copy_n(tooltip, sizeof(tooltip) / sizeof(*tooltip), data.szTip);
            return data;
        }

        struct DeviceContextReleaser {
            HWND window;

            void operator()(HDC dc) const noexcept {
                ReleaseDC(window, dc);
            }
        };

        struct RegionDeleter {
            void operator()(HRGN region) const noexcept {
                DeleteObject(region);
            }
        };

        using DeviceContext = std::unique_ptr<std::remove_pointer_t<HDC>, DeviceContextReleaser>;
        using Region = std::unique_ptr<std::remove_pointer_t<HRGN>, RegionDeleter>;

        [[nodiscard]] DeviceContext acquire_device_context(HWND window) noexcept {
            return DeviceContext{GetDC(window), DeviceContextReleaser{window}};
        }

        class SavedDeviceContext {
        public:
            explicit SavedDeviceContext(HDC dc) noexcept : dc_(dc), state_(SaveDC(dc)) {
            }

            ~SavedDeviceContext() noexcept {
                if (state_ != 0) {
                    RestoreDC(dc_, state_);
                }
            }

            SavedDeviceContext(const SavedDeviceContext &) = delete;
            SavedDeviceContext &operator=(const SavedDeviceContext &) = delete;
            SavedDeviceContext(SavedDeviceContext &&) = delete;
            SavedDeviceContext &operator=(SavedDeviceContext &&) = delete;

            [[nodiscard]] explicit operator bool() const noexcept {
                return state_ != 0;
            }

        private:
            HDC dc_;
            int state_;
        };

        class DrawingPath {
        public:
            explicit DrawingPath(HDC dc) noexcept : dc_(dc), active_(BeginPath(dc) != 0) {
            }

            ~DrawingPath() noexcept {
                if (active_) {
                    AbortPath(dc_);
                }
            }

            DrawingPath(const DrawingPath &) = delete;
            DrawingPath &operator=(const DrawingPath &) = delete;
            DrawingPath(DrawingPath &&) = delete;
            DrawingPath &operator=(DrawingPath &&) = delete;

            [[nodiscard]] explicit operator bool() const noexcept {
                return active_;
            }

            [[nodiscard]] std::expected<Region, WindowError> into_region() noexcept {
                if (!EndPath(dc_)) {
                    return std::unexpected(WindowError{"EndPath", 0});
                }
                Region region{PathToRegion(dc_)};
                if (!region) {
                    return std::unexpected(WindowError{"PathToRegion", 0});
                }
                active_ = false; // PathToRegion consumes the path on success.
                return region;
            }

        private:
            HDC dc_;
            bool active_;
        };

        LRESULT CALLBACK frameless_window_proc(HWND hwnd, UINT message, WPARAM wparam,
                                               LPARAM lparam, UINT_PTR subclass_id,
                                               DWORD_PTR was_minimized) {
            if (message == WM_NCACTIVATE && !IsIconic(hwnd)) {
                // Keep activation processing without painting over the frameless client.
                // Minimized windows need the original message for native icon activation.
                return DefSubclassProc(hwnd, message, wparam, -1);
            }
            if (message == WM_SIZE) {
                if (wparam == SIZE_MINIMIZED) {
                    SetWindowSubclass(hwnd, frameless_window_proc, subclass_id, 1);
                } else if (was_minimized && (wparam == SIZE_RESTORED || wparam == SIZE_MAXIMIZED)) {
                    // Update the existing subclass data before forwarding reentrant messages.
                    SetWindowSubclass(hwnd, frameless_window_proc, subclass_id, 0);
                    const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
                    // The restored software surface must not reuse the iconic window's pixels.
                    InvalidateRect(hwnd, nullptr, TRUE);
                    return result;
                }
            }
            if (message == WM_NCDESTROY) {
                RemoveWindowSubclass(hwnd, frameless_window_proc, subclass_id);
            }
            return DefSubclassProc(hwnd, message, wparam, lparam);
        }

        [[nodiscard]] HWND native_window(slint::Window &window) noexcept {
            if (auto hwnd = window.win32_hwnd()) {
                return hwnd;
            }

            struct Search {
                HWND result = nullptr;
            } search;

            EnumThreadWindows(
                GetCurrentThreadId(),
                [](HWND hwnd, LPARAM data) -> BOOL {
                    std::array<wchar_t, 32> title{};
                    GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
                    if (IsWindowVisible(hwnd) && std::wcscmp(title.data(), L"Oryn") == 0) {
                        reinterpret_cast<Search *>(data)->result = hwnd;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&search));
            return search.result;
        }
    } // namespace

    UINT window_dpi(HWND hwnd) noexcept {
        // Resolve newer APIs at runtime: Bazel may target an older Windows SDK level.
        const auto user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            using WindowDpi = UINT(WINAPI *)(HWND);
            using SystemDpi = UINT(WINAPI *)();
            const auto for_window =
                reinterpret_cast<WindowDpi>(GetProcAddress(user32, "GetDpiForWindow"));
            const auto for_system =
                reinterpret_cast<SystemDpi>(GetProcAddress(user32, "GetDpiForSystem"));
            if (hwnd && for_window) {
                if (const auto dpi = for_window(hwnd)) {
                    return dpi;
                }
            }
            if (for_system) {
                if (const auto dpi = for_system()) {
                    return dpi;
                }
            }
        }
        const auto dc = acquire_device_context(nullptr);
        if (!dc) {
            return 96;
        }
        const auto dpi = GetDeviceCaps(dc.get(), LOGPIXELSX);
        return dpi > 0 ? static_cast<UINT>(dpi) : 96;
    }

    WindowResult shape_window(HWND hwnd, double expansion, double panel_design_height) noexcept {
        if (!std::isfinite(panel_design_height) || panel_design_height < 35.0) {
            return std::unexpected(WindowError{"panel_design_height", 0});
        }
        expansion = std::clamp(expansion, 0.0, 1.0);
        RECT client{};
        if (!GetClientRect(hwnd, &client)) {
            const auto code = GetLastError();
            return std::unexpected(WindowError{"GetClientRect", code});
        }
        const double sx = (client.right - client.left) / 544.0;
        const double sy = sx;
        const auto point = [sx, sy](double x, double y) -> POINT {
            return {static_cast<LONG>(x * sx + 0.5), static_cast<LONG>(y * sy + 0.5)};
        };

        Region region;
        {
            const auto dc = acquire_device_context(hwnd);
            // These GDI calls do not document extended errors; never report stale GetLastError().
            if (!dc) {
                return std::unexpected(WindowError{"GetDC", 0});
            }
            const SavedDeviceContext saved{dc.get()};
            if (!saved) {
                return std::unexpected(WindowError{"SaveDC", 0});
            }
            DrawingPath path{dc.get()};
            if (!path) {
                return std::unexpected(WindowError{"BeginPath", 0});
            }

            const auto start = point(140 - 140 * expansion, 0);
            if (!MoveToEx(dc.get(), start.x, start.y, nullptr)) {
                return std::unexpected(WindowError{"MoveToEx", 0});
            }
            WindowResult drawing;
            const auto line = [&](double x, double y) {
                if (!drawing) {
                    return;
                }
                const auto p = point(x, y);
                if (!LineTo(dc.get(), p.x, p.y)) {
                    drawing = std::unexpected(WindowError{"LineTo", 0});
                }
            };
            const auto curve = [&](double a, double b, double c, double d, double e, double f) {
                if (!drawing) {
                    return;
                }
                const std::array points{point(a, b), point(c, d), point(e, f)};
                if (!PolyBezierTo(dc.get(), points.data(), static_cast<DWORD>(points.size()))) {
                    drawing = std::unexpected(WindowError{"PolyBezierTo", 0});
                }
            };
            line(404 + 140 * expansion, 0);
            curve(397 + 137 * expansion, 0, 392 + 133 * expansion, 1.75 - 1.75 * expansion,
                  388.75 + 130.25 * expansion, 4.875 + 0.125 * expansion);
            curve(385.5 + 127.5 * expansion, 8 + 2 * expansion, 384 + 128 * expansion,
                  12.5 + 6.5 * expansion, 384 + 128 * expansion, 18 + 12 * expansion);
            const double bottom = 35 + (panel_design_height - 35) * expansion;
            const double line_y = 21 + (panel_design_height - 43) * expansion;
            const double control_y = 30.3333 + (panel_design_height - 37.6666) * expansion;
            line(384 + 128 * expansion, line_y);
            curve(384 + 128 * expansion, control_y, 379.3333 + 125.3334 * expansion, bottom,
                  370 + 120 * expansion, bottom);
            line(174 - 120 * expansion, bottom);
            curve(164.6667 - 125.3334 * expansion, bottom, 160 - 128 * expansion, control_y,
                  160 - 128 * expansion, line_y);
            line(160 - 128 * expansion, 18 + 12 * expansion);
            curve(160 - 128 * expansion, 12.5 + 6.5 * expansion, 158.5 - 127.5 * expansion,
                  8 + 2 * expansion, 155.25 - 130.25 * expansion, 4.875 + 0.125 * expansion);
            curve(152 - 133 * expansion, 1.75 - 1.75 * expansion, 147 - 137 * expansion, 0,
                  140 - 140 * expansion, 0);
            if (!drawing) {
                return drawing;
            }
            if (!CloseFigure(dc.get())) {
                return std::unexpected(WindowError{"CloseFigure", 0});
            }
            auto converted = path.into_region();
            if (!converted) {
                return std::unexpected(converted.error());
            }
            region = std::move(*converted);
        }
        // Slint schedules the frame. Windows owns the region only after a successful call.
        if (!SetWindowRgn(hwnd, region.get(), FALSE)) {
            return std::unexpected(WindowError{"SetWindowRgn", 0});
        }
        static_cast<void>(region.release());
        return {};
    }
#endif

    TrayIcon::~TrayIcon() noexcept {
#ifdef _WIN32
        release();
#endif
    }

#ifdef _WIN32
    WindowResult TrayIcon::register_icon() noexcept {
        auto data = tray_data(window_, icon_);
        // Shell_NotifyIcon does not document GetLastError; a failure has no native code.
        if (!Shell_NotifyIconW(NIM_ADD, &data)) {
            return std::unexpected(WindowError{"Shell_NotifyIconW(NIM_ADD)", 0});
        }
        data.uVersion = NOTIFYICON_VERSION_4;
        if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) {
            Shell_NotifyIconW(NIM_DELETE, &data);
            return std::unexpected(WindowError{"Shell_NotifyIconW(NIM_SETVERSION)", 0});
        }
        registered_ = true;
        return {};
    }

    WindowResult TrayIcon::set_taskbar_style(bool tray_only) noexcept {
        SetLastError(ERROR_SUCCESS);
        const auto previous = GetWindowLongPtrW(window_, GWL_EXSTYLE);
        if (previous == 0) {
            if (const auto code = GetLastError(); code != ERROR_SUCCESS) {
                return std::unexpected(WindowError{"GetWindowLongPtrW(GWL_EXSTYLE)", code});
            }
        }
        constexpr LONG_PTR taskbar_bits = WS_EX_TOOLWINDOW | WS_EX_APPWINDOW;
        const auto desired = tray_only
                                 ? (previous | WS_EX_TOOLWINDOW) & ~LONG_PTR{WS_EX_APPWINDOW}
                                 : (previous & ~taskbar_bits) | (original_ex_style_ & taskbar_bits);
        if (desired == previous) {
            return {};
        }
        const bool visible = IsWindowVisible(window_) != FALSE;
        const bool foreground = GetForegroundWindow() == window_;
        // Explorer caches taskbar eligibility: change extended styles while hidden,
        // then refresh the non-client cache without moving/resizing/reordering the panel.
        // Use Slint's visibility lifecycle: Windows loses hidden software pixels
        // without updating buffer age, so the renderer must invalidate on show.
        view_->hide();
        const auto restore_visibility = [&] {
            if (visible) {
                view_->show();
                if (foreground) {
                    SetForegroundWindow(window_);
                }
            }
        };
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(window_, GWL_EXSTYLE, desired) == 0) {
            if (const auto code = GetLastError(); code != ERROR_SUCCESS) {
                restore_visibility();
                return std::unexpected(WindowError{"SetWindowLongPtrW(GWL_EXSTYLE)", code});
            }
        }
        constexpr UINT frame_flags =
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED;
        if (!SetWindowPos(window_, nullptr, 0, 0, 0, 0, frame_flags)) {
            const auto code = GetLastError();
            // Roll back taskbar eligibility before reporting an attach/recovery failure.
            SetWindowLongPtrW(window_, GWL_EXSTYLE, previous);
            SetWindowPos(window_, nullptr, 0, 0, 0, 0, frame_flags);
            restore_visibility();
            return std::unexpected(WindowError{"SetWindowPos(SWP_FRAMECHANGED)", code});
        }
        restore_visibility();
        return {};
    }

    void TrayIcon::hide_panel() noexcept {
        view_->hide();
    }

    void TrayIcon::show_panel() noexcept {
        if (IsIconic(window_)) {
            ShowWindow(window_, SW_RESTORE);
        }
        view_->show();
        view_->request_redraw();
        SetForegroundWindow(window_);
    }

    void TrayIcon::show_menu(LPARAM position) noexcept {
        // Version 4 supplies signed screen coordinates in wParam, including for
        // keyboard invocation. They may be negative on a secondary monitor.
        const POINT anchor{static_cast<short>(LOWORD(position)),
                           static_cast<short>(HIWORD(position))};
        SetForegroundWindow(window_);
        const auto command =
            TrackPopupMenuEx(menu_, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_WORKAREA,
                             anchor.x, anchor.y, window_, nullptr);
        // Required by the notification-area menu focus contract, including cancel.
        PostMessageW(window_, WM_NULL, 0, 0);
        if (command == tray_show_command) {
            show_panel();
        } else if (command == tray_exit_command) {
            slint::quit_event_loop();
        }
    }

    void TrayIcon::release() noexcept {
        if (registered_) {
            auto data = tray_data(window_, icon_);
            Shell_NotifyIconW(NIM_DELETE, &data);
            registered_ = false;
        }
        if (window_ && IsWindow(window_)) {
            if (subclass_installed_) {
                RemoveWindowSubclass(window_, window_proc, tray_subclass_id);
            }
            // The borrowed window can outlive this owner. Do not leave it without
            // a taskbar entry once its notification-area entry is gone.
            static_cast<void>(set_taskbar_style(false));
        }
        subclass_installed_ = false;
        window_ = nullptr;
        view_ = nullptr;
        if (menu_) {
            DestroyMenu(menu_);
            menu_ = nullptr;
        }
        if (icon_) {
            DestroyIcon(icon_);
            icon_ = nullptr;
        }
    }

    LRESULT CALLBACK TrayIcon::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                           UINT_PTR subclass_id, DWORD_PTR owner) {
        auto &tray = *reinterpret_cast<TrayIcon *>(owner);
        if (message == WM_NCDESTROY) {
            if (tray.registered_) {
                auto data = tray_data(hwnd, tray.icon_);
                Shell_NotifyIconW(NIM_DELETE, &data);
                tray.registered_ = false;
            }
            RemoveWindowSubclass(hwnd, window_proc, subclass_id);
            tray.subclass_installed_ = false;
            tray.window_ = nullptr;
            return DefSubclassProc(hwnd, message, wparam, lparam);
        }
        if (message == tray.taskbar_created_) {
            tray.registered_ = false; // Explorer discarded all registrations.
            if (tray.register_icon()) {
                if (tray.set_taskbar_style(true)) {
                    return 0;
                }
                auto data = tray_data(hwnd, tray.icon_);
                Shell_NotifyIconW(NIM_DELETE, &data);
                tray.registered_ = false;
            }
            // No polling/retry thread: leave a visible, ordinary taskbar window
            // if the restarted shell cannot register a usable tray entry.
            static_cast<void>(tray.set_taskbar_style(false));
            tray.show_panel();
            return 0;
        }
        if (tray.registered_) {
            if (message == WM_STYLECHANGING && wparam == static_cast<WPARAM>(GWL_EXSTYLE)) {
                const auto result = DefSubclassProc(hwnd, message, wparam, lparam);
                // Winit reapplies its cached extended styles when showing the panel.
                // Keep tray-only eligibility before Explorer observes that transition.
                auto &style = *reinterpret_cast<STYLESTRUCT *>(lparam);
                style.styleNew = (style.styleNew | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW;
                return result;
            }
            if (message == WM_CLOSE ||
                (message == WM_SYSCOMMAND && (wparam & 0xfff0) == SC_MINIMIZE)) {
                tray.hide_panel();
                return 0;
            }
            if (message == tray_callback_message && HIWORD(lparam) == tray_icon_id) {
                switch (LOWORD(lparam)) {
                case NIN_SELECT:
                    // Version 4 already translates a mouse click to NIN_SELECT;
                    // handling WM_LBUTTONUP as well would toggle twice.
                    if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
                        tray.hide_panel();
                    } else {
                        tray.show_panel();
                    }
                    return 0;
                case NIN_KEYSELECT:
                    tray.show_panel();
                    return 0;
                case WM_CONTEXTMENU:
                    tray.show_menu(static_cast<LPARAM>(wparam));
                    return 0;
                default:
                    break;
                }
            }
        }
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }
#endif

    WindowResult TrayIcon::attach(slint::Window &window) {
#ifdef _WIN32
        if (window_) {
            return std::unexpected(WindowError{"TrayIcon::attach", ERROR_ALREADY_INITIALIZED});
        }
        const auto hwnd = native_window(window);
        if (!hwnd) {
            return std::unexpected(WindowError{"native_window", 0});
        }
        const auto thread = GetWindowThreadProcessId(hwnd, nullptr);
        if (thread == 0) {
            return std::unexpected(WindowError{"GetWindowThreadProcessId", GetLastError()});
        }
        if (thread != GetCurrentThreadId()) {
            return std::unexpected(
                WindowError{"TrayIcon::attach_ui_thread", ERROR_INVALID_THREAD_ID});
        }
        SetLastError(ERROR_SUCCESS);
        const auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (style == 0) {
            if (const auto code = GetLastError(); code != ERROR_SUCCESS) {
                return std::unexpected(WindowError{"GetWindowLongPtrW(GWL_EXSTYLE)", code});
            }
        }
        const auto taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        if (!taskbar_created) {
            return std::unexpected(
                WindowError{"RegisterWindowMessageW(TaskbarCreated)", GetLastError()});
        }
        auto icon = create_tray_icon(hwnd);
        if (!icon) {
            return std::unexpected(icon.error());
        }
        window_ = hwnd;
        view_ = &window;
        original_ex_style_ = style;
        taskbar_created_ = taskbar_created;
        icon_ = *icon;
        const auto fail = [this](WindowError error) -> WindowResult {
            release();
            return std::unexpected(error);
        };
        menu_ = CreatePopupMenu();
        if (!menu_) {
            return fail(WindowError{"CreatePopupMenu", GetLastError()});
        }
        if (!AppendMenuW(menu_, MF_STRING, tray_show_command, L"Tampilkan Oryn") ||
            !AppendMenuW(menu_, MF_STRING, tray_exit_command, L"Keluar")) {
            return fail(WindowError{"AppendMenuW", GetLastError()});
        }
        if (!SetMenuDefaultItem(menu_, tray_show_command, FALSE)) {
            return fail(WindowError{"SetMenuDefaultItem", GetLastError()});
        }
        if (!SetWindowSubclass(hwnd, window_proc, tray_subclass_id,
                               reinterpret_cast<DWORD_PTR>(this))) {
            return fail(WindowError{"SetWindowSubclass(tray)", 0});
        }
        subclass_installed_ = true;
        if (const auto result = register_icon(); !result) {
            return fail(result.error());
        }
        if (const auto result = set_taskbar_style(true); !result) {
            return fail(result.error());
        }
        // The app has requested its initial panel; native creation may still have
        // been hidden when taskbar eligibility was changed.
        show_panel();
        return {};
#else
        return std::unexpected(WindowError{"unsupported_platform", 0});
#endif
    }

    WindowResult place_window(slint::Window &window, float logical_width) {
#ifdef _WIN32
        const auto hwnd = native_window(window);
        MONITORINFO info{sizeof(MONITORINFO)};
        const auto monitor = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)
                                  : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (!monitor) {
            return std::unexpected(WindowError{hwnd ? "MonitorFromWindow" : "MonitorFromPoint", 0});
        }
        if (!GetMonitorInfoW(monitor, &info)) {
            return std::unexpected(WindowError{"GetMonitorInfoW", 0});
        }
        // Before show(), Slint's native size may still be zero or a backend default.
        const auto dpi = window_dpi(hwnd);
        const auto width = static_cast<int>(logical_width * (dpi ? dpi : 96) / 96.0f + 0.5f);
        const auto x = centered_window_x(info.rcWork.left, info.rcWork.right, width);
        slint::PhysicalPosition position;
        position.x = static_cast<std::int32_t>(x);
        position.y = static_cast<std::int32_t>(info.rcWork.top);
        window.set_position(position);
        if (!hwnd) {
            return {};
        }
        if (!SetWindowPos(hwnd, nullptr, x, info.rcWork.top, 0, 0,
                          SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER)) {
            const auto code = GetLastError();
            return std::unexpected(WindowError{"SetWindowPos", code});
        }
        return {};
#else
        return std::unexpected(WindowError{"unsupported_platform", 0});
#endif
    }

    WindowResult apply_window_preferences(slint::Window &window, bool always_on_top,
                                          std::optional<WindowPosition> position) {
#ifdef _WIN32
        const auto hwnd = native_window(window);
        if (position) {
            const auto monitor =
                MonitorFromPoint(POINT{position->x, position->y}, MONITOR_DEFAULTTONEAREST);
            if (!monitor) {
                return std::unexpected(WindowError{"MonitorFromPoint", 0});
            }
            MONITORINFO info{sizeof(MONITORINFO)};
            if (!GetMonitorInfoW(monitor, &info)) {
                return std::unexpected(WindowError{"GetMonitorInfoW", 0});
            }
            const auto size = window.size();
            int width = static_cast<int>((std::min)(size.width, std::uint32_t{INT32_MAX}));
            int height = static_cast<int>((std::min)(size.height, std::uint32_t{INT32_MAX}));
            if (hwnd) {
                RECT bounds{};
                if (!GetWindowRect(hwnd, &bounds)) {
                    return std::unexpected(WindowError{"GetWindowRect", GetLastError()});
                }
                width = bounds.right - bounds.left;
                height = bounds.bottom - bounds.top;
            }
            position->x =
                clamped_window_coordinate(position->x, info.rcWork.left, info.rcWork.right, width);
            position->y =
                clamped_window_coordinate(position->y, info.rcWork.top, info.rcWork.bottom, height);
            slint::PhysicalPosition physical;
            physical.x = position->x;
            physical.y = position->y;
            window.set_position(physical);
        }
        // The caller reapplies after initialize_window when a pre-show HWND was unavailable.
        if (!hwnd) {
            return {};
        }
        if (!SetWindowSubclass(hwnd, preferences_window_proc, preferences_subclass_id,
                               always_on_top ? 1 : 0)) {
            return std::unexpected(WindowError{"SetWindowSubclass(preferences)", 0});
        }
        UINT flags = SWP_NOSIZE | SWP_NOACTIVATE;
        if (!position) {
            flags |= SWP_NOMOVE;
        }
        if (!SetWindowPos(hwnd, always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                          position ? position->x : 0, position ? position->y : 0, 0, 0, flags)) {
            return std::unexpected(WindowError{"SetWindowPos(preferences)", GetLastError()});
        }
        return {};
#else
        return std::unexpected(WindowError{"unsupported_platform", 0});
#endif
    }

    std::optional<WindowPosition> window_position(slint::Window &window) noexcept {
#ifdef _WIN32
        const auto hwnd = native_window(window);
        RECT bounds{};
        if (hwnd && GetWindowRect(hwnd, &bounds)) {
            return WindowPosition{bounds.left, bounds.top};
        }
#endif
        return std::nullopt;
    }

    WindowResult initialize_window(slint::Window &window, double expansion,
                                   double panel_design_height) {
#ifdef _WIN32
        const auto hwnd = native_window(window);
        if (!hwnd) {
            return std::unexpected(WindowError{"native_window", 0});
        }
        // Installed once on the UI thread; the subclass removes itself with the native window.
        if (!SetWindowSubclass(hwnd, frameless_window_proc, 1, IsIconic(hwnd) ? 1 : 0)) {
            return std::unexpected(WindowError{"SetWindowSubclass", 0});
        }
        // Winit retains WS_CAPTION even for no-frame windows. Remove the native
        // caption, but retain WS_SYSMENU and WS_MINIMIZEBOX for taskbar restore.
        SetLastError(ERROR_SUCCESS);
        const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        if (style == 0 && GetLastError() != ERROR_SUCCESS) {
            return std::unexpected(WindowError{"GetWindowLongPtrW", GetLastError()});
        }
        if ((style & WS_CAPTION) != 0) {
            SetLastError(ERROR_SUCCESS);
            if (SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~WS_CAPTION) == 0) {
                if (const auto code = GetLastError(); code != ERROR_SUCCESS) {
                    return std::unexpected(WindowError{"SetWindowLongPtrW", code});
                }
            }
            if (!SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                              SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                                  SWP_FRAMECHANGED)) {
                return std::unexpected(WindowError{"SetWindowPos", GetLastError()});
            }
        }
        return shape_window(hwnd, expansion, panel_design_height);
#else
        return std::unexpected(WindowError{"unsupported_platform", 0});
#endif
    }

    WindowResult update_window_shape(slint::Window &window, double expansion,
                                     double panel_design_height) {
#ifdef _WIN32
        const auto hwnd = native_window(window);
        if (!hwnd) {
            return std::unexpected(WindowError{"native_window", 0});
        }
        return shape_window(hwnd, expansion, panel_design_height);
#else
        return std::unexpected(WindowError{"unsupported_platform", 0});
#endif
    }
} // namespace oryn::platform
