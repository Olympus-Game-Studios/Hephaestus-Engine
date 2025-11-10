#include "JoltPhysicsEngine.h"
#include <iostream>
#include <Jolt/Core/Memory.h>

// A minimal broad phase layer implementation for Jolt
namespace JPH {
    // Forward declarations for minimal implementation
} // namespace JPH

namespace Physics {

// Initialize Jolt on first use
static bool g_JoltInitialized = false;

static void InitializeJolt() {
    if (!g_JoltInitialized) {
        std::cout << "Registering Jolt default allocator...\n";
        JPH::RegisterDefaultAllocator();
        g_JoltInitialized = true;
        std::cout << "Jolt default allocator registered\n";
    }
}

JoltPhysicsEngine::JoltPhysicsEngine() = default;

JoltPhysicsEngine::~JoltPhysicsEngine() {
    Shutdown();
}

void JoltPhysicsEngine::Initialize() {
    try {
        // First, initialize Jolt's memory allocation system
        InitializeJolt();
        
        // Note: TempAllocator is a stack-only class, so we can't use unique_ptr
        // We'll handle memory management differently for now
        // Create temporary allocator on the stack for initialization only
        
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

        // Create the physics system with default layer setup
        // For now, we'll create a simple physics world without complex layer filtering
        std::cout << "Creating physics system...\n";
        m_PhysicsSystem = std::make_unique<JPH::PhysicsSystem>();
        
        // Initialize with default broad phase layers
        // This uses a simple pass-through implementation
        class SimpleBroadPhaseLayer : public JPH::BroadPhaseLayerInterface {
        public:
            virtual JPH::uint GetNumBroadPhaseLayers() const override { return 1; }
            virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer(0); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return "DefaultLayer"; }
#endif
        };
        
        class SimpleObjectVsBroadPhaseFilter : public JPH::ObjectVsBroadPhaseLayerFilter {
        public:
            virtual bool ShouldCollide(JPH::ObjectLayer layer1, JPH::BroadPhaseLayer layer2) const override { return true; }
        };
        
        class SimpleObjectLayerFilter : public JPH::ObjectLayerPairFilter {
        public:
            virtual bool ShouldCollide(JPH::ObjectLayer layer1, JPH::ObjectLayer layer2) const override { return true; }
        };
        
        static SimpleBroadPhaseLayer broadPhaseLayer;
        static SimpleObjectVsBroadPhaseFilter objVsBroadPhaseFilter;
        static SimpleObjectLayerFilter objectLayerFilter;
        
        std::cout << "Initializing physics system with " << MAX_BODIES << " max bodies\n";
        m_PhysicsSystem->Init(
            MAX_BODIES,
            NUM_BODY_MUTEXES,
            MAX_BODY_PAIRS,
            MAX_CONTACT_CONSTRAINTS,
            broadPhaseLayer,
            objVsBroadPhaseFilter,
            objectLayerFilter
        );
        std::cout << "Physics system initialized\n";

        // Set default gravity
        SetGravity(Vec3(0.0f, -9.81f, 0.0f));

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
    m_PhysicsSystem.reset();
    m_JobSystem.reset();
    // No need to reset m_TempAllocator as it's created on demand
    std::cout << "Jolt Physics Engine shutdown complete\n";
}

void JoltPhysicsEngine::Update(float deltaTime) {
    if (!m_PhysicsSystem || !m_JobSystem) {
        return;
    }

    try {
        // Clamp delta time to prevent instability
        const float maxDeltaTime = 0.033f; // ~30 FPS minimum
        float clampedDeltaTime = std::min(deltaTime, maxDeltaTime);

        // Ensure positive time delta
        if (clampedDeltaTime <= 0.0f) {
            return;
        }

        // Step the physics simulation
        // Use nullptr for allocator - Jolt will use its default
        int collisionSteps = 1;
        m_PhysicsSystem->Update(clampedDeltaTime, collisionSteps, nullptr, m_JobSystem.get());
    } catch (const std::exception& e) {
        std::cerr << "Exception during physics update: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "Unknown exception during physics update\n";
    }
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
