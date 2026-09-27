#pragma once

/**
 * @file PluginFirstParty.h
 * @brief Internal first-party plugin boundary.
 *
 * This header is NOT part of the public PluginAPI 1.0 contract. It describes
 * the narrow, engine-owned capabilities that Hephaestus' own plugins (currently
 * the MCP plugin) are allowed to consume. Normal third-party plugins never
 * receive an IFirstPartyPluginContext unless the host explicitly hands one to
 * them, so the public PluginAPI ABI stays untouched.
 *
 * The host passes the context to a first-party plugin by resolving the optional
 * `HP_SetFirstPartyContext` export from the plugin module before calling
 * IHephaestusPlugin::OnLoad. Plugins declare that export with
 * HP_DECLARE_FIRST_PARTY_SET_CONTEXT.
 */

#include "PluginAPI.h"
#include "../Automation/AutomationTypes.h"
#include "../Automation/ToolRegistry.h"
#include "../Core/PlayModeManager.h"

#include <cstdint>
#include <future>

namespace Hephaestus::Plugins {

/// Version of the first-party plugin boundary. Bumped on any ABI change.
inline constexpr std::uint32_t HP_FIRST_PARTY_API_VERSION = 1;

/// Optional plugin export the host probes for before OnLoad.
inline constexpr const char* HP_FIRST_PARTY_SET_CONTEXT_EXPORT = "HP_SetFirstPartyContext";

using ToolFuture = std::future<Automation::ToolResult>;

/**
 * Engine-owned view of the automation service handed to first-party plugins.
 *
 * Deliberately narrow: it exposes the read-only tool registry, the host/play
 * state needed to filter tools, and the main-thread post mechanism. Concrete
 * AutomationService internals (backend, entity refs, queues) remain private to
 * the engine.
 */
class IAutomationService {
public:
    virtual ~IAutomationService() = default;

    [[nodiscard]] virtual const Automation::ToolRegistry& GetRegistry() const = 0;
    [[nodiscard]] virtual Automation::HostMask GetHost() const = 0;
    [[nodiscard]] virtual PlayState GetPlayState() const = 0;

    /// Queue a tool call for execution on the host's main-thread pump.
    virtual ToolFuture PostCall(Automation::ToolCall call) = 0;
};

/**
 * Launch-time overrides supplied by the host on the command line. These are
 * temporary overrides only; persistent state is owned by the plugin itself.
 */
struct FirstPartyStartupOptions {
    bool automationRequested = false;
    bool automationPortSpecified = false;
    int automationPort = 0;
};

/**
 * First-party plugin context. Its version is checked independently of the
 * public PluginAPI version so mismatches fail cleanly rather than corrupting
 * the boundary.
 */
class IFirstPartyPluginContext {
public:
    virtual ~IFirstPartyPluginContext() = default;

    [[nodiscard]] virtual std::uint32_t GetFirstPartyApiVersion() const = 0;
    [[nodiscard]] virtual IAutomationService* GetAutomationService() = 0;
    [[nodiscard]] virtual const FirstPartyStartupOptions& GetStartupOptions() const = 0;
};

/// Invoked by the host on a first-party plugin before IHephaestusPlugin::OnLoad.
using SetFirstPartyContextFn = void (*)(IHephaestusPlugin* instance,
                                        IFirstPartyPluginContext* context);

} // namespace Hephaestus::Plugins

/**
 * Emit the optional first-party context export for a plugin class. The class
 * must provide `void SetFirstPartyContext(Hephaestus::Plugins::IFirstPartyPluginContext*)`.
 */
#define HP_DECLARE_FIRST_PARTY_SET_CONTEXT(PluginClass)                                \
    extern "C" HP_PLUGIN_EXPORT void HP_SetFirstPartyContext(                          \
        ::Hephaestus::Plugins::IHephaestusPlugin* instance,                            \
        ::Hephaestus::Plugins::IFirstPartyPluginContext* context)                      \
    {                                                                                  \
        static_cast<PluginClass*>(instance)->SetFirstPartyContext(context);            \
    }
