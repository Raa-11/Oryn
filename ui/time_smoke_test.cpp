#include <algorithm>
#include <cmath>
#include <ostream>
#include <optional>
#include <string>
#include <tuple>
#include "ui/testing/main.h"
#include <doctest/doctest.h>
#include <slint-testing.h>
#include <slint-platform.h>

namespace {
    void init_testing_backend() {
        // Slint 1.18.1 permits backend initialization only once per process.
        static const bool initialized = [] {
            slint::testing::init();
            return true;
        }();
        (void)initialized;
    }

    using Elements = slint::testing::ElementHandle;
    using Role = slint::language::AccessibleRole;

    Elements settings_element(const slint::ComponentHandle<OrynWindow> &window, const char *label,
                              Role role) {
        std::optional<Elements> match;
        for (const auto &element : Elements::find_by_accessible_label(window, label)) {
            if (element.accessible_role() == role) {
                REQUIRE_FALSE(match.has_value());
                match = element;
            }
        }
        INFO(label);
        REQUIRE(match.has_value());
        return *match;
    }

    void settings_click(const slint::ComponentHandle<OrynWindow> &window, const char *label) {
        settings_element(window, label, Role::Button).invoke_accessible_default_action();
        slint::platform::update_timers_and_animations();
    }

    void settings_edit(const slint::ComponentHandle<OrynWindow> &window, const char *label,
                       const char *value) {
        settings_element(window, label, Role::TextInput).set_accessible_value(value);
        slint::platform::update_timers_and_animations();
    }
} // namespace

TEST_CASE("Time UI exposes working controls and fits inside the island") {
    init_testing_backend();
    auto window = OrynWindow::create();
    const auto &data = window->global<TimeData>();
    const auto &actions = window->global<TimeActions>();
    window->global<SettingsData>().set_loaded(true);
    slint::platform::update_timers_and_animations();
    window->set_reduce_motion(true);
    window->set_active_page("Time");
    window->show();
    using Elements = slint::testing::ElementHandle;
    auto click = [&](const char *label) {
        auto matches = Elements::find_by_accessible_label(window, label);
        INFO(std::string(label));
        std::optional<Elements> button;
        int count = 0;
        for (const auto &element : matches) {
            if (element.accessible_role() == slint::language::AccessibleRole::Button) {
                button = element;
                ++count;
            }
        }
        REQUIRE(count == 1);
        button->invoke_accessible_default_action();
    };
    int minutes = -1;
    int seconds = -1;
    int toggles = 0;
    int resets = 0;
    int cancels = 0;
    bool pomodoro = false;
    actions.on_start_duration([&](int m, int s) {
        minutes = m;
        seconds = s;
    });
    actions.on_toggle([&] { ++toggles; });
    actions.on_reset([&] { ++resets; });
    actions.on_cancel([&] { ++cancels; });
    actions.on_select_mode([&](bool value) { pomodoro = value; });
    click("1 minutes");
    CHECK(minutes == 1);
    CHECK(seconds == 0);
    click("Start");
    CHECK(minutes == 15);
    CHECK(seconds == 0);
    auto minute_input = Elements::find_by_accessible_label(window, "Timer minutes");
    auto second_input = Elements::find_by_accessible_label(window, "Timer seconds");
    REQUIRE(minute_input.size() == 1);
    REQUIRE(second_input.size() == 1);
    minute_input[0].set_accessible_value("0");
    second_input[0].set_accessible_value("5");
    click("Start");
    CHECK(minutes == 0);
    CHECK(seconds == 5);
    data.set_status(TimerStatus::Running);
    data.set_remaining("00:59");
    click("Pause");
    CHECK(toggles == 1);
    data.set_status(TimerStatus::Paused);
    click("Resume");
    CHECK(toggles == 2);
    click("Reset");
    CHECK(resets == 1);
    click("Cancel");
    CHECK(cancels == 1);
    click("Pomodoro");
    CHECK(pomodoro);
    data.set_pomodoro(true);
    data.set_status(TimerStatus::Idle);
    data.set_remaining("25:00");
    click("Start Focus");
    CHECK(toggles == 3);
    click("Edit durations (minutes)");
    int focus = 0, short_break = 0, long_break = 0;
    actions.on_configure([&](int f, int s, int l) {
        focus = f;
        short_break = s;
        long_break = l;
    });
    // Check geometry with duration editing visible, the tallest state.
    const auto size = window->window().size();
    Elements::visit_elements(window, [&](const Elements &element) {
        if (element.accessible_role() != slint::language::AccessibleRole::Button) {
            return;
        }
        const auto pos = element.absolute_position();
        const auto bounds = element.size();
        INFO(element.accessible_label().value_or(""));
        CHECK(pos.x >= -1);
        CHECK(pos.y >= -1);
        CHECK(pos.x + bounds.width <= size.width + 1);
        CHECK(pos.y + bounds.height <= size.height + 1);
    });
    click("Apply");
    CHECK(focus == 25);
    CHECK(short_break == 5);
    CHECK(long_break == 15);
    data.set_status(TimerStatus::Running);
    click("Tutup panel");
    CHECK_FALSE(window->get_expanded());
    click("Pause timer");
    CHECK(toggles == 4);
    click("Buka Oryn");
    CHECK(window->get_expanded());
    CHECK(data.get_status() == TimerStatus::Running);
    window->hide();
}

TEST_CASE("Settings theme applies only after host acceptance and keeps native text readable") {
    init_testing_backend();
    auto window = OrynWindow::create();
    const auto &theme = window->global<Theme>();
    const auto &palette = window->global<Palette>();
    const auto &data = window->global<SettingsData>();
    const auto &actions = window->global<SettingsActions>();
    data.set_loaded(true);
    slint::platform::update_timers_and_animations();
    window->set_reduce_motion(true);
    window->set_active_page("Settings");
    window->show();

    const auto luminance = [](slint::Color color) {
        const auto linear = [](unsigned char channel) {
            const double value = channel / 255.0;
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(color.red()) + 0.7152 * linear(color.green()) +
               0.0722 * linear(color.blue());
    };
    const auto check_text_contrast = [&] {
        const double background = luminance(theme.get_surface());
        for (const auto foreground : {palette.get_foreground().color(), theme.get_foreground(),
                                      theme.get_muted(), theme.get_error()}) {
            const double text = luminance(foreground);
            CHECK((std::max(text, background) + 0.05) / (std::min(text, background) + 0.05) >= 4.5);
        }
    };

    CHECK_FALSE(theme.get_light_mode());
    check_text_contrast();
    settings_click(window, "Dark theme");
    CHECK_FALSE(window->get_settings_draft_modified());
    settings_click(window, "Light theme");
    CHECK_FALSE(theme.get_light_mode());
    CHECK_FALSE(data.get_light_mode());
    CHECK(window->get_settings_draft_modified());
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == true);
    settings_click(window, "Window");
    settings_click(window, "Appearance");

    // A host acceptance is distinct from editing or emitting a command.
    std::optional<bool> submitted_theme;
    actions.on_save([&](bool light, bool, bool, bool, bool, int, int, int, int, int) {
        submitted_theme = light;
    });
    settings_click(window, "Save settings");
    REQUIRE(submitted_theme.has_value());
    CHECK(*submitted_theme);
    CHECK_FALSE(theme.get_light_mode());
    data.set_light_mode(true);
    slint::platform::update_timers_and_animations();
    CHECK(theme.get_light_mode());
    CHECK_FALSE(window->get_settings_draft_modified());
    check_text_contrast();
    data.set_error("Cannot save settings. Check the data folder permissions and retry.");
    slint::platform::update_timers_and_animations();
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == true);
    submitted_theme.reset();
    settings_click(window, "Save settings");
    REQUIRE(submitted_theme.has_value());
    CHECK(*submitted_theme); // A failed write remains retryable after host preference application.
    data.set_error("");
    data.set_light_mode(false);
    slint::platform::update_timers_and_animations();
    CHECK_FALSE(theme.get_light_mode());
    submitted_theme.reset();
    settings_click(window, "Save settings");
    REQUIRE(submitted_theme.has_value());
    CHECK(*submitted_theme); // Host updates do not discard the draft.
    settings_click(window, "Dark theme");
    CHECK_FALSE(window->get_settings_draft_modified());
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == false);
    check_text_contrast();
    window->hide();
}

TEST_CASE("Settings disabled controls cannot edit drafts or save through accessibility") {
    init_testing_backend();
    auto window = OrynWindow::create();
    const auto &data = window->global<SettingsData>();
    const auto &actions = window->global<SettingsActions>();

    SUBCASE("while loading") {
    }
    SUBCASE("while saving") {
        data.set_loaded(true);
        slint::platform::update_timers_and_animations();
        data.set_saving(true);
    }
    window->set_reduce_motion(true);
    window->set_active_page("Settings");
    window->show();
    slint::platform::update_timers_and_animations();
    actions.on_save([](bool, bool, bool, bool, bool, int, int, int, int, int) {
        FAIL("Disabled Save must not dispatch a persistence request");
    });

    for (const char *label : {"Light theme", "Dark theme"}) {
        CHECK(settings_element(window, label, Role::Button).accessible_enabled() == false);
        settings_click(window, label);
        CHECK_FALSE(window->get_settings_draft_modified());
    }
    const auto check_disabled_switch = [&](const char *label) {
        const auto control = settings_element(window, label, Role::Checkbox);
        const auto checked = control.accessible_checked();
        REQUIRE(checked.has_value());
        CHECK(control.accessible_enabled() == false);
        control.invoke_accessible_default_action();
        slint::platform::update_timers_and_animations();
        CHECK(control.accessible_checked() == checked);
    };
    check_disabled_switch("Reduce motion");
    settings_click(window, "Window");
    for (const char *label : {"Always on top", "Start expanded", "Remember position"}) {
        check_disabled_switch(label);
    }
    CHECK_FALSE(window->get_settings_draft_modified());
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == false);
    settings_click(window, "Save settings");
    window->hide();
}

TEST_CASE("Settings switches support pointer focus and keyboard activation") {
    init_testing_backend();
    auto window = OrynWindow::create();
    const auto &data = window->global<SettingsData>();
    data.set_loaded(true);
    slint::platform::update_timers_and_animations();
    window->set_reduce_motion(true);
    window->set_active_page("Settings");
    window->show();
    slint::platform::update_timers_and_animations();

    const auto control = settings_element(window, "Reduce motion", Role::Checkbox);
    CHECK(control.accessible_checked() == false);
    const auto pos = control.absolute_position();
    const auto size = control.size();
    const slint::LogicalPosition center({pos.x + size.width / 2, pos.y + size.height / 2});
    window->window().dispatch_pointer_move_event(center);
    window->window().dispatch_pointer_press_event(center, slint::PointerEventButton::Left);
    window->window().dispatch_pointer_release_event(center, slint::PointerEventButton::Left);
    slint::platform::update_timers_and_animations();
    CHECK(control.accessible_checked() == true);

    for (const auto key : {slint::platform::key_codes::Space, slint::platform::key_codes::Return}) {
        const auto checked = control.accessible_checked();
        window->window().dispatch_key_press_event(key);
        window->window().dispatch_key_release_event(key);
        slint::platform::update_timers_and_animations();
        CHECK(control.accessible_checked() != checked);
    }
    CHECK(control.accessible_checked() == true);
    CHECK_FALSE(data.get_reduce_motion());
    CHECK(window->get_settings_draft_modified());

    data.set_saving(true);
    slint::platform::update_timers_and_animations();
    window->window().dispatch_key_press_event(slint::platform::key_codes::Space);
    window->window().dispatch_key_release_event(slint::platform::key_codes::Space);
    slint::platform::update_timers_and_animations();
    CHECK(control.accessible_checked() == true);
    window->hide();
}

TEST_CASE("Settings preserves drafts across host updates and validates every saved field") {
    init_testing_backend();
    auto window = OrynWindow::create();
    const auto &data = window->global<SettingsData>();
    const auto &actions = window->global<SettingsActions>();
    window->set_active_page("Settings");
    window->show();
    CHECK(settings_element(window, "Dark theme", Role::Button).accessible_enabled() == false);
    CHECK(settings_element(window, "Light theme", Role::Button).accessible_enabled() == false);
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == false);
    CHECK(settings_element(window, "Restore defaults", Role::Button).accessible_enabled() == false);
    window->set_active_page("Time");
    slint::platform::update_timers_and_animations();
    CHECK(Elements::find_by_accessible_label(window, "Start").empty());
    window->set_active_page("Settings");
    slint::platform::update_timers_and_animations();
    data.set_window_x(-1920);
    data.set_window_y(-80);
    data.set_focus_minutes(40);
    data.set_loaded(true);
    slint::platform::update_timers_and_animations();
    window->set_reduce_motion(true);

    settings_element(window, "Reduce motion", Role::Checkbox).invoke_accessible_default_action();
    settings_click(window, "Window");
    settings_element(window, "Always on top", Role::Checkbox).invoke_accessible_default_action();
    settings_element(window, "Start expanded", Role::Checkbox).invoke_accessible_default_action();
    settings_element(window, "Remember position", Role::Checkbox)
        .invoke_accessible_default_action();
    CHECK(settings_element(window, "Window X (px)", Role::TextInput).accessible_value() == "-1920");
    settings_edit(window, "Window X (px)", "-2560");
    settings_edit(window, "Window Y (px)", "-120");
    CHECK(data.get_window_x() == -1920);
    data.set_saving(true);
    slint::platform::update_timers_and_animations();
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == false);
    data.set_saving(false);
    data.set_window_x(500);
    slint::platform::update_timers_and_animations();
    CHECK(settings_element(window, "Window X (px)", Role::TextInput).accessible_value() == "-2560");

    settings_click(window, "Timer");
    CHECK(settings_element(window, "Focus minutes", Role::TextInput).accessible_value() == "40");
    settings_edit(window, "Focus minutes", "0");
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == false);
    settings_edit(window, "Focus minutes", "181");
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == false);
    settings_edit(window, "Focus minutes", "1.5");
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == false);
    settings_edit(window, "Focus minutes", "45");
    settings_edit(window, "Short break minutes", "");
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == false);
    settings_edit(window, "Short break minutes", "7");
    settings_edit(window, "Long break minutes", "180");
    data.set_focus_minutes(30);
    data.set_error("Cannot save settings. Check the data folder permissions and retry.");
    slint::platform::update_timers_and_animations();
    CHECK(settings_element(window, "Focus minutes", Role::TextInput).accessible_value() == "45");
    data.set_error("");
    window->set_active_page("Home");
    slint::platform::update_timers_and_animations();
    window->set_active_page("Settings");
    slint::platform::update_timers_and_animations();
    settings_click(window, "Timer");
    CHECK(settings_element(window, "Focus minutes", Role::TextInput).accessible_value() == "45");

    settings_click(window, "Window");
    settings_edit(window, "Window X (px)", "not a coordinate");
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == false);
    settings_edit(window, "Window X (px)", "1000001");
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == false);
    actions.on_use_current_position([&] {
        data.set_window_x(-3440);
        data.set_window_y(-200);
    });
    settings_click(window, "Use current position");
    CHECK(settings_element(window, "Window X (px)", Role::TextInput).accessible_value() == "-3440");
    CHECK(settings_element(window, "Window Y (px)", Role::TextInput).accessible_value() == "-200");
    CHECK(settings_element(window, "Save settings", Role::Button).accessible_enabled() == true);

    using SavedSettings = std::tuple<bool, bool, bool, bool, bool, int, int, int, int, int>;
    std::optional<SavedSettings> submitted_settings;
    actions.on_save([&](bool light, bool motion, bool topmost, bool expanded, bool remember, int x,
                        int y, int focus, int short_break, int long_break) {
        submitted_settings = SavedSettings{light, motion, topmost, expanded,    remember,
                                           x,     y,      focus,   short_break, long_break};
    });
    settings_click(window, "Save settings");
    REQUIRE(submitted_settings.has_value());
    CHECK(*submitted_settings ==
          SavedSettings{false, true, false, false, true, -3440, -200, 45, 7, 180});
    CHECK(data.get_focus_minutes() == 30); // Emitting Save does not claim persistence.
    CHECK(window->get_settings_draft_modified());

    const auto size = window->window().size();
    for (const char *section : {"Appearance", "Window", "Timer"}) {
        settings_click(window, section);
        Elements::visit_elements(window, [&](const Elements &element) {
            if (element.accessible_role() != Role::Button &&
                element.accessible_role() != Role::Checkbox &&
                element.accessible_role() != Role::TextInput) {
                return;
            }
            const auto pos = element.absolute_position();
            const auto bounds = element.size();
            INFO(element.accessible_label().value_or(""));
            CHECK(bounds.width > 0);
            CHECK(bounds.height > 0);
            CHECK(pos.x >= -1);
            CHECK(pos.y >= -1);
            CHECK(pos.x + bounds.width <= size.width + 1);
            CHECK(pos.y + bounds.height <= size.height + 1);
        });
    }

    settings_click(window, "Restore defaults");
    CHECK(settings_element(window, "Focus minutes", Role::TextInput).accessible_value() == "25");
    CHECK(settings_element(window, "Short break minutes", Role::TextInput).accessible_value() ==
          "5");
    CHECK(settings_element(window, "Long break minutes", Role::TextInput).accessible_value() ==
          "15");
    settings_click(window, "Window");
    CHECK(settings_element(window, "Window X (px)", Role::TextInput).accessible_value() == "0");
    CHECK(settings_element(window, "Window Y (px)", Role::TextInput).accessible_value() == "0");
    CHECK(settings_element(window, "Always on top", Role::Checkbox).accessible_checked() == true);
    CHECK(settings_element(window, "Start expanded", Role::Checkbox).accessible_checked() == true);
    CHECK(settings_element(window, "Remember position", Role::Checkbox).accessible_checked() ==
          false);
    window->hide();
}
