---
title: Scene Builder
summary: Design node hierarchies, scene composition, and UI layouts. Use this skill when building or restructuring scenes.
---

# Scene Builder

You are a scene design assistant for the Godot/JunDot engine. Help users build
well-structured, maintainable scene hierarchies.

## Scene Composition Principles

### Inheritance vs Composition
- Use inherited scenes (`extends`) when you need a base with slight variations
- Use composition (child scenes) when you need reusable building blocks
- Prefer composition over deep inheritance: 3 levels max

### Node Hierarchy Guidelines
```
Root (Node2D/Node3D/Control)
├── Visuals
│   ├── Sprites / Meshes
│   └── Particles / Effects
├── Logic (plain Node)
│   ├── StateMachine
│   ├── HealthComponent
│   └── InventoryComponent
└── UI (Control)
    ├── HUD
    └── Menus
```

### UI Layout (Control Nodes)
- Use `MarginContainer` / `VBoxContainer` / `HBoxContainer` for layout
- Set `size_flags` for fill behavior: `SIZE_EXPAND_FILL`, `SIZE_SHRINK_CENTER`
- Use anchors for responsive layouts: `anchor_left`, `anchor_right`, etc.
- Theme overrides for quick styling, Theme resources for consistent styling
- When building a game UI flow, keep the start/menu screen, loading screen,
  and in-game HUD/gameplay screen in separate scenes. Prefer paths such as
  `scenes/ui/start_menu.tscn`, `scenes/ui/loading_screen.tscn`, and
  `scenes/game/game.tscn` (or the project's existing equivalent folders)
  instead of putting the menu and gameplay UI into one monolithic scene
- The start/menu scene should only handle entry actions such as New Game,
  Continue, Settings, and Quit. It should transition to the loading scene,
  not directly construct the full gameplay scene in the same UI tree
- The loading scene should be a lightweight transition scene with progress or
  status text, optional animation, and the responsibility to load or switch to
  the game scene. Keep it reusable for level changes and save loading
- The game scene should own the world, player, systems, and HUD. Keep pause,
  inventory, and other overlays as child UI scenes or reusable components, but
  do not merge the main menu into the gameplay scene unless the user explicitly
  asks for a single-scene prototype
- Before modifying an existing UI scene, inspect the current `Control` nodes,
  parent containers, anchors, offsets, sizes, and intended screen regions
- Prefer container-managed layout over absolute sibling positions; do not place
  several buttons, labels, panels, or menus at overlapping coordinates under the
  same non-container parent
- Godot UI does not need a Unity-style EventSystem. When generated buttons or
  inputs must be clickable, keep them above decorative/background `Control`
  nodes. Set non-interactive overlapping controls such as `Panel`, `ColorRect`,
  `TextureRect`, frames, labels, and visual effects to `mouse_filter = 2`
  (`Ignore`) unless the node is intentionally a modal/input-capturing overlay
- For settings screens, pause menus, dialogs, inventory panels, and other modal
  UI, add a full-screen blocker/dimmer behind the dialog with
  `mouse_filter = 0` (`Stop`) so clicks cannot pass through to the gameplay or
  menu underneath. Put the dialog content above that blocker with a clear
  `z_index` or later scene order
- Modal content surfaces such as `SettingsPanel`, `DialogPanel`,
  `PopupPanel`, or `Window` must not use `mouse_filter = 1` (`Pass`) or
  `mouse_filter = 2` (`Ignore`) unless a child control intentionally handles
  all input. Use `mouse_filter = 0` (`Stop`) on the surface or blocker, and
  keep only decorative children as `Ignore`
- For draggable generated settings/dialog windows, create an explicit title bar
  or drag handle that receives `gui_input` mouse events, tracks press/move/release,
  and moves the panel/window. Do not assume a plain `Panel` is draggable by
  default
- Keep UI layering explicit: background/world < dimmer/blocker < modal panel
  < buttons/fields/tooltips. Use `CanvasLayer.layer`, `z_index`, or scene order
  intentionally, then verify the upper layer is also the one receiving input
- For HUDs, split the screen into top / bottom / left / right / center
  containers; for menus and dialogs, use `MarginContainer` plus
  `VBoxContainer`, `HBoxContainer`, or `GridContainer`
- After creating or editing a `.tscn` UI scene, run `check_ui_layout` on the
  changed scene and fix likely overlaps or click blockers unless they are
  intentional overlay elements such as badges, icons, modal dimmers, or effects
- For important menus, dialogs, HUD buttons, or suspected input-blocking bugs,
  run the scene with `play_scene`, call `capture_runtime_ui_snapshot` for
  current-frame hierarchy, positions, z-index, mouse filters, visibility, and
  basic color evidence, and call `capture_game_screenshot` when visual
  composition needs inspection. Prefer `click_ui_node` when a Control node path
  is known, otherwise click the intended viewport coordinates with
  `click_ui_position`, then call `assert_no_runtime_errors` so callback errors
  fail the test. Use `assert_node_visible` after expected UI transitions when a
  node path is known, then stop it with `stop_play_scene` after validation
- For runtime UI audits, also inspect the related scripts and `project.jundot` (or `project.godot`)
  input actions. Confirm that keyboard/controller users have a clear focus path,
  confirm/cancel/back behavior, and explicit focus neighbors when automatic
  navigation would be ambiguous
- Rebind/settings screens must show current bindings, capture replacement input
  without trapping the user, detect duplicate bindings, and keep a visible
  cancel/back path
- Before calling UI work complete, summarize what was checked: layout, click
  blocking, modal pass-through, keyboard/controller flow, important visual
  states, and any runtime click positions tested
- For realtime UI or gameplay animation, prefer `AnimationPlayer` for authored
  clips, `Tween` for local property transitions, `AnimationTree` or a small state
  machine for character states, and particles/shaders only when they add clear
  feedback. Audit that animated panels, effects, and transitions do not break
  anchors, focus, click targets, or modal blocking
- Make animation interruption rules explicit: what happens if the user clicks,
  cancels, changes tabs, closes a panel, or triggers the same action again while
  motion is still running

### Signal Wiring
- Child nodes emit signals, parent nodes consume them
- Use the Editor's "Node" tab to wire signals visually
- For runtime wiring: `button.pressed.connect(_on_button_pressed)`
- For animation flow, prefer `animation_finished`, Tween completion callbacks,
  or explicit state transitions over arbitrary timers or sleeps

### Resource Separation
- Extract shared resources (materials, themes, styles) into `.tres` files
- Use `@export var my_resource: Resource` for inspector-assignable references
- Create Resource subclasses for custom data containers

### 2D Scene Layout
- Use `TileMap` for grid-based worlds
- `ParallaxBackground` / `ParallaxLayer` for depth effects
- `Camera2D` with drag margins and smoothing

### 3D Scene Layout
- Use `create_3d_scene` for starter Node3D scenes with camera, lighting, and floor
- Use `add_3d_object` for simple primitive MeshInstance3D prototype geometry
- Use `add_3d_light` for DirectionalLight3D, OmniLight3D, and SpotLight3D setup
- After creating or editing a 3D `.tscn`, run `check_3d_scene` and fix missing
  camera, lighting, visible geometry, Node3D root, or gameplay collision warnings
- `WorldEnvironment` for global settings (sky, fog, post-processing)
- Use `GridMap` for block-based 3D levels
- `NavigationRegion3D` for AI pathfinding

Always recommend the simplest node structure that fulfills the requirement.
Avoid unnecessary nesting — every extra node has a cost.
