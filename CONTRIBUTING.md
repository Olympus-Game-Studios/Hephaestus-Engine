# Contributing to Hephaestus Engine

Thanks for your interest in improving the project! This guide explains how to set up your environment, propose changes, and work with the maintainers. Please read it end-to-end before opening a pull request.

## Code of Conduct
- Treat everyone with respect. We expect constructive, inclusive conversations in issues, pull requests, chats, and reviews.
- Do not submit offensive, harassing, or discriminatory content. Violations may result in blocked contributions.

## Questions & Support
- **Bug reports**: Open a GitHub issue that clearly lists steps to reproduce, expected behaviour, and actual results.
- **Feature proposals**: Start with a discussion issue or draft PR so we can align on scope before implementation.
- **Security concerns**: Please email the maintainers instead of opening a public issue.

## Getting Started
1. Fork the repository and clone your fork locally.
2. Install the required tooling (Visual Studio 2022 with C++, CMake ≥ 3.24, Vulkan SDK, Git). Optional: Python for helper scripts. See `README.md` for setup details.
3. Pull dependencies:
	- Run `./setup_entt.ps1` once to download EnTT into `extern/entt/`.
	- Check that the `extern/` directory contains the rest of the vendor libraries after cloning (GLFW, ImGui, ImGuizmo, etc.).
4. Configure and build with the CMake preset for your host (`windows-release`, `macos-release`, or `linux-release`):
	- `cmake --preset windows-release`
	- `cmake --build --preset windows-release`
	- On Windows, run these from a Visual Studio Developer PowerShell. Substitute the matching preset on macOS or Linux.

## Development Workflow
- Work from feature branches created off `main`. Use a descriptive branch name, such as `feature/ecs-undo-stack` or `fix/crash-on-exit`.
- Keep your fork up to date by regularly pulling from `upstream/main` before pushing a PR.
- Prefer small, logically scoped commits. Each commit should compile and describe the change using imperative mood (e.g. `Add point light gizmo preview`).
- Reference GitHub issues in commit messages or PR descriptions when applicable (`Fixes #123`).

## Coding Guidelines
- Follow the existing code style in surrounding files. Match brace placement, naming conventions, and include order.
- Use modern C++20 standard-library facilities and RAII. Avoid raw `new`/`delete` in new code unless ownership semantics require it.
- Keep headers lean: avoid unnecessary includes and prefer forward declarations where practical.
- Document non-obvious logic with concise comments. Focus comments on intent rather than line-by-line narration.
- For shaders and scripts, mirror the formatting used in the corresponding directory.

## Testing & Validation
- **C++ builds and tests**: Build the affected target/configuration and run the relevant CTest tests when available. For the full preset workflow, see `CMakePresets.json` and `AGENTS.md`.
- **Runtime smoke test**: Launch `build/Release/HephaestusEngine.exe` (or Debug) and confirm the affected features behave as expected.
- **Shaders**: If you change GLSL files, recompile SPIR-V with `glslangValidator` and include updated `.spv` outputs.
- **Documentation/examples**: Update or add markdown snippets to reflect behaviour changes; run spell checks where possible.
- Provide logs, screenshots, or captures when fixing rendering or editor issues to help reviewers verify results.

## Documentation Expectations
- Keep `README.md`, `CONTRIBUTING.md`, and `roadmap.md` accurate when workflows or priorities change.
- All new systems should ship with a markdown explainer in `Documentation/` (diagram, quickstart, or reference guide).
- Include code comments and inline documentation for new public APIs.

## Pull Request Checklist
- [ ] The branch is rebased on `main` without merge commits.
- [ ] All builds succeed locally (Release and/or Debug as appropriate).
- [ ] Tests, shader recompiles, and runtime smoke checks are complete.
- [ ] Documentation is updated and new assets are included in git.
- [ ] PR description summarises motivation, approach, and validation steps.

## Review Process
- Maintainers aim to respond to new PRs within a few business days. Complex changes may require multiple reviewers.
- Please address review feedback promptly. Mark conversations as resolved only after updating the code or providing a clear rationale.
- Squash or amend commits when requested to keep history readable.

## Release Notes & Changelog
- Major features and user-facing fixes should add an entry to the appropriate documentation (for now, `Documentation/` or `roadmap.md`).
- Tag follow-up issues if additional work is required post-merge.

Thanks again for contributing! Your work helps the engine grow and stay healthy for everyone.
