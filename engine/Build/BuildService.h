#pragma once

#include "Build/PackageStager.h"
#include "Build/ProjectValidator.h"

#include <memory>

namespace Hephaestus::Build {

class BuildService {
public:
    explicit BuildService(
        std::shared_ptr<RuntimeBuilder> runtimeBuilder = std::make_shared<CMakeRuntimeBuilder>());

    BuildResult execute(const BuildRequest& request) const;

private:
    std::shared_ptr<RuntimeBuilder> m_runtimeBuilder;
};

} // namespace Hephaestus::Build
