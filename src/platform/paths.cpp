module;
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

module oryn.platform.paths;

namespace oryn::platform {
    std::expected<std::filesystem::path, PathError> app_data_directory() {
#ifdef _WIN32
        SetLastError(ERROR_SUCCESS);
        const auto required = GetEnvironmentVariableW(L"ORYN_DATA_DIR", nullptr, 0);
        if (required != 0) {
            std::wstring value(required, L'\0');
            const auto copied = GetEnvironmentVariableW(L"ORYN_DATA_DIR", value.data(), required);
            if (copied == 0) {
                return std::unexpected(PathError{"GetEnvironmentVariableW", GetLastError()});
            }
            if (copied >= required) {
                return std::unexpected(PathError{"GetEnvironmentVariableW", ERROR_MORE_DATA});
            }
            value.resize(copied);
            std::filesystem::path path{value};
            if (!path.is_absolute()) {
                return std::unexpected(
                    PathError{"ORYN_DATA_DIR_absolute_path", ERROR_BAD_PATHNAME});
            }
            return path;
        }
        const auto environment_error = GetLastError();
        if (environment_error != ERROR_ENVVAR_NOT_FOUND && environment_error != ERROR_SUCCESS) {
            return std::unexpected(PathError{"GetEnvironmentVariableW", environment_error});
        }

        struct PathDeleter {
            void operator()(wchar_t *value) const noexcept {
                CoTaskMemFree(value);
            }
        };

        PWSTR raw = nullptr;
        // DONT_VERIFY avoids filesystem access and never creates the known folder.
        const auto result =
            SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &raw);
        const std::unique_ptr<wchar_t, PathDeleter> local_data{raw};
        if (FAILED(result)) {
            return std::unexpected(PathError{"SHGetKnownFolderPath(LocalAppData)",
                                             static_cast<std::uint32_t>(result)});
        }
        return std::filesystem::path{local_data.get()} / L"Oryn";
#else
        return std::unexpected(PathError{"unsupported_platform", 0});
#endif
    }
} // namespace oryn::platform
