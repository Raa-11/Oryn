#include <array>
#include <expected>
#include <filesystem>
#include <string>
#include <windows.h>
#include <objbase.h>

import oryn.platform.paths;

namespace {
    class EnvironmentRestore {
    public:
        EnvironmentRestore() {
            const auto count = GetEnvironmentVariableW(L"ORYN_DATA_DIR", nullptr, 0);
            present_ = count != 0;
            if (present_) {
                original_.resize(count);
                const auto copied =
                    GetEnvironmentVariableW(L"ORYN_DATA_DIR", original_.data(), count);
                original_.resize(copied);
            }
        }

        ~EnvironmentRestore() noexcept {
            SetEnvironmentVariableW(L"ORYN_DATA_DIR", present_ ? original_.c_str() : nullptr);
        }

        EnvironmentRestore(const EnvironmentRestore &) = delete;
        EnvironmentRestore &operator=(const EnvironmentRestore &) = delete;
        EnvironmentRestore(EnvironmentRestore &&) = delete;
        EnvironmentRestore &operator=(EnvironmentRestore &&) = delete;

    private:
        std::wstring original_;
        bool present_ = false;
    };
} // namespace

int main() {
    const EnvironmentRestore restore;
    if (!SetEnvironmentVariableW(L"ORYN_DATA_DIR", nullptr)) {
        return 1;
    }
    const auto default_path = oryn::platform::app_data_directory();
    if (!default_path || !default_path->is_absolute() || default_path->filename() != L"Oryn") {
        return 2;
    }
    GUID guid{};
    std::array<wchar_t, 40> unique{};
    std::array<wchar_t, MAX_PATH + 1> temporary{};
    if (FAILED(CoCreateGuid(&guid)) ||
        StringFromGUID2(guid, unique.data(), static_cast<int>(unique.size())) == 0) {
        return 3;
    }
    const auto count = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
    if (count == 0 || count >= temporary.size()) {
        return 4;
    }
    const auto isolated = std::filesystem::path{temporary.data()} /
                          (std::wstring{L"Oryn-path-resolution-"} + unique.data());
    if (GetFileAttributesW(isolated.c_str()) != INVALID_FILE_ATTRIBUTES ||
        !SetEnvironmentVariableW(L"ORYN_DATA_DIR", isolated.c_str())) {
        return 5;
    }
    const auto overridden = oryn::platform::app_data_directory();
    if (!overridden || *overridden != isolated ||
        GetFileAttributesW(isolated.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return 6;
    }
    if (!SetEnvironmentVariableW(L"ORYN_DATA_DIR", L"relative-user-data")) {
        return 7;
    }
    const auto relative = oryn::platform::app_data_directory();
    if (relative || relative.error().operation != "ORYN_DATA_DIR_absolute_path") {
        return 8;
    }
    return 0;
}
