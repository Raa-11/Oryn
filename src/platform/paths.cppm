module;
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string_view>

export module oryn.platform.paths;

export namespace oryn::platform {
    struct PathError {
        std::string_view operation;
        std::uint32_t system_code;
    };

    // Resolves only: the storage worker creates the directory when opening its database.
    // ORYN_DATA_DIR is an explicit absolute-path override for isolated runtime smoke tests.
    [[nodiscard]] std::expected<std::filesystem::path, PathError> app_data_directory();
} // namespace oryn::platform
