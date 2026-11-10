module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

export module oryn.platform.credentials;

export namespace oryn::platform {
    enum class CredentialErrorKind { invalid_input, unsupported, system };

    struct CredentialError {
        CredentialErrorKind kind;
        // Static labels and native codes only; never a target, account, or secret.
        std::string_view operation;
        std::uint32_t system_code;
    };

    // Owns binary secret bytes. Destruction and replacement wipe the complete allocation.
    // Views borrow this owner and must not survive its destruction or move assignment.
    class SecureBuffer {
    public:
        SecureBuffer() noexcept = default;
        explicit SecureBuffer(std::span<const std::byte> bytes);
        ~SecureBuffer() noexcept;

        SecureBuffer(const SecureBuffer &) = delete;
        SecureBuffer &operator=(const SecureBuffer &) = delete;
        SecureBuffer(SecureBuffer &&other) noexcept;
        SecureBuffer &operator=(SecureBuffer &&other) noexcept;

        [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

    private:
        void clear() noexcept;

        std::unique_ptr<std::byte[]> data_;
        std::size_t size_ = 0;
    };

    // Synchronous native vault I/O: call outside the UI thread. Identifiers are nonempty UTF-8.
    // The caller retains ownership of store's input and is responsible for wiping that input.
    [[nodiscard]] std::expected<void, CredentialError>
    store_credential(std::string_view service, std::string_view account,
                     std::span<const std::byte> secret);

    // A successful nullopt is not found; other native failures remain typed errors.
    [[nodiscard]] std::expected<std::optional<SecureBuffer>, CredentialError>
    read_credential(std::string_view service, std::string_view account);

    // Idempotent: an already absent credential is success.
    [[nodiscard]] std::expected<void, CredentialError> erase_credential(std::string_view service,
                                                                        std::string_view account);
} // namespace oryn::platform
