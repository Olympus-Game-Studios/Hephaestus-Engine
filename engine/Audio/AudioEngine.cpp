#include "AudioEngine.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {
std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsSupportedAudioExtension(const std::filesystem::path& path) {
    const std::string ext = ToLower(path.extension().string());
    return ext == ".wav" || ext == ".mp3";
}
}

AudioEngine::AudioEngine() {
    m_SearchPaths.push_back(std::filesystem::current_path());
    m_SearchPaths.push_back(std::filesystem::current_path() / "assets");
    m_SearchPaths.push_back(std::filesystem::current_path() / "assets" / "audio");
}

AudioEngine::~AudioEngine() {
    Shutdown();
}

bool AudioEngine::Initialize() {
    m_Initialized = true;
#ifdef _WIN32
    std::cout << "[Audio] Initialized WinMM MCI audio backend (WAV/MP3)" << std::endl;
#else
    std::cout << "[Audio] Audio backend unavailable on this platform (stub mode)" << std::endl;
#endif
    return true;
}

void AudioEngine::Shutdown() {
    if (!m_Initialized) {
        return;
    }

    StopAll();
    m_Initialized = false;
}

void AudioEngine::SetProjectRoot(const std::filesystem::path& rootPath) {
    m_ProjectRoot = rootPath.lexically_normal();

    if (!m_ProjectRoot.empty()) {
        auto addIfMissing = [this](const std::filesystem::path& path) {
            std::filesystem::path normalized = path.lexically_normal();
            if (std::find(m_SearchPaths.begin(), m_SearchPaths.end(), normalized) == m_SearchPaths.end()) {
                m_SearchPaths.push_back(normalized);
            }
        };

        addIfMissing(m_ProjectRoot);
        addIfMissing(m_ProjectRoot / "assets");
        addIfMissing(m_ProjectRoot / "assets" / "audio");
    }
}

AudioEngine::SoundHandle AudioEngine::Play(const std::string& path, bool loop, float volume) {
    if (!m_Initialized) {
        return InvalidHandle;
    }

    std::filesystem::path resolved = ResolveAudioPath(path);
    if (resolved.empty() || !std::filesystem::exists(resolved) || !IsSupportedAudioExtension(resolved)) {
        std::cerr << "[Audio] Cannot play sound. Invalid or unsupported path: " << path << std::endl;
        return InvalidHandle;
    }

    SoundInstance instance;
    instance.handle = m_NextHandle++;
    instance.alias = BuildAlias(instance.handle);
    instance.sourcePath = resolved;
    instance.looping = loop;
    instance.volume = std::clamp(volume, 0.0f, 1.0f);

#ifdef _WIN32
    if (!OpenSound(instance)) {
        return InvalidHandle;
    }

    std::ostringstream playCmd;
    playCmd << "play " << instance.alias;
    if (instance.looping) {
        playCmd << " repeat";
    }

    if (!SendMciCommand(playCmd.str())) {
        SendMciCommand("close " + instance.alias);
        return InvalidHandle;
    }

    SetVolume(instance.handle, instance.volume);
#endif

    m_Instances.emplace(instance.handle, std::move(instance));
    return m_NextHandle - 1;
}

bool AudioEngine::Stop(SoundHandle handle) {
    auto it = m_Instances.find(handle);
    if (it == m_Instances.end()) {
        return false;
    }

#ifdef _WIN32
    SendMciCommand("stop " + it->second.alias);
    SendMciCommand("close " + it->second.alias);
#endif

    m_Instances.erase(it);
    return true;
}

bool AudioEngine::Pause(SoundHandle handle) {
    auto it = m_Instances.find(handle);
    if (it == m_Instances.end()) {
        return false;
    }

#ifdef _WIN32
    if (!SendMciCommand("pause " + it->second.alias)) {
        return false;
    }
#endif

    it->second.paused = true;
    return true;
}

bool AudioEngine::Resume(SoundHandle handle) {
    auto it = m_Instances.find(handle);
    if (it == m_Instances.end()) {
        return false;
    }

#ifdef _WIN32
    if (!SendMciCommand("resume " + it->second.alias)) {
        return false;
    }
#endif

    it->second.paused = false;
    return true;
}

bool AudioEngine::SetVolume(SoundHandle handle, float volume) {
    auto it = m_Instances.find(handle);
    if (it == m_Instances.end()) {
        return false;
    }

    it->second.volume = std::clamp(volume, 0.0f, 1.0f);

#ifdef _WIN32
    std::ostringstream cmd;
    cmd << "setaudio " << it->second.alias << " volume to " << ClampMciVolume(it->second.volume);
    if (!SendMciCommand(cmd.str())) {
        return false;
    }
#endif

    return true;
}

bool AudioEngine::IsPlaying(SoundHandle handle) const {
    auto it = m_Instances.find(handle);
    if (it == m_Instances.end()) {
        return false;
    }

#ifdef _WIN32
    if (it->second.paused) {
        return false;
    }

    std::string command = "status " + it->second.alias + " mode";
    char mode[64] = {};
    MCIERROR error = mciSendStringA(command.c_str(), mode, static_cast<UINT>(sizeof(mode)), nullptr);
    if (error != 0) {
        return false;
    }

    return std::string(mode) == "playing";
#else
    return !it->second.paused;
#endif
}

void AudioEngine::StopAll() {
    std::vector<SoundHandle> handles;
    handles.reserve(m_Instances.size());
    for (const auto& [handle, _] : m_Instances) {
        (void)_;
        handles.push_back(handle);
    }

    for (SoundHandle handle : handles) {
        Stop(handle);
    }
}

bool AudioEngine::IsBackendAvailable() const {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

std::filesystem::path AudioEngine::ResolveAudioPath(const std::string& inputPath) const {
    if (inputPath.empty()) {
        return {};
    }

    std::filesystem::path candidate(inputPath);
    if (candidate.is_absolute()) {
        return candidate.lexically_normal();
    }

    for (const auto& base : m_SearchPaths) {
        const std::filesystem::path resolved = (base / candidate).lexically_normal();
        if (std::filesystem::exists(resolved)) {
            return resolved;
        }
    }

    return (std::filesystem::current_path() / candidate).lexically_normal();
}

std::string AudioEngine::BuildAlias(SoundHandle handle) const {
    return "heph_sound_" + std::to_string(handle);
}

int AudioEngine::ClampMciVolume(float volume) {
    const float clamped = std::clamp(volume, 0.0f, 1.0f);
    return static_cast<int>(clamped * 1000.0f);
}

#ifdef _WIN32
bool AudioEngine::OpenSound(SoundInstance& instance) {
    std::ostringstream openCmd;
    openCmd << "open \"" << instance.sourcePath.string() << "\" alias " << instance.alias;

    if (!SendMciCommand(openCmd.str())) {
        std::cerr << "[Audio] Failed to open sound: " << instance.sourcePath << std::endl;
        return false;
    }

    return true;
}

bool AudioEngine::SendMciCommand(const std::string& command) const {
    MCIERROR error = mciSendStringA(command.c_str(), nullptr, 0, nullptr);
    if (error == 0) {
        return true;
    }

    char errorText[256] = {};
    mciGetErrorStringA(error, errorText, static_cast<UINT>(sizeof(errorText)));
    std::cerr << "[Audio] MCI command failed: '" << command << "' -> " << errorText << std::endl;
    return false;
}
#endif
