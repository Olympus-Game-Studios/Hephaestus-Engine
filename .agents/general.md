# General engineering rules

Use the smallest complete, maintainable change that correctly solves the request. First understand the behavior and inspect the relevant implementation, callers, tests, and existing systems. Reuse standard-library, platform, engine, and installed-dependency capabilities before introducing new code or dependencies.

- Avoid speculative extension points, unnecessary abstractions, wrappers, options, feature flags, and unrelated refactoring. Add structure when it materially improves reuse, clarity, testability, or a real boundary.
- Keep edits focused and follow nearby conventions. Do not rewrite a subsystem or reformat unrelated files as a shortcut.
- Do not trade correctness, memory/thread safety, resource lifetime, security, stability, API compatibility, accessibility, cross-platform behavior, or measured performance for fewer lines.
- Prefer existing behavior and interfaces, but do not preserve a broken design boundary when a complete change requires coordinated work.
- Make ordinary implementation decisions within the request. Ask only when requirements are genuinely ambiguous, an action is destructive, or the requested scope exceeds authorization.
- Do not add third-party dependencies unless they are necessary and justified; inspect existing facilities first.
- Never hardcode credentials or secrets. Use the project's established secure configuration mechanisms and do not expose secret values in logs or documentation.

Read this file for every implementation task, then load the relevant topic files listed in the root `AGENTS.md` before editing.
