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
2. Install the required tooling (Visual Studio 2022 with C++, CMake 3.24 or newer, the Vulkan SDK, Qt 6 for MSVC and Git). See `README.md` for setup details.
3. Pull dependencies: clone with `--recurse-submodules` (or run `git submodule update --init --recursive`) to get Jolt, GLFW and ufbx. EnTT, Lua and the other vendored libraries are already in `extern/`.
4. Configure and build with the CMake preset for your host (`windows-release`, `macos-release`, or `linux-release`):
	- `cmake --preset windows-release`
	- `cmake --build --preset windows-release`
	- On Windows, run these from a Visual Studio Developer PowerShell. Substitute the matching preset on macOS or Linux.

## Licensing your contribution
- Hephaestus Engine is source available under [`LICENSE.md`](LICENSE.md); it is not open source.
- By submitting a contribution (pull request, patch, issue attachment or similar) you agree to section 5 of `LICENSE.md`: you grant the licensor named in `LICENSE.md` a perpetual, irrevocable licence to use, modify, distribute, sublicense and relicense it under any terms, and you confirm the work is yours to give.
- Do not submit code or assets you don't have the right to license this way, such as code copied from projects under incompatible licences.

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
- **Runtime smoke test**: Launch the editor from your build folder (for example `build/windows-release/Release/HephaestusEngine.exe`) and confirm the affected features behave as expected, including in Play and, for runtime changes, a packaged game.
- **Shaders**: The build compiles GLSL in `shaders/` with `glslc` from the Vulkan SDK. Make sure changed shaders compile cleanly.
- **Documentation/examples**: Update or add markdown snippets to reflect behaviour changes; run spell checks where possible.
- Provide logs, screenshots, or captures when fixing rendering or editor issues to help reviewers verify results.

## Documentation Expectations
- Keep `README.md`, `CONTRIBUTING.md` and `ROADMAP.md` accurate when workflows or priorities change.
- User facing changes update the matching page in `docs/user-guide/` (and "What's not there yet" when a gap closes). Technical contracts and file formats go in `Documentation/`.
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
- Major features and user-facing fixes add a dated entry under `## [Unreleased]` in `CHANGELOG.md` (see `.agents/changelog.md` for the format) and update the user guide.
- Tag follow-up issues if additional work is required post-merge.

Thanks again for contributing! Your work helps the engine grow and stay healthy for everyone.
