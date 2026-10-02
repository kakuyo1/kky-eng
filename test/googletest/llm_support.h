#pragma once

#include <gtest/gtest.h>

#include "llm/llm_protocol.h"
#include "support.h"

/**
 * @file llm_support.h
 * @brief The wire protocol loader for the LLM-facing suites.
 *
 * Split off support.h because llm_protocol.h is Qt-typed: including it drags Qt into every
 * target that uses the file, and the pure-core perf target must stay Qt-free.
 */

namespace lens::test {

/// @brief Load the word channel's request template and response schema, once.
inline void requireLlmProtocolLoaded()
{
    static const bool loaded = [] {
        lens::llm::loadLlmProtocol(lens::llm::Channel::Word, sourceDir() / "data" / "llm");
        return true;
    }();
    (void)loaded;
}

/// @brief Base for tests that need the wire protocol, and the filter tables with it.
struct LlmTest : ::testing::Test {
protected:
    void SetUp() override
    {
        requireFilterTablesLoaded();
        requireLlmProtocolLoaded();
    }
};

}
