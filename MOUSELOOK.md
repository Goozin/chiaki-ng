# Mouse look

This fork adds a mouse-to-right-stick layer so you can aim/look with a mouse while streaming.

## Use

- Mouse look turns on automatically when a stream starts (setting `mouselook/enabled`).
- **F8** toggles it. While it's on the cursor is hidden and locked to the window; it's released
  automatically when the window loses focus or a chiaki menu opens.
- Mouse buttons: left = R2, right = L2, middle = R3, back = L1, forward = R1, wheel = D-Pad Up/Down.
- **Caps Lock** toggles driving mode: while on, the left-stick up/down keys (W/S) also press
  R2/L2 (gas/brake) so you can drive PC-style. Turn it off on foot, or walking forward fires R2.
  In driving mode the mouse buttons switch to drive-by: left = R1, right = L1.
  Driving mode has its own camera sensitivity (x0.60) and heavier smoothing (1.5 Hz); F9/F10 and
  F5/F6 tune those while driving mode is on.
- Keyboard: run `scripts/windows-gta-keymap.ps1` (with chiaki-ng closed) for a WASD layout,
  or set keys in Settings > Keys.

## Tuning (while streaming)

| Keys | Adjusts | Default |
| --- | --- | --- |
| F9 / F10 | sensitivity down / up (x1.1 steps) | 1.00 |
| F9 / F10 while holding right mouse | aim sensitivity multiplier down / up | x1.50 |
| F3 / F4 (or Shift+F9) | deadzone compensation -/+ 0.01 | 0.15 |
| Ctrl+F9 / Ctrl+F10 | response curve -/+ 0.05 | 1.00 |
| F5 / F6 | low-speed smoothing less / more | 2.5 Hz |
| hold F7 | calibrate: holds the stick at exactly the deadzone value | |

Current values are shown in the window title (visible when windowed) and saved automatically.

- **Sensitivity**: how far the stick moves for a given mouse speed.
- **Deadzone**: the smallest stick deflection sent for any movement, to get past the game's own
  stick deadzone. Too low and small movements do nothing; too high and the view drifts/jitters
  when you barely touch the mouse.
- **Calibrating the deadzone**: stand still, hold F7, and tap F3 / F4 until the view
  *just barely* creeps. That's the game's deadzone, and where fine aiming is smoothest.
- **Smoothing**: adaptive (One Euro filter) - strong at low speed for steady fine aim, almost none
  for fast flicks. F6 for steadier fine aim, F5 if slow movements feel laggy.
- **Curve**: below 1 makes slow movements stronger (snappier), above 1 gives finer control at low
  speed.

## How it works

`gui/src/mouselook.cpp` reads raw mouse counts (Windows raw input, so pointer acceleration
doesn't apply), averages them over a few 4 ms ticks into a velocity, shapes that into a stick
deflection, and hands the resulting controller state to `StreamSession`, which merges it with the
keyboard and controller states before sending to the console.
