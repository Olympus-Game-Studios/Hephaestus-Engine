# Automation-Controlled Simulation

Play automation can choose whether gameplay follows the host's wall clock or
advances only when explicitly stepped.

```text
play.set_realtime({"enabled": false})
input.set_action({"action": "Fire", "value": 1})
play.step_frames({"count": 30})
input.set_action({"action": "Fire", "value": 0})
play.step_frames({"count": 1})
```

`play.get_state` reports `realtime` and `executionMode` (`realtime` or
`manual`). Realtime is the default. In manual mode, host render/event-loop work
continues, but gameplay input frames, scripts, physics, AI, animation, and
runtime UI updates are not advanced by elapsed wall-clock time. Queries and
runtime frame capture remain available.

Input actions, axis overrides, and mouse deltas may be submitted while manual
mode is active. Action overrides and mouse deltas are consumed by the next
intentional input frame, preserving pressed/released edges and the normal
`InputSystem::BeginFrame` behavior. Axis overrides remain in effect until
changed or cleared. `play.step_frames` uses the host's existing gameplay frame
path and defaults to 1/60 second per frame; its simulation work is independent
of the time spent between automation requests.

Editor Play starts in realtime mode. Stopping Play restores realtime mode, and
the next Play session also starts in realtime mode. A packaged Development
runtime starts in realtime mode; in-game scene restart preserves its current
automation execution mode. Runtime automation and its MCP endpoint remain
compiled out unless `HEPHAESTUS_RUNTIME_AUTOMATION=ON` is selected for the
Development runtime build. Release packaging rejects that option.
