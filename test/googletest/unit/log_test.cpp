/**
 * @file log_test.cpp
 * @brief The three failure paths through log::init(): an unknown level name, a directory that
 *        cannot be made, and a log file that cannot be opened.
 *
 * None of them can happen on a healthy machine, which is exactly why they are the lines most
 * likely to be wrong and least likely to be exercised. All three are reachable offline by handing
 * init() a bad level name or a bad path, so they are pinned here rather than left to the coverage
 * scan to report as never run.
 *
 * Every case goes through the process-wide logger on purpose: init()'s whole job is to install
 * one, so that is the only place its behaviour is observable. The sink count is what tells a
 * disabled file sink from a working one without reading a log file back.
 */

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "core/log.h"

namespace {

namespace fs = std::filesystem;

/// @brief A scratch directory under the system temp folder, removed with the object.
///
/// One fixed name per tag is enough: the cases run in one process, one after another, and each
/// removes what it made on the way out.
class ScratchDir {
public:
    explicit ScratchDir(const char* tag)
        : path_(fs::temp_directory_path() / (std::string("lens-log-test-") + tag))
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }

    ~ScratchDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const fs::path& path() const
    {
        return path_;
    }

private:
    fs::path path_;
};

/// @return How many sinks the process-wide logger carries. init() adds the rotating file sink
///         only when the file opened, so a failure leaves the stderr sink alone.
std::size_t sinkCount()
{
    return spdlog::default_logger()->sinks().size();
}

} // namespace

/// The level name is the one thing a reader can retune from outside, so a typo in it must not
/// silence logging -- the fallback is what stops that.
TEST(LogInit, AnUnknownLevelNameFallsBackToThePassedLevel)
{
    ScratchDir scratch("level");

    // No override at all: the caller's level stands.
    _putenv_s("LENS_LOG_LEVEL", "");
    lens::log::init(spdlog::level::warn, scratch.path());
    EXPECT_EQ(spdlog::default_logger()->level(), spdlog::level::warn);

    // A name spdlog knows wins over the caller's.
    _putenv_s("LENS_LOG_LEVEL", "debug");
    lens::log::init(spdlog::level::warn, scratch.path());
    EXPECT_EQ(spdlog::default_logger()->level(), spdlog::level::debug);

    // A name spdlog does not know is a typo, not an instruction to go quiet.
    _putenv_s("LENS_LOG_LEVEL", "verbose-nonsense");
    lens::log::init(spdlog::level::warn, scratch.path());
    EXPECT_EQ(spdlog::default_logger()->level(), spdlog::level::warn);

    _putenv_s("LENS_LOG_LEVEL", "");
}

/// A good directory is the control the two failure cases are read against: it must produce the
/// second, file sink.
TEST(LogInit, AUsableDirectoryAddsTheFileSink)
{
    ScratchDir scratch("good");
    lens::log::init(spdlog::level::info, scratch.path());

    EXPECT_EQ(sinkCount(), 2u) << "the rotating file sink was not added for a writable directory";
}

/// A path under a regular file can never be made into a directory, so the file sink is dropped
/// and the program still starts.
TEST(LogInit, ADirectoryUnderAFileLeavesTheFileSinkOut)
{
    ScratchDir scratch("nested");
    const fs::path file = scratch.path() / "not-a-dir";
    {
        std::ofstream touch(file);
    }
    ASSERT_TRUE(fs::exists(file));

    lens::log::init(spdlog::level::info, file / "sub");

    EXPECT_EQ(sinkCount(), 1u) << "a file sink was added for a directory that does not exist";
}

/// The directory exists but the log file cannot be opened -- here because a directory sits where
/// the file should be. This is the path where the rotating sink itself throws, so it is the case
/// that proves the catch around it does its job.
TEST(LogInit, ALogFileThatCannotBeOpenedLeavesTheFileSinkOut)
{
    ScratchDir scratch("blocked");
    std::error_code ec;
    fs::create_directories(scratch.path() / lens::log::kLogFileName, ec);
    ASSERT_FALSE(ec) << "could not block the log file path";

    lens::log::init(spdlog::level::info, scratch.path());

    EXPECT_EQ(sinkCount(), 1u) << "the file sink was kept even though its file could not be opened";
}
