# Qt editor and launcher

First identify which host and UI owns the requested behavior. Qt 6 Widgets is the existing editor/launcher technology in `engine/UI/QtEditor` and `engine/UI/Launcher`; it is not the runtime UI framework. Editor Play previews runtime UI through editor integration, but preview and packaged behavior are not interchangeable.

- Reuse existing widgets, shared property editors, docks, command registrations, services, and styling/interaction patterns before adding a widget or pathway.
- Keep domain behavior in engine systems/services rather than embedding it in widget code. Keep Qt-specific dependencies in appropriate editor/launcher targets.
- Use Qt parent ownership for QObject/widget lifetimes where appropriate; respect plugin panel ownership defined by the host and avoid double ownership.
- Use context-safe signal/slot connections. Disconnect or cancel callbacks, timers, workers, and queued work when the owning document, widget, project, or engine closes.
- Keep widget operations on the UI thread. Use existing worker/service paths for blocking work and marshal results back safely.
- Preserve editor lifecycle and state: active project/scene, selection, dirty documents, undo, Play/Stop, and shutdown can be affected by local UI changes.
- Follow nearby layout, styling, keyboard, accessibility, and interaction patterns. Prefer layouts for adaptable widgets; fixed geometry is appropriate where existing viewport/platform integration requires it.
- Do not introduce another editor UI framework or convert a subsystem solely for consistency. Cross-framework work is appropriate when requested and architecturally justified.

Read `.agents/architecture.md` for dependency placement and `.agents/testing.md` for host-specific verification.
