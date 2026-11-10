#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <expected>
#include <limits>
#include <memory>
#include <string_view>
#include <type_traits>
#include <windows.h>
import oryn.platform.window;

namespace {
    struct WindowDeleter {
        void operator()(HWND window) const noexcept {
            DestroyWindow(window);
        }
    };

    struct RegionDeleter {
        void operator()(HRGN region) const noexcept {
            DeleteObject(region);
        }
    };

    using Window = std::unique_ptr<std::remove_pointer_t<HWND>, WindowDeleter>;
    using Region = std::unique_ptr<std::remove_pointer_t<HRGN>, RegionDeleter>;

    void assert_invalid_window(HWND hwnd) {
        SetLastError(ERROR_ACCESS_DENIED);
        const auto result = oryn::platform::shape_window(hwnd, 1.0, 289.0);
        assert(!result.has_value());
        // The result must retain the actual API error, not the stale thread error or later cleanup.
        SetLastError(ERROR_SUCCESS);
        assert(result.error().operation == "GetClientRect");
        assert(result.error().system_code == ERROR_INVALID_WINDOW_HANDLE);
    }
} // namespace

int main() {
    static_assert(oryn::platform::centered_window_x(0, 1920, 762) == 579);
    static_assert(oryn::platform::centered_window_x(0, 1366, 762) == 302);
    static_assert(oryn::platform::centered_window_x(0, 3840, 1142) == 1349);
    static_assert(oryn::platform::centered_window_x(-1920, 0, 762) == -1341);
    static_assert(oryn::platform::clamped_window_coordinate(-2500, -1920, 0, 762) == -1920);
    static_assert(oryn::platform::clamped_window_coordinate(-100, -1920, 0, 762) == -762);
    static_assert(oryn::platform::clamped_window_coordinate(-1341, -1920, 0, 762) == -1341);
    static_assert(oryn::platform::clamped_window_coordinate(1900, 0, 1920, 762) == 1158);
    static_assert(oryn::platform::clamped_window_coordinate(20, 40, 100, 762) == 40);
    static_assert(oryn::platform::clamped_window_coordinate(500, 40, 100, 0) == 100);
    static_assert(oryn::platform::clamped_window_coordinate(
                      (std::numeric_limits<int>::max)(), (std::numeric_limits<int>::min)(),
                      (std::numeric_limits<int>::max)(),
                      762) == (std::numeric_limits<int>::max)() - 762);
    static_assert(oryn::platform::clamped_window_coordinate((std::numeric_limits<int>::min)(),
                                                            -1920, 0, 762) == -1920);
    Window window{CreateWindowExW(0, L"STATIC", L"Oryn shape test", WS_POPUP, 0, 0, 762, 405,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
    const auto hwnd = window.get();
    assert(hwnd);
    assert(oryn::platform::window_dpi(hwnd) > 0);
    assert(oryn::platform::window_dpi(nullptr) > 0);
    assert(oryn::platform::shape_window(hwnd, 1.0, 289.0).has_value());
    Region region{CreateRectRgn(0, 0, 0, 0)};
    assert(region);
    std::array<Region, 3> silhouettes;
    for (auto &silhouette : silhouettes) {
        silhouette.reset(CreateRectRgn(0, 0, 0, 0));
        assert(silhouette);
    }
    assert(GetWindowRgn(hwnd, region.get()) == COMPLEXREGION);
    assert(GetWindowRgn(hwnd, silhouettes[2].get()) == COMPLEXREGION);
    assert(!PtInRegion(region.get(), 0, 404));
    assert(!PtInRegion(region.get(), 761, 404));
    assert(PtInRegion(region.get(), 381, 210));
    assert(oryn::platform::shape_window(hwnd, 0.0, 289.0).has_value());
    assert(GetWindowRgn(hwnd, region.get()) == COMPLEXREGION);
    assert(GetWindowRgn(hwnd, silhouettes[0].get()) == COMPLEXREGION);
    assert(!PtInRegion(region.get(), 0, 49));
    assert(PtInRegion(region.get(), 381, 0));
    assert(!PtInRegion(region.get(), 100, 25));
    assert(PtInRegion(region.get(), 381, 25));
    assert(!PtInRegion(region.get(), 381, 80));
    RECT compact_bounds{};
    GetRgnBox(region.get(), &compact_bounds);
    assert(compact_bounds.bottom <= 50);
    assert(oryn::platform::shape_window(hwnd, 0.5, 289.0).has_value());
    assert(GetWindowRgn(hwnd, region.get()) == COMPLEXREGION);
    assert(GetWindowRgn(hwnd, silhouettes[1].get()) == COMPLEXREGION);
    RECT bounds{};
    GetRgnBox(region.get(), &bounds);
    assert(bounds.left > 90 && bounds.left < 110);
    assert(!PtInRegion(region.get(), 0, 25));
    assert(PtInRegion(region.get(), 381, 25));
    assert(!PtInRegion(region.get(), 381, 300));
    RECT client{};
    GetClientRect(hwnd, &client);
    assert(client.right == 762 && client.bottom == 405);

    // Replacing the window-owned region must neither leak nor free it too soon.
    const auto gdi_objects = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    assert(gdi_objects != 0);
    for (int frame = 0; frame < 256; ++frame) {
        const double expansion = (frame % 3) * 0.5;
        assert(oryn::platform::shape_window(hwnd, expansion, 289.0).has_value());
        assert(GetWindowRgn(hwnd, region.get()) == COMPLEXREGION);
        assert(EqualRgn(region.get(), silhouettes[static_cast<std::size_t>(frame % 3)].get()));
        assert(PtInRegion(region.get(), 381, 0));
        assert((PtInRegion(region.get(), 381, 80) != 0) == (expansion > 0.0));
        assert_invalid_window(nullptr);
    }
    assert(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == gdi_objects);

    // Both axes follow the width-derived scale, including during an asynchronous height resize.
    Region compact_reference{CreateRectRgn(0, 0, 0, 0)};
    Region expanded_reference{CreateRectRgn(0, 0, 0, 0)};
    assert(compact_reference && expanded_reference);
    for (const int width : {544, 762, 1088}) {
        const double scale = width / 544.0;
        const auto pixel = [scale](double value) {
            return static_cast<int>(value * scale + 0.5);
        };
        assert(SetWindowPos(hwnd, nullptr, 0, 0, width, pixel(289),
                            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        assert(oryn::platform::shape_window(hwnd, 0.0, 289.0).has_value());
        assert(GetWindowRgn(hwnd, compact_reference.get()) == COMPLEXREGION);
        for (const double height : {331.0, 394.0}) {
            // The new silhouette must not depend on whether Winit has resized the HWND yet.
            assert(oryn::platform::shape_window(hwnd, 1.0, height).has_value());
            assert(GetWindowRgn(hwnd, expanded_reference.get()) == COMPLEXREGION);
            assert(GetRgnBox(expanded_reference.get(), &bounds) == COMPLEXREGION);
            assert(bounds.bottom == pixel(height));
            assert(PtInRegion(expanded_reference.get(), pixel(272), pixel(height - 1)));
            assert(!PtInRegion(expanded_reference.get(), pixel(272), pixel(height + 1)));
            assert(!PtInRegion(expanded_reference.get(), 0, pixel(height - 1)));
            assert(SetWindowPos(hwnd, nullptr, 0, 0, width, pixel(height),
                                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
            assert(GetClientRect(hwnd, &client));
            assert(client.right == width && client.bottom == pixel(height));
            assert(oryn::platform::shape_window(hwnd, 1.0, height).has_value());
            assert(GetWindowRgn(hwnd, region.get()) == COMPLEXREGION);
            assert(EqualRgn(region.get(), expanded_reference.get()));
            for (const double expansion : {0.0, 0.5, 1.0, 0.0, 1.0}) {
                assert(oryn::platform::shape_window(hwnd, expansion, height).has_value());
                assert(GetWindowRgn(hwnd, region.get()) == COMPLEXREGION);
                assert(GetRgnBox(region.get(), &bounds) == COMPLEXREGION);
                assert(bounds.bottom == pixel(35 + (height - 35) * expansion));
                assert(PtInRegion(region.get(), pixel(272), 0));
                if (expansion == 0.0) {
                    assert(EqualRgn(region.get(), compact_reference.get()));
                    assert(!PtInRegion(region.get(), pixel(272), pixel(36)));
                } else if (expansion == 1.0) {
                    assert(EqualRgn(region.get(), expanded_reference.get()));
                }
                // Collapse is clipping only; native surface height remains the expanded height.
                assert(GetClientRect(hwnd, &client));
                assert(client.right == width && client.bottom == pixel(height));
            }
        }
    }
    assert(oryn::platform::shape_window(hwnd, 0.0, 35.0).has_value());
    assert(GetWindowRgn(hwnd, region.get()) == COMPLEXREGION);
    assert(EqualRgn(region.get(), compact_reference.get()));
    for (const double height : {34.999, -1.0, std::numeric_limits<double>::infinity(),
                                -std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::quiet_NaN()}) {
        SetLastError(ERROR_ACCESS_DENIED);
        const auto result = oryn::platform::shape_window(hwnd, 1.0, height);
        assert(!result.has_value());
        assert(result.error().operation == "panel_design_height");
        assert(result.error().system_code == 0);
        assert(GetWindowRgn(hwnd, region.get()) == COMPLEXREGION);
        assert(EqualRgn(region.get(), compact_reference.get()));
    }
    window.reset();
    assert_invalid_window(hwnd); // A real window handle that has just been destroyed.
}
