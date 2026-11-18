#include "Assets/RecoveryStore.h"
#include "Core/EditorContext.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

fs::path MakeTempProject(const fs::path& base) {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = base / ("recovery_test_" + std::to_string(stamp));
    fs::create_directories(root / "assets" / "scenes");
    return root;
}

bool ReadJsonFile(const fs::path& path, nlohmann::json& out) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    try {
        in >> out;
        return true;
    } catch (...) {
        return false;
    }
}

bool TestSessionLifecycle(const fs::path& tempRoot) {
    bool ok = true;
    const fs::path project = MakeTempProject(tempRoot);
    RecoveryStore store;
    ok &= Check(store.BeginSession(project), "BeginSession succeeds");
    ok &= Check(store.HasActiveSession(), "session active");

    const fs::path sessionPath = RecoveryStore::RecoveryRoot(project) / "session.json";
    ok &= Check(fs::exists(sessionPath), "session.json created");

    nlohmann::json session;
    ok &= Check(ReadJsonFile(sessionPath, session), "session.json readable");
    ok &= Check(session.value("cleanExit", true) == false, "cleanExit starts false");
    ok &= Check(session.contains("pid"), "session has pid");
    ok &= Check(session.contains("lastHeartbeat"), "session has lastHeartbeat");
    ok &= Check(session.contains("projectRoot"), "session has projectRoot");

    ok &= Check(store.Heartbeat(), "Heartbeat succeeds");
    ok &= Check(store.EndSessionClean(), "EndSessionClean succeeds");
    ok &= Check(ReadJsonFile(sessionPath, session), "session reload after clean");
    ok &= Check(session.value("cleanExit", false) == true, "cleanExit true after EndSessionClean");

    fs::remove_all(project);
    return ok;
}

bool TestSnapshotRoundtripAndNoAssetWrite(const fs::path& tempRoot) {
    bool ok = true;
    const fs::path project = MakeTempProject(tempRoot);
    const fs::path realScene = project / "assets" / "scenes" / "Main.hpscene";
    {
        std::ofstream out(realScene);
        out << "{\"schema\":\"HephaestusScene\",\"original\":true}\n";
    }
    const auto originalSize = fs::file_size(realScene);

    RecoveryStore store;
    ok &= Check(store.BeginSession(project), "BeginSession");

    const std::string content = "{\"schema\":\"HephaestusScene\",\"name\":\"Recovered\"}\n";
    const std::string id = store.WriteSceneSnapshot(
        "doc-main",
        "assets/scenes/Main.hpscene",
        content,
        std::string("2026-09-01T00:00:00Z"));
    ok &= Check(!id.empty(), "WriteSceneSnapshot returns id");
    ok &= Check(id.find("scene_") == 0, "id prefix scene_");

    const fs::path snapDir = RecoveryStore::RecoveryRoot(project) / "snapshots";
    ok &= Check(fs::exists(snapDir), "snapshots dir exists");
    ok &= Check(fs::exists(RecoveryStore::RecoveryRoot(project) / "index.json"), "index.json exists");

    bool foundSnapshotExt = false;
    for (const auto& entry : fs::directory_iterator(snapDir)) {
        const auto name = entry.path().filename().string();
        if (name.find(".hpscene.snapshot") != std::string::npos) {
            foundSnapshotExt = true;
        }
        ok &= Check(name.find(".hpscene.snapshot") != std::string::npos,
                    "snapshot uses .hpscene.snapshot extension, not bare .hpscene");
    }
    ok &= Check(foundSnapshotExt, "at least one snapshot file");

    ok &= Check(fs::file_size(realScene) == originalSize, "real .hpscene asset untouched (size)");
    {
        std::ifstream in(realScene);
        std::string line;
        std::getline(in, line);
        ok &= Check(line.find("original") != std::string::npos, "real .hpscene content untouched");
    }

    // Clean session => no recovery candidates yet
    ok &= Check(store.ListRecoveryCandidates().empty(), "no candidates while pending unset");

    const auto loaded = store.LoadSnapshotContent(id);
    ok &= Check(loaded.has_value(), "LoadSnapshotContent finds snapshot");
    ok &= Check(loaded && *loaded == content, "LoadSnapshotContent matches written content");

    ok &= Check(store.DiscardSnapshot(id), "DiscardSnapshot");
    ok &= Check(!store.LoadSnapshotContent(id).has_value(), "discarded snapshot gone");

    fs::remove_all(project);
    return ok;
}

bool TestUncleanDetectionAndCandidates(const fs::path& tempRoot) {
    bool ok = true;
    const fs::path project = MakeTempProject(tempRoot);

    {
        RecoveryStore writer;
        ok &= Check(writer.BeginSession(project), "writer BeginSession");
        const std::string id = writer.WriteSceneSnapshot(
            "docA", "assets/scenes/A.hpscene", "{ \"name\": \"A\" }\n");
        ok &= Check(!id.empty(), "snapshot written before crash");
        // Leave session unclean (no EndSessionClean). Forge dead PID + stale heartbeat.
        const fs::path sessionPath = RecoveryStore::RecoveryRoot(project) / "session.json";
        nlohmann::json session;
        ok &= Check(ReadJsonFile(sessionPath, session), "read session for forge");
        session["pid"] = 4294967294u; // unlikely live editor PID
        session["cleanExit"] = false;
        session["lastHeartbeat"] = "2000-01-01T00:00:00Z";
        {
            std::ofstream out(sessionPath);
            out << session.dump(2);
        }
        ok &= Check(RecoveryStore::IsUncleanSession(sessionPath), "IsUncleanSession detects crash");
    }

    RecoveryStore next;
    ok &= Check(next.BeginSession(project), "BeginSession after crash");
    ok &= Check(next.HasPendingRecovery(), "pending recovery flagged");
    const auto candidates = next.ListRecoveryCandidates();
    ok &= Check(!candidates.empty(), "ListRecoveryCandidates non-empty after unclean");
    ok &= Check(candidates[0].documentId == "docA", "candidate documentId");

    const auto content = next.LoadSnapshotContent(candidates[0].id);
    ok &= Check(content.has_value() && content->find("\"A\"") != std::string::npos,
                "candidate content loadable");

    ok &= Check(next.DiscardAll(), "DiscardAll");
    ok &= Check(next.ListRecoveryCandidates().empty(), "candidates cleared after DiscardAll");
    ok &= Check(!next.HasPendingRecovery(), "pending cleared");

    fs::remove_all(project);
    return ok;
}

bool TestPrune(const fs::path& tempRoot) {
    bool ok = true;
    const fs::path project = MakeTempProject(tempRoot);
    RecoveryStore store;
    ok &= Check(store.BeginSession(project), "BeginSession prune");

    for (int i = 0; i < 4; ++i) {
        const std::string id = store.WriteSceneSnapshot(
            "docPrune",
            "assets/scenes/P.hpscene",
            std::string("{ \"i\": ") + std::to_string(i) + " }\n");
        ok &= Check(!id.empty(), "prune snapshot write " + std::to_string(i));
    }

    ok &= Check(store.Prune(2, std::nullopt), "Prune max 2 per doc");
    nlohmann::json index;
    ok &= Check(ReadJsonFile(RecoveryStore::RecoveryRoot(project) / "index.json", index),
                "index after prune");
    ok &= Check(index["snapshots"].is_array() && index["snapshots"].size() == 2,
                "prune kept 2 snapshots");

    for (auto& item : index["snapshots"]) {
        item["timestamp"] = "2000-01-01T00:00:00Z";
    }
    {
        std::ofstream out(RecoveryStore::RecoveryRoot(project) / "index.json");
        out << index.dump(2);
    }
    ok &= Check(store.Prune(10, std::chrono::seconds(60)), "Prune by maxAge");
    ok &= Check(ReadJsonFile(RecoveryStore::RecoveryRoot(project) / "index.json", index),
                "index after age prune");
    ok &= Check(index["snapshots"].empty(), "age prune removed all");

    fs::remove_all(project);
    return ok;
}

std::string ReadText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool HasEntityNamed(Scene& scene, const std::string& name) {
    for (auto handle : scene.GetEntitiesWith<TagComponent>()) {
        if (scene.GetRegistry().get<TagComponent>(handle).tag == name) {
            return true;
        }
    }
    return false;
}

// A snapshot of one scene must come back in that scene, never in the one that happens to be open.
bool TestRestoreIntoOwnScene(const fs::path& base) {
    bool ok = true;
    const fs::path project = MakeTempProject(base);
    const fs::path a = project / "assets" / "scenes" / "A.hpscene";
    const fs::path b = project / "assets" / "scenes" / "B.hpscene";
    {
        Scene sceneA("A");
        sceneA.CreateEntity("OnlyInA");
        Scene sceneB("B");
        sceneB.CreateEntity("OnlyInB");
        std::string error;
        ok &= Check(SceneSerializer(sceneA).Serialize(a, error) && SceneSerializer(sceneB).Serialize(b, error),
                    "write the two scenes");
    }
    Scene edited("A");
    edited.CreateEntity("OnlyInA");
    edited.CreateEntity("RecoveredEdit");
    SceneSerializer serializer(edited);
    const std::string snapshot = serializer.SerializeToString();
    const std::string bOnDisk = ReadText(b);

    Hephaestus::EditorContext context;
    ok &= Check(context.LoadScene(b.string()), "open B");
    bool openedSource = false;
    std::string error;
    ok &= Check(context.RestoreSceneSnapshot(a.string(), snapshot, openedSource, error), "restore A's snapshot: " + error);
    ok &= Check(openedSource && fs::path(context.GetScenePath()) == a, "the snapshot reopens its own scene, not B");
    ok &= Check(context.GetActiveScene() && HasEntityNamed(*context.GetActiveScene(), "RecoveredEdit")
                    && !HasEntityNamed(*context.GetActiveScene(), "OnlyInB"),
                "the restored scene holds the snapshot");
    ok &= Check(context.IsDirty(), "the restored scene is unsaved");
    ok &= Check(ReadText(b) == bOnDisk, "B's file is untouched");

    fs::remove(a);
    ok &= Check(context.LoadScene(b.string()), "open B again");
    ok &= Check(context.RestoreSceneSnapshot(a.string(), snapshot, openedSource, error) && !openedSource
                    && context.GetScenePath().empty(),
                "a snapshot whose scene file is gone goes into a new unsaved scene");
    ok &= Check(ReadText(b) == bOnDisk, "B's file is still untouched");

    fs::remove_all(project);
    return ok;
}

} // namespace

int main() {
    const fs::path tempRoot = fs::temp_directory_path() / "hephaestus_recovery_tests";
    fs::create_directories(tempRoot);

    bool ok = true;
    ok &= TestSessionLifecycle(tempRoot);
    ok &= TestSnapshotRoundtripAndNoAssetWrite(tempRoot);
    ok &= TestUncleanDetectionAndCandidates(tempRoot);
    ok &= TestPrune(tempRoot);
    ok &= TestRestoreIntoOwnScene(tempRoot);

    if (ok) {
        std::cout << "[PASS] RecoveryStoreTests\n";
        return 0;
    }
    std::cerr << "[FAIL] RecoveryStoreTests\n";
    return 1;
}
