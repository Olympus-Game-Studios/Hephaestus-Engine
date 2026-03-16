# Hephaestus Engine – GitHub Copilot Repository Instructions

These instructions define how GitHub Copilot should behave when generating, editing, or refactoring code inside the **Hephaestus Engine** repository.

Copilot should treat these rules as required development guidelines for this project.

The Hephaestus Engine is a custom C++ game engine focused on performance, modularity, and mod-friendly game development.

---

# Core Development Principles

Copilot should behave like a careful engine developer.

Rules:

* Prefer **small incremental edits** instead of rewriting files.
* Never replace entire systems unless explicitly asked.
* Preserve the **engine architecture**.
* Maintain backwards compatibility where possible.
* Avoid introducing unnecessary abstractions.

When editing code:

* Modify only the necessary sections.
* Keep diffs minimal.
* Respect existing code structure.

---

# Changelog and Versioning Rules

The **CHANGELOG.md** file must always be updated when Copilot modifies the repository.

This includes:

* New features
* Bug fixes
* Refactors
* Performance improvements
* Removed functionality
* Internal engine changes

A task modifying code is **not complete** unless the changelog is updated.

---

# Changelog Format

Hephaestus Engine uses **date-based entries grouped under version numbers**.

* **Every individual change must include a date.**
* **Version numbers are added manually by the maintainer.**
* Copilot must only append dated entries under the current working section (usually `Unreleased`).

Example structure:

```
# Changelog

## [Unreleased]

### Added
- 2026-03-16: Implemented asset import pipeline improvements

### Changed
- 2026-03-16: Refactored renderer initialization

### Fixed
- 2026-03-16: Fixed crash when loading missing textures

### Internal
- 2026-03-16: Cleaned up renderer module structure

## [1.0.1]
Release date set by maintainer

### Added
- 2026-03-10: Vulkan renderer with PBR support

### Changed
- 2026-03-12: Improved shader compilation pipeline
```

Guidelines:

* Dates must use **ISO format (YYYY-MM-DD)**.
* Each changelog bullet must start with a date.
* Copilot should **append entries rather than rewriting existing changelog sections**.
* Copilot must never create or modify version numbers.
* Version sections are created manually by the maintainer when releasing.

---

# Changelog Format

Hephaestus Engine uses a **versioned changelog system** with version numbers and dates.

Entries must follow this format:

```
# Changelog

## [1.0.1] - YYYY-MM-DD

### Added
- Description of new features

### Changed
- Description of modifications

### Fixed
- Description of bug fixes

### Removed
- Description of removed functionality

### Performance
- Performance improvements

### Internal
- Engine internals, refactors, or tooling updates
```

Guidelines:

* Version numbers are defined by the maintainer.
* Dates must use **ISO format (YYYY-MM-DD)**.
* Keep entries concise and technical.

Example:

```
## [1.0.1] - 2026-03-16

### Added
- Initial public release of Hephaestus Engine
- Vulkan renderer with PBR support

### Changed
- Improved asset pipeline loading performance

### Fixed
- Crash when loading missing textures

### Internal
- Refactored renderer module structure
```

---

# Hephaestus Engine Architecture

Copilot must respect the engine's modular architecture.

Primary subsystems include:

* Core
* Renderer
* Physics
* Scripting
* Asset Pipeline
* Editor

Guidelines:

* Do not introduce circular dependencies.
* Rendering code must remain within the renderer module.
* Editor code must not directly depend on runtime engine systems.
* Core utilities should remain lightweight and reusable.

When implementing features:

* Integrate with existing modules.
* Follow patterns used in current engine code.

---

# Coding Standards

Language: **C++20**

Copilot must follow modern C++ best practices.

General rules:

* Prefer RAII patterns.
* Use smart pointers instead of raw pointers where appropriate.
* Avoid global variables.
* Avoid undefined behavior.

Naming conventions:

* Classes: PascalCase
* Functions: camelCase
* Variables: camelCase
* Constants: UPPER_CASE

Header rules:

* Header files should avoid heavy implementations.
* Templates may include implementations in headers.

---

# Performance Guidelines

Hephaestus Engine prioritizes performance.

Avoid:

* Unnecessary heap allocations
* Redundant object copies
* Blocking operations in render loops

Prefer:

* Stack allocations where possible
* Passing large objects by reference
* Efficient data structures

Rendering code should minimize CPU–GPU synchronization.

---

# Dependency Rules

Do not introduce new third-party dependencies unless explicitly requested.

Before adding any dependency:

1. Check if the functionality already exists in the engine.
2. Prefer internal implementations.
3. Avoid large frameworks.

---

# File Organization

Copilot must respect the repository directory layout.

Typical structure:

```
src/
include/
editor/
assets/
tools/
```

Rules:

* Do not introduce new root folders without reason.
* Engine code must remain separate from editor code.
* Assets should never contain engine source code.

---

# Refactoring Guidelines

Refactoring must be conservative.

Rules:

* Preserve engine behavior.
* Avoid large-scale rewrites.
* Refactors should improve readability or maintainability.

Acceptable refactors:

* Reducing duplicated code
* Improving function clarity
* Simplifying logic

Unacceptable refactors:

* Rewriting major subsystems without request
* Changing architecture patterns

---

# Documentation Rules

Copilot should generate useful documentation.

Guidelines:

* Public APIs should have brief comments.
* Complex engine systems should include overview comments.
* Comments should explain **why**, not repeat the code.

---

# Error Handling

Engine code must handle errors explicitly.

Avoid:

* Silent failures
* Ignoring error states

Prefer:

* Clear error messages
* Safe fallback behavior

---

# Security Considerations

Copilot must never introduce security risks.

Do not:

* Hardcode credentials
* Store secrets in source code

Configuration should come from:

* Environment variables
* Config files

---

# Editing Behavior

When modifying existing files:

* Preserve formatting and style.
* Do not reformat entire files unnecessarily.
* Modify only relevant sections.

Prefer **minimal diffs**.

---

# Code Generation Guidelines

Generated code should be:

* Clear
* Maintainable
* Consistent with engine style

Avoid overly complex generated code.

---

# Final Rule

Copilot should act like a cautious engine developer.

If unsure:

* Ask the user for clarification before making changes.
* Prefer conservative edits.
* Preserve existing architecture.
* Avoid destructive refactors or rewrites.
* Do not guess implementation details if the design is unclear.

The stability and maintainability of the Hephaestus Engine must always take priority.
