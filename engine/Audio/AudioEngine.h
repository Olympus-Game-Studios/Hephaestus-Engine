#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Minimal runtime audio engine.
 *
 * Current backend:
 * - Windows: WinMM MCI (supports WAV/MP3 playback)
 * - Other platforms: no-op stubs (keeps build portable until a cross-platform backend is added)
 */
class AudioEngine {
public:
    using SoundHandle = uint32_t;
    static constexpr SoundHandle InvalidHandle = 0;

    AudioEngine();
    ~AudioEngine();

    bool Initialize();
    void Shutdown();

    void SetProjectRoot(const std::filesystem::path& rootPath);

    SoundHandle Play(const std::string& path, bool loop = false, float volume = 1.0f);
    bool Stop(SoundHandle handle);
    bool Pause(SoundHandle handle);
    bool Resume(SoundHandle handle);
    bool SetVolume(SoundHandle handle, float volume);
    bool IsPlaying(SoundHandle handle) const;
    void StopAll();

    bool IsBackendAvailable() const;

private:
    struct SoundInstance {
        SoundHandle handle = InvalidHandle;
        std::string alias;
        std::filesystem::path sourcePath;
        bool looping = false;
        bool paused = false;
        float volume = 1.0f;
    };

    /// One-shot sounds keep an open MCI device until closed; release the ones that ended.
    void CloseFinishedSounds();
    std::filesystem::path ResolveAudioPath(const std::string& inputPath) const;
    std::string BuildAlias(SoundHandle handle) const;
    static int ClampMciVolume(float volume);

#ifdef _WIN32
    bool OpenSound(SoundInstance& instance);
    bool SendMciCommand(const std::string& command) const;
#endif

    std::filesystem::path m_ProjectRoot;
    std::vector<std::filesystem::path> m_SearchPaths;
    std::unordered_map<SoundHandle, SoundInstance> m_Instances;
    SoundHandle m_NextHandle = 1;
    bool m_Initialized = false;
};
