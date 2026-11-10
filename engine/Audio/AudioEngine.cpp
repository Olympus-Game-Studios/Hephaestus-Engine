#include "AudioEngine.h"
#include "AudioMath.h"
#include "../Assets/AssetPathPolicy.h"

#include "miniaudio/miniaudio.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <random>
#include <unordered_map>

namespace {

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsSupportedAudioExtension(const std::filesystem::path& path)
{
    const std::string ext = ToLower(path.extension().string());
    return ext == ".wav" || ext == ".mp3" || ext == ".flac";
}

ma_result InitSoundFromFile(ma_engine* engine, const std::filesystem::path& path, ma_uint32 flags, ma_sound* sound)
{
#ifdef _WIN32
    return ma_sound_init_from_file_w(engine, path.wstring().c_str(), flags, nullptr, nullptr, sound);
#else
    return ma_sound_init_from_file(engine, path.string().c_str(), flags, nullptr, nullptr, sound);
#endif
}

} // namespace

struct AudioEngine::Impl {
    struct Voice {
        ma_sound sound{};
        ma_lpf_node filter{}; ///< between sound and bus once the voice is spatial
        bool filtered = false;
        float cutoffHz = 0.0f;
        PlayParams params;
        float baseVolume = 1.0f;
        float gain = 1.0f;
        bool paused = false;
        uint64_t startOrder = 0;
    };

    ma_context context{};
    bool contextInitialized = false;
    ma_engine engine{};
    bool engineInitialized = false;
    std::unordered_map<std::string, std::unique_ptr<ma_sound_group>> buses;
    std::unordered_map<std::string, float> busVolumes;
    std::unordered_map<std::string, std::unique_ptr<ma_sound>> cache;
    std::unordered_map<SoundHandle, std::unique_ptr<Voice>> voices;
    SoundHandle nextHandle = 1;
    uint64_t nextStartOrder = 1;
    uint32_t maxVoices = 48;
    Vec3 listenerPosition{0.0f, 0.0f, 0.0f};
    Vec3 listenerForward{0.0f, 0.0f, -1.0f};
    Vec3 listenerUp{0.0f, 1.0f, 0.0f};
    std::minstd_rand random{0x48455048u};

    float Unit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(random); }

    float BusVolume(const std::string& name) const
    {
        const auto it = busVolumes.find(name);
        return it == busVolumes.end() ? 1.0f : it->second;
    }

    ma_sound_group* Bus(const std::string& name)
    {
        const auto it = buses.find(name);
        if (it != buses.end()) return it->second.get();
        return buses.at("sfx").get();
    }

    void ApplyVoiceVolume(Voice& voice)
    {
        float pan = 0.0f;
        voice.gain = 1.0f;
        if (voice.params.spatial) {
            const auto spatial = Hephaestus::AudioMath::Spatialize(listenerPosition, listenerForward, listenerUp,
                                                                    voice.params.position, voice.params.minDistance,
                                                                    voice.params.maxDistance);
            voice.gain = spatial.gain;
            pan = spatial.pan;
            ApplyCutoff(voice, spatial.cutoffHz);
        }
        ma_sound_set_volume(&voice.sound, voice.baseVolume * voice.gain);
        ma_sound_set_pan(&voice.sound, pan);
    }

    /// Puts a low-pass filter between a spatial voice and its bus the first time, then moves its cutoff.
    void ApplyCutoff(Voice& voice, float cutoffHz)
    {
        const ma_uint32 sampleRate = ma_engine_get_sample_rate(&engine);
        const ma_uint32 channels = ma_node_get_output_channels(&voice.sound, 0);
        cutoffHz = std::min(cutoffHz, 0.45f * static_cast<float>(sampleRate));
        if (!voice.filtered) {
            const ma_lpf_node_config config = ma_lpf_node_config_init(channels, sampleRate, cutoffHz, 2);
            if (ma_lpf_node_init(ma_engine_get_node_graph(&engine), &config, nullptr, &voice.filter) != MA_SUCCESS) {
                return;
            }
            ma_node_attach_output_bus(&voice.filter, 0, Bus(voice.params.bus), 0);
            ma_node_attach_output_bus(&voice.sound, 0, &voice.filter, 0);
            voice.filtered = true;
            voice.cutoffHz = cutoffHz;
            return;
        }
        if (std::abs(cutoffHz - voice.cutoffHz) < 0.05f * voice.cutoffHz) return;
        const ma_lpf_config config = ma_lpf_config_init(ma_format_f32, channels, sampleRate, cutoffHz, 2);
        if (ma_lpf_node_reinit(&config, &voice.filter) == MA_SUCCESS) voice.cutoffHz = cutoffHz;
    }

    static void Uninit(Voice& voice)
    {
        ma_sound_uninit(&voice.sound);
        if (voice.filtered) ma_lpf_node_uninit(&voice.filter, nullptr);
    }

    void Release(SoundHandle handle)
    {
        const auto it = voices.find(handle);
        if (it == voices.end()) return;
        Uninit(*it->second);
        voices.erase(it);
    }

    void ReleaseFinished()
    {
        std::vector<SoundHandle> finished;
        for (const auto& [handle, voice] : voices) {
            if (!voice->paused && !voice->params.loop && ma_sound_at_end(&voice->sound)) {
                finished.push_back(handle);
            }
        }
        for (SoundHandle handle : finished) Release(handle);
    }

    /// Makes room for a new voice of priority; false when every voice outranks it.
    bool MakeRoom(int priority)
    {
        if (voices.size() < maxVoices) return true;
        ReleaseFinished();
        if (voices.size() < maxVoices) return true;
        std::vector<Hephaestus::AudioMath::VoiceCandidate> candidates;
        std::vector<SoundHandle> handles;
        for (const auto& [handle, voice] : voices) {
            candidates.push_back({voice->params.priority, voice->baseVolume * voice->gain * BusVolume(voice->params.bus),
                                  voice->startOrder});
            handles.push_back(handle);
        }
        const int victim = Hephaestus::AudioMath::ChooseVoiceToSteal(candidates, priority);
        if (victim < 0) return false;
        Release(handles[static_cast<size_t>(victim)]);
        return true;
    }

    ma_sound* Cached(const std::filesystem::path& path)
    {
        const std::string key = path.generic_string();
        const auto it = cache.find(key);
        if (it != cache.end()) return it->second.get();
        auto sound = std::make_unique<ma_sound>();
        const ma_result result = InitSoundFromFile(&engine, path, MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                                   sound.get());
        if (result != MA_SUCCESS) {
            std::cerr << "[Audio] Failed to load sound: " << key << " (" << ma_result_description(result) << ")"
                      << std::endl;
            return nullptr;
        }
        ma_sound* raw = sound.get();
        cache.emplace(key, std::move(sound));
        return raw;
    }
};

AudioEngine::AudioEngine()
{
    m_SearchPaths.push_back(std::filesystem::current_path());
    m_SearchPaths.push_back(std::filesystem::current_path() / "assets");
    m_SearchPaths.push_back(std::filesystem::current_path() / "assets" / "audio");
}

AudioEngine::~AudioEngine()
{
    Shutdown();
}

const std::vector<std::string>& AudioEngine::BusNames()
{
    static const std::vector<std::string> names{"master", "sfx", "weapons", "music", "ui"};
    return names;
}

bool AudioEngine::Initialize()
{
    return Initialize(Options{});
}

bool AudioEngine::Initialize(const Options& options)
{
    if (m_Impl) return m_Impl->engineInitialized;
    m_Impl = std::make_unique<Impl>();
    m_Impl->maxVoices = std::max<uint32_t>(1, options.maxVoices);

    ma_engine_config config = ma_engine_config_init();
    if (options.nullDevice) {
        const ma_backend backends[] = {ma_backend_null};
        if (ma_context_init(backends, 1, nullptr, &m_Impl->context) != MA_SUCCESS) {
            std::cerr << "[Audio] Failed to create the null audio context" << std::endl;
            return false;
        }
        m_Impl->contextInitialized = true;
        config.pContext = &m_Impl->context;
    }
    const ma_result result = ma_engine_init(&config, &m_Impl->engine);
    if (result != MA_SUCCESS) {
        std::cerr << "[Audio] No audio output (" << ma_result_description(result) << "); sounds are disabled"
                  << std::endl;
        return false;
    }
    m_Impl->engineInitialized = true;
    for (const std::string& name : BusNames()) {
        m_Impl->busVolumes[name] = 1.0f;
        if (name == "master") continue;
        auto group = std::make_unique<ma_sound_group>();
        if (ma_sound_group_init(&m_Impl->engine, 0, nullptr, group.get()) == MA_SUCCESS) {
            m_Impl->buses.emplace(name, std::move(group));
        }
    }
    ma_device* device = ma_engine_get_device(&m_Impl->engine);
    std::cout << "[Audio] miniaudio " << MA_VERSION_STRING << " on "
              << (device ? ma_get_backend_name(device->pContext->backend) : "no device") << ", "
              << ma_engine_get_sample_rate(&m_Impl->engine) << " Hz, " << m_Impl->maxVoices << " voices" << std::endl;
    return true;
}

void AudioEngine::Shutdown()
{
    if (!m_Impl) return;
    if (m_Impl->engineInitialized) {
        for (auto& [handle, voice] : m_Impl->voices) {
            (void)handle;
            Impl::Uninit(*voice);
        }
        m_Impl->voices.clear();
        for (auto& [path, sound] : m_Impl->cache) {
            (void)path;
            ma_sound_uninit(sound.get());
        }
        m_Impl->cache.clear();
        for (auto& [name, group] : m_Impl->buses) {
            (void)name;
            ma_sound_group_uninit(group.get());
        }
        m_Impl->buses.clear();
        ma_engine_uninit(&m_Impl->engine);
    }
    if (m_Impl->contextInitialized) {
        ma_context_uninit(&m_Impl->context);
    }
    m_Impl.reset();
}

void AudioEngine::SetProjectRoot(const std::filesystem::path& rootPath)
{
    m_ProjectRoot = rootPath.lexically_normal();
    if (m_ProjectRoot.empty()) return;
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

AudioEngine::SoundHandle AudioEngine::Play(const std::string& path, bool loop, float volume)
{
    PlayParams params;
    params.loop = loop;
    params.volume = volume;
    return Play(path, params);
}

AudioEngine::SoundHandle AudioEngine::Play(const std::string& path, const PlayParams& params)
{
    if (!IsBackendAvailable()) return InvalidHandle;
    Impl& impl = *m_Impl;

    const std::filesystem::path resolved = ResolveAudioPath(path);
    std::error_code ec;
    if (resolved.empty() || !std::filesystem::exists(resolved, ec) || !IsSupportedAudioExtension(resolved)) {
        if (Hephaestus::AssetPathPolicy::IsPluginContentKey(path) &&
            Hephaestus::AssetPathPolicy::ResolvePortableKey(m_ProjectRoot, path).empty()) {
            std::cerr << "[Audio] " << Hephaestus::AssetPathPolicy::UnresolvedPluginContentMessage(path) << " ("
                      << path << ")" << std::endl;
        } else {
            std::cerr << "[Audio] Cannot play sound. Invalid or unsupported path: " << path << std::endl;
        }
        return InvalidHandle;
    }
    ma_sound* source = impl.Cached(resolved);
    if (!source || !impl.MakeRoom(params.priority)) return InvalidHandle;

    auto voice = std::make_unique<Impl::Voice>();
    voice->params = params;
    if (impl.buses.find(voice->params.bus) == impl.buses.end()) voice->params.bus = "sfx";
    if (ma_sound_init_copy(&impl.engine, source, MA_SOUND_FLAG_NO_SPATIALIZATION, impl.Bus(voice->params.bus),
                           &voice->sound) != MA_SUCCESS) {
        std::cerr << "[Audio] Failed to start a voice for " << path << std::endl;
        return InvalidHandle;
    }
    voice->baseVolume = std::max(0.0f, Hephaestus::AudioMath::ApplyJitter(std::clamp(params.volume, 0.0f, 4.0f),
                                                                           params.volumeJitter, impl.Unit()));
    const float pitch = Hephaestus::AudioMath::ApplyJitter(params.pitch, params.pitchJitter, impl.Unit());
    ma_sound_set_pitch(&voice->sound, std::max(0.01f, pitch));
    ma_sound_set_looping(&voice->sound, params.loop ? MA_TRUE : MA_FALSE);
    impl.ApplyVoiceVolume(*voice);
    voice->startOrder = impl.nextStartOrder++;
    if (ma_sound_start(&voice->sound) != MA_SUCCESS) {
        Impl::Uninit(*voice);
        return InvalidHandle;
    }
    const SoundHandle handle = impl.nextHandle++;
    impl.voices.emplace(handle, std::move(voice));
    return handle;
}

bool AudioEngine::Stop(SoundHandle handle)
{
    if (!m_Impl || m_Impl->voices.find(handle) == m_Impl->voices.end()) return false;
    m_Impl->Release(handle);
    return true;
}

bool AudioEngine::Pause(SoundHandle handle)
{
    if (!m_Impl) return false;
    const auto it = m_Impl->voices.find(handle);
    if (it == m_Impl->voices.end()) return false;
    ma_sound_stop(&it->second->sound);
    it->second->paused = true;
    return true;
}

bool AudioEngine::Resume(SoundHandle handle)
{
    if (!m_Impl) return false;
    const auto it = m_Impl->voices.find(handle);
    if (it == m_Impl->voices.end()) return false;
    it->second->paused = false;
    return ma_sound_start(&it->second->sound) == MA_SUCCESS;
}

bool AudioEngine::SetVolume(SoundHandle handle, float volume)
{
    if (!m_Impl) return false;
    const auto it = m_Impl->voices.find(handle);
    if (it == m_Impl->voices.end()) return false;
    it->second->baseVolume = std::clamp(volume, 0.0f, 4.0f);
    m_Impl->ApplyVoiceVolume(*it->second);
    return true;
}

bool AudioEngine::SetPitch(SoundHandle handle, float pitch)
{
    if (!m_Impl) return false;
    const auto it = m_Impl->voices.find(handle);
    if (it == m_Impl->voices.end()) return false;
    ma_sound_set_pitch(&it->second->sound, std::max(0.01f, pitch));
    return true;
}

bool AudioEngine::SetPosition(SoundHandle handle, const Vec3& position)
{
    if (!m_Impl) return false;
    const auto it = m_Impl->voices.find(handle);
    if (it == m_Impl->voices.end()) return false;
    it->second->params.spatial = true;
    it->second->params.position = position;
    m_Impl->ApplyVoiceVolume(*it->second);
    return true;
}

bool AudioEngine::IsPlaying(SoundHandle handle) const
{
    if (!m_Impl) return false;
    const auto it = m_Impl->voices.find(handle);
    if (it == m_Impl->voices.end() || it->second->paused) return false;
    return ma_sound_is_playing(&it->second->sound) == MA_TRUE;
}

void AudioEngine::StopAll()
{
    if (!m_Impl) return;
    for (auto& [handle, voice] : m_Impl->voices) {
        (void)handle;
        Impl::Uninit(*voice);
    }
    m_Impl->voices.clear();
}

void AudioEngine::SetListener(const Vec3& position, const Vec3& forward, const Vec3& up)
{
    if (!m_Impl) return;
    m_Impl->listenerPosition = position;
    if (vlen(forward) > 0.0f) m_Impl->listenerForward = vnorm(forward);
    if (vlen(up) > 0.0f) m_Impl->listenerUp = vnorm(up);
}

bool AudioEngine::SetBusVolume(const std::string& bus, float volume)
{
    if (!IsBackendAvailable()) return false;
    volume = std::clamp(volume, 0.0f, 4.0f);
    if (bus == "master") {
        ma_engine_set_volume(&m_Impl->engine, volume);
    } else {
        const auto it = m_Impl->buses.find(bus);
        if (it == m_Impl->buses.end()) return false;
        ma_sound_group_set_volume(it->second.get(), volume);
    }
    m_Impl->busVolumes[bus] = volume;
    return true;
}

float AudioEngine::GetBusVolume(const std::string& bus) const
{
    return m_Impl ? m_Impl->BusVolume(bus) : 1.0f;
}

void AudioEngine::Update()
{
    if (!IsBackendAvailable()) return;
    m_Impl->ReleaseFinished();
    for (auto& [handle, voice] : m_Impl->voices) {
        (void)handle;
        if (voice->params.spatial) m_Impl->ApplyVoiceVolume(*voice);
    }
}

uint32_t AudioEngine::GetActiveVoiceCount() const
{
    return m_Impl ? static_cast<uint32_t>(m_Impl->voices.size()) : 0;
}

uint32_t AudioEngine::GetMaxVoices() const
{
    return m_Impl ? m_Impl->maxVoices : 0;
}

void AudioEngine::SetMaxVoices(uint32_t maxVoices)
{
    if (m_Impl) m_Impl->maxVoices = std::max<uint32_t>(1, maxVoices);
}

uint32_t AudioEngine::GetCachedSoundCount() const
{
    return m_Impl ? static_cast<uint32_t>(m_Impl->cache.size()) : 0;
}

bool AudioEngine::IsBackendAvailable() const
{
    return m_Impl && m_Impl->engineInitialized;
}

std::filesystem::path AudioEngine::ResolveAudioPath(const std::string& inputPath) const
{
    if (inputPath.empty()) return {};
    if (Hephaestus::AssetPathPolicy::IsPluginContentKey(inputPath)) {
        return Hephaestus::AssetPathPolicy::ResolvePortableKey(m_ProjectRoot, inputPath);
    }
    std::filesystem::path candidate(inputPath);
    if (candidate.is_absolute()) return candidate.lexically_normal();
    for (const auto& base : m_SearchPaths) {
        const std::filesystem::path resolved = (base / candidate).lexically_normal();
        std::error_code ec;
        if (std::filesystem::exists(resolved, ec)) return resolved;
    }
    return (std::filesystem::current_path() / candidate).lexically_normal();
}
