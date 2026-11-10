module;

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <ctime>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <entt/entt.hpp>

module oryn.calendar;
import oryn.database;

namespace oryn::calendar {
    namespace {
        constexpr std::size_t queue_capacity = 64;
        constexpr std::size_t page_size = 50;

        bool valid_day(std::string_view text) {
            if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
                return false;
            }
            int year = 0;
            unsigned month = 0;
            unsigned day = 0;
            for (std::size_t index = 0; index < text.size(); ++index) {
                if (index == 4 || index == 7) {
                    continue;
                }
                if (text[index] < '0' || text[index] > '9') {
                    return false;
                }
                const auto digit = static_cast<unsigned>(text[index] - '0');
                if (index < 4) {
                    year = year * 10 + static_cast<int>(digit);
                } else if (index < 7) {
                    month = month * 10 + digit;
                } else {
                    day = day * 10 + digit;
                }
            }
            return year >= 1 &&
                   std::chrono::year_month_day{std::chrono::year{year}, std::chrono::month{month},
                                               std::chrono::day{day}}
                       .ok();
        }

        std::optional<database::TaskFilter> storage_filter(Filter filter) {
            switch (filter) {
            case Filter::today:
                return database::TaskFilter::today;
            case Filter::upcoming:
                return database::TaskFilter::upcoming;
            case Filter::completed:
                return database::TaskFilter::completed;
            }
            return std::nullopt;
        }

        struct TaskCreated {
            TaskId id;
        };

        struct TaskUpdated {
            TaskId id;
        };

        struct TaskCompleted {
            TaskId id;
        };

        struct TaskDeleted {
            TaskId id;
        };
    } // namespace

    std::string local_today() {
        const auto now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        const bool converted = now != std::time_t{-1} && localtime_s(&local, &now) == 0;
#else
        const bool converted = now != std::time_t{-1} && localtime_r(&now, &local) != nullptr;
#endif
        if (!converted || local.tm_year < -1899 || local.tm_year > 8099) {
            throw std::runtime_error("The local calendar date is unavailable.");
        }
        auto result =
            std::format("{:04}-{:02}-{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
        if (!valid_day(result)) {
            throw std::runtime_error("The local calendar date is unavailable.");
        }
        return result;
    }

    struct CalendarService::Impl {
        enum class Kind { create, update, complete, erase, filter, page, refresh };

        struct Command {
            Kind kind = Kind::refresh;
            Draft draft;
            TaskId id = 0;
            bool completed = false;
            Filter filter = Filter::today;
            int page = 0;
        };

        std::filesystem::path path;
        Observer observer;
        Today today;
        std::mutex mutex;
        std::condition_variable ready;
        std::array<std::optional<Command>, queue_capacity> commands;
        std::size_t first = 0;
        std::size_t size = 0;
        bool stopping = false;
        std::jthread worker;

        // Created and destroyed inside run(): SQLite and EnTT never cross worker threads.
        struct Storage {
            Impl &owner;
            std::unique_ptr<database::Database> connection;
            entt::dispatcher events;
            Snapshot state;
            std::string day;

            explicit Storage(Impl &service) : owner(service) {
                events.sink<TaskCreated>().connect<&Storage::created>(*this);
                events.sink<TaskUpdated>().connect<&Storage::updated>(*this);
                events.sink<TaskCompleted>().connect<&Storage::completed>(*this);
                events.sink<TaskDeleted>().connect<&Storage::deleted>(*this);
            }

            void publish() noexcept {
                try {
                    if (owner.observer) {
                        owner.observer(state);
                    }
                } catch (...) {
                    // An observer failure must never abandon accepted storage commands.
                }
            }

            void fail(const database::Error &error, bool mutation) {
                switch (error.kind) {
                case database::ErrorKind::invalid_task:
                    state.error = "Check task fields: title, notes, or due date (YYYY-MM-DD).";
                    break;
                case database::ErrorKind::not_found:
                    state.error = "This task no longer exists. Refresh Calendar and try again.";
                    break;
                default:
                    state.error = mutation
                                      ? "Calendar could not save changes. Refresh and try again."
                                      : "Calendar could not load tasks. Refresh and try again.";
                    break;
                }
            }

            void reload(Filter filter, int requested_page) {
                const auto selected = storage_filter(filter);
                if (!selected || requested_page < 0) {
                    state.error = "Choose a valid Calendar filter and page.";
                    return;
                }
                auto today_count = connection->count_tasks(database::TaskFilter::today, day);
                if (!today_count) {
                    fail(today_count.error(), false);
                    return;
                }
                auto count =
                    filter == Filter::today ? today_count : connection->count_tasks(*selected, day);
                if (!count) {
                    fail(count.error(), false);
                    return;
                }
                const auto last_page =
                    *count == 0 ? 0 : (*count - 1) / static_cast<std::int64_t>(page_size);
                const auto page =
                    static_cast<int>(std::min<std::int64_t>(requested_page, last_page));
                if (static_cast<std::uint64_t>(page) >
                    std::numeric_limits<std::size_t>::max() / page_size) {
                    state.error = "Choose a smaller Calendar page.";
                    return;
                }
                auto rows = connection->list_tasks(
                    *selected, day, static_cast<std::size_t>(page) * page_size, page_size + 1);
                if (!rows) {
                    fail(rows.error(), false);
                    return;
                }
                const bool has_more = rows->size() > page_size;
                std::vector<Task> tasks;
                tasks.reserve(std::min(rows->size(), page_size));
                for (std::size_t index = 0; index < rows->size() && index < page_size; ++index) {
                    auto &row = (*rows)[index];
                    tasks.push_back({row.id, std::move(row.title), std::move(row.notes),
                                     std::move(row.due_date), row.completed});
                }
                // Commit the list and its navigation metadata together, never an optimistic edit.
                state.today = day;
                state.tasks = std::move(tasks);
                state.filter = filter;
                state.page = page;
                state.has_more = has_more;
                state.today_count = *today_count;
                state.loaded = true;
            }

            void committed(TaskId id, ChangeKind change) {
                ++state.revision;
                state.changed_id = id;
                state.change = change;
                reload(state.filter, state.page);
            }

            void created(const TaskCreated &event) {
                committed(event.id, ChangeKind::created);
            }

            void updated(const TaskUpdated &event) {
                committed(event.id, ChangeKind::updated);
            }

            void completed(const TaskCompleted &event) {
                committed(event.id, ChangeKind::completed);
            }

            void deleted(const TaskDeleted &event) {
                committed(event.id, ChangeKind::deleted);
            }

            void execute(Command command) {
                state.busy = true;
                state.error.clear();
                publish();
                try {
                    try {
                        day = owner.today();
                    } catch (...) {
                        day.clear();
                    }
                    if (!valid_day(day)) {
                        state.error = "Calendar could not read the local date. Check the system "
                                      "date and refresh.";
                    } else {
                        if (!connection) {
                            auto opened = database::Database::open(owner.path);
                            if (opened) {
                                connection = std::move(*opened);
                            } else {
                                fail(opened.error(), false);
                            }
                        }
                        if (connection) {
                            apply(std::move(command));
                        }
                    }
                } catch (...) {
                    state.error =
                        "Calendar could not finish the operation. Refresh before trying again.";
                }
                state.busy = false;
                publish();
            }

            void apply(Command command) {
                switch (command.kind) {
                case Kind::refresh:
                    reload(state.filter, state.page);
                    break;
                case Kind::filter:
                    reload(command.filter, 0);
                    break;
                case Kind::page:
                    reload(state.filter, command.page);
                    break;
                case Kind::create: {
                    auto result = connection->create_task({std::move(command.draft.title),
                                                           std::move(command.draft.notes),
                                                           std::move(command.draft.due_date)});
                    if (result) {
                        events.trigger(TaskCreated{result->id});
                    } else {
                        fail(result.error(), true);
                    }
                    break;
                }
                case Kind::update: {
                    auto result = connection->update_task(
                        command.id, {std::move(command.draft.title), std::move(command.draft.notes),
                                     std::move(command.draft.due_date)});
                    if (result) {
                        events.trigger(TaskUpdated{result->id});
                    } else {
                        fail(result.error(), true);
                    }
                    break;
                }
                case Kind::complete: {
                    auto result = connection->set_task_completed(command.id, command.completed);
                    if (result) {
                        events.trigger(TaskCompleted{result->id});
                    } else {
                        fail(result.error(), true);
                    }
                    break;
                }
                case Kind::erase: {
                    auto result = connection->erase_task(command.id);
                    if (result) {
                        events.trigger(TaskDeleted{command.id});
                    } else {
                        fail(result.error(), true);
                    }
                    break;
                }
                }
            }
        };

        Impl(std::filesystem::path database, Observer callback, Today date)
            : path(std::move(database)), observer(std::move(callback)), today(std::move(date)),
              worker([this] { run(); }) {
        }

        ~Impl() {
            {
                const std::lock_guard lock{mutex};
                stopping = true;
            }
            ready.notify_one();
            worker.join();
        }

        bool enqueue(Command command) {
            {
                const std::lock_guard lock{mutex};
                if (stopping || size == queue_capacity) {
                    return false;
                }
                commands[(first + size) % queue_capacity].emplace(std::move(command));
                ++size;
            }
            ready.notify_one();
            return true;
        }

        void run() {
            Storage storage{*this};
            storage.execute({});
            for (;;) {
                Command command;
                {
                    std::unique_lock lock{mutex};
                    ready.wait(lock, [this] { return stopping || size != 0; });
                    if (size == 0) {
                        return;
                    }
                    command = std::move(*commands[first]);
                    commands[first].reset();
                    first = (first + 1) % queue_capacity;
                    --size;
                }
                storage.execute(std::move(command));
            }
        }
    };

    CalendarService::CalendarService(std::filesystem::path database, Observer observer, Today today)
        : impl_(
              std::make_unique<Impl>(std::move(database), std::move(observer), std::move(today))) {
    }

    CalendarService::~CalendarService() = default;

    bool CalendarService::create(Draft draft) {
        return impl_->enqueue({Impl::Kind::create, std::move(draft)});
    }

    bool CalendarService::update(TaskId id, Draft draft) {
        return impl_->enqueue({Impl::Kind::update, std::move(draft), id});
    }

    bool CalendarService::set_completed(TaskId id, bool completed) {
        return impl_->enqueue({Impl::Kind::complete, {}, id, completed});
    }

    bool CalendarService::erase(TaskId id) {
        return impl_->enqueue({Impl::Kind::erase, {}, id});
    }

    bool CalendarService::select_filter(Filter filter) {
        return impl_->enqueue({Impl::Kind::filter, {}, 0, false, filter});
    }

    bool CalendarService::page(int page) {
        return impl_->enqueue({Impl::Kind::page, {}, 0, false, Filter::today, page});
    }

    bool CalendarService::refresh() {
        return impl_->enqueue({});
    }
} // namespace oryn::calendar
