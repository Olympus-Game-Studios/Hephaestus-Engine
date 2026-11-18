#include "Audio/AudioEngine.h"
#include "Audio/AudioMath.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace Hephaestus::AudioMath;

namespace {

bool Check(bool condition, const std::string& message)
{
    if (!condition) std::cerr << "[FAIL] " << message << '\n';
    return condition;
}

bool Near(float a, float b, float eps = 1e-4f)
{
    return std::abs(a - b) <= eps;
}

void WriteU32(std::ofstream& out, uint32_t value) { out.write(reinterpret_cast<const char*>(&value), 4); }
void WriteU16(std::ofstream& out, uint16_t value) { out.write(reinterpret_cast<const char*>(&value), 2); }

/// A 16-bit stereo 44.1 kHz sine of the given length.
void WriteTone(const fs::path& path, float seconds)
{
    const uint32_t rate = 44100;
    const uint32_t frames = static_cast<uint32_t>(seconds * rate);
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4);
    WriteU32(out, 36 + frames * 4);
    out.write("WAVEfmt ", 8);
    WriteU32(out, 16);
    WriteU16(out, 1);
    WriteU16(out, 2);
    WriteU32(out, rate);
    WriteU32(out, rate * 4);
    WriteU16(out, 4);
    WriteU16(out, 16);
    out.write("data", 4);
    WriteU32(out, frames * 4);
    for (uint32_t i = 0; i < frames; ++i) {
        const int16_t sample = static_cast<int16_t>(8000.0 * std::sin(2.0 * 3.14159265 * 440.0 * i / rate));
        WriteU16(out, static_cast<uint16_t>(sample));
        WriteU16(out, static_cast<uint16_t>(sample));
    }
}

bool TestMath()
{
    bool ok = true;
    ok &= Check(Near(DistanceGain(0.5f, 1.0f, 40.0f), 1.0f), "full gain inside the minimum distance");
    ok &= Check(Near(DistanceGain(2.0f, 1.0f, 40.0f), 0.5f), "inverse distance beyond the minimum");
    ok &= Check(DistanceGain(40.0f, 1.0f, 40.0f) == 0.0f && DistanceGain(90.0f, 1.0f, 40.0f) == 0.0f,
                "silent at and beyond the maximum distance");
    float previous = 2.0f;
    bool monotonic = true;
    for (float d = 0.0f; d <= 45.0f; d += 0.25f) {
        const float gain = DistanceGain(d, 1.0f, 40.0f);
        monotonic = monotonic && gain <= previous + 1e-6f && gain >= 0.0f;
        previous = gain;
    }
    ok &= Check(monotonic, "gain never rises with distance");
    ok &= Check(DistanceGain(38.0f, 1.0f, 40.0f) < 1.0f / 38.0f, "gain fades to zero before the maximum");

    const Vec3 listener(0.0f, 0.0f, 0.0f), forward(1.0f, 0.0f, 0.0f), up(0.0f, 1.0f, 0.0f);
    // The engine's right axis is forward x up, which is +Z when looking along +X.
    const Spatial right = Spatialize(listener, forward, up, Vec3(0.0f, 0.0f, 5.0f), 1.0f, 40.0f);
    const Spatial left = Spatialize(listener, forward, up, Vec3(0.0f, 0.0f, -5.0f), 1.0f, 40.0f);
    const Spatial ahead = Spatialize(listener, forward, up, Vec3(5.0f, 0.0f, 0.0f), 1.0f, 40.0f);
    ok &= Check(Near(right.pan, 1.0f) && Near(left.pan, -1.0f) && Near(ahead.pan, 0.0f),
                "sources pan to the side they are on");
    ok &= Check(Near(right.gain, 0.2f) && Near(ahead.gain, 0.2f), "spatial gain follows distance");
    const Spatial close = Spatialize(listener, forward, up, Vec3(0.0f, 0.0f, 0.25f), 1.0f, 40.0f);
    ok &= Check(close.pan > 0.0f && close.pan < 0.5f, "a source at the head is not hard-panned");

    ok &= Check(DistanceCutoff(2.0f, 5.0f) == kMaxCutoffHz && DistanceCutoff(5.0f, 5.0f) == kMaxCutoffHz,
                "no distance filtering inside the minimum distance");
    ok &= Check(DistanceCutoff(30.0f, 5.0f) < 7000.0f && DistanceCutoff(60.0f, 5.0f) < DistanceCutoff(30.0f, 5.0f),
                "far sources lose their high frequencies, more the further they are");
    ok &= Check(DistanceCutoff(1.0e6f, 5.0f) == kMinCutoffHz, "the cutoff never falls below its floor");
    ok &= Check(Spatialize(listener, forward, up, Vec3(30.0f, 0.0f, 0.0f), 5.0f, 150.0f).cutoffHz ==
                    DistanceCutoff(30.0f, 5.0f),
                "spatialized sources carry their distance cutoff");

    std::vector<VoiceCandidate> voices{{0, 0.8f, 1}, {0, 0.2f, 2}, {5, 0.1f, 3}};
    ok &= Check(ChooseVoiceToSteal(voices, 0) == 1, "the quietest voice of the lowest priority is taken");
    voices[1].audibility = 0.8f;
    ok &= Check(ChooseVoiceToSteal(voices, 0) == 0, "with equal loudness the oldest voice is taken");
    ok &= Check(ChooseVoiceToSteal({{5, 0.1f, 1}, {6, 0.1f, 2}}, 1) == -1,
                "a sound never takes a voice of higher priority");
    ok &= Check(ChooseVoiceToSteal({{2, 0.9f, 1}, {1, 1.0f, 2}}, 3) == 1, "lowest priority goes first");

    ok &= Check(Near(ApplyJitter(1.0f, 0.1f, 0.0f), 0.9f) && Near(ApplyJitter(1.0f, 0.1f, 0.5f), 1.0f) &&
                    ApplyJitter(1.0f, 0.1f, 0.9999f) < 1.1f,
                "jitter stays inside its range");
    return ok;
}

bool TestEngine(const fs::path& folder)
{
    bool ok = true;
    const fs::path tone = folder / "tone.wav";
    WriteTone(tone, 0.25f);

    AudioEngine audio;
    AudioEngine::Options options;
    options.nullDevice = true;
    options.maxVoices = 3;
    if (!Check(audio.Initialize(options) && audio.IsBackendAvailable(), "engine starts on the null device")) {
        return false;
    }
    audio.SetProjectRoot(folder);

    AudioEngine::PlayParams low;
    low.loop = true;
    const auto a = audio.Play("tone.wav", low);
    const auto b = audio.Play("tone.wav", low);
    const auto c = audio.Play("tone.wav", low);
    ok &= Check(a && b && c && audio.GetActiveVoiceCount() == 3, "three looping voices play");
    ok &= Check(audio.GetCachedSoundCount() == 1, "repeat plays share one decoded sound");

    AudioEngine::PlayParams high = low;
    high.priority = 5;
    const auto d = audio.Play("tone.wav", high);
    ok &= Check(d != AudioEngine::InvalidHandle && audio.GetActiveVoiceCount() == 3 && !audio.IsPlaying(a) &&
                    audio.IsPlaying(d),
                "a higher-priority sound takes the oldest voice when the budget is full");
    const auto e = audio.Play("tone.wav", low);
    ok &= Check(e != AudioEngine::InvalidHandle && audio.IsPlaying(d) && !audio.IsPlaying(b),
                "an equal-priority sound takes the oldest low-priority voice and keeps the high one");
    audio.SetMaxVoices(1);
    audio.StopAll();
    const auto keep = audio.Play("tone.wav", high);
    ok &= Check(audio.Play("tone.wav", low) == AudioEngine::InvalidHandle && audio.IsPlaying(keep),
                "a lower-priority sound is dropped when every voice outranks it");
    audio.StopAll();
    audio.SetMaxVoices(8);

    ok &= Check(audio.SetBusVolume("weapons", 0.5f) && Near(audio.GetBusVolume("weapons"), 0.5f) &&
                    audio.SetBusVolume("master", 0.8f) && !audio.SetBusVolume("nope", 1.0f),
                "bus volumes are set by name");

    AudioEngine::PlayParams spatial;
    spatial.spatial = true;
    spatial.position = Vec3(10.0f, 0.0f, 0.0f);
    spatial.bus = "weapons";
    spatial.pitchJitter = 0.1f;
    spatial.volumeJitter = 0.1f;
    audio.SetListener(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const auto shot = audio.Play("tone.wav", spatial);
    ok &= Check(shot != AudioEngine::InvalidHandle && audio.SetPosition(shot, Vec3(0.0f, 0.0f, 3.0f)) &&
                    audio.SetPitch(shot, 1.2f),
                "a 3D voice plays and can be moved");
    ok &= Check(audio.Play("missing.wav") == AudioEngine::InvalidHandle, "a missing file does not play");

    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    audio.Update();
    ok &= Check(audio.GetActiveVoiceCount() == 0, "finished one-shot voices are released");

    AudioEngine::PlayParams region;
    region.loop = true;
    region.loopStart = 0.1f;
    region.loopEnd = 0.2f;
    const auto looped = audio.Play("tone.wav", region);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    audio.Update();
    ok &= Check(audio.IsPlaying(looped), "a sound looping over a region keeps playing past its length");
    ok &= Check(audio.Stop(looped, 0.1f) && audio.GetActiveVoiceCount() == 1,
                "a faded stop keeps the voice while it fades");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    audio.Update();
    ok &= Check(!audio.IsPlaying(looped) && audio.GetActiveVoiceCount() == 0,
                "a faded stop releases the voice once the fade is done");
    audio.Shutdown();
    ok &= Check(!audio.IsBackendAvailable() && audio.Play("tone.wav") == AudioEngine::InvalidHandle,
                "a shut-down engine plays nothing");
    return ok;
}

} // namespace

int main()
{
    const fs::path folder = fs::temp_directory_path() / "hephaestus-audio-tests";
    std::error_code ec;
    fs::remove_all(folder, ec);
    fs::create_directories(folder, ec);
    bool ok = TestMath();
    ok &= TestEngine(folder);
    fs::remove_all(folder, ec);
    if (!ok) return 1;
    std::cout << "AudioEngineTests passed\n";
    return 0;
}
