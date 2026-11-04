#if HEPHAESTUS_EDITOR_QT_UI

#include "EditorSystemUsage.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#pragma comment(lib, "pdh.lib")
#else
#include <sys/resource.h>
#endif

namespace Hephaestus {
namespace QtEditor {

namespace {

/** Process user + kernel CPU time in microseconds. */
std::uint64_t ProcessCpuMicros()
{
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    auto toMicros = [](const FILETIME& t) {
        return ((static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 10;
    };
    return toMicros(kernel) + toMicros(user);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    auto toMicros = [](const timeval& t) {
        return static_cast<std::uint64_t>(t.tv_sec) * 1000000u + static_cast<std::uint64_t>(t.tv_usec);
    };
    return toMicros(usage.ru_utime) + toMicros(usage.ru_stime);
#endif
}

std::uint64_t WallMicros()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

#ifdef _WIN32
/** "GPU Engine" performance counters for this process's 3D engines (the figure Task Manager shows). */
struct EditorSystemUsage::GpuCounters
{
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    bool primed = false;

    GpuCounters()
    {
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
            query = nullptr;
            return;
        }
        const std::wstring path = L"\\GPU Engine(pid_" + std::to_wstring(GetCurrentProcessId()) + L"_*engtype_3D)\\Utilization Percentage";
        if (PdhAddEnglishCounterW(query, path.c_str(), 0, &counter) != ERROR_SUCCESS) {
            PdhCloseQuery(query);
            query = nullptr;
        }
    }
    ~GpuCounters()
    {
        if (query) PdhCloseQuery(query);
    }

    double Sample()
    {
        if (!query || PdhCollectQueryData(query) != ERROR_SUCCESS) return -1.0;
        if (!primed) {
            primed = true;  // utilisation is a rate, so the first collection only sets the baseline
            return -1.0;
        }
        DWORD bytes = 0;
        DWORD count = 0;
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != PDH_MORE_DATA) return -1.0;
        std::vector<unsigned char> buffer(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS) return -1.0;
        double total = 0.0;
        bool valid = false;
        for (DWORD i = 0; i < count; ++i) {
            if (items[i].FmtValue.CStatus == ERROR_SUCCESS) {
                total += items[i].FmtValue.doubleValue;
                valid = true;
            }
        }
        return valid ? std::min(total, 100.0) : -1.0;
    }
};
#else
struct EditorSystemUsage::GpuCounters
{
    double Sample() { return -1.0; }
};
#endif

EditorSystemUsage::EditorSystemUsage()
    : m_lastCpuTime(ProcessCpuMicros())
    , m_lastWallTime(WallMicros())
    , m_gpu(std::make_unique<GpuCounters>())
{
}

EditorSystemUsage::~EditorSystemUsage() = default;

bool EditorSystemUsage::GpuSupported() const
{
#ifdef _WIN32
    return m_gpu->query != nullptr;
#else
    return false;
#endif
}

void EditorSystemUsage::Sample()
{
    const std::uint64_t cpu = ProcessCpuMicros();
    const std::uint64_t wall = WallMicros();
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    if (cpu && wall > m_lastWallTime) {
        const double busy = static_cast<double>(cpu - m_lastCpuTime);
        m_cpuPercent = std::clamp(100.0 * busy / (static_cast<double>(wall - m_lastWallTime) * cores), 0.0, 100.0);
    }
    m_lastCpuTime = cpu;
    m_lastWallTime = wall;
    m_gpuPercent = m_gpu->Sample();
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
