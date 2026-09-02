# Audio audit

**Overall: 2 / 5 (Prototype-to-functional 2D on Windows)**  
Runtime 2 · Editor 3 · Lua 2 · Tests 0

This is **not** production audio middleware. It is enough for menu stings and looping BGM on Windows.

---

## Backend / formats

**Evidence:** `engine/Audio/AudioEngine.h`

- Windows: **WinMM MCI**
- Other platforms: **no-op stubs** (`IsBackendAvailable` false)
- Playback: **WAV / MP3** (MCI)
- Importer validation: WAV header + OggS probe in `AssetImporters.cpp`
- Catalog may list `.ogg`/`.flac`; **playback does not implement them**

**Maturity:** 2 Windows 2D · 0 elsewhere

---

## Playback features

| Feature | Status |
|---------|--------|
| Play / stop / pause / resume | Yes |
| Loop | Yes |
| Volume 0–1 | Yes (MCI mapped) |
| Multiple sounds | Multiple MCI aliases |
| Play on start | `AudioSourceComponent` |
| Streaming / compression pipeline | No (MCI opens files) |
| Effects / reverb / occlusion | No |
| Buses / mixer | No |
| Music vs SFX routing | Convention only |

---

## Spatial / 3D

- **Exists:** No listener, attenuation, or world position in `AudioEngine`
- `AudioSourceComponent` is clip path + loop/volume/playOnStart + runtime handle
- **Maturity:** 0 spatial

---

## Component / editor / packaging

- Inspector: `AudioSourceInspector` (Phase 4)
- Prefab/scene IO: yes
- Placement: Phase 3 audio placement
- Packages: audio files copied; portability fixture includes `.wav`
- Preview: play via runtime engine, not a dedicated waveform editor

---

## Lua

`Audio.play/stop/pause/resume/setVolume/isPlaying/stopAll` (+ `isAvailable`)

No 3D API.

---

## Verdict

**Prototype-level as an engine audio system. Functional for 2D Windows demos.**

Replace WinMM before Linux packages or 3D horror attenuation. miniaudio/OpenAL would be a new backend behind the same `AudioEngine` facade (facade is already small — **good**).

---

## Recommended next step

Keep the facade. Swap backend. Then listener + distance attenuation. Do not integrate Wwise/FMOD until the engine has a real mixer need.
