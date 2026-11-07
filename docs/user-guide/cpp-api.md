---
title: C++ API reference
description: Everything HephaestusScript.h gives your C++ scripts, and the Lua function each one matches.
order: 16
---

# C++ API reference

This is what `HephaestusScript.h` gives your C++ scripts. For how to set up and write one, start with [Scripting with C++](scripting-cpp.md). Everything lives in the `hp` namespace, and each part matches a piece of the [Lua API](lua-api.md), so if you know one you know the other.

A few conventions:

- **Entity rotations are in degrees** (`GetRotation`, `Rotate`, spawn rotations), like Lua's transforms. Physics bodies, sockets and the view camera use **radians**, also like Lua.
- Things an entity might not have (a camera, an animator, a character controller, a socket) come back as `std::optional`, so check them before use: `if (auto camera = e.GetCamera()) camera->SetFov(70);`.
- Where Lua's global tables act on the script's own entity (`Physics.hasBody()`, `Character.jump()`), C++ goes through the entity: `GetEntity().GetBody().Exists()`, `GetEntity().GetController()->Jump()`.

## Scripts

| Member of `hp::Script` | Does |
| --- | --- |
| `OnAwake`, `OnUpdate`, `OnLateUpdate`, `OnDestroy` | Lifecycle callbacks. |
| `OnTriggerEnter/Stay/Exit`, `OnCollisionEnter/Stay/Exit` | Physics callbacks, each with `(hp::Entity other, const hp::Contact* contact)`. |
| `OnCall(function, args)` | Another script ran `Call(function, ...)` on this entity. |
| `GetEntity()` | The entity the script is on (Lua's `this`). |
| `Raycast(origin, direction, maxDistance[, layer])` | `Physics::Raycast` that passes through this script's entity, like Lua's `Physics.raycast`. |
| `HP_FIELD(type, name, default...)` | An Inspector field. |

## Entities

| Method of `hp::Entity` | Lua |
| --- | --- |
| `GetName()` / `SetName(name)` / `IsValid()` | `e:GetName()`, `e:SetName()`, `e:IsValid()` |
| `GetPosition()` / `SetPosition(v)` / `Translate(v)` | Transform position |
| `GetRotation()` / `SetRotation(v)` / `Rotate(v)` | Transform rotation, degrees |
| `GetScale()` / `SetScale(v)` | Transform scale |
| `SetVisible(on)` / `IsVisible()` | `Mesh.setVisible`, `Mesh.isVisible` |
| `Call(name, args...)` | `e:Call(name, ...)` |
| `GetBody()` | `Physics.*` for this entity |
| `GetController()` | `Character.*` for this entity |
| `GetAnimator()` | `e:GetAnimator()` |
| `GetCamera()` | `e:GetCamera()` |
| `GetSocket(name[, worldSpace])` | `c:GetSocket(name)` (world) and `c:GetSocketLocal(name)` (`worldSpace = false`) |

## Scene

| Function in `hp::Scene` | Lua |
| --- | --- |
| `Find(name)` | Finding an entity by name |
| `FindAllWithPrefix(prefix)` | `Scene.findAllWithPrefix` |
| `Instantiate(path, options)` | `Scene.instantiate`. `hp::SpawnOptions` has `position`, `rotation` (degrees) and `vars`. |
| `Destroy(entity)` | `Scene.destroy`: removed once this frame's scripts have run |
| `Reload()` | `Scene.reload` |
| `FindFirstCamera()` / `FindCameras()` | `Scene.FindFirstCamera`, `Scene.FindCameras` |
| `GetActiveCamera()` / `SetActiveCamera(e)` | `Scene.GetActiveCamera`, `Scene.SetActiveCamera` |

```cpp
hp::SpawnOptions options;
options.position = hp::Vec3{0, 1, 0};
options.vars.emplace_back("health", 40);
hp::Entity enemy = hp::Scene::Instantiate("assets/prefabs/Enemy.hpprefab", options);
```

## Time, application and logging

`hp::Time::DeltaTime()`, `hp::Application::Quit()` (stops Play in the editor) and `hp::Log`, `hp::LogWarning` and `hp::LogError`.

## Input

| Function in `hp::Input` | Lua |
| --- | --- |
| `IsActionPressed`, `IsActionDown`, `IsActionReleased`, `GetAxis` | The same names |
| `GetMouseDelta()`, `GetMousePosition()`, `GetScrollDelta()` | The same names, as `hp::Vec2` |
| `IsMouseButtonDown(hp::MouseButton::Left)` | `Input.isMouseButtonPressed("left")` |
| `IsGamepadConnected()` | `Input.isGamepadConnected` |
| `IsKeyDown("space")` | `Input.isKeyPressed("space")` |
| `SetCursorMode(hp::CursorMode::Captured)` / `GetCursorMode()` | `Input.setCursorMode("disabled")` |

## Cameras

`hp::ViewCamera` is Lua's `Camera` table, the free script camera: `SetActive`, `IsActive`, `SetPosition`, `GetPosition`, `SetRotation`, `GetRotation` (radians) and `Translate`.

`entity.GetCamera()` gives an `hp::Camera` for a Camera component: `GetFov`/`SetFov` (degrees), `GetNear`/`SetNear`, `GetFar`/`SetFar`, `IsActive` and `SetActive`. Making one camera active makes the others inactive.

## Physics

`hp::Physics::IsAvailable()` and `hp::Physics::Raycast(origin, direction, maxDistance, options)`, which returns an `hp::RaycastHit` (`point`, `normal`, `distance` and `entity`) or nothing. `hp::RaycastOptions` has `layer`, `ignoreSensors` and `ignore`.

`entity.GetBody()` gives an `hp::Body`: `Exists`, `GetLinearVelocity`, `SetLinearVelocity`, `SetAngularVelocity`, `SetRotation` (radians), `Move(x, z, speed)`, `Jump(speed)` and `IsGrounded`.

## Character controllers

`entity.GetController()` gives an `hp::CharacterController`: `Move(x, z)`, `Jump`, `SetCrouched`/`IsCrouched`, `IsStandBlocked`, `IsGrounded`, `GetGroundState`, `GetVelocity`, `GetGroundNormal`, `GetGroundVelocity`, `SetSpeedMultiplier`/`GetSpeedMultiplier`, `Teleport` and `SetFacingYaw` (radians).

## Animation

`entity.GetAnimator()` gives an `hp::Animator`: `Play(clip)`, `CrossFade(clip, seconds)`, `Pause`, `Resume`, `Stop`, `SetSpeed`, `IsPlaying([clip])`, `GetCurrentAnimation`, `GetTime` and `GetDuration([clip])`.

## Audio

| Function in `hp::Audio` | Does |
| --- | --- |
| `Play(path, loop, volume)` | Play a `.wav` or `.mp3`. Returns an `hp::Sound`, or `0` if it failed. |
| `Stop(s)` / `Pause(s)` / `Resume(s)` / `SetVolume(s, volume)` / `IsPlaying(s)` | Control a sound. |
| `StopAll()` / `IsAvailable()` | Silence everything, or check sound works. |

## Game UI

`hp::UI` is Lua's `UI`, `display` and `destroy` tables. Screens and widgets are shared with Lua, so a C++ script can change a widget a Lua script made, and the other way round.

| Function in `hp::UI` | Lua |
| --- | --- |
| `Display(path[, &error])` | `display.ui(path)`. Returns an `hp::Screen`, empty on failure; errors go to the Console unless you pass a string. |
| `Destroy(screen)` / `DestroyCurrent()` / `DestroyAll()` | `destroy.ui(handle)`, `destroy.ui("current")`, `destroy.ui("all")` |
| `Load(path)` | `UI.loadHUI` |
| `CreateScreen(name)` / `CreateSimpleMenu()` | `UI.createScreen`, `UI.createSimpleMenu` |
| `ClearScreens()`, `GetScreenCount()`, `SetScreenVisible(name, on)`, `SetScreenActive(name, on)`, `SetViewportSize(w, h)`, `GetDrawCommandCount()` | The same names |
| `RegisterThemeStyle(name, style)` | `UI.registerThemeStyle` |
| `SetCursorMode(hp::CursorMode::Normal)` / `GetCursorMode()` | `UI.setCursorMode(CursorMode.UI)`. `Captured` is `CursorMode.Game`. |

An `hp::Screen` has `GetRoot()` (the canvas to build on) and `GetWidget(id)` (a `.hui` widget by its ID).

An `hp::Widget` has every `UI.set*` function as a method: `SetPosition`, `SetSize`, `SetMinSize`, `SetMaxSize`, `SetVisible`, `SetEnabled`, `SetFocused`, `SetOpacity`, `SetZOrder`, `SetText`, `SetImage`, `SetImageTint`, `SetImageScaleMode`, `SetSpacing`, `SetChildAlignment`, `SetGridColumns`, `SetGridSpacing`, `SetPadding`, `SetMargin`, `SetAnchor`, `SetAlignment`, `SetBackgroundColor`, `SetBorderColor`, `SetBorderWidth`, `SetCornerRadius`, `SetTextColor`, `SetFontSize`, `SetFontBold`, `SetFontItalic`, `SetTextAlignment`, `SetWordWrap`, `SetTextShadow`, `SetStyle`, `SetButtonStyle` and `ApplyThemeStyle`. Each returns false when the widget is gone or doesn't have that property (a text has no grid columns). Enums are `hp::UIAnchor`, `hp::UIAlign`, `hp::UIScaleMode` and `hp::ButtonState`; styles are an `hp::UIStyle` with fields named like Lua's style tables.

It builds children with `AddPanel`, `AddText`, `AddButton`, `AddImage`, `AddVerticalBox`, `AddHorizontalBox` and `AddGrid`.

Button events name a function instead of passing one, so they work across languages and survive a reload. The function arrives in `OnCall`, and a hover gets one bool, true on enter:

```cpp
void OnAwake() override {
    hp::Screen menu = hp::UI::Display("assets/ui/MainMenu.hui");
    SetOnClick(menu.GetWidget("PlayButton"), "OnPlay");
}

bool OnCall(std::string_view function, hp::Args args) override {
    if (function == "OnPlay") {
        hp::UI::DestroyCurrent();
        hp::UI::SetCursorMode(hp::CursorMode::Captured);
        return true;
    }
    return false;
}
```

`widget.SetOnClick(entity, "Name")` does the same for another entity's script, in any language.

## Values

`hp::Vec2`, `hp::Vec3` (with `+`, `-`, `*`, `Length` and `Normalized`), `hp::Vec4` and `hp::Color`. `hp::Value` carries numbers, booleans, strings and vectors between scripts; read one with `AsFloat`, `AsInt`, `AsBool`, `AsString` and `AsVec2/3/4`.

## Not in C++ yet

`onAnimationFinished`, `onEnable`/`onDisable`, character controller scripts and `CallController` are still Lua only.
