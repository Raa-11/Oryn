module;
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/sinks/ConsoleSink.h>

module oryn.logging;

namespace oryn::logging {
    namespace {
        struct FrontendOptions : quill::FrontendOptions {
            static constexpr quill::QueueType queue_type = quill::QueueType::BoundedDropping;
            static constexpr std::size_t initial_queue_capacity = 64U * 1024U;
        };

        using Frontend = quill::FrontendImpl<FrontendOptions>;
        using Logger = Frontend::logger_t;

        // Borrowed from Quill's registry; never exposed through the module interface.
        std::atomic<Logger *> active_logger = nullptr;

        Logger *require_logger() {
            auto *logger = active_logger.load(std::memory_order_acquire);
            if (logger == nullptr) {
                throw std::logic_error("Logging requires a live LoggingSession");
            }
            return logger;
        }
    } // namespace

    LoggingSession::LoggingSession() {
        if (active_logger.load(std::memory_order_acquire) != nullptr ||
            quill::Backend::is_running()) {
            throw std::logic_error("LoggingSession requires exclusive Quill ownership");
        }

        quill::BackendOptions options;
        options.thread_name = "OrynLogging";
        options.sleep_duration = std::chrono::milliseconds{100};
        options.transit_event_buffer_initial_capacity = 64;
        options.transit_events_soft_limit = 256;
        options.transit_events_hard_limit = 1024;
        options.wait_for_queues_to_empty_before_exit = true;

        quill::ConsoleSinkConfig console;
        console.set_stream("stderr");
        console.set_colour_mode(quill::ConsoleSinkConfig::ColourMode::Never);
        auto *logger = Frontend::create_or_get_logger(
            "oryn", Frontend::create_or_get_sink<quill::ConsoleSink>("oryn.console", console),
            quill::PatternFormatterOptions{}, quill::ClockSourceType::System);

        // Allocate the owner's bounded queue before starting the worker, including the
        // control-event storage used by flush/removal during same-thread destruction.
        Frontend::preallocate();
        quill::Backend::start(options);
        active_logger.store(logger, std::memory_order_release);
    }

    LoggingSession::~LoggingSession() noexcept {
        auto *logger = active_logger.exchange(nullptr, std::memory_order_acq_rel);
        logger->flush_log();
        Frontend::remove_logger_blocking(logger);
        quill::Backend::stop();
    }

    void info(std::string_view event) {
        auto *logger = require_logger();
        QUILL_LOG_INFO(logger, "{}", event);
    }

    void error(std::string_view operation, std::uint32_t code) {
        auto *logger = require_logger();
        QUILL_LOG_ERROR(logger, "{} system_code={}", operation, code);
    }
} // namespace oryn::logging
