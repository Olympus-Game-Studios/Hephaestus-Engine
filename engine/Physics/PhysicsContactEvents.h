#pragma once

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Physics {

inline constexpr std::uint32_t kNullPhysicsEntity = 0xFFFFFFFFu;
inline constexpr int kPhysicsContactBufferCapacity = 4096;

enum class PhysicsContactKind : std::uint8_t { Added, Persisted, Removed };

struct PhysicsContactRecord {
    PhysicsContactKind kind = PhysicsContactKind::Added;
    std::uint32_t body1 = 0;
    std::uint32_t body2 = 0;
    std::uint32_t entity1 = kNullPhysicsEntity;
    std::uint32_t entity2 = kNullPhysicsEntity;
    bool entity1Live = true;
    bool entity2Live = true;
    bool sensor = false;
    bool lastContact = false;
    bool hasPoint = false;
    float normalX = 0.0f;
    float normalY = 1.0f;
    float normalZ = 0.0f;
    float point1X = 0.0f;
    float point1Y = 0.0f;
    float point1Z = 0.0f;
    float point2X = 0.0f;
    float point2Y = 0.0f;
    float point2Z = 0.0f;
    float penetration = 0.0f;
    float vel1X = 0.0f;
    float vel1Y = 0.0f;
    float vel1Z = 0.0f;
    float vel2X = 0.0f;
    float vel2Y = 0.0f;
    float vel2Z = 0.0f;
};

enum class PhysicsEventKind : std::uint8_t {
    TriggerEnter,
    TriggerStay,
    TriggerExit,
    CollisionEnter,
    CollisionStay,
    CollisionExit
};

struct PhysicsContactEvent {
    PhysicsEventKind kind = PhysicsEventKind::CollisionEnter;
    std::uint32_t receiver = kNullPhysicsEntity;
    std::uint32_t other = kNullPhysicsEntity;
    bool otherAlive = false;
    bool hasContact = false;
    float pointX = 0.0f;
    float pointY = 0.0f;
    float pointZ = 0.0f;
    float normalX = 0.0f;
    float normalY = 0.0f;
    float normalZ = 0.0f;
    float relativeVelocityX = 0.0f;
    float relativeVelocityY = 0.0f;
    float relativeVelocityZ = 0.0f;
    float penetration = 0.0f;
};

struct PhysicsOverlapState {
    bool sensor = false;
    std::uint32_t entity1 = kNullPhysicsEntity;
    std::uint32_t entity2 = kNullPhysicsEntity;
    std::uint32_t body1 = 0;
    std::uint32_t body2 = 0;
};

inline std::uint64_t PhysicsPairKey(std::uint32_t a, std::uint32_t b)
{
    const std::uint32_t lo = a < b ? a : b;
    const std::uint32_t hi = a < b ? b : a;
    return (static_cast<std::uint64_t>(lo) << 32) | hi;
}

inline void CollapseContactRecords(
    const std::vector<PhysicsContactRecord>& records,
    std::unordered_map<std::uint64_t, PhysicsOverlapState>& overlap,
    std::vector<PhysicsContactEvent>& out)
{
    out.clear();
    auto appendPair = [&](const PhysicsContactRecord& rec, std::uint32_t body1, std::uint32_t body2,
                    std::uint32_t entity1, std::uint32_t entity2, bool live1, bool live2,
                    bool sensor, bool enter, bool exitEvent, float nx, float ny, float nz) {
        auto one = [&](std::uint32_t receiver, bool receiverLive, std::uint32_t receiverBody,
                       std::uint32_t otherEntity, bool otherLive) {
            if (!receiverLive || receiver == kNullPhysicsEntity) {
                return;
            }
            PhysicsContactEvent event;
            if (sensor) {
                event.kind = exitEvent ? PhysicsEventKind::TriggerExit
                    : (enter ? PhysicsEventKind::TriggerEnter : PhysicsEventKind::TriggerStay);
            } else {
                event.kind = exitEvent ? PhysicsEventKind::CollisionExit
                    : (enter ? PhysicsEventKind::CollisionEnter : PhysicsEventKind::CollisionStay);
            }
            event.receiver = receiver;
            event.otherAlive = otherLive && otherEntity != kNullPhysicsEntity;
            event.other = event.otherAlive ? otherEntity : kNullPhysicsEntity;
            if (!exitEvent && rec.hasPoint && rec.penetration >= 0.0f) {
                event.hasContact = true;
                event.pointX = 0.5f * (rec.point1X + rec.point2X);
                event.pointY = 0.5f * (rec.point1Y + rec.point2Y);
                event.pointZ = 0.5f * (rec.point1Z + rec.point2Z);
                const bool isBody2 = receiverBody == body2;
                event.normalX = isBody2 ? nx : -nx;
                event.normalY = isBody2 ? ny : -ny;
                event.normalZ = isBody2 ? nz : -nz;
                if (isBody2) {
                    event.relativeVelocityX = rec.vel1X - rec.vel2X;
                    event.relativeVelocityY = rec.vel1Y - rec.vel2Y;
                    event.relativeVelocityZ = rec.vel1Z - rec.vel2Z;
                } else {
                    event.relativeVelocityX = rec.vel2X - rec.vel1X;
                    event.relativeVelocityY = rec.vel2Y - rec.vel1Y;
                    event.relativeVelocityZ = rec.vel2Z - rec.vel1Z;
                }
                event.penetration = rec.penetration;
            }
            out.push_back(event);
        };
        one(entity1, live1, body1, entity2, live2);
        one(entity2, live2, body2, entity1, live1);
    };

    for (PhysicsContactRecord rec : records) {
        if (rec.body1 > rec.body2) {
            std::swap(rec.body1, rec.body2);
            std::swap(rec.entity1, rec.entity2);
            std::swap(rec.entity1Live, rec.entity2Live);
            std::swap(rec.point1X, rec.point2X);
            std::swap(rec.point1Y, rec.point2Y);
            std::swap(rec.point1Z, rec.point2Z);
            std::swap(rec.vel1X, rec.vel2X);
            std::swap(rec.vel1Y, rec.vel2Y);
            std::swap(rec.vel1Z, rec.vel2Z);
            rec.normalX = -rec.normalX;
            rec.normalY = -rec.normalY;
            rec.normalZ = -rec.normalZ;
        }
        const std::uint64_t key = PhysicsPairKey(rec.body1, rec.body2);
        auto it = overlap.find(key);
        if (rec.kind == PhysicsContactKind::Removed) {
            if (!rec.lastContact || it == overlap.end()) {
                continue;
            }
            const PhysicsOverlapState state = it->second;
            overlap.erase(it);
            const std::uint32_t entity1 = rec.entity1 != kNullPhysicsEntity ? rec.entity1 : state.entity1;
            const std::uint32_t entity2 = rec.entity2 != kNullPhysicsEntity ? rec.entity2 : state.entity2;
            appendPair(rec, rec.body1, rec.body2, entity1, entity2, rec.entity1Live, rec.entity2Live,
                       state.sensor, false, true, rec.normalX, rec.normalY, rec.normalZ);
            continue;
        }

        const bool existed = it != overlap.end();
        if (!existed) {
            overlap.emplace(key, PhysicsOverlapState{rec.sensor, rec.entity1, rec.entity2, rec.body1, rec.body2});
        } else {
            it->second.sensor = rec.sensor;
            it->second.entity1 = rec.entity1;
            it->second.entity2 = rec.entity2;
        }
        if (rec.kind == PhysicsContactKind::Added && existed) {
            continue;
        }
        appendPair(rec, rec.body1, rec.body2, rec.entity1, rec.entity2, rec.entity1Live, rec.entity2Live,
                   rec.sensor, !existed, false, rec.normalX, rec.normalY, rec.normalZ);
    }

    std::sort(out.begin(), out.end(), [](const PhysicsContactEvent& a, const PhysicsContactEvent& b) {
        if (a.receiver != b.receiver) {
            return a.receiver < b.receiver;
        }
        if (a.other != b.other) {
            return a.other < b.other;
        }
        return static_cast<std::uint8_t>(a.kind) < static_cast<std::uint8_t>(b.kind);
    });
}

} // namespace Physics
