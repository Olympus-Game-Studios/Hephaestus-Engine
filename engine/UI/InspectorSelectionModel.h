#pragma once

/**
 * @file InspectorSelectionModel.h
 * @brief Headless Inspector selection, mixed-value, and batch component helpers.
 *
 * No Qt dependency. Used by the Details panel and InspectorEditTests.
 */

#include "../Core/Math.h"

#include <entt/entt.hpp>
#include <string>
#include <vector>

class Scene;

namespace Hephaestus {

struct MixedScalar {
    bool present = false;
    bool mixed = false;
    double value = 0.0;
};

struct MixedVec3Axes {
    MixedScalar x;
    MixedScalar y;
    MixedScalar z;
};

struct MixedBool {
    bool present = false;
    bool mixed = false;
    bool value = false;
};

struct MixedString {
    bool present = false;
    bool mixed = false;
    std::string value;
};

void AccumulateScalar(MixedScalar& acc, double value, double epsilon = 0.0001);
void AccumulateBool(MixedBool& acc, bool value);
void AccumulateString(MixedString& acc, const std::string& value);
MixedVec3Axes MixVec3(const std::vector<Vec3>& values, double epsilon = 0.0001);
void WriteVec3Axis(Vec3& v, int axis, float value);

struct InspectorComponentDesc {
    const char* id = "";
    const char* displayName = "";
    const char* category = "";
    bool sceneUnique = false;
};

const std::vector<InspectorComponentDesc>& InspectorAddableComponents();
const InspectorComponentDesc* FindInspectorComponentDesc(const std::string& id);
std::string InspectorDisplayName(const std::string& id);
std::vector<std::string> InspectorComponentsOn(const entt::registry& reg, entt::entity entity);
bool InspectorEntityHasComponent(const entt::registry& reg, entt::entity entity, const std::string& type);

struct InspectorSelectionModel {
    std::vector<entt::entity> entities;
    entt::entity primary = entt::null;
    bool pinned = false;
    entt::entity pinnedEntity = entt::null;
    bool pinnedMissing = false;

    size_t Count() const { return entities.size(); }
    bool Empty() const { return entities.empty(); }

    static InspectorSelectionModel Resolve(
        const std::vector<entt::entity>& selected,
        entt::entity primary,
        bool pinned,
        entt::entity pinnedEntity,
        const entt::registry* registry);

    std::vector<std::string> SharedComponents(const entt::registry& reg) const;
    std::vector<std::string> PartialComponents(const entt::registry& reg) const;
};

struct BatchComponentResult {
    int added = 0;
    int skippedExisting = 0;
    int skippedUnique = 0;
    int failed = 0;

    int Requested() const { return added + skippedExisting + skippedUnique + failed; }
};

BatchComponentResult BatchAddComponent(Scene& scene, const std::vector<entt::entity>& entities,
                                       const std::string& type);
BatchComponentResult BatchRemoveComponent(Scene& scene, const std::vector<entt::entity>& entities,
                                          const std::string& type);

int CountComponentsInScene(Scene& scene, const std::string& type);

} // namespace Hephaestus
