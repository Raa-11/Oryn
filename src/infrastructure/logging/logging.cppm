module;
#include <cstdint>
#include <string_view>

export module oryn.logging;

export namespace oryn::logging {
    // Exclusive process-wide Quill owner. Construct/destroy on the same application thread;
    // all logging callers must finish before destruction. Sessions must not overlap.
    class LoggingSession {
    public:
        LoggingSession();
        ~LoggingSession() noexcept;

        LoggingSession(const LoggingSession &) = delete;
        LoggingSession &operator=(const LoggingSession &) = delete;
        LoggingSession(LoggingSession &&) = delete;
        LoggingSession &operator=(LoggingSession &&) = delete;
    };

    // Call only inside a LoggingSession lifetime, with small static diagnostic labels.
    // Never pass credentials, prompts, preferences, source code, or user content.
    // Calls outside the lifetime throw std::logic_error. Saturation drops records rather
    // than blocking the UI; shutdown drains accepted records and flushes the console.
    void info(std::string_view event);
    void error(std::string_view operation, std::uint32_t code);
} // namespace oryn::logging
