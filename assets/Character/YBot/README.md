# YBot (Cultist) source

Mixamo FBX sources for the Stage G Cultist vertical slice. Import Character accepts **GLB and FBX**.

## Import (FBX, no conversion)

Use the FBX files in this folder directly:

- `Y-Bot.fbx` — mesh + skin + rest pose
- `Anims/YBot-Crouching-Idle.fbx` → Idle
- `Anims/YBot-Walking.fbx` → Walk
- `Anims/YBot-Jog-Forward.fbx` → Run
- `Anims/YBot-Punching.fbx` → Shoot

Space conversion: Mixamo is treated as **Modify Geometry** when Auto is selected. Leave **Strip bone namespace** off to keep `mixamorig:` names.

There is no Hurt or Death take in this folder. Add those clips later, or leave `Play("Hurt")` / `Play("Death")` to fail safely (logged once).

## Steps

1. Asset Browser → **Import Character…** and pick `Y-Bot.fbx`. Name it `YBot` (destination `assets/characters/`). Owned source is `mesh.fbx`.
2. Open the character and **Import Animation** for each anim FBX. Do not overwrite `mesh.fbx`. Mixamo takes named `mixamo.com` default to the file name (`YBot-Walking`); edit **Name** in the dialog to `Idle` / `Walk` / `Run` / `Shoot`, or rename later with F2.
3. Loop Idle/Walk/Run; do not loop Shoot/Hurt/Death.
4. Assign `assets/scripts/CultistCharacter.lua` as the controller (or keep the generated `.source/YBot/YBotCharacter.lua` and copy that script). Enable **Run Controller**.
5. Optional: add socket `Weapon_R` on a right-hand bone.
6. Place the character, add `LuaScriptComponent` with `assets/scripts/CultistAI.lua`, Create Prefab From Selection as `CultistEnemy`.
