# Hephaestus Plugin System 1.0

## Architecture audit and decisions

The Qt editor starts in `EditorQtApp`, constructs one `EditorMainWindow`, then initializes `QtEditorEngine` after the window is visible. Native panels already use `QDockWidget`; menus and toolbars are owned by that main window. Plugin UI therefore adapts those existing facilities through `EditorPluginHost` instead of introducing another window system.

Project state is centralized in `ProjectContext`; scene state, selection, and dirty state are centralized in `EditorContext`. Scene undo is the snapshot-based `EditorUndoManager`. Plugin scene and transaction services are narrow adapters over these objects. They do not expose the renderer, EnTT registry, Vulkan objects, physics backend, allocator, or platform layer.

The Asset Browser uses `AssetManager` and hard-coded native `AssetType` values. Plugin types are resolved through the editor asset registry before native fallback. `AssetMountRegistry` generalizes browsing and `AssetPathPolicy` resolution for `plugin://id/...` content. `OpenDocumentRegistry` gains a plugin document kind so registered custom asset editors participate in save/dirty prompts, close, rename/move retargeting, and the existing open-document inventory.

Project settings already have editor and project managers. Plugin settings are namespaced under their owner ID and cannot modify unrelated settings. Packaging remains centered on `HephaestusBuildCore`; Plugin 1.0 does not load editor DLLs in packaged games.

## Lifecycle and failure isolation

The editor discovers engine plugins after its native UI exists. Opening or closing a project unloads plugins in reverse order, rebuilds discovery from engine and project roots, restores project-specific enable state, mounts content, and loads enabled compatible libraries. The native entry point is resolved before plugin construction. Any missing file, bad manifest, duplicate ID, version mismatch, DLL load error, missing symbol, incomplete exports, exception, or `OnLoad` failure is recorded on that plugin while unrelated plugins continue.

Partial initialization is unwound: `OnUnload` is attempted, the instance is destroyed by its own DLL, registrations are removed with the context, then the library is released. Normal shutdown uses reverse load order. Hot unloading is deliberately unavailable; the Plugin Manager labels changes as restart-required.

## Extension flow

Commands are the reusable unit. Menus, toolbars, and the Asset Browser/scene hierarchy/viewport context menus reference command IDs. Dock panels use native Qt docking. Custom extensions and importers live in registries rather than central extension switches. Scene mutations go through high-level operations and a standard undo transaction.

Plugin content is browsable as a visually named `Plugin - <id>` read-only root. Its stable serialized address is `plugin://<id>/...`. It is not copied into the host project.

## Terrain design validation

A terrain plugin can register `.hpterrain`, its creation action/editor/importers, a Terrain dock, sculpt/paint commands, settings, and viewport context actions today. Asset references and content mounts are sufficient for terrain materials and textures; scene creation and undo transactions cover entity representation and coarse edits. A production sculpt brush will require the next small API addition: typed viewport pointer/ray events and an opaque plugin component/data binding. That can be added to the Scene capability without exposing rendering internals or changing the binary entry point.

## Atlas-modeler design validation

An atlas plugin can register `.hpatlas` and `.hpatlasmodel`, create an integrated dock/custom editor, store its documents, invoke importers, mount atlas resources, create scene entities, and transact edits. A full Crocotile-style editor will need a document-session service (save/dirty/close callbacks) and a high-level generated-mesh authoring service. Those are outward additions to Assets and Scene; no renderer or ECS access is required.

## Known 1.0 limits and 1.1 direction

- Plugin dependencies and optional dependencies are reserved but not parsed in manifest version 1.
- Plugin importers are exposed from the Asset Browser creation menu; richer batch-import options are deferred.
- Viewport tools can contribute commands/context actions, but continuous pointer/raycast input is not public yet.
- Runtime/hybrid types are recognized for forward compatibility; packaged-game loading and runtime staging are not enabled.
- `File -> Open Plugin` is deferred. A `.hpplugin` already defines a root with `Content`, `Source`, and `Resources`, and its mounted-content abstraction is independent of `ProjectContext`, so a shared workspace facade can be added without cloning the project system.

Future capability interfaces should be added beside the existing Editor, Assets, Scene, Project, Settings, and Undo services. Rendering, Physics, Audio, Networking, and Runtime capabilities must remain explicit opt-in groups rather than arbitrary internal pointers.
