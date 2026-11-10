module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

module oryn.platform.credentials;

namespace oryn::platform {
    SecureBuffer::SecureBuffer(std::span<const std::byte> bytes)
        : data_(bytes.empty() ? nullptr
                              : std::make_unique_for_overwrite<std::byte[]>(bytes.size())),
          size_(bytes.size()) {
        if (!bytes.empty()) {
            std::copy(bytes.begin(), bytes.end(), data_.get());
        }
    }

    SecureBuffer::~SecureBuffer() noexcept {
        clear();
    }

    SecureBuffer::SecureBuffer(SecureBuffer &&other) noexcept
        : data_(std::move(other.data_)), size_(std::exchange(other.size_, 0)) {
    }

    SecureBuffer &SecureBuffer::operator=(SecureBuffer &&other) noexcept {
        if (this != &other) {
            clear();
            data_ = std::move(other.data_);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    std::span<const std::byte> SecureBuffer::bytes() const noexcept {
        return {data_.get(), size_};
    }

    void SecureBuffer::clear() noexcept {
        if (size_ != 0) {
#ifdef _WIN32
            SecureZeroMemory(data_.get(), size_);
#else
            // Volatile stores cannot be removed as dead writes before deallocation.
            auto *bytes = static_cast<volatile std::byte *>(data_.get());
            for (std::size_t index = 0; index < size_; ++index) {
                bytes[index] = std::byte{};
            }
#endif
        }
        data_.reset();
        size_ = 0;
    }

#ifdef _WIN32
    namespace {
        [[nodiscard]] CredentialError native_error(std::string_view operation,
                                                   DWORD code) noexcept {
            return {CredentialErrorKind::system, operation, code};
        }

        [[nodiscard]] std::expected<std::wstring, CredentialError>
        wide_identifier(std::string_view value) {
            if (value.empty() || value.find('\0') != std::string_view::npos ||
                value.size() > CRED_MAX_GENERIC_TARGET_NAME_LENGTH) {
                return std::unexpected(CredentialError{CredentialErrorKind::invalid_input,
                                                       "credential_identifier",
                                                       ERROR_INVALID_PARAMETER});
            }
            const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                                   static_cast<int>(value.size()), nullptr, 0);
            if (count == 0) {
                return std::unexpected(CredentialError{CredentialErrorKind::invalid_input,
                                                       "MultiByteToWideChar", GetLastError()});
            }
            std::wstring result(static_cast<std::size_t>(count), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                    static_cast<int>(value.size()), result.data(), count) == 0) {
                return std::unexpected(CredentialError{CredentialErrorKind::invalid_input,
                                                       "MultiByteToWideChar", GetLastError()});
            }
            return result;
        }

        struct CredentialKey {
            std::wstring target;
            std::wstring account;
        };

        [[nodiscard]] std::expected<CredentialKey, CredentialError>
        credential_key(std::string_view service, std::string_view account) {
            auto wide_service = wide_identifier(service);
            if (!wide_service) {
                return std::unexpected(wide_service.error());
            }
            auto wide_account = wide_identifier(account);
            if (!wide_account) {
                return std::unexpected(wide_account.error());
            }
            // A length-prefixed service prevents collisions when identifiers contain '/'.
            auto target = L"Oryn/" + std::to_wstring(wide_service->size()) + L"/" + *wide_service +
                          L"/" + *wide_account;
            if (target.size() > CRED_MAX_GENERIC_TARGET_NAME_LENGTH ||
                wide_account->size() > CRED_MAX_USERNAME_LENGTH) {
                return std::unexpected(CredentialError{CredentialErrorKind::invalid_input,
                                                       "credential_identifier_length",
                                                       ERROR_INVALID_PARAMETER});
            }
            return CredentialKey{std::move(target), std::move(*wide_account)};
        }

        struct CredentialDeleter {
            void operator()(CREDENTIALW *credential) const noexcept {
                if (credential->CredentialBlob && credential->CredentialBlobSize != 0) {
                    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
                }
                CredFree(credential);
            }
        };
    } // namespace
#endif

    std::expected<void, CredentialError> store_credential(std::string_view service,
                                                          std::string_view account,
                                                          std::span<const std::byte> secret) {
#ifdef _WIN32
        if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
            return std::unexpected(CredentialError{CredentialErrorKind::invalid_input,
                                                   "credential_secret_length",
                                                   ERROR_INVALID_PARAMETER});
        }
        auto key = credential_key(service, account);
        if (!key) {
            return std::unexpected(key.error());
        }
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = key->target.data();
        credential.UserName = key->account.data();
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
        // CredWriteW reads the blob synchronously; its SDK input type is not const-correct.
        credential.CredentialBlob =
            reinterpret_cast<BYTE *>(const_cast<std::byte *>(secret.data()));
        if (!CredWriteW(&credential, 0)) {
            return std::unexpected(native_error("CredWriteW", GetLastError()));
        }
        return {};
#else
        return std::unexpected(
            CredentialError{CredentialErrorKind::unsupported, "unsupported_platform", 0});
#endif
    }

    std::expected<std::optional<SecureBuffer>, CredentialError>
    read_credential(std::string_view service, std::string_view account) {
#ifdef _WIN32
        const auto key = credential_key(service, account);
        if (!key) {
            return std::unexpected(key.error());
        }
        PCREDENTIALW raw = nullptr;
        const auto found = CredReadW(key->target.c_str(), CRED_TYPE_GENERIC, 0, &raw);
        const auto code = found ? ERROR_SUCCESS : GetLastError();
        const std::unique_ptr<CREDENTIALW, CredentialDeleter> credential{raw};
        if (!found) {
            if (code == ERROR_NOT_FOUND) {
                return std::optional<SecureBuffer>{};
            }
            return std::unexpected(native_error("CredReadW", code));
        }
        if (!credential || credential->CredentialBlobSize > CRED_MAX_CREDENTIAL_BLOB_SIZE ||
            (credential->CredentialBlobSize != 0 && !credential->CredentialBlob)) {
            return std::unexpected(native_error("CredReadW(blob)", ERROR_INVALID_DATA));
        }
        const auto bytes = std::span<const std::byte>{
            reinterpret_cast<const std::byte *>(credential->CredentialBlob),
            credential->CredentialBlobSize};
        return std::optional<SecureBuffer>{std::in_place, bytes};
#else
        return std::unexpected(
            CredentialError{CredentialErrorKind::unsupported, "unsupported_platform", 0});
#endif
    }

    std::expected<void, CredentialError> erase_credential(std::string_view service,
                                                          std::string_view account) {
#ifdef _WIN32
        const auto key = credential_key(service, account);
        if (!key) {
            return std::unexpected(key.error());
        }
        if (!CredDeleteW(key->target.c_str(), CRED_TYPE_GENERIC, 0)) {
            const auto code = GetLastError();
            if (code != ERROR_NOT_FOUND) {
                return std::unexpected(native_error("CredDeleteW", code));
            }
        }
        return {};
#else
        return std::unexpected(
            CredentialError{CredentialErrorKind::unsupported, "unsupported_platform", 0});
#endif
    }
} // namespace oryn::platform
