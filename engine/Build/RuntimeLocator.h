#pragma once

#include "Build/BuildTypes.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Hephaestus::Build {

struct RuntimeArtifacts {
    std::filesystem::path executable;
    std::filesystem::path shadersDirectory;
    std::filesystem::path luaLibrary;

    bool complete() const {
        return !executable.empty() && !shadersDirectory.empty() && !luaLibrary.empty();
    }
};

struct RuntimeLocationResult {
    RuntimeArtifacts artifacts;
    std::vector<EditorProblem> problems;

    bool success() const { return artifacts.complete(); }
};

class RuntimeLocator {
public:
    RuntimeLocationResult locate(const std::filesystem::path& engineRoot,
                                 const std::filesystem::path& searchRoot,
                                 BuildConfiguration configuration) const;
};

class RuntimeBuilder {
public:
    virtual ~RuntimeBuilder() = default;
    virtual bool build(const std::filesystem::path& engineRoot,
                       BuildConfiguration configuration,
                       const std::filesystem::path& applicationIcon,
                       const LogCallback& log,
                       const CancellationCallback& cancelled) = 0;
};

class CMakeRuntimeBuilder final : public RuntimeBuilder {
public:
    explicit CMakeRuntimeBuilder(std::filesystem::path buildDirectory = {});

    bool build(const std::filesystem::path& engineRoot,
               BuildConfiguration configuration,
               const std::filesystem::path& applicationIcon,
               const LogCallback& log,
               const CancellationCallback& cancelled) override;

private:
    std::filesystem::path m_buildDirectory;
};

} // namespace Hephaestus::Build
