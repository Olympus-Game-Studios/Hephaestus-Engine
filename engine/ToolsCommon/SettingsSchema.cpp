#include "SettingsSchema.h"

namespace {

using Hephaestus::Tools::SettingControlType;
using Hephaestus::Tools::SettingDefinition;

SettingDefinition MakeDef(const QString& id,
                          const QString& categoryId,
                          const QString& categoryLabel,
                          const QString& key,
                          const QString& label,
                          SettingControlType type,
                          const QVariant& defaultValue,
                          const QVariant& minValue = {},
                          const QVariant& maxValue = {},
                          const QStringList& enumOptions = {})
{
    SettingDefinition def;
    def.id = id;
    def.categoryId = categoryId;
    def.categoryLabel = categoryLabel;
    def.key = key;
    def.label = label;
    def.controlType = type;
    def.defaultValue = defaultValue;
    def.minValue = minValue;
    def.maxValue = maxValue;
    def.enumOptions = enumOptions;
    return def;
}

} // namespace

namespace Hephaestus::Tools {

SettingsSchemaRegistry& SettingsSchemaRegistry::Instance()
{
    static SettingsSchemaRegistry instance;
    return instance;
}

SettingsSchemaRegistry::SettingsSchemaRegistry()
{
    RegisterBuiltins();
}

const QVector<SettingDefinition>& SettingsSchemaRegistry::EngineSettings() const
{
    return m_engineDefinitions;
}

const QVector<SettingDefinition>& SettingsSchemaRegistry::ProjectSettings() const
{
    return m_projectDefinitions;
}

void SettingsSchemaRegistry::RegisterEngineSetting(const SettingDefinition& definition)
{
    if (definition.id.isEmpty() || ContainsId(m_engineDefinitions, definition.id)) {
        return;
    }
    m_engineDefinitions.push_back(definition);
}

void SettingsSchemaRegistry::RegisterProjectSetting(const SettingDefinition& definition)
{
    if (definition.id.isEmpty() || ContainsId(m_projectDefinitions, definition.id)) {
        return;
    }
    m_projectDefinitions.push_back(definition);
}

bool SettingsSchemaRegistry::ContainsId(const QVector<SettingDefinition>& definitions, const QString& id)
{
    for (const SettingDefinition& definition : definitions) {
        if (definition.id.compare(id, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void SettingsSchemaRegistry::RegisterBuiltins()
{
    m_engineDefinitions = {
        MakeDef("engine.general.theme", "general", "General", "theme", "Theme", SettingControlType::Enum,
            QStringLiteral("Dark"), {}, {}, {QStringLiteral("Dark"), QStringLiteral("Light"), QStringLiteral("Custom")}),
        MakeDef("engine.general.autosaveInterval", "general", "General", "autosaveInterval", "Autosave Interval (s)", SettingControlType::Int,
            120, 10, 7200),
        MakeDef("engine.general.defaultProjectLocation", "general", "General", "defaultProjectLocation", "Default Project Location", SettingControlType::Path,
            QString()),

        MakeDef("engine.rendering.defaultRhi", "rendering", "Rendering", "defaultRhi", "Default RHI", SettingControlType::Enum,
            QStringLiteral("Vulkan"), {}, {}, {QStringLiteral("Vulkan")}),
        MakeDef("engine.rendering.defaultShadowQuality", "rendering", "Rendering", "defaultShadowQuality", "Default Shadow Quality", SettingControlType::Enum,
            QStringLiteral("High"), {}, {}, {QStringLiteral("Low"), QStringLiteral("Medium"), QStringLiteral("High"), QStringLiteral("Epic")}),
        MakeDef("engine.rendering.defaultHdrEnabled", "rendering", "Rendering", "defaultHdrEnabled", "Default HDR Enabled", SettingControlType::Bool,
            true),

        MakeDef("engine.editor.gizmoSize", "editor", "Editor", "gizmoSize", "Gizmo Size", SettingControlType::Float,
            1.0, 0.1, 5.0),
        MakeDef("engine.editor.gridSnappingDefault", "editor", "Editor", "gridSnappingDefault", "Grid Snapping Default", SettingControlType::Bool,
            true),
        MakeDef("engine.editor.cameraSpeedDefault", "editor", "Editor", "cameraSpeedDefault", "Camera Speed Default", SettingControlType::Float,
            4.0, 0.1, 50.0),
        MakeDef("engine.editor.assetBrowserThumbnailSize", "editor", "Editor", "assetBrowserThumbnailSize", "Asset Browser Thumbnail Size", SettingControlType::Int,
            96, 32, 256),

        MakeDef("engine.paths.sdkPath", "paths", "Paths", "sdkPath", "SDK Path", SettingControlType::Path,
            QString()),
        MakeDef("engine.paths.externalToolsPath", "paths", "Paths", "externalToolsPath", "External Tools Path", SettingControlType::Path,
            QString())
    };

    m_projectDefinitions = {
        MakeDef("project.info.name", "projectInfo", "Project Info", "name", "Project Name", SettingControlType::String,
            QStringLiteral("Untitled Project")),
        MakeDef("project.info.description", "projectInfo", "Project Info", "description", "Description", SettingControlType::String,
            QString()),
        MakeDef("project.info.version", "projectInfo", "Project Info", "version", "Version", SettingControlType::String,
            QStringLiteral("1.0.0")),
        MakeDef("project.info.companyName", "projectInfo", "Project Info", "companyName", "Company Name", SettingControlType::String,
            QString()),
        MakeDef("project.info.defaultScene", "projectInfo", "Project Info", "defaultScene", "Startup Scene", SettingControlType::String,
            QString()),
        MakeDef("project.info.lastScene", "projectInfo", "Project Info", "lastScene", "Last Scene", SettingControlType::String,
            QString()),

        MakeDef("project.rendering.defaultSkybox", "rendering", "Rendering", "defaultSkybox", "Default Skybox", SettingControlType::Path,
            QString()),
        MakeDef("project.rendering.enableHdr", "rendering", "Rendering", "enableHdr", "Enable HDR", SettingControlType::Bool,
            true),
        MakeDef("project.rendering.defaultExposure", "rendering", "Rendering", "defaultExposure", "Default Exposure", SettingControlType::Float,
            1.0, 0.1, 10.0),
        MakeDef("project.rendering.physicsEnabled", "rendering", "Rendering", "physicsEnabled", "Physics Enabled", SettingControlType::Bool,
            true),

        MakeDef("project.input.defaultInputProfile", "input", "Input", "defaultInputProfile", "Default Input Profile", SettingControlType::String,
            QStringLiteral("Default")),
        MakeDef("project.input.mouseSensitivityDefault", "input", "Input", "mouseSensitivityDefault", "Mouse Sensitivity Default", SettingControlType::Float,
            1.0, 0.1, 10.0),

        MakeDef("project.build.targetPlatform", "build", "Build", "targetPlatform", "Target Platform", SettingControlType::Enum,
            QStringLiteral("Windows x64"), {}, {}, {QStringLiteral("Windows x64")}),
        MakeDef("project.build.defaultConfiguration", "build", "Build", "defaultConfiguration", "Default Configuration", SettingControlType::Enum,
            QStringLiteral("Development"), {}, {}, {QStringLiteral("Development"), QStringLiteral("Release")}),
        MakeDef("project.build.windowTitle", "build", "Build", "windowTitle", "Window Title", SettingControlType::String,
            QString()),
        MakeDef("project.build.applicationIcon", "build", "Build", "applicationIcon", "Application Icon", SettingControlType::Path,
            QString()),
        MakeDef("project.build.defaultResolution", "build", "Build", "defaultResolution", "Default Resolution", SettingControlType::Enum,
            QStringLiteral("1920x1080"), {}, {}, {QStringLiteral("1280x720"), QStringLiteral("1600x900"), QStringLiteral("1920x1080"), QStringLiteral("2560x1440")}),
        MakeDef("project.build.fullscreenDefault", "build", "Build", "fullscreenDefault", "Fullscreen Default", SettingControlType::Bool,
            false),
        MakeDef("project.build.vsyncDefault", "build", "Build", "vsyncDefault", "VSync Default", SettingControlType::Bool,
            true)
    };
}

} // namespace Hephaestus::Tools
