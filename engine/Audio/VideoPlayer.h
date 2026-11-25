#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Hephaestus {

/**
 * Plays a video file (MP4 and whatever else the platform decodes) with its sound, handing each new frame to a
 * UIDynamicImages image so an image element can show it.
 *
 * Windows uses Media Foundation's media engine, which plays the sound itself; other platforms have no backend yet,
 * so Open fails there.
 */
class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    /** Starts playing `path` into the dynamic image `imageId`. False (with `error`) when it cannot. */
    bool Open(const std::string& path, const std::string& imageId, float volume, std::string& error);
    /** Publishes the latest frame when there is a new one. Returns false once the video has ended or failed. */
    bool Update();
    /** Stops playback and removes its image. */
    void Stop();
    bool IsPlaying() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
    std::string m_ImageId;
    std::vector<uint8_t> m_Pixels;
    uint64_t m_Frames = 0; // frames shown, for the log
};

} // namespace Hephaestus
