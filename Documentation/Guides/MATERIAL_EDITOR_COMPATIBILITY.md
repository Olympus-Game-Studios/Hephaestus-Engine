# Material Editor Compatibility Guarantees

## Guarantees

The Material Editor is designed to be backwards compatible with legacy `.hematerial` files used in existing Hephaestus projects.

- Existing materials load without crashing, including files with missing fields, unknown keys, unknown nodes, or partial/invalid graphs.
- Unknown JSON is preserved on roundtrip save:
  - unknown top-level keys
  - unknown `graph` keys
  - unknown per-node keys
  - unknown per-link keys
- Missing standard fields are defaulted to engine-safe values:
  - `albedo=[1,1,1]`
  - `metallic=0`
  - `roughness=0.5`
  - `ao=1`
  - `emissive=[0,0,0]`
  - `emissiveStrength=0`
  - `use*Texture=false`
  - `*TexturePath=""`
  - `useWorldAlignedTexture=false`
  - `worldTextureScale=1`
- Existing node IDs, link IDs, and pin IDs are preserved.

## Legacy Materials (No Graph / Invalid Graph)

If a material has no graph, or a graph that cannot be safely edited, the editor enters **Legacy PBR Mode**.

- Legacy PBR fields remain editable.
- Save preserves file structure and unknown metadata as closely as possible.
- Graph editing is not forced.
- Selecting **Enable Graph Editing** generates a minimal graph from current PBR values.

## Unknown Nodes and Links

- Unknown `node.type` entries load as opaque **Unknown Node** entries.
- Unknown nodes keep position, pin IDs, and unknown JSON payloads.
- Links referencing missing pins are retained as orphan links and reported in warnings.
- Swapped link direction is repaired in-memory for editing, but original endpoints are kept unless **Fix Graph on Save** is enabled.

## Optional Migration to Graph Editing

Migration is optional.

1. Open a material in Legacy PBR Mode.
2. Click **Enable Graph Editing**.
3. The editor generates a minimal graph (`MaterialOutput` + constant/texture nodes from PBR flags/values).
4. Save to continue editing in graph mode.

## Automated Compatibility Tests

Compatibility tests are provided in:

- `engine/Tests/MaterialCompatibilityTests.cpp`
- `assets/tests/material_compat/*.hematerial`

CTest name:

- `MaterialCompatibilityRoundtrip`

These tests validate:

- roundtrip preservation of unknown keys
- ID stability
- defaulting behavior for missing fields
- unknown node/orphan link safety
- tolerant golden roundtrip JSON equivalence (value-based, key-order independent)
