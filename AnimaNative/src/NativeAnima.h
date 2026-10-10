#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace anima::native {
struct ProgramSummary {
    std::string appName;
    std::size_t stateCount = 0;
    std::size_t sceneCount = 0;
    std::size_t nodeCount = 0;
    std::size_t tokenCount = 0;
};

// Phase 1 native front-end: tokenizes and structurally validates ANIMA source,
// and reports declarations. This is deliberately a stepping stone, not yet a
// complete replacement for the browser evaluator.
ProgramSummary ParseProgram(std::string_view source);
ProgramSummary ParseFile(const std::filesystem::path& path);
}
