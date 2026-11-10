#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <string_view>
#include <utility>
#include <windows.h>
#include <wincred.h>
#include <objbase.h>

import oryn.platform.credentials;

namespace {
    struct TestCredential {
        std::string service;
        std::string account;

        ~TestCredential() noexcept {
            // Also runs on a failing return after a successful store; never touch other targets.
            try {
                static_cast<void>(oryn::platform::erase_credential(service, account));
            } catch (...) {
                // Cleanup must not mask the test's original failure.
            }
        }
    };

    bool matches(const oryn::platform::SecureBuffer &secret, std::span<const std::byte> expected) {
        return std::ranges::equal(secret.bytes(), expected);
    }
} // namespace

int main() {
    using namespace oryn::platform;
    static_assert(!std::is_copy_constructible_v<SecureBuffer>);
    static_assert(std::is_nothrow_move_constructible_v<SecureBuffer>);
    static_assert(std::is_nothrow_move_assignable_v<SecureBuffer>);

    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        return 1;
    }
    std::array<wchar_t, 40> unique{};
    const auto count = StringFromGUID2(guid, unique.data(), static_cast<int>(unique.size()));
    if (count == 0) {
        return 2;
    }
    std::string service = "credential-runtime-test/";
    for (int index = 0; index < count - 1; ++index) {
        service.push_back(static_cast<char>(unique[static_cast<std::size_t>(index)]));
    }
    // Exercise UTF-8 conversion, '/' disambiguation, and binary secrets including embedded NUL.
    TestCredential test{service + "/\xE6\xB5\x8B\xE8\xAF\x95", "account/\xC3\xA9"};
    constexpr std::array original{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff},
                                  std::byte{0x42}};
    constexpr std::array replacement{std::byte{0x91}, std::byte{0x00}, std::byte{0x17}};

    auto absent = read_credential(test.service, test.account);
    if (!absent || absent->has_value()) {
        return 3;
    }
    if (!erase_credential(test.service, test.account) ||
        !erase_credential(test.service, test.account)) {
        return 4;
    }
    if (!store_credential(test.service, test.account, original)) {
        return 5;
    }
    {
        auto restored = read_credential(test.service, test.account);
        if (!restored || !*restored || !matches(**restored, original)) {
            return 6;
        }
        SecureBuffer moved{std::move(**restored)};
        if (!matches(moved, original) || !(**restored).bytes().empty()) {
            return 7;
        }
        SecureBuffer assigned{replacement};
        assigned = std::move(moved);
        if (!matches(assigned, original) || !moved.bytes().empty()) {
            return 8;
        }
    }
    if (!store_credential(test.service, test.account, replacement)) {
        return 9;
    }
    {
        const auto restored = read_credential(test.service, test.account);
        if (!restored || !*restored || !matches(**restored, replacement)) {
            return 10;
        }
    }
    if (!store_credential(test.service, test.account, {})) {
        return 11;
    }
    {
        const auto empty = read_credential(test.service, test.account);
        if (!empty || !*empty || !(**empty).bytes().empty()) {
            return 12;
        }
    }
    const auto invalid = store_credential("", test.account, original);
    const auto invalid_utf8 = read_credential("\xff", test.account);
    const auto embedded_nul = erase_credential(std::string_view{"a\0b", 3}, test.account);
    std::array<std::byte, CRED_MAX_CREDENTIAL_BLOB_SIZE + 1> too_large{};
    const auto oversized = store_credential(test.service, test.account, too_large);
    if (invalid || invalid.error().kind != CredentialErrorKind::invalid_input || invalid_utf8 ||
        invalid_utf8.error().kind != CredentialErrorKind::invalid_input || embedded_nul ||
        embedded_nul.error().kind != CredentialErrorKind::invalid_input || oversized ||
        oversized.error().kind != CredentialErrorKind::invalid_input) {
        return 13;
    }
    if (!erase_credential(test.service, test.account)) {
        return 14;
    }
    const auto deleted = read_credential(test.service, test.account);
    if (!deleted || deleted->has_value() || !erase_credential(test.service, test.account)) {
        return 15;
    }
    return 0;
}
