/**
 * @file paths.cpp
 * @brief The one path helper with a body: see paths.h for why it is not in main.cpp.
 */

#include "paths.h"

#include <QStandardPaths>

#include "util/log.h"

namespace lens::app {

std::filesystem::path settingsPath(const std::filesystem::path& legacy)
{
    const std::filesystem::path target =
        toPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)) / "settings.json";

    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        LENS_WARN("settings directory {} could not be created: {}", narrow(target.parent_path()), ec.message());
        return target;
    }

    if (!std::filesystem::exists(target, ec) && std::filesystem::exists(legacy, ec)) {
        std::filesystem::copy_file(legacy, target, ec);
        if (ec)
            LENS_WARN("the settings document was not carried over from {}: {}", narrow(legacy), ec.message());
        else
            LENS_INFO("settings carried over from {} to {}", narrow(legacy), narrow(target));
    }
    return target;
}

}
