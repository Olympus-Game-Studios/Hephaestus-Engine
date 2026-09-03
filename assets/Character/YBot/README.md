# YBot (Cultist) source

Mixamo FBX sources for the Stage G Cultist vertical slice. Import Character accepts **GLB only**.

## Convert

Export from Blender (or another DCC) to GLB:

- `Y-Bot.fbx` → mesh GLB (skin + rest pose)
- `Anims/YBot-Crouching-Idle.fbx` → Idle
- `Anims/YBot-Walking.fbx` → Walk
- `Anims/YBot-Jog-Forward.fbx` → Run
- `Anims/YBot-Punching.fbx` → Shoot

There is no Hurt or Death take in this folder. Add those clips later, or leave `Play("Hurt")` / `Play("Death")` to fail safely (logged once).

## Import

1. Asset Browser → **Import Character…** and pick the mesh GLB. Name it `YBot` (destination `assets/characters/`).
2. Open the character and **Import Animation** for each anim GLB. Do not overwrite `mesh.glb`.
3. Set clip ids to `Idle`, `Walk`, `Run`, `Shoot` (and `Hurt` / `Death` if present). Loop Idle/Walk/Run; do not loop Shoot/Hurt/Death.
4. Assign `assets/scripts/CultistCharacter.lua` as the controller (or keep the generated `.source/YBot/YBotCharacter.lua` and copy that script). Enable **Run Controller**.
5. Optional: add socket `Weapon_R` on a right-hand bone.
6. Place the character, add `LuaScriptComponent` with `assets/scripts/CultistAI.lua`, Create Prefab From Selection as `CultistEnemy`.
