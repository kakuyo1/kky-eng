#pragma once

#include <filesystem>

#include <gtest/gtest.h>

#include "core/filter_core.h"

/**
 * @file support.h
 * @brief What the gtest targets need from the repository: its root, and the loaded tables.
 *
 * Kept free of Qt, so the pure-core perf target can include it without pulling in lens_llm.
 * The wire protocol and its loader live in llm_support.h.
 */

#ifndef LENS_SOURCE_DIR
#error "LENS_SOURCE_DIR is undefined; build through a lens_gtest_* target"
#endif

namespace lens::test {

/// @return The repository root the tests read their data files from.
inline std::filesystem::path sourceDir()
{
    return std::filesystem::path(LENS_SOURCE_DIR);
}

/**
 * @brief Load the word list and the irregular table, once.
 *
 * filterWords() and lemmatize() throw unless their tables are loaded, and every suite needs
 * both. They run behind a function-local static so the second suite to ask pays nothing, and
 * so a load failure surfaces inside a test rather than in a global constructor.
 */
inline void requireFilterTablesLoaded()
{
    static const bool loaded = [] {
        lens::core::loadWordlist(sourceDir() / "data" / "wordlist.txt");
        lens::core::loadIrregulars(sourceDir() / "data" / "irregulars.tsv");
        return true;
    }();
    (void)loaded;
}

/// @brief Base for tests that need the filter tables in memory.
struct CoreTest : ::testing::Test {
protected:
    void SetUp() override
    {
        requireFilterTablesLoaded();
    }
};

}
