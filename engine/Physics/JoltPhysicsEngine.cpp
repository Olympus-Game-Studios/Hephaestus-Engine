#include "JoltPhysicsEngine.h"
#include "../Core/EngineLog.h"

#include <cctype>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <Jolt/Core/Core.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>

// A minimal broad phase layer implementation for Jolt
namespace JPH {
    // Forward declarations for minimal implementation
} // namespace JPH

namespace Physics {

// Process-wide Jolt factory. Released when the last engine shuts down.
static int g_JoltInitCount = 0;

namespace {

void JoltTraceImpl(const char* fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    std::cerr << "[Jolt] " << buffer;
    const size_t len = std::strlen(buffer);
    if (len == 0 || buffer[len - 1] != '\n') {
        std::cerr << '\n';
    }
}

bool JoltAssertFailedImpl(const char* expr, const char* message, const char* file, JPH::uint line)
{
    const char* text = message ? message : (expr ? expr : "(no message)");
    std::cerr << "[Jolt][Assert] " << file << ':' << line << " - " << text << std::endl;
    return false; // Do not trigger breakpoint, let application handle abort
}

// TempAllocatorImplWithMallocFallback policy, plus one Physics warning the
// first time a request misses the fixed block. The Jolt type is final and
// has no fallback hook, so the branch is the same two allocators it uses.
class LoggingFallbackTempAllocator final : public JPH::TempAllocator {
public:
    explicit LoggingFallbackTempAllocator(JPH::uint bytes)
        : m_Fixed(bytes) {}

    void* Allocate(JPH::uint inSize) override
    {
        if (m_Fixed.CanAllocate(inSize)) {
            return m_Fixed.Allocate(inSize);
        }
        bool expected = false;
        if (m_LoggedFallback.compare_exchange_strong(expected, true)) {
            Hephaestus::EngineLog::Write(
                Hephaestus::EngineLogLevel::Warning,
                "Physics temp allocator exceeded the fixed block and used the malloc fallback",
                "Physics");
        }
        return m_Fallback.Allocate(inSize);
    }

    void Free(void* inAddress, JPH::uint inSize) override
    {
        if (inAddress == nullptr) {
            return;
        }
        if (m_Fixed.OwnsMemory(inAddress)) {
            m_Fixed.Free(inAddress, inSize);
        } else {
            m_Fallback.Free(inAddress, inSize);
        }
    }

private:
    JPH::TempAllocatorImpl m_Fixed;
    JPH::TempAllocatorMalloc m_Fallback;
    std::atomic<bool> m_LoggedFallback{false};
};

void LogPhysicsUpdateError(JPH::EPhysicsUpdateError error)
{
    if (error == JPH::EPhysicsUpdateError::None) {
        return;
    }
    const auto bits = static_cast<JPH::uint32>(error);
    auto logBit = [&](JPH::EPhysicsUpdateError flag, const char* name) {
        if ((bits & static_cast<JPH::uint32>(flag)) == 0) {
            return;
        }
        Hephaestus::EngineLog::Write(
            Hephaestus::EngineLogLevel::Error,
            std::string("PhysicsSystem::Update failed: ") + name,
            "Physics");
    };
    logBit(JPH::EPhysicsUpdateError::ManifoldCacheFull, "ManifoldCacheFull");
    logBit(JPH::EPhysicsUpdateError::BodyPairCacheFull, "BodyPairCacheFull");
    logBit(JPH::EPhysicsUpdateError::ContactConstraintsFull, "ContactConstraintsFull");
}

void AcquireJoltRuntime()
{
    if (g_JoltInitCount++ != 0) {
        return;
    }
    std::cout << "Registering Jolt default allocator...\n";
    JPH::RegisterDefaultAllocator();

    std::cout << "Installing Jolt trace and assert callbacks...\n";
    JPH::Trace = JoltTraceImpl;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = JoltAssertFailedImpl;
#endif

    std::cout << "Creating Jolt factory...\n";
    JPH::Factory::sInstance = new JPH::Factory();

    std::cout << "Registering Jolt physics types...\n";
    JPH::RegisterTypes();
    std::cout << "Jolt Physics fully initialized\n";
}

void ReleaseJoltRuntime()
{
    if (g_JoltInitCount == 0) {
        return;
    }
    if (--g_JoltInitCount != 0) {
        return;
    }
    std::cout << "Unregistering Jolt types...\n";
    JPH::UnregisterTypes();
    std::cout << "Destroying Jolt factory...\n";
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
}

} // namespace

JoltPhysicsEngine::JoltPhysicsEngine() = default;

JoltPhysicsEngine::~JoltPhysicsEngine() {
    Shutdown();
}

void JoltPhysicsEngine::Initialize() {
    try {
        if (!m_HoldsGlobalJolt) {
            AcquireJoltRuntime();
            m_HoldsGlobalJolt = true;
        }
        
        // Create the job system with hardware concurrency
        int numThreads = std::max(1, (int)std::thread::hardware_concurrency() - 1);
        std::cout << "Creating job system with " << numThreads << " threads\n";
        
        // Clamp threads to reasonable range
        numThreads = std::max(1, std::min(numThreads, 16));
        
        m_JobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            numThreads
        );
        std::cout << "Job system created successfully\n";

        // 18 MiB fixed block: twice the heavier-scene peak recorded by
        // PhysicsSettlingRepro (9039344 bytes), rounded up to 1 MiB. The
        // historic 8-box peak was 9028112. That scene survived on Jolt 5.4.1;
        // the 4.1.0-rc1 settling-crash comment was not reproduced. Overflow
        // mallocs instead of aborting and logs one Physics warning.
        m_TempAllocator = std::make_unique<LoggingFallbackTempAllocator>(
            static_cast<JPH::uint>(kTempAllocatorBytes));
        std::cout << "Temp allocator created (" << (kTempAllocatorBytes / (1024 * 1024))
                  << " MB fixed block, malloc fallback)\n";

        BeginPlayWorld(PhysicsLayerConfig::Compatibility());
        std::cout << "Jolt Physics Engine initialized successfully\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception during physics initialization: " << e.what() << "\n";
        throw;
    } catch (...) {
        std::cerr << "Unknown exception during physics initialization\n";
        throw;
    }
}

void JoltPhysicsEngine::Shutdown() {
    EndPlayWorld();
    m_JobSystem.reset();
    m_TempAllocator.reset();

    if (m_HoldsGlobalJolt) {
        ReleaseJoltRuntime();
        m_HoldsGlobalJolt = false;
    }

    std::cout << "Jolt Physics Engine shutdown complete\n";
}

void JoltPhysicsEngine::Update(float deltaTime) {
    if (!m_PhysicsSystem || !m_JobSystem) {
        return;
    }

        try {
        if (deltaTime <= 0.0f) {
            return;
        }

        // The host accumulator is the only clamp. This steps the supplied dt once.
        int collisionSteps = 1;
        JPH::TempAllocator* tempAllocator = m_TempAllocator.get();
        if (!tempAllocator) {
            m_TempAllocator = std::make_unique<LoggingFallbackTempAllocator>(
                static_cast<JPH::uint>(kTempAllocatorBytes));
            tempAllocator = m_TempAllocator.get();
        }

        const JPH::EPhysicsUpdateError error = m_PhysicsSystem->Update(
            deltaTime,
            collisionSteps,
            tempAllocator,
            m_JobSystem.get()
        );
        LogPhysicsUpdateError(error);
    } catch (const std::exception& e) {
        std::cerr << "Exception during physics update: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "Unknown exception during physics update (likely Jolt crash)\n";
    }
}

std::unique_ptr<JPH::TempAllocator> JoltPhysicsEngine::ExchangeTempAllocatorForTest(
    std::unique_ptr<JPH::TempAllocator> replacement)
{
    m_TempAllocator.swap(replacement);
    return replacement;
}

bool JoltPhysicsEngine::TryResolveLayerToken(const std::string& token, std::uint16_t& outId, std::string& outError) const
{
    const std::uint16_t byName = m_LayerConfig.FindIdByName(token);
    if (byName != kInvalidLayer) {
        outId = byName;
        return true;
    }
    if (!token.empty() && std::all_of(token.begin(), token.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        const int value = std::atoi(token.c_str());
        if (value >= 0 && value < static_cast<int>(kMaxGameplayLayers) &&
            m_LayerConfig.HasLayer(static_cast<std::uint16_t>(value))) {
            outId = static_cast<std::uint16_t>(value);
            return true;
        }
    }
    outError = "unknown physics layer '" + token + "'";
    return false;
}

void JoltPhysicsEngine::BuildFilterTables(const PhysicsLayerConfig& config)
{
    constexpr JPH::uint kObjectLayers = static_cast<JPH::uint>(kMaxGameplayLayers) * 2;
    constexpr JPH::uint kBroadPhaseLayers = 2;
    m_BroadPhase = std::make_unique<JPH::BroadPhaseLayerInterfaceTable>(kObjectLayers, kBroadPhaseLayers);
    m_ObjectPairFilter = std::make_unique<JPH::ObjectLayerPairFilterTable>(kObjectLayers);
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    m_BroadPhase->SetBroadPhaseLayerName(JPH::BroadPhaseLayer(0), "NonMoving");
    m_BroadPhase->SetBroadPhaseLayerName(JPH::BroadPhaseLayer(1), "Moving");
#endif
    for (JPH::uint gameplay = 0; gameplay < kMaxGameplayLayers; ++gameplay) {
        m_BroadPhase->MapObjectToBroadPhaseLayer(static_cast<JPH::ObjectLayer>(gameplay * 2), JPH::BroadPhaseLayer(0));
        m_BroadPhase->MapObjectToBroadPhaseLayer(static_cast<JPH::ObjectLayer>(gameplay * 2 + 1), JPH::BroadPhaseLayer(1));
    }
    for (JPH::uint first = 0; first < kObjectLayers; ++first) {
        for (JPH::uint second = 0; second <= first; ++second) {
            const auto gameplayA = static_cast<std::uint16_t>(first / 2);
            const auto gameplayB = static_cast<std::uint16_t>(second / 2);
            const bool staticA = (first % 2) == 0;
            const bool staticB = (second % 2) == 0;
            if (staticA && staticB) {
                continue;
            }
            if (!config.HasLayer(gameplayA) || !config.HasLayer(gameplayB)) {
                continue;
            }
            if (!config.Collides(gameplayA, gameplayB)) {
                continue;
            }
            m_ObjectPairFilter->EnableCollision(static_cast<JPH::ObjectLayer>(first),
                                                static_cast<JPH::ObjectLayer>(second));
        }
    }
    m_ObjectVsBroadPhase = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(
        *m_BroadPhase, kBroadPhaseLayers, *m_ObjectPairFilter, kObjectLayers);
}

void JoltPhysicsEngine::EndPlayWorld()
{
    if (m_PhysicsSystem) {
        m_PhysicsSystem->SetContactListener(nullptr);
    }
    m_PhysicsSystem.reset();
    m_ObjectVsBroadPhase.reset();
    m_ObjectPairFilter.reset();
    m_BroadPhase.reset();
}

void JoltPhysicsEngine::BeginPlayWorld(const PhysicsLayerConfig& config)
{
    EndPlayWorld();
    m_LayerConfig = config;
    if (!m_HoldsGlobalJolt) {
        AcquireJoltRuntime();
        m_HoldsGlobalJolt = true;
    }
    BuildFilterTables(m_LayerConfig);
    std::cout << "Creating physics system...\n";
    m_PhysicsSystem = std::make_unique<JPH::PhysicsSystem>();
    std::cout << "Initializing physics system with " << MAX_BODIES << " max bodies\n";
    m_PhysicsSystem->Init(
        MAX_BODIES,
        NUM_BODY_MUTEXES,
        MAX_BODY_PAIRS,
        MAX_CONTACT_CONSTRAINTS,
        *m_BroadPhase,
        *m_ObjectVsBroadPhase,
        *m_ObjectPairFilter);
    SetGravity(Vec3(0.0f, -9.81f, 0.0f));
    std::cout << "Physics system initialized\n";
}

JPH::ObjectLayer JoltPhysicsEngine::ObjectLayerFor(std::uint16_t gameplayLayer, bool isStatic) const
{
    const std::uint16_t resolved = m_LayerConfig.Resolve(gameplayLayer);
    return static_cast<JPH::ObjectLayer>(static_cast<unsigned>(resolved) * 2u + (isStatic ? 0u : 1u));
}

bool JoltPhysicsEngine::EncodedLayersShouldCollide(std::uint16_t gameplayA, bool staticA,
                                                   std::uint16_t gameplayB, bool staticB) const
{
    if (!m_ObjectPairFilter) {
        return false;
    }
    return m_ObjectPairFilter->ShouldCollide(ObjectLayerFor(gameplayA, staticA),
                                             ObjectLayerFor(gameplayB, staticB));
}

void JoltPhysicsEngine::SetGravity(const Vec3& gravity) {
    if (m_PhysicsSystem) {
        m_PhysicsSystem->SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }
}

Vec3 JoltPhysicsEngine::GetGravity() const {
    if (m_PhysicsSystem) {
        JPH::Vec3 joltGravity = m_PhysicsSystem->GetGravity();
        return Vec3(joltGravity.GetX(), joltGravity.GetY(), joltGravity.GetZ());
    }
    return Vec3(0.0f, 0.0f, 0.0f);
}

} // namespace Physics
