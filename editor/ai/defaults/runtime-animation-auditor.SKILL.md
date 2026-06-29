---
title: Runtime Animation Auditor
summary: Create and audit realtime gameplay/UI animation, transitions, feedback motion, Tween flows, AnimationPlayer tracks, and AnimationTree states.
---

# Runtime Animation Auditor

Use this skill when creating, modifying, or reviewing realtime animation:
menu transitions, HUD feedback, button motion, damage/heal popups, loading
motion, character state animation, attack/cast effects, camera shake, particles,
Tween-driven motion, AnimationPlayer clips, AnimationTree state machines, and
procedural `_process` or `_physics_process` animation.

The goal is to make generated motion readable, responsive, performant, and
verifiable at runtime instead of only existing as static scene data.

## Motion Design Rules

- Define the gameplay or UI purpose of each motion before implementing it:
  attention, confirmation, affordance, impact, continuity, navigation, warning,
  reward, or state change.
- Keep realtime animation responsive. UI feedback should usually start within
  the same input event; gameplay animation should not hide control latency
  unless the design intentionally adds commitment.
- Prefer `AnimationPlayer` for authored clips, `Tween` for local UI/property
  transitions, `AnimationTree` or a small state machine for character states,
  and particles/shaders only for effects that benefit from them.
- Avoid endless busy motion. Idle loops should be subtle, readable, and cheap.
- Use easing intentionally: quick-out for confirmations, anticipation before
  impact, smooth in/out for panels, and snappy easing for buttons and tabs.
- Keep motion accessible: do not rely only on flashing, avoid excessive camera
  shake, and allow reduced motion or lower-intensity settings for heavy effects
  when the UI or gameplay uses constant motion.

## Implementation Checklist

- Inspect existing scenes, scripts, AnimationPlayer clips, AnimationTree setup,
  Tween usage, particles, shaders, timers, and project settings before editing.
- Name animations by intent, such as `open`, `close`, `hover`, `pressed`,
  `selected`, `damage_flash`, `attack_start`, `attack_recover`, or `level_in`.
- Keep one owner for each animated property. Do not let Tween, AnimationPlayer,
  `_process`, and physics code fight over the same property in the same state.
- For UI panels, animate `modulate`, `scale`, `position`, `rotation`, or theme
  feedback without breaking anchors, containers, focus, or click targets.
- For gameplay, keep hitboxes, hurtboxes, movement locks, invulnerability, and
  animation events synchronized with the visible frame or state.
- Use signals such as `animation_finished`, Tween completion callbacks, or state
  transitions to unlock input and advance flow; avoid arbitrary sleep chains.
- Make transition interruption rules explicit: can the player cancel, reverse,
  spam, close, reopen, or switch tabs mid-animation?

## Runtime Audit Workflow

1. Static inspection:
   - Read the changed `.tscn`, scripts, animation resources, and relevant input
     actions.
   - Check for missing AnimationPlayer paths, stale node references, duplicate
     property owners, unbounded Tweens, orphaned effects, and animations that
     leave controls disabled or invisible.

2. Validation:
   - Run `check_project_scripts` after editing animation scripts.
   - Run `check_ui_layout` after editing UI scenes whose animated panels or
     effects may overlap interactive controls.
   - Use `play_scene` for important animated scenes. For UI animation, prefer
     `capture_runtime_ui_snapshot` and `capture_game_screenshot` before/after
     the motion when visual state matters. Prefer `click_ui_node` when a
     Control node path is known, otherwise use `click_ui_position` on
     representative controls before and after the motion. Call
     `assert_no_runtime_errors` after interactions, then use
     `stop_play_scene` when finished.

3. Audit result:
   - Report which animations were checked, what starts them, what stops them,
     how interruption is handled, and whether click/focus/input still works
     while or after the animation runs.
   - State any limits honestly, such as not being able to judge frame pacing,
     exact visual polish, controller hardware, or long-run particle performance
     without a longer play session.

## Red Flags

- A Tween or AnimationPlayer changes `position` or `scale` on a container-managed
  Control and breaks layout or click regions.
- A modal/panel fade leaves a transparent node with `mouse_filter = Stop` above
  the screen after closing.
- A button has hover/pressed motion but no keyboard/controller focus state.
- An animation disables input and never restores it on interruption.
- A character attack animation and hitbox timing disagree.
- Multiple systems animate the same property in parallel.
- Effects spawn repeatedly without cleanup.
- A transition looks nice once but fails when clicked rapidly, canceled, or
  reopened mid-motion.
