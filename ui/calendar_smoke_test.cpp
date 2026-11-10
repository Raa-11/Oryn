#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <utility>
#include "ui/testing/main.h"
#include <doctest/doctest.h>
#include <slint-platform.h>
#include <slint-testing.h>

namespace {
    using Element = slint::testing::ElementHandle;
    using Role = slint::language::AccessibleRole;
    using Window = slint::ComponentHandle<OrynWindow>;

    void settle() {
        slint::platform::update_timers_and_animations();
    }

    Window make_window() {
        static const bool initialized = [] {
            slint::testing::init();
            return true;
        }();
        (void)initialized;
        auto window = OrynWindow::create();
        window->global<SettingsData>().set_loaded(true);
        window->set_reduce_motion(true);
        window->global<CalendarData>().set_today("2026-11-08");
        window->global<CalendarData>().set_today_label("Sun, 08 Nov");
        window->set_active_page("Calendar");
        window->show();
        settle();
        return window;
    }

    std::optional<Element> find(const Window &window, const char *label, Role role) {
        std::optional<Element> result;
        for (const auto &element : Element::find_by_accessible_label(window, label)) {
            if (element.accessible_role() == role) {
                REQUIRE_FALSE(result.has_value());
                result = element;
            }
        }
        return result;
    }

    Element control(const Window &window, const char *label, Role role = Role::Button) {
        auto result = find(window, label, role);
        if (!result && std::string_view{label} == "Task notes") {
            if (const auto expand = find(window, "Expand notes", Role::Button)) {
                expand->invoke_accessible_default_action();
                settle();
                result = find(window, label, role);
            }
        }
        INFO(std::string{label});
        REQUIRE(result.has_value());
        return *result;
    }

    void click(const Window &window, const char *label, Role role = Role::Button) {
        const std::string_view name{label};
        if ((name == "Today" || name == "Upcoming" || name == "Completed") &&
            !find(window, label, role)) {
            control(window, "Task filter").invoke_accessible_default_action();
            settle();
        }
        control(window, label, role).invoke_accessible_default_action();
        settle();
    }

    void edit(const Window &window, const char *label, const char *value) {
        control(window, label, Role::TextInput).set_accessible_value(value);
        settle();
    }

    CalendarTask task(bool completed = false) {
        CalendarTask result;
        result.id = "9223372036854775806";
        result.title = "Write release notes";
        result.notes = "First line\nSecond line";
        result.due_date = "2026-11-08";
        result.completed = completed;
        result.group_label = "Today · 2026-11-08";
        result.group_start = true;
        return result;
    }

    void publish(const CalendarData &data, std::vector<CalendarTask> tasks) {
        data.set_tasks(std::make_shared<slint::VectorModel<CalendarTask>>(std::move(tasks)));
        data.set_loaded(true);
        data.set_busy(false);
        settle();
    }

    void check_bounds(const Window &window) {
        const auto size = window->window().size();
        Element::visit_elements(window, [&](const Element &element) {
            const auto role = element.accessible_role();
            if (role != Role::Button && role != Role::Checkbox && role != Role::TextInput) {
                return;
            }
            const auto position = element.absolute_position();
            const auto bounds = element.size();
            INFO(element.accessible_label().value_or(""));
            CHECK(bounds.width > 0);
            CHECK(bounds.height > 0);
            CHECK(position.x >= -1);
            CHECK(position.y >= -1);
            CHECK(position.x + bounds.width <= size.width + 1);
            CHECK(position.y + bounds.height <= size.height + 1);
        });
    }
} // namespace

TEST_CASE("Calendar draft survives navigation and failed saves until committed revision") {
    auto window = make_window();
    const auto &data = window->global<CalendarData>();
    const auto &actions = window->global<CalendarActions>();
    publish(data, {});
    click(window, "New task");
    CHECK(control(window, "Task due date", Role::TextInput).accessible_value() == "2026-11-08");
    CHECK(control(window, "Create task").accessible_enabled() == false);
    edit(window, "Task title", "   ");
    CHECK(control(window, "Create task").accessible_enabled() == false);
    edit(window, "Task title", "Prepare café release");
    edit(window, "Task notes", "First line\nSecond line");
    edit(window, "Task due date", "2026-02-30");
    window->set_active_page("Home");
    settle();
    data.set_today("2026-11-09");
    window->set_active_page("Calendar");
    settle();
    CHECK(control(window, "Task title", Role::TextInput).accessible_value() ==
          "Prepare café release");
    CHECK(control(window, "Task notes", Role::TextInput).accessible_value() ==
          "First line\nSecond line");
    CHECK(control(window, "Task due date", Role::TextInput).accessible_value() == "2026-02-30");

    using Saved = std::tuple<std::string, std::string, std::string, std::string>;
    std::optional<Saved> submitted;
    actions.on_save([&](slint::SharedString id, slint::SharedString title,
                        slint::SharedString notes, slint::SharedString due) {
        submitted.emplace(id.data(), title.data(), notes.data(), due.data());
        data.set_busy(true);
    });
    click(window, "Create task");
    REQUIRE(submitted.has_value());
    CHECK(*submitted == Saved{"", "Prepare café release", "First line\nSecond line", "2026-02-30"});
    CHECK(control(window, "Create task").accessible_enabled() == false);
    CHECK(control(window, "Task title", Role::TextInput).accessible_read_only() == true);
    data.set_error("Enter a valid due date (YYYY-MM-DD), or leave it blank.");
    data.set_busy(false);
    settle();
    CHECK(control(window, "Task due date", Role::TextInput).accessible_value() == "2026-02-30");
    CHECK(control(window, "Create task").accessible_enabled() == true);
    edit(window, "Task due date", "");
    data.set_error("");
    click(window, "Create task");
    CHECK(std::get<3>(*submitted).empty());
    data.set_busy(false);
    settle();
    CHECK(find(window, "Task title", Role::TextInput).has_value());
    // A successful host load is not a committed mutation.
    data.set_page(0);
    data.set_tasks(std::make_shared<slint::VectorModel<CalendarTask>>());
    settle();
    CHECK(find(window, "Task title", Role::TextInput).has_value());
    // Revision can arrive while another page is open; draft acknowledgement is global.
    window->set_active_page("Home");
    settle();
    data.set_revision("1");
    settle();
    window->set_active_page("Calendar");
    settle();
    CHECK_FALSE(find(window, "Task title", Role::TextInput).has_value());
    CHECK(control(window, "New task").accessible_enabled() == true);
    window->hide();
}

TEST_CASE("Calendar edits every field and completion does not discard unsaved changes") {
    auto window = make_window();
    const auto &data = window->global<CalendarData>();
    const auto &actions = window->global<CalendarActions>();
    publish(data, {task()});
    click(window, "Edit Write release notes");
    CHECK(control(window, "Task notes", Role::TextInput).accessible_value() ==
          "First line\nSecond line");
    edit(window, "Task title", "Updated release notes");
    edit(window, "Task notes", "Unsaved details\nKeep these");
    std::string completed_id;
    bool completed = false;
    actions.on_set_completed([&](slint::SharedString id, bool value) {
        completed_id = id.data();
        completed = value;
        data.set_busy(true);
    });
    click(window, "Complete Updated release notes", Role::Checkbox);
    CHECK(completed_id == "9223372036854775806");
    CHECK(completed);
    CHECK(control(window, "Complete Updated release notes", Role::Checkbox).accessible_checked() ==
          false);
    data.set_busy(false);
    data.set_revision("1");
    settle();
    CHECK(control(window, "Reopen Updated release notes", Role::Checkbox).accessible_checked() ==
          true);
    CHECK(control(window, "Task notes", Role::TextInput).accessible_value() ==
          "Unsaved details\nKeep these");
    std::string saved_id;
    actions.on_save([&](slint::SharedString id, slint::SharedString title,
                        slint::SharedString notes, slint::SharedString due) {
        saved_id = id.data();
        CHECK(title == "Updated release notes");
        CHECK(notes == "Unsaved details\nKeep these");
        CHECK(due == "2026-11-08");
        data.set_busy(true);
    });
    click(window, "Save task");
    CHECK(saved_id == "9223372036854775806");
    data.set_busy(false);
    data.set_revision("2");
    settle();
    CHECK_FALSE(find(window, "Task title", Role::TextInput).has_value());
    click(window, "Edit Write release notes");
    int removed = 0;
    actions.on_remove([&](slint::SharedString id) {
        CHECK(id == "9223372036854775806");
        ++removed;
        data.set_busy(true);
    });
    click(window, "Delete Write release notes");
    CHECK(removed == 0);
    check_bounds(window);
    click(window, "Confirm delete");
    CHECK(removed == 1);
    CHECK(find(window, "Task title", Role::TextInput).has_value());
    publish(data, {});
    data.set_revision("3");
    settle();
    CHECK_FALSE(find(window, "Task title", Role::TextInput).has_value());
    window->hide();
}

TEST_CASE("Calendar filters pages completion and confirmed deletion follow accepted host state") {
    auto window = make_window();
    const auto &data = window->global<CalendarData>();
    const auto &actions = window->global<CalendarActions>();
    int refreshes = 0;
    actions.on_refresh([&] { ++refreshes; });
    data.set_error("Cannot open task storage. Use Refresh to retry.");
    settle();
    CHECK(control(window, "New task").accessible_enabled() == false);
    CHECK(control(window, "Refresh").accessible_enabled() == true);
    click(window, "Refresh");
    CHECK(refreshes == 1);
    data.set_error("");
    publish(data, {});
    std::optional<CalendarFilter> selected;
    actions.on_select_filter([&](CalendarFilter value) {
        selected = value;
        data.set_filter(value);
    });
    click(window, "Upcoming");
    CHECK(selected == CalendarFilter::Upcoming);
    click(window, "Completed");
    CHECK(selected == CalendarFilter::Completed);
    publish(data, {task(true)});
    bool reopened = false;
    actions.on_set_completed([&](slint::SharedString, bool complete) {
        reopened = !complete;
        data.set_busy(true);
    });
    click(window, "Reopen Write release notes", Role::Checkbox);
    CHECK(reopened);
    CHECK(control(window, "Reopen Write release notes", Role::Checkbox).accessible_checked() ==
          true);
    data.set_busy(false);
    data.set_revision("1");
    settle();
    int deletes = 0;
    actions.on_remove([&](slint::SharedString id) {
        CHECK(id == "9223372036854775806");
        ++deletes;
        data.set_busy(true);
    });
    click(window, "Delete Write release notes");
    CHECK(deletes == 0);
    click(window, "Keep task");
    CHECK_FALSE(find(window, "Confirm delete", Role::Button).has_value());
    click(window, "Delete Write release notes");
    click(window, "Confirm delete");
    CHECK(deletes == 1);
    CHECK(control(window, "Confirm delete").accessible_enabled() == false);
    data.set_error("Could not delete task.");
    data.set_busy(false);
    settle();
    CHECK(control(window, "Confirm delete").accessible_enabled() == true);
    data.set_error("");
    click(window, "Confirm delete");
    publish(data, {});
    data.set_revision("2");
    settle();
    CHECK_FALSE(find(window, "Confirm delete", Role::Button).has_value());
    CHECK_FALSE(find(window, "Delete Write release notes", Role::Button).has_value());
    int requested_page = -1;
    actions.on_change_page([&](int page) {
        requested_page = page;
        data.set_page(page);
    });
    data.set_has_more(true);
    settle();
    click(window, "Next page");
    CHECK(requested_page == 1);
    CHECK(control(window, "Previous page").accessible_enabled() == true);
    click(window, "Previous page");
    CHECK(requested_page == 0);
    window->set_active_page("Home");
    settle();
    click(window, "Open Calendar");
    CHECK(refreshes == 2);
    window->hide();
}

TEST_CASE("Calendar keyboard controls and fields fit the selected Pencil page geometry") {
    auto window = make_window();
    const auto &data = window->global<CalendarData>();
    publish(data, {});
    CHECK(window->get_panel_design_height() == 331);
    const auto agenda_width = window->window().size().width;
    const auto button = control(window, "New task");
    const auto position = button.absolute_position();
    const auto bounds = button.size();
    const slint::LogicalPosition center(
        {position.x + bounds.width / 2, position.y + bounds.height / 2});
    window->window().dispatch_pointer_move_event(center);
    window->window().dispatch_pointer_press_event(center, slint::PointerEventButton::Left);
    window->window().dispatch_pointer_release_event(center, slint::PointerEventButton::Left);
    settle();
    REQUIRE(find(window, "Task title", Role::TextInput).has_value());
    CHECK(window->get_panel_design_height() == 394);
    CHECK(window->window().size().width == agenda_width);
    edit(window, "Task title", "Keyboard task");
    check_bounds(window);
    // Cancel returns to the list without changing host data.
    const auto cancel = control(window, "Cancel");
    const auto cancel_position = cancel.absolute_position();
    const auto cancel_size = cancel.size();
    const slint::LogicalPosition cancel_center(
        {cancel_position.x + cancel_size.width / 2, cancel_position.y + cancel_size.height / 2});
    window->window().dispatch_pointer_move_event(cancel_center);
    window->window().dispatch_pointer_press_event(cancel_center, slint::PointerEventButton::Left);
    window->window().dispatch_pointer_release_event(cancel_center, slint::PointerEventButton::Left);
    settle();
    CHECK_FALSE(find(window, "Task title", Role::TextInput).has_value());
    CHECK(window->get_panel_design_height() == 331);
    CHECK(window->window().size().width == agenda_width);
    check_bounds(window);
    publish(data, {task()});
    int toggles = 0;
    window->global<CalendarActions>().on_set_completed(
        [&](slint::SharedString, bool) { ++toggles; });
    const auto checkbox = control(window, "Complete Write release notes", Role::Checkbox);
    const auto checkbox_position = checkbox.absolute_position();
    const auto checkbox_size = checkbox.size();
    const slint::LogicalPosition checkbox_center({checkbox_position.x + checkbox_size.width / 2,
                                                  checkbox_position.y + checkbox_size.height / 2});
    window->window().dispatch_pointer_move_event(checkbox_center);
    window->window().dispatch_pointer_press_event(checkbox_center, slint::PointerEventButton::Left);
    window->window().dispatch_pointer_release_event(checkbox_center,
                                                    slint::PointerEventButton::Left);
    settle();
    CHECK(toggles == 1);
    for (const auto key : {slint::platform::key_codes::Space, slint::platform::key_codes::Return}) {
        window->window().dispatch_key_press_event(key);
        window->window().dispatch_key_release_event(key);
        settle();
    }
    CHECK(toggles == 3);
    CHECK(checkbox.accessible_checked() == false); // No optimistic completion.
    data.set_busy(true);
    settle();
    window->window().dispatch_key_press_event(slint::platform::key_codes::Space);
    window->window().dispatch_key_release_event(slint::platform::key_codes::Space);
    checkbox.invoke_accessible_default_action();
    settle();
    CHECK(toggles == 3);
    window->hide();
}

TEST_CASE("Calendar scroll reaches the last task without growing the native window") {
    auto window = make_window();
    const auto &data = window->global<CalendarData>();
    std::vector<CalendarTask> tasks;
    tasks.reserve(50);
    for (int index = 0; index != 50; ++index) {
        auto item = task();
        item.id = std::to_string(index + 1).c_str();
        item.title = ("Task " + std::to_string(index + 1)).c_str();
        item.group_start = index == 0;
        tasks.push_back(std::move(item));
    }
    publish(data, std::move(tasks));
    const auto native_size = window->window().size();
    const auto first = control(window, "Complete Task 1", Role::Checkbox);
    const auto position = first.absolute_position();
    const auto bounds = first.size();
    const slint::LogicalPosition scroll_origin(
        {position.x + bounds.width / 2, position.y + bounds.height / 2});
    window->window().dispatch_pointer_move_event(scroll_origin);
    window->window().dispatch_pointer_scroll_event(scroll_origin, 0, -10000);
    // ScrollView animates wheel motion; advance the pinned testing backend clock deterministically.
    slint::cbindgen_private::slint_mock_elapsed_time(500);
    settle();
    const auto last = control(window, "Complete Task 50", Role::Checkbox);
    const auto last_position = last.absolute_position();
    const auto last_bounds = last.size();
    CHECK(last_position.y > 0);
    CHECK(last_position.y + last_bounds.height <= native_size.height);
    std::string completed_id;
    window->global<CalendarActions>().on_set_completed([&](slint::SharedString id, bool value) {
        CHECK(value);
        completed_id = id.data();
    });
    const slint::LogicalPosition last_center(
        {last_position.x + last_bounds.width / 2, last_position.y + last_bounds.height / 2});
    window->window().dispatch_pointer_move_event(last_center);
    window->window().dispatch_pointer_press_event(last_center, slint::PointerEventButton::Left);
    window->window().dispatch_pointer_release_event(last_center, slint::PointerEventButton::Left);
    settle();
    CHECK(completed_id == "50");
    CHECK(window->window().size().width == native_size.width);
    CHECK(window->window().size().height == native_size.height);
    window->hide();
}
