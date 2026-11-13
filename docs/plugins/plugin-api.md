# Plugin API 1.0

The public native surface is [PluginAPI.h](../../engine/PluginAPI/PluginAPI.h). A plugin implements `IHephaestusPlugin`, returns `false` from `OnLoad` on initialization failure, and exports the standard `HP_GetPluginExports` symbol. `HP_DECLARE_PLUGIN(MyPlugin)` supplies the entry point and matching create/destroy functions.

```cpp
class MyPlugin final : public Hephaestus::Plugins::IHephaestusPlugin {
public:
    bool OnLoad(Hephaestus::Plugins::IPluginContext& context) override {
        // Register commands first, then their menu/toolbar/context placements.
        return true;
    }
    void OnUnload() override {}
};

HP_DECLARE_PLUGIN(MyPlugin)
```

`IPluginContext` intentionally exposes services, not the editor singleton:

- `Commands`: command IDs, labels, callbacks, descriptions, and optional default shortcuts.
- `UI`: native Qt dock panels plus menu, toolbar, and context-menu command placements.
- `Assets`: custom asset type/create/editor/save/dirty/close/retarget callbacks and importer registrations, plus `ImportAssetPack`, which imports a downloaded pack (`.zip`, folder or `.fbx`/`.glb`/`.gltf`/`.obj`) into a folder under `assets/`: one prefab per model with a material made and applied for every slot (glTF and FBX materials, loose Megascans/Unreal/Substance texture sets, packed ORM and metal/rough maps read per channel), leftover textures and documents, and an optional credit written as `credits.json` and gathered into `assets/scripts/ImportedCredits.lua`. `ImportAssetPack` was appended to `IAssetService` after API 1 first shipped, so a plugin calling it needs an `engineVersion` that includes it. The Fab and Sketchfab plugins use it.
- `Scene`: presence, selection, deletion, and representative high-level primitive/entity creation.
- `Undo`: begin/commit/rollback transactions backed by the editor scene undo stack.
- `Project`: project/plugin roots and portable asset resolution.
- `Settings`: plugin-owned editor or project setting definitions and values.

The host copies every descriptor during `OnLoad`; descriptor strings need only remain valid for the call. Plugin callback `userData` remains plugin-owned and must stay valid until `OnUnload`. Widgets returned by panel factories become Qt children of the host dock. The plugin must not delete them independently. Commands must be registered before UI placements that reference them.

The exported structure uses fixed-width integers and function pointers. The C++ service interface and optional Qt widget boundary still require the same compiler family, architecture, Qt major version, and compatible C++ runtime as the editor. API version 1 is therefore an engine-versioned native SDK, not a permanent cross-toolchain ABI promise. STL containers are not passed in public descriptors.

Plugin assets use `plugin://<id>/path` (also accepted as `/plugin/<id>/path`). Content mounts are read-only in the Content Browser. Project assets and scenes can persist those portable keys; `AssetPathPolicy` resolves them only while the owning plugin is enabled.

See `Plugins/PrimitiveTools` for a complete example that registers commands, a dock, a menu/toolbar/context action, scene edits with undo, a setting, and `.hptestasset` creation/editor support.

Scripting languages use a separate plain C interface so they can run in packaged games and be written in any language. See [script-languages.md](script-languages.md).
