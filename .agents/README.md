# Hephaestus agent instructions

## How instruction loading works

Root [`AGENTS.md`](../AGENTS.md) is the concise, always-applicable entry point. It tells agents which shared rule files to read. The detailed, mandatory engineering rules in this directory are authoritative; a directory listing or link alone does not load a file. Before implementation, read `general.md` and `git.md`, then explicitly read the topic files relevant to the task. For source/config implementation, include `testing.md`. Read multiple files when work crosses boundaries; for example, a Qt editor change affecting packaged runtime behavior needs `editor.md`, `runtime.md`, `architecture.md`, and `testing.md`.

| Rule file | Use for |
| --- | --- |
| `general.md` | All implementation work: simple complete changes, reuse, scope, and avoiding overengineering |
| `architecture.md` | Subsystem ownership, target boundaries, editor/runtime separation, cross-system work |
| `cpp.md` | C/C++ implementation, lifetime, errors, headers, threads, and performance |
| `rendering.md` | Vulkan frame rendering, swapchain, GPU resources, and rendering lifecycle |
| `editor.md` | Qt editor and launcher UI |
| `runtime.md` | HephaestusUI, game runtime, lifecycle, and packaged behavior |
| `plugins.md` | Native plugin API, lifecycle, editor extensions, and packaged plugin content |
| `testing.md` | Build/test selection and accurate reporting of verification evidence |
| `changelog.md` | Whether/how to update `CHANGELOG.md` |
| `documentation.md` | User/developer guides, public APIs, and implementation-accurate docs |
| `git.md` | Preserving work and repository operation safety |

## Agent compatibility

- **Cursor:** current Cursor supports root/nested `AGENTS.md`, plus skills in `.agents/skills/` with `paths` scoping. Existing project skills are small routing adapters that direct Cursor to canonical rule files; they do not restate standards. Cursor can conditionally surface matching skills. As with other agents, a plain path/link is not a guarantee the target file was loaded; root `AGENTS.md` explicitly requires reading applicable rules. The previous always-on `.cursor/rules/*.mdc` policies were retired to avoid duplicating shared guidance.
- **OpenCode:** current OpenCode automatically discovers root `AGENTS.md` and `.agents/skills/*/SKILL.md`. Its `AGENTS.md` reader does not automatically follow Markdown references, so root `AGENTS.md` explicitly instructs it to read applicable rule files. OpenCode skills are available on demand; Cursor's `paths` field is not an OpenCode file-scoping mechanism. No `opencode.json` is needed: adding the same instructions there would duplicate root discovery or eagerly load unrelated files.
- **OpenAI Codex:** Codex automatically discovers root and nested `AGENTS.md` files and supports Agent Skills. The root instructions explicitly select detailed files because `AGENTS.md` discovery does not imply arbitrary Markdown references are expanded.
- **GitHub Copilot:** current GitHub Copilot recognizes the nearest `AGENTS.md` for agent work. Arbitrary reference expansion is not guaranteed by that convention, so root `AGENTS.md` explicitly requires reading applicable rules. The old `.github/copilot-instructions.md` was redundant and contradicted the unified rules (notably its requirement to change the changelog after every edit), so it was retired. No Copilot-specific adapter is needed for the documented current behavior.
- **Claude Code:** current Claude Code (v2.1.277+) reads `AGENTS.md` directly by default when no `CLAUDE.md` exists. Root `CLAUDE.md` contains only `@AGENTS.md` as compatibility for older or differently configured sessions. Claude loads imported root instructions, but `.agents/*.md` still need to be read as directed by `AGENTS.md`.
- **Other agents:** agents implementing the `AGENTS.md` convention can use the root entry point. Support for following references and native skills varies by product/version; the explicit read instructions provide a fallback. Do not assume every AGENTS-compatible agent discovers `.agents/skills/`.

The AGENTS.md convention is a Markdown instruction-file convention, not a common specification for following links, conditionally loading files, or discovering skills. This setup relies on root instructions telling the agent to explicitly read relevant paths, then uses native skill discovery only as an optional convenience.

Native discovery is not a behavioral test of an agent. Confirm actual loading in the agent's own interface when available (for example, Cursor's selected skills, Codex's loaded instruction chain, Claude Code `/context`, or Copilot references). This repository has no project-local OpenCode config or runtime test harness for those clients.

## Updating the system

1. Put mandatory, reusable engineering policy in the most relevant `.agents/*.md` file. Keep topic files focused and update cross-references when a file is renamed or split.
2. Update root `AGENTS.md` if selection guidance or the file inventory changes.
3. Keep platform adapters as references only. Do not copy policy into `.cursor/`, `.opencode/`, `.github/`, `.codex/`, or `CLAUDE.md` when native AGENTS discovery or a lightweight reference works.
4. Add a skill for a reusable, task-specific workflow that benefits from on-demand steps/resources. A minimal routing adapter is also appropriate when needed for native selective discovery, but it must only point to canonical rules. Do not make mandatory standards depend on optional skill invocation. Check each target agent's current skill discovery mechanism first.
5. Validate paths and links, review for contradictory/duplicated policy, and check that platform configuration does not load the same rules twice.

## Official documentation checked

Current documentation checked for this migration (2026-10-16):

- [AGENTS.md convention](https://agents.md/)
- [Cursor rules](https://cursor.com/docs/context/rules) and [Cursor skills](https://cursor.com/docs/context/skills)
- [OpenCode rules](https://opencode.ai/docs/rules/) and [OpenCode skills](https://opencode.ai/docs/skills/)
- [OpenAI Codex AGENTS.md](https://developers.openai.com/codex/agent-configuration/agents-md) and [Codex skills](https://developers.openai.com/codex/skills)
- [GitHub Copilot repository instructions](https://docs.github.com/en/copilot/customizing-copilot/adding-custom-instructions-for-github-copilot)
- [Claude Code memory and AGENTS.md](https://docs.anthropic.com/en/docs/claude-code/memory) and [Claude Code skills](https://docs.anthropic.com/en/docs/claude-code/skills)

Product behavior and minimum versions can change. Re-check official docs before adding or changing an adapter.
