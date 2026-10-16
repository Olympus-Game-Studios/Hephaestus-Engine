# C++ engineering

The root CMake configuration requires C++20. Match the neighboring code's naming, formatting, include order, error handling, and namespace conventions; the repository is not perfectly uniform, so do not impose a repository-wide style rewrite.

- Use RAII and explicit ownership. Prefer values or standard smart pointers for owned resources. Raw pointers and references are appropriate for clearly borrowed or nullable relationships when consistent with existing interfaces.
- Pair resource acquisition and release, including Vulkan, Jolt, Lua, Qt, subscriptions, callbacks, threads, and plugin-owned data. Respect shutdown order and thread affinity.
- Use const-correctness and non-owning references appropriately. Avoid dangling pointers/references, stale entity handles, data races, and hidden ownership transfers.
- Prefer standard-library facilities and existing engine utilities. Avoid unnecessary allocations, copies on measured hot paths, global mutable state, unnecessary virtual interfaces, over-general templates, and heavyweight headers. Forward-declare where it reduces coupling without obscuring interfaces.
- Propagate failures through the surrounding API's established result/logging conventions. Validate untrusted or externally supplied data at trust boundaries; do not duplicate checks for states already guaranteed by an invariant.
- Keep interfaces understandable and side effects visible. Add abstractions only when they remove real duplication or isolate a real dependency/lifetime boundary.
- Protect frame, render, physics, and asset hot paths from material regressions. Measure or identify evidence before micro-optimizing; do not trade clarity for speculative speed.
- Preserve public API and serialized-data compatibility unless the task justifies a deliberate change. Review callers when changing signatures, ownership, lifecycle, or behavior.

Read `.agents/architecture.md` for target/dependency boundaries, `.agents/rendering.md` for Vulkan changes, and `.agents/testing.md` for applicable checks.
