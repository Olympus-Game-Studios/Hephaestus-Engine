#pragma once

#include "../Core/Math.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

/**
 * Runtime audio engine: a miniaudio mixer with decoded sounds cached per file, a voice budget with priorities,
 * named buses, per-play pitch and volume variation, and stereo-panned 3D sources heard from a listener.
 *
 * Plays WAV, MP3 and FLAC. Uses WASAPI on Windows and Core Audio on macOS through miniaudio.
 */
class AudioEngine {
public:
    using SoundHandle = uint32_t;
    static constexpr SoundHandle InvalidHandle = 0;

    struct Options {
        bool nullDevice = false; ///< mix without an output device (tests)
        uint32_t maxVoices = 48;
    };

    struct PlayParams {
        float volume = 1.0f;
        float pitch = 1.0f;
        float volumeJitter = 0.0f; ///< volume varies by up to this fraction each play
        float pitchJitter = 0.0f;  ///< pitch varies by up to this fraction each play
        bool loop = false;
        /// With loop: the stretch that repeats, in seconds (the sound plays from its start, then loops from
        /// loopStart once it reaches loopEnd). loopEnd <= 0 is the end of the sound.
        float loopStart = 0.0f;
        float loopEnd = 0.0f;
        std::string bus = "sfx";
        int priority = 0; ///< a full voice budget never drops a sound for one of lower priority
        bool spatial = false;
        Vec3 position{0.0f, 0.0f, 0.0f};
        float minDistance = 1.0f;
        float maxDistance = 40.0f;
    };

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool Initialize();
    bool Initialize(const Options& options);
    void Shutdown();

    void SetProjectRoot(const std::filesystem::path& rootPath);

    SoundHandle Play(const std::string& path, bool loop = false, float volume = 1.0f);
    SoundHandle Play(const std::string& path, const PlayParams& params);
    bool Stop(SoundHandle handle);
    /// Fades the sound out over fadeSeconds, then releases it (0 stops at once).
    bool Stop(SoundHandle handle, float fadeSeconds);
    bool Pause(SoundHandle handle);
    bool Resume(SoundHandle handle);
    bool SetVolume(SoundHandle handle, float volume);
    bool SetPitch(SoundHandle handle, float pitch);
    bool SetPosition(SoundHandle handle, const Vec3& position);
    bool IsPlaying(SoundHandle handle) const;
    void StopAll();

    void SetListener(const Vec3& position, const Vec3& forward, const Vec3& up);
    bool SetBusVolume(const std::string& bus, float volume);
    float GetBusVolume(const std::string& bus) const;
    static const std::vector<std::string>& BusNames();

    /// Releases finished voices and re-spatializes 3D voices for the current listener. Call once per frame.
    void Update();

    uint32_t GetActiveVoiceCount() const;
    uint32_t GetMaxVoices() const;
    void SetMaxVoices(uint32_t maxVoices);
    uint32_t GetCachedSoundCount() const;

    bool IsBackendAvailable() const;

private:
    struct Impl;

    std::filesystem::path ResolveAudioPath(const std::string& inputPath) const;

    std::filesystem::path m_ProjectRoot;
    std::vector<std::filesystem::path> m_SearchPaths;
    std::unique_ptr<Impl> m_Impl;
};
