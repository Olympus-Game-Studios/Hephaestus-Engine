# Build and verification

Choose checks based on changed behavior and risk. Do not run the whole suite for every trivial edit, and do not claim more than a check proves.

## Evidence levels

- **Compilation/build:** a selected target and configuration compiles and links.
- **Unit test:** a focused executable/test passes at its defined boundary.
- **Integration test:** cooperating subsystems work together in the exercised harness.
- **Editor Play:** behavior was exercised in the editor's live simulation/preview host.
- **Packaged Development / Release:** behavior was exercised in that package and compile/runtime configuration. These are distinct configurations.
- **Runtime behavior:** the user-observable path was actually exercised; a build or mocked test alone does not establish it.

Name the checks run, configurations/hosts tested, and important paths not verified. Never describe these verification levels as equivalent.

## Configured build workflows

Root `CMakePresets.json` defines `windows-release`, `macos-release`, and `linux-release`. Presets select Release builds, using Visual Studio on Windows and Ninja on macOS/Linux. On the matching host, for example:

```text
cmake --preset windows-release
cmake --build --preset windows-release
ctest --test-dir build/windows-release -C Release --output-on-failure
```

Substitute the matching platform preset. Use `ctest --test-dir <build-dir> -N` to inspect available tests and `-R <test-name>` for focused execution. `HEPHAESTUS_BUILD_COMPAT_TESTS` defaults ON; `HEPHAESTUS_BUILD_QT_TESTS` defaults OFF and additionally needs Qt Test. Check configure output and selected CMake options rather than assuming an optional target is enabled. Target-specific builds may use `cmake --build <build-dir> --config <Debug|Release> --target <target>`.

The Windows package portability harness is `tools/test_package_portability.ps1`; inspect prerequisites/configuration and use it only after building the required package tool and runtime. Its result is not cross-platform evidence.

## Selection and lifecycle

- Prefer the focused existing CTest target covering the change. Add a meaningful deterministic regression test for new behavior or a reproduced bug; exercise relevant failures and do not weaken expectations to make a regression pass.
- Prefer real subsystem integration when feasible; use fakes at narrow boundaries when unavailable host systems would obscure the contract. Keep mocks aligned with the behavior claimed.
- Changes to scene load/reload, runtime start/stop, entity ownership, scripting, plugins, or resource lifetime warrant consideration of repeated initialization/teardown, stale references/handles, cleanup/leaks, and partial-startup failure.
- Shared editor/runtime behavior requires checking host-specific branches and compile definitions. User-visible behavior may require Editor Play and/or packaged Development/Release exercise beyond a build.
- Rendering, audio, OS integration, and package resolution require platform-aware checks. Do not infer another platform's behavior from Windows compilation; note when a backend is a stub or dependencies are unavailable.
- Avoid redundant test frameworks, elaborate fixtures, and broad runs when a focused check establishes the changed contract.

Read `.agents/architecture.md`, `.agents/editor.md`, `.agents/runtime.md`, or `.agents/plugins.md` for additional boundary and host requirements as applicable.
