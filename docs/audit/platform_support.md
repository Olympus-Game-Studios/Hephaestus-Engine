# Platform support audit

**Overall: 2 / 5 (Windows x64 is the product)**  
Do not mark a platform supported because CMake or Vulkan has an `#ifdef`.

---

## Windows x64

- **Maturity:** 4
- **Evidence:** Editor Qt + Win32 Vulkan surface; `winmm` audio; Lua DLL copy; packaging enum Windows x64; dbghelp minidumps; Stage 6 tests
- **Limitations:** Dual renderer still; audio is MCI

---

## Linux

- **Maturity:** 1 foundation
- **Evidence:** GLFW can create Vulkan surfaces for **game** theoretically. `VulkanDevice::AttachToSurface` and `QtViewportWindow::GetNativeSurface` **do not** implement X11/Wayland. Audio stubs. No package target.
- **Verdict:** Not a shipping editor. Untested game.

---

## macOS

- **Maturity:** 1
- **Evidence:** `MACOSX_BUNDLE_*` on `HephaestusEngine`; MoltenVK comments; `VK_KHR_portability_subset` mention. No Qt Metal/MoltenVK surface path in viewport.
- **Verdict:** Metadata only.

---

## Consoles

- **Maturity:** 0
- No GDK/DevKit, no restricted APIs, no packaging.

---

## CI / installers

- No first-party CI
- No install/update architecture
- Distribution = folder/ZIP of generic player + content

---

## Recommended

Document **Windows-first** everywhere (README currently implies a generic desktop engine). Next port: **Linux GLFW player**, not Linux Qt editor.
