#include "JoltPhysicsEngine.h"
#include <iostream>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <Jolt/Core/Core.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>

// A minimal broad phase layer implementation for Jolt
namespace JPH {
    // Forward declarations for minimal implementation
} // namespace JPH

namespace Physics {

// Initialize Jolt on first use
static bool g_JoltInitialized = false;

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

} // namespace

static void InitializeJolt() {
    if (!g_JoltInitialized) {
        std::cout << "Registering Jolt default allocator...\n";
        JPH::RegisterDefaultAllocator();
        
        std::cout << "Installing Jolt trace and assert callbacks...\n";
        JPH::Trace = JoltTraceImpl;
        #ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = JoltAssertFailedImpl;
        #endif
        
        // Create factory for deserialization
        std::cout << "Creating Jolt factory...\n";
        JPH::Factory::sInstance = new JPH::Factory();
        
        // Register all Jolt physics types
        std::cout << "Registering Jolt physics types...\n";
        JPH::RegisterTypes();
        
        g_JoltInitialized = true;
        std::cout << "Jolt Physics fully initialized\n";
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

        // Persistent temporary allocator used during simulation steps
        // Increased to 1024 MB (1 GB) to handle island building during settling phase
        m_TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(kStockTempAllocatorBytes);
        std::cout << "Temp allocator created (" << (kStockTempAllocatorBytes / (1024 * 1024)) << " MB)\n";

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
    m_TempAllocator.reset();
    
    // Clean up Jolt global state
    if (g_JoltInitialized) {
        std::cout << "Unregistering Jolt types...\n";
        JPH::UnregisterTypes();
        
        std::cout << "Destroying Jolt factory...\n";
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
        
        g_JoltInitialized = false;
    }
    
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

        // Step the physics simulation with our temp allocator
        int collisionSteps = 1;
        JPH::TempAllocator* tempAllocator = m_TempAllocator.get();
        if (!tempAllocator) {
            // Fallback for safety if initialization was skipped
            m_TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(kStockTempAllocatorBytes);
            tempAllocator = m_TempAllocator.get();
        }

        // NOTE: Known issue with Jolt Physics 4.1.0-rc1:
        // Physics simulation crashes after ~50-60 frames of body settling
        // This appears to be related to island management or contact constraint evolution
        // Workaround: reduce max bodies or use a different Jolt version
        m_PhysicsSystem->Update(
            clampedDeltaTime,
            collisionSteps,
            tempAllocator,
            m_JobSystem.get()
        );
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
