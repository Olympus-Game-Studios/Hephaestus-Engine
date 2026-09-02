#pragma once

#include <string>
#include <vector>

namespace Hephaestus::Build {

enum class ProblemSeverity {
    Error,
    Warning,
    Info
};

struct EditorProblem {
    ProblemSeverity severity = ProblemSeverity::Info;
    std::string code;
    std::string message;
    std::string detail;
    std::string assetPath;
    std::vector<std::string> referrers;
    std::vector<std::string> actions;
};

} // namespace Hephaestus::Build
