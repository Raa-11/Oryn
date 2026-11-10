module;

#include "ui/main.h"
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <filesystem>
#include <string>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <vector>
#include <format>

module oryn.app;

import oryn.platform.window;
import oryn.time;
import oryn.settings;
import oryn.logging;
import oryn.platform.paths;
import oryn.calendar;

namespace oryn::app {
    namespace {
        void apply_time_snapshot(const slint::ComponentHandle<OrynWindow> &view,
                                 const time::Snapshot &state) {
            const char *phase = "Long break";
            switch (state.phase) {
            case time::Phase::focus:
                phase = "Focus";
                break;
            case time::Phase::short_break:
                phase = "Short break";
                break;
            default:
                break;
            }

            const char *status = "Ready";
            if (state.completed) {
                status = "Finished";
            } else if (state.status == time::Status::running) {
                status = "Running";
            } else if (state.status == time::Status::paused) {
                status = "Paused";
            }

            const auto &data = view->global<TimeData>();
            switch (state.status) {
            case time::Status::idle:
                data.set_status(TimerStatus::Idle);
                break;
            case time::Status::running:
                data.set_status(TimerStatus::Running);
                break;
            case time::Status::paused:
                data.set_status(TimerStatus::Paused);
                break;
            case time::Status::finished:
                data.set_status(TimerStatus::Finished);
                break;
            }
            data.set_pomodoro(state.pomodoro);
            data.set_focus_minutes(static_cast<int>(state.config.focus.count() / 60));
            data.set_short_minutes(static_cast<int>(state.config.short_break.count() / 60));
            data.set_long_minutes(static_cast<int>(state.config.long_break.count() / 60));
            data.set_remaining(time::format_remaining(state.remaining_seconds).c_str());
            data.set_phase(phase);
            data.set_title(state.pomodoro ? phase : "Countdown");
            data.set_status_text(status);
            data.set_session(state.focus_session);
            data.set_progress(1.0f -
                              static_cast<float>(state.remaining_seconds) / state.total_seconds);
            data.set_error(state.error.c_str());
            data.set_completed(state.completed);
        }

        struct SettingsBinding {
            settings::Preferences preferences;
            settings::SettingsService *service = nullptr;
            std::optional<settings::Preferences> pending_timer_preferences;
            bool loaded = false;
            bool native_ready = false;
        };

        void apply_preferences(const slint::ComponentHandle<OrynWindow> &view,
                               const settings::Preferences &preferences, bool startup,
                               bool native_ready) {
            const bool refresh_draft = !view->get_settings_draft_modified();
            const auto &data = view->global<SettingsData>();
            data.set_light_mode(preferences.light_mode);
            data.set_reduce_motion(preferences.reduce_motion);
            data.set_always_on_top(preferences.always_on_top);
            data.set_expanded(preferences.expanded);
            data.set_remember_position(preferences.remember_position);
            data.set_window_x(preferences.window_x);
            data.set_window_y(preferences.window_y);
            data.set_focus_minutes(preferences.focus_minutes);
            data.set_short_minutes(preferences.short_minutes);
            data.set_long_minutes(preferences.long_minutes);
            if (refresh_draft) {
                view->invoke_refresh_settings_draft();
            }
            view->global<Theme>().set_light_mode(preferences.light_mode);
            view->set_reduce_motion(preferences.reduce_motion);
            if (startup) {
                view->set_expanded(preferences.expanded);
            }
            if (native_ready) {
                std::optional<platform::WindowPosition> position;
                if (preferences.remember_position) {
                    position = platform::WindowPosition{preferences.window_x, preferences.window_y};
                }
                if (const auto result = platform::apply_window_preferences(
                        view->window(), preferences.always_on_top, position);
                    !result) {
                    logging::error(result.error().operation, result.error().system_code);
                    data.set_error("Window preferences could not be applied.");
                }
            }
        }

        bool matches_configuration(const time::Snapshot &snapshot,
                                   const settings::Preferences &preferences) {
            return snapshot.config.focus == std::chrono::minutes{preferences.focus_minutes} &&
                   snapshot.config.short_break == std::chrono::minutes{preferences.short_minutes} &&
                   snapshot.config.long_break == std::chrono::minutes{preferences.long_minutes};
        }

        std::optional<calendar::TaskId> task_id(const slint::SharedString &text) {
            const std::string_view value = text;
            calendar::TaskId id = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), id);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || id <= 0) {
                return std::nullopt;
            }
            return id;
        }

        std::string calendar_day_label(std::string_view iso) {
            if (iso.size() != 10) {
                return {};
            }
            int year = 0, month = 0, day = 0;
            const auto first = std::from_chars(iso.data(), iso.data() + 4, year);
            const auto second = std::from_chars(iso.data() + 5, iso.data() + 7, month);
            const auto third = std::from_chars(iso.data() + 8, iso.data() + 10, day);
            const std::chrono::year_month_day date{
                std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                std::chrono::day{static_cast<unsigned>(day)}};
            if (first.ec != std::errc{} || second.ec != std::errc{} || third.ec != std::errc{} ||
                !date.ok()) {
                return {};
            }
            return std::format("{:%a, %d %b}", std::chrono::sys_days{date});
        }

        void apply_calendar_snapshot(const slint::ComponentHandle<OrynWindow> &view,
                                     const calendar::Snapshot &state) {
            const auto &data = view->global<CalendarData>();
            if (!state.busy) {
                std::vector<CalendarTask> rows;
                rows.reserve(state.tasks.size());
                std::string_view previous_date;
                for (const auto &task : state.tasks) {
                    CalendarTask row;
                    row.id = std::to_string(task.id);
                    row.title = task.title;
                    row.notes = task.notes;
                    row.due_date = task.due_date;
                    row.completed = task.completed;
                    row.group_start = rows.empty() || previous_date != task.due_date;
                    if (task.due_date.empty()) {
                        row.group_label = "No due date";
                    } else if (state.filter != calendar::Filter::completed &&
                               task.due_date < state.today) {
                        row.group_label = ("Overdue · " + task.due_date).c_str();
                    } else if (task.due_date == state.today) {
                        row.group_label = ("Today · " + task.due_date).c_str();
                    } else {
                        row.group_label = task.due_date;
                    }
                    previous_date = task.due_date;
                    rows.push_back(std::move(row));
                }
                data.set_tasks(std::make_shared<slint::VectorModel<CalendarTask>>(std::move(rows)));
            }
            switch (state.filter) {
            case calendar::Filter::today:
                data.set_filter(CalendarFilter::Today);
                break;
            case calendar::Filter::upcoming:
                data.set_filter(CalendarFilter::Upcoming);
                break;
            case calendar::Filter::completed:
                data.set_filter(CalendarFilter::Completed);
                break;
            }
            data.set_today(state.today.c_str());
            data.set_today_label(calendar_day_label(state.today).c_str());
            data.set_page(state.page);
            data.set_has_more(state.has_more);
            data.set_today_count(std::to_string(state.today_count).c_str());
            data.set_loaded(state.loaded);
            data.set_error(state.error.c_str());
            data.set_busy(state.busy);
            // A revision is proof of a committed mutation, not an optimistic UI command.
            data.set_revision(std::to_string(state.revision).c_str());
            if (!state.error.empty()) {
                logging::error("calendar.storage_or_command", 0);
            }
        }
    } // namespace

    int Application::run() const {
        logging::LoggingSession logging_session;
        logging::info("application.start");
        auto window = OrynWindow::create();
        platform::TrayIcon tray;
        auto binding = std::make_shared<SettingsBinding>();
        const auto weak = slint::ComponentWeakHandle(window);
        const auto directory = platform::app_data_directory();
        const std::optional<std::filesystem::path> database_path =
            directory ? std::optional{*directory / "settings.sqlite3"} : std::nullopt;
        std::unique_ptr<calendar::CalendarService> calendar_service;
        window->global<CalendarData>().set_busy(true);
        const auto start_calendar = [weak, &calendar_service](std::filesystem::path path) {
            if (calendar_service) {
                return;
            }
            struct CalendarMailbox {
                std::mutex mutex;
                std::optional<calendar::Snapshot> latest;
                bool queued = false;
            };
            auto mailbox = std::make_shared<CalendarMailbox>();
            calendar_service = std::make_unique<calendar::CalendarService>(
                std::move(path), [weak, mailbox](calendar::Snapshot snapshot) {
                    {
                        const std::lock_guard lock{mailbox->mutex};
                        mailbox->latest = std::move(snapshot);
                        if (mailbox->queued) {
                            return;
                        }
                        mailbox->queued = true;
                    }
                    slint::invoke_from_event_loop([weak, mailbox] {
                        std::optional<calendar::Snapshot> state;
                        {
                            const std::lock_guard lock{mailbox->mutex};
                            state = std::move(mailbox->latest);
                            mailbox->latest.reset();
                            mailbox->queued = false;
                        }
                        if (auto ui = weak.lock(); ui && state) {
                            apply_calendar_snapshot(*ui, *state);
                        }
                    });
                });
        };

        const auto submit_calendar = [weak, &calendar_service](auto command) {
            if (auto ui = weak.lock()) {
                const auto &data = (*ui)->global<CalendarData>();
                if (!calendar_service) {
                    data.set_busy(false);
                    data.set_error("Calendar storage is unavailable.");
                    return;
                }
                data.set_error("");
                data.set_busy(true);
                if (!command(*calendar_service)) {
                    data.set_error("Calendar is busy. Retry after the current operation.");
                }
            }
        };
        const auto &calendar_actions = window->global<CalendarActions>();
        calendar_actions.on_save(
            [submit_calendar, weak](slint::SharedString id_text, slint::SharedString title,
                                    slint::SharedString notes, slint::SharedString due_date) {
                const auto id = task_id(id_text);
                if (!id_text.empty() && !id) {
                    if (auto ui = weak.lock()) {
                        (*ui)->global<CalendarData>().set_error("Select a valid task.");
                    }
                    return;
                }
                calendar::Draft draft{std::string{std::string_view{title}},
                                      std::string{std::string_view{notes}},
                                      std::string{std::string_view{due_date}}};
                submit_calendar(
                    [id, draft = std::move(draft)](calendar::CalendarService &service) mutable {
                        return id ? service.update(*id, std::move(draft))
                                  : service.create(std::move(draft));
                    });
            });
        calendar_actions.on_set_completed(
            [submit_calendar, weak](slint::SharedString id_text, bool completed) {
                if (const auto id = task_id(id_text)) {
                    submit_calendar([id = *id, completed](calendar::CalendarService &service) {
                        return service.set_completed(id, completed);
                    });
                } else if (auto ui = weak.lock()) {
                    (*ui)->global<CalendarData>().set_error("Select a valid task.");
                }
            });
        calendar_actions.on_remove([submit_calendar, weak](slint::SharedString id_text) {
            if (const auto id = task_id(id_text)) {
                submit_calendar(
                    [id = *id](calendar::CalendarService &service) { return service.erase(id); });
            } else if (auto ui = weak.lock()) {
                (*ui)->global<CalendarData>().set_error("Select a valid task.");
            }
        });
        calendar_actions.on_select_filter([submit_calendar](CalendarFilter selected) {
            auto filter = calendar::Filter::today;
            switch (selected) {
            case CalendarFilter::Today:
                break;
            case CalendarFilter::Upcoming:
                filter = calendar::Filter::upcoming;
                break;
            case CalendarFilter::Completed:
                filter = calendar::Filter::completed;
                break;
            }
            submit_calendar([filter](calendar::CalendarService &service) {
                return service.select_filter(filter);
            });
        });
        calendar_actions.on_change_page([submit_calendar](int page) {
            submit_calendar(
                [page](calendar::CalendarService &service) { return service.page(page); });
        });
        calendar_actions.on_refresh([submit_calendar] {
            submit_calendar([](calendar::CalendarService &service) { return service.refresh(); });
        });

        const auto commit_preferences = [binding, weak](settings::Preferences preferences) {
            binding->preferences = preferences;
            if (auto ui = weak.lock()) {
                apply_preferences(*ui, preferences, false, binding->native_ready);
                if (binding->service) {
                    binding->service->save(preferences);
                } else {
                    (*ui)->global<SettingsData>().set_saving(false);
                    (*ui)->global<SettingsData>().set_error("Settings storage is unavailable.");
                }
            }
        };

        // One replaceable timer snapshot and at most one queued UI callback.
        struct Mailbox {
            std::mutex mutex;
            std::optional<time::Snapshot> latest;
            bool queued = false;
        };

        auto mailbox = std::make_shared<Mailbox>();
        time::TimerService timer(
            [mailbox, binding, weak, commit_preferences](time::Snapshot snapshot) {
                {
                    std::lock_guard lock(mailbox->mutex);
                    mailbox->latest = std::move(snapshot);
                    if (mailbox->queued) {
                        return;
                    }
                    mailbox->queued = true;
                }
                slint::invoke_from_event_loop([mailbox, binding, weak, commit_preferences] {
                    std::optional<time::Snapshot> state;
                    {
                        std::lock_guard lock(mailbox->mutex);
                        state = std::move(mailbox->latest);
                        mailbox->latest.reset();
                        mailbox->queued = false;
                    }
                    if (auto ui = weak.lock(); ui && state) {
                        apply_time_snapshot(*ui, *state);
                        if (binding->pending_timer_preferences) {
                            if (!state->error.empty()) {
                                (*ui)->global<SettingsData>().set_saving(false);
                                (*ui)->global<SettingsData>().set_error(state->error.c_str());
                                binding->pending_timer_preferences.reset();
                            } else if (matches_configuration(*state,
                                                             *binding->pending_timer_preferences)) {
                                const auto preferences = *binding->pending_timer_preferences;
                                binding->pending_timer_preferences.reset();
                                commit_preferences(preferences);
                            }
                        }
                    }
                });
            });

        std::unique_ptr<settings::SettingsService> settings_service;
        if (directory) {
            settings_service = std::make_unique<settings::SettingsService>(
                *database_path, [binding, weak, &timer, start_calendar,
                                 &database_path](settings::Snapshot snapshot) {
                    slint::invoke_from_event_loop([binding, weak, &timer, start_calendar,
                                                   &database_path, snapshot = std::move(snapshot)] {
                        if (auto ui = weak.lock()) {
                            const auto &data = (*ui)->global<SettingsData>();
                            data.set_saving(snapshot.saving);
                            data.set_error(snapshot.error.c_str());
                            if (!snapshot.error.empty()) {
                                logging::error("settings.storage", 0);
                            }
                            if (!binding->loaded) {
                                binding->loaded = true;
                                binding->preferences = snapshot.preferences;
                                apply_preferences(*ui, snapshot.preferences, true,
                                                  binding->native_ready);
                                timer.configure(snapshot.preferences.focus_minutes,
                                                snapshot.preferences.short_minutes,
                                                snapshot.preferences.long_minutes);
                                data.set_loaded(true);
                                logging::info(snapshot.loaded ? "settings.loaded"
                                                              : "settings.defaults.storage_error");
                                // Start the second connection only after the first migration/load
                                // attempt.
                                start_calendar(*database_path);
                            }
                        }
                    });
                });
            binding->service = settings_service.get();
        } else {
            logging::error(directory.error().operation, directory.error().system_code);
            window->global<SettingsData>().set_error("Application data directory is unavailable.");
            window->global<SettingsData>().set_loaded(true);
            window->global<CalendarData>().set_busy(false);
            window->global<CalendarData>().set_error("Application data directory is unavailable.");
            binding->loaded = true;
        }

        const auto request_preferences = [binding, weak, &timer,
                                          commit_preferences](settings::Preferences preferences) {
            if (!binding->loaded) {
                return;
            }
            if (preferences.focus_minutes < 1 || preferences.focus_minutes > 180 ||
                preferences.short_minutes < 1 || preferences.short_minutes > 180 ||
                preferences.long_minutes < 1 || preferences.long_minutes > 180) {
                if (auto ui = weak.lock()) {
                    (*ui)->global<SettingsData>().set_error(
                        "Timer durations must be 1–180 minutes.");
                }
                return;
            }
            const auto &previous = binding->preferences;
            if (preferences.focus_minutes != previous.focus_minutes ||
                preferences.short_minutes != previous.short_minutes ||
                preferences.long_minutes != previous.long_minutes) {
                if (auto ui = weak.lock()) {
                    (*ui)->global<SettingsData>().set_saving(true);
                }
                binding->pending_timer_preferences = preferences;
                timer.configure(preferences.focus_minutes, preferences.short_minutes,
                                preferences.long_minutes);
            } else {
                commit_preferences(preferences);
            }
        };

        const auto &settings_actions = window->global<SettingsActions>();
        settings_actions.on_save([request_preferences](bool light_mode, bool reduce_motion,
                                                       bool always_on_top, bool expanded,
                                                       bool remember_position, int x, int y,
                                                       int focus, int short_break, int long_break) {
            request_preferences(settings::Preferences{.light_mode = light_mode,
                                                      .reduce_motion = reduce_motion,
                                                      .always_on_top = always_on_top,
                                                      .expanded = expanded,
                                                      .remember_position = remember_position,
                                                      .window_x = x,
                                                      .window_y = y,
                                                      .focus_minutes = focus,
                                                      .short_minutes = short_break,
                                                      .long_minutes = long_break});
        });
        settings_actions.on_restore_defaults(
            [request_preferences] { request_preferences(settings::Preferences{}); });
        settings_actions.on_use_current_position([weak] {
            if (auto ui = weak.lock()) {
                if (const auto position = platform::window_position((*ui)->window())) {
                    const auto &data = (*ui)->global<SettingsData>();
                    data.set_window_x(position->x);
                    data.set_window_y(position->y);
                }
            }
        });
        settings_actions.on_exit([] { slint::quit_event_loop(); });

        const auto &actions = window->global<TimeActions>();
        actions.on_start_duration(
            [&timer](int minutes, int seconds) { timer.countdown(minutes, seconds); });
        actions.on_toggle([&timer] { timer.toggle(); });
        actions.on_reset([&timer] { timer.reset(); });
        actions.on_cancel([&timer] { timer.cancel(); });
        actions.on_select_mode([&timer](bool pomodoro) { timer.select_mode(pomodoro); });
        actions.on_configure(
            [binding, request_preferences](int focus, int short_break, int long_break) {
                auto preferences = binding->preferences;
                preferences.focus_minutes = focus;
                preferences.short_minutes = short_break;
                preferences.long_minutes = long_break;
                request_preferences(preferences);
            });
        window->on_exit_requested([] { slint::quit_event_loop(); });
        const auto refresh_window = [weak] {
            if (auto ui = weak.lock()) {
                if (const auto result =
                        platform::update_window_shape((*ui)->window(), (*ui)->get_reveal(),
                                                      (*ui)->get_panel_design_height());
                    !result) {
                    logging::error(result.error().operation, result.error().system_code);
                }
            }
        };
        window->on_shape_changed(refresh_window);
        if (const auto result = platform::place_window(window->window(), window->get_panel_width());
            !result) {
            logging::error(result.error().operation, result.error().system_code);
        }
        window->show();
        slint::Timer::single_shot(std::chrono::milliseconds{50}, [weak, binding, &tray] {
            if (auto ui = weak.lock()) {
                if (const auto result =
                        platform::initialize_window((*ui)->window(), (*ui)->get_reveal(),
                                                    (*ui)->get_panel_design_height());
                    !result) {
                    logging::error(result.error().operation, result.error().system_code);
                    return;
                }
                binding->native_ready = true;
                if (binding->loaded) {
                    apply_preferences(*ui, binding->preferences, false, true);
                }
                if (const auto result = tray.attach((*ui)->window()); !result) {
                    logging::error(result.error().operation, result.error().system_code);
                }
            }
        });
        slint::run_event_loop(slint::EventLoopMode::RunUntilQuit);
        // Drain accepted storage commands while the logging backend and UI still exist.
        calendar_service.reset();
        settings_service.reset();
        binding->service = nullptr;
        logging::info("application.stop");
        return 0;
    }
} // namespace oryn::app
