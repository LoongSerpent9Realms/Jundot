---
title: Runtime UI Auditor
summary: Audit generated game UI at runtime for clickability, keyboard/controller flow, modal blocking, focus, and visual polish.
---

# Runtime UI Auditor

Use this skill when creating, modifying, or reviewing player-facing UI such as
HUDs, menus, pause screens, settings panels, dialogs, inventory screens, shops,
skill trees, result screens, and tutorials.

The goal is to catch problems that only become obvious when the game runs:
unclickable buttons, click-through modals, missing keyboard shortcuts, broken
controller focus, unreadable state feedback, and UI that technically works but
still feels like a plain tool form.

## Audit Workflow

1. Inspect the UI scene and scripts before changing anything.
   - Read the `.tscn` file and relevant `.gd`/`.cs` scripts.
   - Identify Control nodes, CanvasLayer usage, containers, anchors, offsets,
     size, `mouse_filter`, `z_index`, focus mode, signal wiring, and navigation.
   - Inspect `project.godot` or input setup for actions used by the screen.

2. Define acceptance criteria before editing.
   - Mouse/touch: which buttons, sliders, tabs, fields, or drag handles must work.
   - Keyboard/controller: which actions move focus, confirm, cancel, pause,
     reopen, close, or rebind controls.
   - Modal safety: which layer must block clicks to the world or lower menus.
   - Visual quality: what style, hierarchy, states, readability, and feedback
     must be visible when the screen is idle, hovered/focused, pressed, disabled,
     selected, or invalid.

3. Fix the static structure first.
   - Prefer MarginContainer, VBoxContainer, HBoxContainer, GridContainer,
     CenterContainer, PanelContainer, and TabContainer for layout.
   - Keep decorative overlapping nodes at `mouse_filter = 2` (`Ignore`).
   - Use `mouse_filter = 0` (`Stop`) for modal blockers and input-capturing
     surfaces.
   - Put interactive controls above backgrounds, frames, dimmers, and effects
     using CanvasLayer, `z_index`, or later scene order.
   - For movable windows, add an explicit title bar or drag handle that receives
     `gui_input`; do not assume a plain Panel can be dragged.

4. Audit keyboard and controller flow.
   - Use named InputMap actions instead of hard-coded keycodes for gameplay UI.
   - Menus should support confirm/cancel/back and predictable focus movement.
   - Set focus mode on interactive controls that should be keyboard/controller
     reachable.
   - Set explicit focus neighbors when automatic navigation would be ambiguous.
   - Rebind screens must show the current binding, listen for a new input, avoid
     duplicate conflicts, and preserve a cancel/back path.

5. Run static and runtime validation.
   - After editing a Control scene, call `check_ui_layout` on each changed UI
     `.tscn` and fix reported overlap, mouse blocking, z-order, and modal issues.
   - For important screens or suspected input bugs, call `play_scene`, wait for
     the game to start, use `click_ui_position` on representative controls, then
     call `stop_play_scene`.
   - If coordinates are uncertain, infer them from scene layout and state that
     the runtime click coverage was approximate.

6. Finish with an audit summary.
   - List what was checked: layout, clickability, modal blocking, keyboard or
     controller flow, visual states, and runtime clicks.
   - Mention intentional overlaps such as icons, badges, dimmers, or effects.
   - Mention anything that could not be verified, such as unknown controller
     hardware, missing runtime coordinates, or an external startup error.

## Visual Quality Rules

- Build game UI around the player's task and fantasy, not around default form
  widgets.
- Use a clear hierarchy: title/focus area, primary action, secondary actions,
  status/feedback, and escape/back path.
- Use game-appropriate panels, slots, tabs, meters, badges, icons, dividers,
  glow/shadow, selected states, hover/focus states, disabled states, and
  transition feedback.
- Avoid raw grey panels, unstyled vertical button stacks, tiny labels, crowded
  text, spreadsheet-like tables, and debug-looking utility screens unless the
  user explicitly asks for an editor/debug UI.
- Reuse existing project themes, fonts, textures, sprites, and colors first. If
  none exist, create a small reusable UI style foundation instead of styling
  every Control independently.

## Red Flags

- A full-screen ColorRect, Panel, TextureRect, Label, or decorative frame sits
  above buttons with `mouse_filter` set to Stop or Pass.
- A settings, pause, inventory, or dialog panel lacks a blocker behind it.
- Buttons can be clicked by mouse but cannot be reached by keyboard/controller.
- Escape/cancel closes the wrong layer or resumes gameplay through a modal.
- Rebind UI captures mouse/keyboard input but has no cancel path.
- Hover/focus/selected/disabled states are visually indistinguishable.
- The UI only looks acceptable at one resolution because it relies on absolute
  sibling positions instead of responsive containers and anchors.
