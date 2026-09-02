#include "EditorViewState.h"

#include "../Scene/Json.h"

#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>

namespace Hephaestus {
namespace {

void ReadIds(const JsonValue* value, std::unordered_set<uint32_t>& out)
{
    out.clear();
    if (!value || !value->IsArray()) return;
    for (const JsonValue& item : value->AsArray()) {
        if (item.IsNumber() && item.AsNumber() >= 0.0) {
            out.insert(static_cast<uint32_t>(item.AsNumber()));
        }
    }
}

double Number(const JsonValue& object, const char* key, double fallback = 0.0)
{
    const JsonValue* value = object.Find(key);
    return value ? value->AsNumber(fallback) : fallback;
}

} // namespace

std::filesystem::path EditorViewState::StatePath(
    const std::filesystem::path& projectRoot,
    const std::string& sceneKey)
{
    const size_t hash = std::hash<std::string>{}(sceneKey);
    std::ostringstream name;
    name << std::hex << hash << ".json";
    return projectRoot / ".hephaestus" / "editor" / "scenes" / name.str();
}

bool EditorViewState::Load(const std::filesystem::path& path)
{
    hiddenEntities.clear();
    lockedEntities.clear();
    bookmarks.clear();

    std::ifstream input(path);
    if (!input.is_open()) return false;
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    JsonValue root;
    std::string error;
    JsonParser parser(text);
    if (!parser.Parse(root, error) || !root.IsObject()) return false;

    ReadIds(root.Find("hidden"), hiddenEntities);
    ReadIds(root.Find("locked"), lockedEntities);

    const JsonValue* bookmarkObject = root.Find("bookmarks");
    if (bookmarkObject && bookmarkObject->IsObject()) {
        for (const auto& [key, value] : bookmarkObject->AsObject()) {
            if (!value.IsObject()) continue;
            int slot = 0;
            try {
                slot = std::stoi(key);
            } catch (...) {
                continue;
            }
            if (slot < 1 || slot > 9) continue;
            EditorCameraBookmark bookmark;
            bookmark.positionX = static_cast<float>(Number(value, "x"));
            bookmark.positionY = static_cast<float>(Number(value, "y"));
            bookmark.positionZ = static_cast<float>(Number(value, "z"));
            bookmark.yawDeg = static_cast<float>(Number(value, "yaw", -90.0));
            bookmark.pitchDeg = static_cast<float>(Number(value, "pitch", -15.0));
            bookmark.orthoHalfExtent =
                static_cast<float>(Number(value, "orthoExtent", 10.0));
            const JsonValue* orthographic = value.Find("orthographic");
            bookmark.orthographic =
                orthographic && orthographic->AsBool(false);
            bookmarks[slot] = bookmark;
        }
    }
    return true;
}

bool EditorViewState::Save(const std::filesystem::path& path) const
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;

    const std::filesystem::path temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output.is_open()) return false;

    auto writeIds = [&output](const std::unordered_set<uint32_t>& ids) {
        bool first = true;
        output << '[';
        for (uint32_t id : ids) {
            if (!first) output << ',';
            output << id;
            first = false;
        }
        output << ']';
    };

    output << "{\n  \"hidden\": ";
    writeIds(hiddenEntities);
    output << ",\n  \"locked\": ";
    writeIds(lockedEntities);
    output << ",\n  \"bookmarks\": {";
    bool first = true;
    output << std::fixed << std::setprecision(6);
    for (const auto& [slot, bookmark] : bookmarks) {
        if (!first) output << ',';
        output << "\n    \"" << slot << "\": {"
               << "\"x\":" << bookmark.positionX
               << ",\"y\":" << bookmark.positionY
               << ",\"z\":" << bookmark.positionZ
               << ",\"yaw\":" << bookmark.yawDeg
               << ",\"pitch\":" << bookmark.pitchDeg
               << ",\"orthographic\":"
               << (bookmark.orthographic ? "true" : "false")
               << ",\"orthoExtent\":" << bookmark.orthoHalfExtent << '}';
        first = false;
    }
    if (!bookmarks.empty()) output << '\n';
    output << "  }\n}\n";
    output.close();
    if (!output.good()) return false;

    std::filesystem::rename(temporary, path, ec);
    if (!ec) return true;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temporary, path, ec);
    return !ec;
}

} // namespace Hephaestus
