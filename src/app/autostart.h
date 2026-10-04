#pragma once

#include <string>
#include <string_view>

/**
 * @file autostart.h
 * @brief The Run entry that starts Lens with the reader's session.
 *
 * Nothing else in the project writes the registry, so the whole of it is here: one value under
 * HKEY_CURRENT_USER, through QSettings in native format. The key and the command are assembled
 * by the pure functions below; only writeAutostart() reads or writes the machine, which is why
 * the assembly can be asserted offline and the write cannot.
 *
 * @note Windows only, like the rest of src/app.
 */

namespace lens::app {

/// @brief Where the entry lives, spelled as QSettings' native format wants a registry path.
inline constexpr const char* kAutostartRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

/// @brief The value name under that key. One entry per user, so the name is the product's.
inline constexpr const char* kAutostartValueName = "Lens";

/**
 * @brief The command the Run entry holds.
 * @param executablePath Path to this executable, as the process reports it.
 * @return The path quoted, because a Run value is a command line and an install path may
 *         contain spaces.
 */
inline std::string autostartCommand(std::string_view executablePath)
{
    return '"' + std::string(executablePath) + '"';
}

/**
 * @brief Write or remove the Run entry.
 * @param on True to start with the session, false to stop.
 * @return Whether the registry accepted the write or the removal.
 * @note A failure is the registry's, not the reader's: a policy that forbids the key is logged
 *       and reported, so the settings switch does not stay on over a value that is not there.
 */
bool writeAutostart(bool on);

/// @return Whether the Run entry is present.
/// @note Presence is the state. The command's text is this build's business and changes
///       whenever the executable moves, which is no reason to call the feature off.
bool autostartEnabled();

}
