#pragma once

/**
 * @file EditorSystemUsage.h
 * @brief Samples the editor process's CPU and GPU utilisation for the status bar.
 */

#if HEPHAESTUS_EDITOR_QT_UI

#include <cstdint>
#include <memory>

namespace Hephaestus {
namespace QtEditor {

class EditorSystemUsage
{
public:
    EditorSystemUsage();
    ~EditorSystemUsage();

    /** Takes a new sample; call about once a second (GPU counters need that interval). */
    void Sample();

    /** Process CPU time over the last interval as a share of all cores (0-100), or negative when unknown. */
    double CpuPercent() const { return m_cpuPercent; }
    /** Process utilisation of the GPU 3D engines (0-100), or negative when the platform cannot report it. */
    double GpuPercent() const { return m_gpuPercent; }
    /** Whether this platform can report GPU utilisation at all. */
    bool GpuSupported() const;

private:
    struct GpuCounters;

    double m_cpuPercent = -1.0;
    double m_gpuPercent = -1.0;
    std::uint64_t m_lastCpuTime = 0;
    std::uint64_t m_lastWallTime = 0;
    std::unique_ptr<GpuCounters> m_gpu;
};

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
