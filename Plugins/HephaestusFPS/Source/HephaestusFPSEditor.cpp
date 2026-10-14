#include "PluginAPI/PluginAPI.h"
#include "FPSInputMerge.h"

#include <QDesktopServices>
#include <QFile>
#include <QMessageBox>
#include <QUrl>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace Hephaestus::Plugins;

namespace {

std::string ReadFile(const std::filesystem::path& path, std::string& error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open " + path.generic_string();
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool WriteFile(const std::filesystem::path& path, const std::string& text, std::string& error)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not write " + path.generic_string();
        return false;
    }
    output << text;
    return true;
}

} // namespace

class HephaestusFPSPlugin final : public IHephaestusPlugin {
public:
    bool OnLoad(IPluginContext& context) override
    {
        m_context = &context;
        bool ok = true;
        ok &= context.Commands().Register(
            {"fps.installInput", "Install FPS Input Defaults",
             "Append missing FPS action and axis names to the project input profile.",
             "", &InstallInput, this});
        ok &= context.Commands().Register(
            {"fps.openDocs", "Open FPS Plugin Notes",
             "Open Documentation/FPS-Plugin.md from the plugin root.", "", &OpenDocs, this});
        ok &= context.UI().RegisterMenuAction(
            {"Tools/FPS/Install FPS Input Defaults", "fps.installInput", "fps"});
        ok &= context.UI().RegisterMenuAction(
            {"Tools/FPS/Open FPS Plugin Notes", "fps.openDocs", "fps"});
        ok &= context.Settings().Register(
            {"inputFragmentVersion", "Input fragment version",
             "Written by Install FPS Input Defaults.", "", SettingScope::Project});
        ok &= context.Settings().Register(
            {"inputProfile", "Input profile",
             "Optional project input map path. Empty uses assets/input/Default.hpinput.",
             "assets/input/Default.hpinput", SettingScope::Project});
        return ok;
    }

    void OnUnload() override { m_context = nullptr; }

private:
    static void OpenDocs(void* user)
    {
        auto* self = static_cast<HephaestusFPSPlugin*>(user);
        if (!self->m_context) {
            return;
        }
        const std::filesystem::path docs =
            std::filesystem::path(self->m_context->Project().PluginRoot()) /
            "Documentation" / "FPS-Plugin.md";
        const QUrl url = QUrl::fromLocalFile(QString::fromStdString(docs.string()));
        if (!QDesktopServices::openUrl(url)) {
            QMessageBox::information(nullptr, "Hephaestus FPS",
                                     QString::fromStdString(docs.generic_string()));
        }
    }

    static void InstallInput(void* user)
    {
        auto* self = static_cast<HephaestusFPSPlugin*>(user);
        if (!self->m_context) {
            return;
        }
        const std::filesystem::path projectRoot(self->m_context->Project().ProjectRoot());
        const std::filesystem::path pluginRoot(self->m_context->Project().PluginRoot());
        const char* profileSetting = self->m_context->Settings().Get("inputProfile");
        std::filesystem::path profilePath = projectRoot / "assets" / "input" / "Default.hpinput";
        if (profileSetting && profileSetting[0] != '\0') {
            const std::filesystem::path configured(profileSetting);
            profilePath = configured.is_absolute() ? configured : projectRoot / configured;
        }

        const std::filesystem::path fragmentPath =
            pluginRoot / "Content" / "Input" / "FPSDefaults.hpinput";
        const std::filesystem::path starterPath =
            pluginRoot / "Content" / "Input" / "FPSStarter.hpinput";

        std::string error;
        std::string destText;
        bool wroteStarter = false;
        if (!std::filesystem::exists(profilePath)) {
            destText = ReadFile(starterPath, error);
            wroteStarter = true;
        } else {
            destText = ReadFile(profilePath, error);
        }
        if (destText.empty()) {
            QMessageBox::warning(nullptr, "Hephaestus FPS", QString::fromStdString(error));
            return;
        }
        const std::string fragmentText = ReadFile(fragmentPath, error);
        if (fragmentText.empty()) {
            QMessageBox::warning(nullptr, "Hephaestus FPS", QString::fromStdString(error));
            return;
        }

        nlohmann::json destination;
        nlohmann::json fragment;
        try {
            destination = nlohmann::json::parse(destText);
            fragment = nlohmann::json::parse(fragmentText);
        } catch (const std::exception& parseError) {
            QMessageBox::warning(nullptr, "Hephaestus FPS", parseError.what());
            return;
        }

        HephaestusFPS::InputMergeResult merged =
            HephaestusFPS::MergeInputFragment(destination, fragment);
        if (!merged.error.empty()) {
            QMessageBox::warning(nullptr, "Hephaestus FPS",
                                 QString::fromStdString(merged.error));
            return;
        }

        if (wroteStarter || merged.changed) {
            if (!WriteFile(profilePath, destination.dump(2) + "\n", error)) {
                QMessageBox::warning(nullptr, "Hephaestus FPS", QString::fromStdString(error));
                return;
            }
        }
        self->m_context->Settings().Set("inputFragmentVersion", "1");

        QString message;
        if (wroteStarter) {
            message = "Wrote a starter input profile at\n" +
                      QString::fromStdString(profilePath.generic_string());
        } else if (merged.added.empty()) {
            message = "No FPS names were missing. Existing bindings were left unchanged.";
        } else {
            message = "Added:\n";
            for (const std::string& name : merged.added) {
                message += QString::fromStdString("- " + name + "\n");
            }
            message += "\nDisable does not remove these names. Fix overlaps in the input editor.";
        }
        QMessageBox::information(nullptr, "Install FPS Input Defaults", message);
    }

    IPluginContext* m_context = nullptr;
};

HP_DECLARE_PLUGIN(HephaestusFPSPlugin)
