#pragma once

#include <QString>

#include <filesystem>
#include <string>

/**
 * @file paths.h
 * @brief The path helpers src/app needs, and the one of them that draws a boundary.
 *
 * The two conversions are what main.cpp used to keep to itself. settingsPath() moved out of
 * main.cpp's anonymous namespace because nothing in there can be reached from a test, and the
 * boundary it draws -- the reader's API key rides in that document, so the document belongs to
 * the profile and never to the install tree -- is the one a case locks.
 */

namespace lens::app {

/// @return @p text as a std::filesystem::path, for the loaders that take one.
inline std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path{text.toStdWString()};
}

/// @return @p path as the log wants it: UTF-8, whatever the account is named.
inline std::string narrow(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring()).toStdString();
}

/**
 * @brief The settings document's path, making its directory and carrying a legacy copy over.
 *
 * `%APPDATA%\Lens\settings.json`, from QStandardPaths::AppDataLocation with the application
 * name main() sets and no organization name -- naming one would add a directory level for a
 * name the product does not use. The install folder is wrong for this file twice over: it can
 * be read-only under Program Files, and it is shared by every account, while the document
 * carries one reader's API key and word marks.
 *
 * The document used to live at the repository root. A first run whose profile copy is missing
 * copies that one over, once, so a key already configured keeps working. Only the path is ever
 * logged -- the document holds the key, and its contents do not belong in a log.
 *
 * @param legacy Document to carry over from, when the profile has none yet. May be empty.
 * @return The path to load and save; its directory exists on return unless it could not be
 *         created at all.
 */
std::filesystem::path settingsPath(const std::filesystem::path& legacy);

}
